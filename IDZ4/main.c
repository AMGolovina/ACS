#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <semaphore.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static const char *SHM_NAME        = "/posix-shar-object"; // Имя объекта разделяемой памяти (shared memory)
static const char *SEM_MUTEX_NAME  = "/sem_mutex_rw"; // Именованный семафор для защиты read_coun
static const char *SEM_RWM_NAME    = "/sem_db_rw"; // Именованный семафор для эксклюзивного доступа к БД
static const char *SEM_LOG_NAME    = "/sem_log_rw"; // Именованный семафор для синхронизации вывода

// Структура, лежащая в POSIX shared memory
typedef struct {
    int db[20]; // Массив целых положительных чисел(база данных)
    int read_count; // Число читателей
    int terminate; // Флаг завершения
} shared_t;

static shared_t *shared = NULL; // Указатель разделяемую память
static int shm_fd = -1; // Дескриптор

static sem_t *mutex = NULL; // Семафор "mutex"
static sem_t *rw_mutex = NULL; // Семафор "rw_mutex"
static sem_t *log_sem = NULL; // Семафор для вывода

static FILE *logf = NULL; // Файл журнала

// Флаг завершения.
static void on_sigint(int signo) {
    (void)signo;
    if (shared) shared->terminate = 1;
}

// Унифицированный вывод: пишет одну строку и в консоль, и в файл.
static void log_msg(const char *fmt, ...) {
    va_list ap;

    sem_wait(log_sem);

    // Печать в консоль
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    fflush(stdout);

    // Печать в файл, если файл успешно открыт
    if (logf) {
        va_start(ap, fmt);
        vfprintf(logf, fmt, ap);
        va_end(ap);
        fflush(logf);
    }

    sem_post(log_sem);
}

// Фибоначи
static int fib(int n) {
    if (n <= 1) return n;
    int a = 0, b = 1;
    for (int i = 2; i <= n; ++i) {
        int c = a + b;
        a = b;
        b = c;
    }
    return b;
}

// Сортировка
static int int_cmp(const void *a, const void *b) {
    int ia = *(const int *)a;
    int ib = *(const int *)b;
    return (ia > ib) - (ia < ib);
}

// Дочерний процесс открывает именованные семафоры по тем же именам.
static void open_sems_in_child_or_exit(void) {
    mutex = sem_open(SEM_MUTEX_NAME, 0);
    rw_mutex = sem_open(SEM_RWM_NAME, 0);
    log_sem = sem_open(SEM_LOG_NAME, 0);
    if (mutex == SEM_FAILED || rw_mutex == SEM_FAILED || log_sem == SEM_FAILED) {
        perror("sem_open (child)");
        _exit(1);
    }
}

// Процесс-читатель: читает случайную запись, печатает idx/value/fib, не изменяет БД.
static void reader_process(int id) {
    open_sems_in_child_or_exit();
    srand((unsigned)getpid());

    while (!shared->terminate) {
        sem_wait(mutex);
        shared->read_count++;

        if (shared->read_count == 1) sem_wait(rw_mutex);

        sem_post(mutex);

        int idx = rand() % 20;
        int value = shared->db[idx];

        int f = fib(value % 20);

        log_msg("READER #%d | PID=%d : idx=%d value=%d fib=%d\n",
                id, getpid(), idx, value, f);

        sem_wait(mutex);
        shared->read_count--;

        if (shared->read_count == 0) sem_post(rw_mutex);

        sem_post(mutex);

        sleep(1);
    }

    sem_close(mutex);
    sem_close(rw_mutex);
    sem_close(log_sem);
    _exit(0);
}

// Процесс-писатель: эксклюзивно меняет запись, затем сортирует БД и печатает old/new.
static void writer_process(int id) {
    open_sems_in_child_or_exit();
    srand((unsigned)getpid());

    while (!shared->terminate) {
        sem_wait(rw_mutex);

        int idx = rand() % 20;
        int old = shared->db[idx];

        int new_val = (rand() % 1000) + 1;

        shared->db[idx] = new_val;

        qsort(shared->db, 20, sizeof(int), int_cmp);

        log_msg("WRITER #%d | PID=%d : idx=%d old=%d new=%d\n",
                id, getpid(), idx, old, new_val);

        sem_post(rw_mutex);

        sleep(2);
    }

    sem_close(mutex);
    sem_close(rw_mutex);
    sem_close(log_sem);
    _exit(0);
}

// Очистка ресурсов
static void cleanup_parent(void) {
    if (mutex && mutex != SEM_FAILED) { sem_close(mutex); mutex = NULL; }
    if (rw_mutex && rw_mutex != SEM_FAILED) { sem_close(rw_mutex); rw_mutex = NULL; }
    if (log_sem && log_sem != SEM_FAILED) { sem_close(log_sem); log_sem = NULL; }

    sem_unlink(SEM_MUTEX_NAME);
    sem_unlink(SEM_RWM_NAME);
    sem_unlink(SEM_LOG_NAME);

    if (shared) { munmap(shared, sizeof(shared_t)); shared = NULL; }

    if (shm_fd != -1) { close(shm_fd); shm_fd = -1; shm_unlink(SHM_NAME); }

    if (logf) { fclose(logf); logf = NULL; }
}

// Разбор строки в целое положительное число (для argv и для конфига)
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

