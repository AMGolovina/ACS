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

// Сортировка по возрастанию(непротиворечивое состояние для массива)
int int_cmp(const void *a, const void *b) {
    int ia = *(const int *)a;
    int ib = *(const int *)b;
    if (ia < ib) return -1;
    if (ia > ib) return 1;
    return 0;
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

    // Инициализация PID
    srand(getpid());
    printf("Writer started, PID=%d\n", getpid());

    // Основной цикл работы писателя
    while (!shared->terminate) {
        // Получение доступа к базе данных
        sem_wait(rw_mutex);

        int idx = rand() % 20; // Выбираем случайны индекс
        int old = shared->db[idx]; // Запоминаем старое значение для индекса
        int new_val = (rand() % 1000) + 1; // Генерируем новое значение от 1 до 1000
        shared->db[idx] = new_val; // Записываем его в массив
        qsort(shared->db, 20, sizeof(int), int_cmp); // Переход в новое непротиворечивое состояние

        // Вывод результата
        printf("WRITER | PID=%d : idx=%d old=%d new=%d\n",
               getpid(), idx, old, new_val);

        sem_post(&shared->rw_mutex); // Освобожадаем rw_mutex

        sleep(2);  // Пауза
    }

    munmap(shared, sizeof(shared_t)); // Удаление shared memory
    close(shm_fd); // Закрываем дескриптор

    // Удаление именованных семафоров
    sem_close(mutex);
    sem_close(rw_mutex);

    return 0;
}
