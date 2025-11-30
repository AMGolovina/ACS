#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <time.h>
#include <semaphore.h>
#include <sys/wait.h>

// Имя объекта разделяемой памяти
const char *shar_object = "/posix-shar-object";

// Структура, лежащая в POSIX shared memory
typedef struct {
    int db[20]; // Массив целых положительных чисел(база данных)
    int read_count; // Число читателей
    int terminate; // Флаг завершения 
    sem_t mutex; // Семафор для read_count
    sem_t rw_mutex; // Семафор для доступа к базе данных
} shared_t;

shared_t *shared = NULL; // Указатель на разделяемую память
int shm_fd = -1; // Дескриптор shared memory

// Флаг завершения
void parent_sigint(int signo) {
    if (shared) {
        shared->terminate = 1;
    }
}

// Фибоначчи
int fib(int n) {
    if (n <= 1) return n;
    int a = 0, b = 1;
    for (int i = 2; i <= n; ++i) {
        int c = a + b;
        a = b;
        b = c;
    }
    return b;
}

// Процесс-читатель
void reader_process(int id) {
    srand(getpid()); // Инициализация PID

    // Основной цикл читателя
    while (!shared->terminate) {
        // Блокируем mutex, изменяем read_count
        sem_wait(&shared->mutex); 
        shared->read_count++; 
        if (shared->read_count == 1) {
            sem_wait(&shared->rw_mutex); // Первый читатель блокирует писателей
        }
        sem_post(&shared->mutex); // Освобождаем mutex, чтобы другие читатели могли менять

        int idx = rand() % 20; // Выбираем случайно число из быза данных
        int value = shared->db[idx]; // Считываем это число из массива
        int fib_val = fib(value % 20); // Вычисляем для него число Фибоначи

        // Вывод результата
        printf("READER %d | PID=%d : idx=%d value=%d fib=%d\n",
               id, getpid(), idx, value, fib_val); 

        // Блокируем mutex, уменьшаем read_count
        sem_wait(&shared->mutex);
        shared->read_count--;
        // Если читатель последний, то открываем доступ писателям
        if (shared->read_count == 0) {
            sem_post(&shared->rw_mutex);
        }
        sem_post(&shared->mutex); // Освобождаем mutex

        sleep(1); // Пауза
    }

    _exit(0); // Когда флаг 1, завершаем процесс
}

// Сортировка по возрастанию(непротиворечивое состояние для массива)
int int_cmp(const void *a, const void *b) {
    int ia = *(const int *)a;
    int ib = *(const int *)b;
    if (ia < ib) return -1;
    if (ia > ib) return 1;
    return 0;
}

// Процесс-писатель
void writer_process(int id) {
    srand(getpid()); // Инициализация PID

    // Основной цикл писателя 
    while (!shared->terminate) {
        // Получение доступа к базе данных
        sem_wait(&shared->rw_mutex);

        int idx = rand() % 20; // Выбираем случайны индекс
        int old = shared->db[idx]; // Запоминаем старое значение для индекса
        int new_val = (rand() % 1000) + 1; // Генерируем новое значение от 1 до 1000
        shared->db[idx] = new_val; // Записываем его в массив
        qsort(shared->db, 20, sizeof(int), int_cmp); // Переход в новое непротиворечивое состояние

        // Вывод результата
        printf("WRITER %d | PID=%d : idx=%d old=%d new=%d\n",
               id, getpid(), idx, old, new_val); 

        sem_post(&shared->rw_mutex); // Освобожадаем rw_mutex

        sleep(2); // Пауза
    }

    _exit(0); // Когда флаг 1, завершаем процесс
}

// Удаление семафоров и shared memory 
void cleanup_parent(void) {
    if (shared) {
        // Удаление неименованных семафоров
        sem_destroy(&shared->mutex);
        sem_destroy(&shared->rw_mutex);

        // Удаление shared memory
        munmap(shared, sizeof(shared_t));
        shared = NULL;
    }

    if (shm_fd != -1) {
        // Закрываем дескриптор
        close(shm_fd);
        shm_fd = -1;

        // Удаление объекта POSIX 
        shm_unlink(shar_object); 
    }
}

int main(void) {
    // Генерация рандомного количества писателей и читателей от 1 до 5
    srand(time(NULL));
    int N = (rand() % 5) + 1; 
    int K = (rand() % 5) + 1; 

    // Вывод информации о количестве писателей и читателей
    printf("Starting with %d readers and %d writers\n", N, K);

    // Создаем/открываем объект в циклах обработки ошибок
    shm_fd = shm_open(shar_object, O_CREAT | O_RDWR, 0666);
    if (shm_fd == -1) {
        perror("shm_open");
        exit(1);
    }
    if (ftruncate(shm_fd, sizeof(shared_t)) == -1) {
        perror("ftruncate");
        shm_unlink(shar_object);
        exit(1);
    }

    // Получаем доступ к памяти в цикле обработка ошибки
    shared = mmap(NULL, sizeof(shared_t),
                  PROT_READ | PROT_WRITE, MAP_SHARED,
                  shm_fd, 0);
    if (shared == MAP_FAILED) {
        perror("mmap");
        shm_unlink(shar_object);
        exit(1);
    }

    // Инициализируем массив последовательностью от 1 до 20
    for (int i = 0; i < 20; ++i) {
        shared->db[i] = i + 1;  
    }

    shared->read_count = 0; // Ни один читатель не активен
    shared->terminate = 0; // Флаг завершения = 0 - все процессы работают

    // Инициализация неименованных семафоров
    if (sem_init(&shared->mutex, 1, 1) == -1) {
        perror("sem_init mutex");
        cleanup_parent();
        exit(1);
    }
    if (sem_init(&shared->rw_mutex, 1, 1) == -1) {
        perror("sem_init rw_mutex");
        cleanup_parent();
        exit(1);
    }

    // обработчик SIGINT в родителе
    signal(SIGINT, parent_sigint);

    // Порождаем N читателей
    for (int i = 0; i < N; ++i) {
        pid_t pid = fork();
        if (pid == 0) {
            reader_process(i);
        }  
    }

    // Порождаем K писателей
    for (int i = 0; i < K; ++i) {
        pid_t pid = fork();
        if (pid == 0) {
            writer_process(i);
        } 
    }

    // Ждём завершения всех процессов читателей и писателей
    int status;
    while (wait(&status) > 0)
        ;

    // очистка ресурсов
    cleanup_parent();

    return 0;
}