// Чтение N и K из конфиг-файла вида N=... и K=...
static void read_config(const char *path, int *N, int *K) {
    FILE *f = fopen(path, "r");
    if (!f) {
        perror("fopen config");
        exit(1);
    }

    int n = -1, k = -1;
    char line[256];

    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = '\0';

        if (line[0] == '#' || line[0] == '\0') continue;

        if (strncmp(line, "N=", 2) == 0) {
            n = parse_positive_int(line + 2, "N");
        } else if (strncmp(line, "K=", 2) == 0) {
            k = parse_positive_int(line + 2, "K");
        }
    }

    fclose(f);

    if (n <= 0 || k <= 0) {
        fprintf(stderr, "Ошибка: в конфиге должны быть строки N=... и K=...\n");
        exit(1);
    }

    *N = n;
    *K = k;
}

// Начальная БД
static void init_db_random_sorted(void) {
    srand((unsigned)time(NULL) ^ (unsigned)getpid());
    for (int i = 0; i < 20; ++i) {
        shared->db[i] = (rand() % 1000) + 1;
    }
    qsort(shared->db, 20, sizeof(int), int_cmp);
}

// Печать подсказки по использованию программы с ключами командной строки, при неправильном вводе
static void usage(const char *prog) {
    fprintf(stderr,
        "Использование:\n"
        "  %s -n N -k K -o out.log\n"
        "  %s -c config.txt -o out.log\n"
        "\n"
        "Ключи:\n"
        "  -n N        число читателей (целое > 0)\n"
        "  -k K        число писателей (целое > 0)\n"
        "  -c file     конфигурационный файл (вместо -n и -k)\n"
        "  -o file     файл для записи результатов (обязательно)\n",
        prog, prog
    );
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
            default:
                usage(argv[0]);
                return 1;
        }
    }

    // Файл вывода, проверка на наличие
    if (!out_path) {
        fprintf(stderr, "Ошибка: не задан файл вывода (-o).\n");
        usage(argv[0]);
        return 1;
    }

    // Режимы ввода: либо конфиг (-c), либо параметры (-n и -k).
    if (cfg_path) {
        // Если задан -c, то -n/-k запрещены
        if (N != -1 || K != -1) {
            fprintf(stderr, "Ошибка: при использовании -c нельзя задавать -n/-k.\n");
            usage(argv[0]);
            return 1;
        }
        read_config(cfg_path, &N, &K);
    } else {
        // Если конфиг не задан, то N и K должны быть заданы через ключи -n и -k.
        if (N <= 0 || K <= 0) {
            fprintf(stderr, "Ошибка: задайте -n N -k K или используйте -c config.\n");
            usage(argv[0]);
            return 1;
        }
    }

    logf = fopen(out_path, "a");
    if (!logf) {
        perror("fopen out");
        return 1;
    }

    // Создаем/открываем объект в циклах обработки ошибок
    shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
    if (shm_fd == -1) { perror("shm_open"); cleanup_parent(); return 1; }
    if (ftruncate(shm_fd, (off_t)sizeof(shared_t)) == -1) { perror("ftruncate"); cleanup_parent(); return 1; }

    // Получаем доступ к памяти в цикле обработка ошибки
    shared = mmap(NULL, sizeof(shared_t), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (shared == MAP_FAILED) { perror("mmap"); shared = NULL; cleanup_parent(); return 1; }

    // Инициализируем служебные поля
    shared->read_count = 0;
    shared->terminate = 0;

    // Генерируем начальные данные БД 
    init_db_random_sorted();

    // Перед созданием семафоров удаляем их имена 
    sem_unlink(SEM_MUTEX_NAME);
    sem_unlink(SEM_RWM_NAME);
    sem_unlink(SEM_LOG_NAME);

    // Создаём именованные семафоры
    mutex = sem_open(SEM_MUTEX_NAME, O_CREAT | O_EXCL, 0666, 1);
    if (mutex == SEM_FAILED) { perror("sem_open mutex"); cleanup_parent(); return 1; }

    rw_mutex = sem_open(SEM_RWM_NAME, O_CREAT | O_EXCL, 0666, 1);
    if (rw_mutex == SEM_FAILED) { perror("sem_open rw_mutex"); cleanup_parent(); return 1; }

    log_sem = sem_open(SEM_LOG_NAME, O_CREAT | O_EXCL, 0666, 1);
    if (log_sem == SEM_FAILED) { perror("sem_open log_sem"); cleanup_parent(); return 1; }

    // Ctrl+C
    struct sigaction sa;
    sa.sa_handler = on_sigint;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);

    // Стартовое сообщение 
    log_msg("Старт: читателей=%d, писателей=%d, out=%s%s%s\n",
            N, K, out_path, cfg_path ? ", config=" : "", cfg_path ? cfg_path : "");

    // N процессов-читателей.
    for (int i = 0; i < N; ++i) {
        pid_t pid = fork();
        if (pid == 0) reader_process(i);
        if (pid < 0) { perror("fork (reader)"); shared->terminate = 1; break; }
    }

    // K процессов-писателей.
    for (int i = 0; i < K; ++i) {
        pid_t pid = fork();
        if (pid == 0) writer_process(i);
        if (pid < 0) { perror("fork (writer)"); shared->terminate = 1; break; }
    }

    // Родитель ждёт завершения всех дочерних процессов
    int status;
    while (1) {
        pid_t w = wait(&status);
        if (w > 0) continue;
        if (w == -1 && errno == EINTR) continue;
        break;
    }

    // Освобождаем все ресурсы
    cleanup_parent();
    return 0;
}
