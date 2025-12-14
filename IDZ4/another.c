#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static int parse_positive_int(const char *s, const char *what) {
    char *end = NULL;
    errno = 0;
    long v = strtol(s, &end, 10);
    if (errno != 0 || end == s || *end != '\0' || v <= 0) {
        fprintf(stderr, "Ошибка: %s должно быть целым положительным числом.\n", what);
        exit(1);
    }
    if (v > 200000) {
        fprintf(stderr, "Ошибка: %s слишком большое (ограничение 200000).\n", what);
        exit(1);
    }
    return (int)v;
}

static void read_config(const char *path, int *N, int *K) {
    FILE *f = fopen(path, "r");
    if (!f) { perror("fopen config"); exit(1); }

    int n = -1, k = -1;
    char line[256];

    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (line[0] == '#' || line[0] == '\0') continue;

        if (strncmp(line, "N=", 2) == 0) n = parse_positive_int(line + 2, "N");
        else if (strncmp(line, "K=", 2) == 0) k = parse_positive_int(line + 2, "K");
    }

    fclose(f);

    if (n <= 0 || k <= 0) {
        fprintf(stderr, "Ошибка: в конфиге должны быть строки N=... и K=...\n");
        exit(1);
    }

    *N = n;
    *K = k;
}

static void usage(const char *prog) {
    fprintf(stderr,
        "Использование:\n"
        "  %s -n N -k K -o out.log\n"
        "  %s -c config.txt -o out.log\n"
        "Ключи: -n читатели, -k писатели, -c конфиг, -o файл вывода\n",
        prog, prog);
}

static FILE *logf = NULL;
static pthread_mutex_t log_lock = PTHREAD_MUTEX_INITIALIZER; // pthread_mutex вместо sem_log: вывод не перемешивается

static void log_msg(const char *fmt, ...) {
    va_list ap;

    pthread_mutex_lock(&log_lock);

    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    fflush(stdout);

    if (logf) {
        va_start(ap, fmt);
        vfprintf(logf, fmt, ap);
        va_end(ap);
        fflush(logf);
    }

    pthread_mutex_unlock(&log_lock);
}

static int db[20]; // БД общая в одном процессе

static int int_cmp(const void *a, const void *b) {
    int ia = *(const int*)a, ib = *(const int*)b;
    return (ia > ib) - (ia < ib);
}

static void init_db_random_sorted(unsigned seed) {
    srand(seed);
    for (int i = 0; i < 20; ++i) db[i] = (rand() % 1000) + 1;
    qsort(db, 20, sizeof(int), int_cmp);
}

static int fib(int n) {
    if (n <= 1) return n;
    int a = 0, b = 1;
    for (int i = 2; i <= n; ++i) { int c = a + b; a = b; b = c; }
    return b;
}

// Монитор читатели/писатели на mutex + condition variables (альтернатива семафорам)
typedef struct {
    pthread_mutex_t m;
    pthread_cond_t can_read;
    pthread_cond_t can_write;
    int active_readers;
    int active_writer;
    int waiting_writers;
} rw_monitor_t;

// Общее состояние синхронизации
static rw_monitor_t mon = {
    .m = PTHREAD_MUTEX_INITIALIZER,
    .can_read = PTHREAD_COND_INITIALIZER,
    .can_write = PTHREAD_COND_INITIALIZER,
    .active_readers = 0,
    .active_writer = 0,
    .waiting_writers = 0
};

// Вход читателя
static void begin_read(void) {
    pthread_mutex_lock(&mon.m);
    while (mon.active_writer || mon.waiting_writers > 0) {
        pthread_cond_wait(&mon.can_read, &mon.m);
    }
    mon.active_readers++;
    pthread_mutex_unlock(&mon.m);
}

// Выход читателя: если последний — будим писателя
static void end_read(void) {
    pthread_mutex_lock(&mon.m);
    mon.active_readers--;
    if (mon.active_readers == 0) pthread_cond_signal(&mon.can_write);
    pthread_mutex_unlock(&mon.m);
}

// Вход писателя
static void begin_write(void) {
    pthread_mutex_lock(&mon.m);
    mon.waiting_writers++;
    while (mon.active_writer || mon.active_readers > 0) {
        pthread_cond_wait(&mon.can_write, &mon.m);
    }
    mon.waiting_writers--;
    mon.active_writer = 1;
    pthread_mutex_unlock(&mon.m);
}

// Выход писателя: вызов других писателей, иначе будим читателей
static void end_write(void) {
    pthread_mutex_lock(&mon.m);
    mon.active_writer = 0;
    if (mon.waiting_writers > 0) pthread_cond_signal(&mon.can_write);
    else pthread_cond_broadcast(&mon.can_read);
    pthread_mutex_unlock(&mon.m);
}

