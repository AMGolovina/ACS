#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <semaphore.h>
#include <signal.h>
#include <time.h>

const char *shm_name       = "/posix-shar-object2"; // Имя объекта разделяемой памяти
const char *sem_mutex_name = "/rw_mutex_sem_named"; // Имя семафора для счётчика читателей
const char *sem_rw_name    = "/rw_db_sem_named"; // Имя семафора для доступа к массиву
const char *fifo_name      = "/tmp/idz_observer_fifo"; // Очередь для отправки сообщений наблюдателю

// Структура, лежащая в POSIX shared memory
typedef struct {
    int db[20]; // Массив целых положительных чисел(база данных)
    int read_count; // Число читателей
    int terminate; // Флаг завершения
} shared_t;

shared_t *shared = NULL; // Указатель на shared memory
sem_t *mutex = NULL; // Семафор для read_count
sem_t *rw_mutex = NULL; // Семафор для эксклюзивного доступа к массиву

// Флаг завершения
void sigint_handler(int signo) {
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

int main(void) {
    // Обработчик SIGINT для завершения по Ctrl+C
    signal(SIGINT, sigint_handler);

    // Создаем/открываем объект в цикле обработка ошибоки
    int shm_fd = shm_open(shm_name, O_RDWR, 0666);
    if (shm_fd == -1) {
        perror("shm_open");
        exit(1);
    }

    // Получаем доступ к памяти в цикле обработка ошибки
    shared = mmap(NULL, sizeof(shared_t),
                  PROT_READ | PROT_WRITE, MAP_SHARED,
                  shm_fd, 0);
    if (shared == MAP_FAILED) {
        perror("mmap");
        exit(1);
    }

    // Открываем именованный семафор для счётчика читателей
    mutex = sem_open(sem_mutex_name, 0);
    if (mutex == SEM_FAILED) {
        perror("sem_open mutex");
        exit(1);
    }

    // Открываем именованный семафор для доступа к массиву
    rw_mutex = sem_open(sem_rw_name, 0);
    if (rw_mutex == SEM_FAILED) {
        perror("sem_open rw_mutex");
        exit(1);
    }

    // открываем FIFO для записи в наблюдатель
    int fifo_fd = open(fifo_name, O_WRONLY);
    if (fifo_fd == -1) {
        perror("open fifo");
    }

    // Инициализация PID
    srand(getpid());
    printf("Reader started, PID=%d\n", getpid());

    // Основной цикл работы читателя
    while (!shared->terminate) {
        // Блокируем mutex, изменяем read_count
        sem_wait(mutex);
        shared->read_count++;
        if (shared->read_count == 1) {
            sem_wait(rw_mutex); // Первый читатель блокирует писателей
        }
        sem_post(mutex); // Освобождаем mutex, чтобы другие читатели могли менять

        int idx = rand() % 20; // Выбираем случайно число из быза данных
        int value = shared->db[idx]; // Считываем это число из массива
        int fib_val = fib(value % 20); // Вычисляем для него число Фибоначи

        // Вывод результата
        printf("READER | PID=%d : idx=%d value=%d fib=%d\n",
               getpid(), idx, value, fib_val);

        // дублируем сообщение в именованный канал для observer
        char buf[128];
        int len = snprintf(buf, sizeof(buf),
                           "READER | PID=%d : idx=%d value=%d fib=%d\n",
                           getpid(), idx, value, fib_val);
        if (len > 0) {
            write(fifo_fd, buf, (size_t)len);
        }


        // Блокируем mutex, уменьшаем read_count
        sem_wait(mutex);
        shared->read_count--;
        // Если читатель последний, то открываем доступ писателям
        if (shared->read_count == 0) {
            sem_post(rw_mutex);
        }
        sem_post(mutex); // Освобождаем mutex

        sleep(1); // Пауза
    }

    munmap(shared, sizeof(shared_t)); // Удаление shared memory
    close(shm_fd); // Закрываем дескриптор

    // Удаление именованных семафоров
    sem_close(mutex);
    sem_close(rw_mutex);

    close(fifo_fd); // Закрываем FIFO

    return 0;
}