static atomic_int stop_flag = 0; // Атомарный флаг остановки для корректного завершения потоков

// Ctrl+C
static void on_sigint(int signo) {
    (void)signo;
    atomic_store(&stop_flag, 1);
    pthread_mutex_lock(&mon.m);
    pthread_cond_broadcast(&mon.can_read);
    pthread_cond_broadcast(&mon.can_write);
    pthread_mutex_unlock(&mon.m);
}

typedef struct { int id; unsigned seed; } thr_arg_t;

// Поток-читатель
static void *reader_thread(void *argp) {
    thr_arg_t a = *(thr_arg_t*)argp;
    srand(a.seed);

    while (!atomic_load(&stop_flag)) {
        begin_read();

        int idx = rand() % 20;
        int value = db[idx];
        int f = fib(value % 20);

        log_msg("READER #%d | TID=%lu : idx=%d value=%d fib=%d\n",
                a.id, (unsigned long)pthread_self(), idx, value, f);

        end_read();
        sleep(1);
    }
    return NULL;
}

// Поток-писатель
static void *writer_thread(void *argp) {
    thr_arg_t a = *(thr_arg_t*)argp;
    srand(a.seed);

    while (!atomic_load(&stop_flag)) {
        begin_write();

        int idx = rand() % 20;
        int old = db[idx];
        int new_val = (rand() % 1000) + 1;
        db[idx] = new_val;
        qsort(db, 20, sizeof(int), int_cmp);

        log_msg("WRITER #%d | TID=%lu : idx=%d old=%d new=%d\n",
                a.id, (unsigned long)pthread_self(), idx, old, new_val);

        end_write();
        sleep(2);
    }
    return NULL;
}

int main(int argc, char *argv[]) {
    int N = -1, K = -1;
    const char *cfg_path = NULL;
    const char *out_path = NULL;

    int opt;
    while ((opt = getopt(argc, argv, "n:k:c:o:")) != -1) {
        switch (opt) {
            case 'n': N = parse_positive_int(optarg, "N"); break;
            case 'k': K = parse_positive_int(optarg, "K"); break;
            case 'c': cfg_path = optarg; break;
            case 'o': out_path = optarg; break;
            default: usage(argv[0]); return 1;
        }
    }

    if (!out_path) { fprintf(stderr, "Ошибка: не задан файл вывода (-o).\n"); usage(argv[0]); return 1; }

    if (cfg_path) {
        if (N != -1 || K != -1) { fprintf(stderr, "Ошибка: при -c нельзя задавать -n/-k.\n"); return 1; }
        read_config(cfg_path, &N, &K);
    } else {
        if (N <= 0 || K <= 0) { fprintf(stderr, "Ошибка: задайте -n N -k K или используйте -c.\n"); return 1; }
    }

    logf = fopen(out_path, "a");
    if (!logf) { perror("fopen out"); return 1; }

    unsigned base_seed = 12345u ^ (unsigned)N ^ ((unsigned)K << 16); // Фиксируем seed для повторяемости входных данных
    init_db_random_sorted(base_seed);

    struct sigaction sa;
    sa.sa_handler = on_sigint; // Стоп-флаг + пробуждение ожидающих
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);

    log_msg("Старт (pthread/cv): читателей=%d, писателей=%d, out=%s%s%s\n",
            N, K, out_path, cfg_path ? ", config=" : "", cfg_path ? cfg_path : "");

    pthread_t *r = calloc((size_t)N, sizeof(pthread_t));
    pthread_t *w = calloc((size_t)K, sizeof(pthread_t));
    thr_arg_t *ra = calloc((size_t)N, sizeof(thr_arg_t));
    thr_arg_t *wa = calloc((size_t)K, sizeof(thr_arg_t));
    if (!r || !w || !ra || !wa) { fprintf(stderr, "Ошибка: нехватка памяти.\n"); return 1; }

    for (int i = 0; i < N; ++i) {
        ra[i].id = i;
        ra[i].seed = base_seed + 1000u + (unsigned)i; // Отдельный seed для каждого читателя
        pthread_create(&r[i], NULL, reader_thread, &ra[i]);
    }
    for (int i = 0; i < K; ++i) {
        wa[i].id = i;
        wa[i].seed = base_seed + 2000u + (unsigned)i; // Отдельный seed для каждого писателя
        pthread_create(&w[i], NULL, writer_thread, &wa[i]);
    }

    for (int i = 0; i < N; ++i) pthread_join(r[i], NULL);
    for (int i = 0; i < K; ++i) pthread_join(w[i], NULL);

    fclose(logf);
    free(r); free(w); free(ra); free(wa);
    return 0;
}
