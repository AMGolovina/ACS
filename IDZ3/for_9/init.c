#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>
#include <semaphore.h>
#include <sys/stat.h>

const char *shm_name       = "/posix-shar-object2"; // Имя объекта разделяемой памяти
const char *sem_mutex_name = "/rw_mutex_sem_named"; // Имя семафора для счётчика читателей
const char *sem_rw_name    = "/rw_db_sem_named"; // Имя семафора для доступа к массиву
const char *fifo_name      = "/tmp/idz_observer_fifo"; // Имя канала для наблюдателя

// Структура, лежащая в POSIX shared memory
typedef struct {
    int db[20]; // Массив целых положительных чисел(база данных)
    int read_count; // Число читателей
    int terminate; // Флаг завершения
} shared_t;

int main(void) {
    int shm_fd; // Дескриптор shared memory
    shared_t *shared; // указатель на отображённую разделяемую память
    sem_t *mutex; // указатель на именованный семафор для read_count
    sem_t *rw_mutex; // указатель на именованный семафор для БД

    // Удаляем старые объекты от прошлых запусков
    shm_unlink(shm_name);
    sem_unlink(sem_mutex_name);
    sem_unlink(sem_rw_name);

    mkfifo(fifo_name, 0666); // Создаем fifo

    // Создаем/открываем объект
    shm_fd = shm_open(shm_name, O_CREAT | O_RDWR, 0666);
    if (shm_fd == -1) {
        perror("shm_open");
        exit(1);
    }
    if (ftruncate(shm_fd, sizeof(shared_t)) == -1) { // Устанавливаем размер объекта
        perror("ftruncate");
        exit(1);
    }

    // Получаем доступ к памяти
    shared = mmap(NULL, sizeof(shared_t),
                  PROT_READ | PROT_WRITE, MAP_SHARED,
                  shm_fd, 0);
    if (shared == MAP_FAILED) {
        perror("mmap");
        exit(1);
    }

    // Инициализируем массив последовательностью от 1 до 20
    for (int i = 0; i < 20; ++i) {
        shared->db[i] = i + 1;
    }
    shared->read_count = 0; // Ни один читатель не активен
    shared->terminate = 0; // Флаг завершения = 0 - все процессы работают

    // Создаём именованный семафор для счётчика читателей
    mutex = sem_open(sem_mutex_name, O_CREAT, 0666, 1);
    if (mutex == SEM_FAILED) {
        perror("sem_open mutex");
        exit(1);
    }

    // Создаём именованный семафор для эксклюзивного доступа к массиву
    rw_mutex = sem_open(sem_rw_name, O_CREAT, 0666, 1);
    if (rw_mutex == SEM_FAILED) {
        perror("sem_open rw_mutex");
        exit(1);
    }

    // Сообщаем, что ресурсы инициализированы
    printf("Init: shared memory and named semaphores created.\n");
    printf("Run readers and writers in other consoles.\n");

    munmap(shared, sizeof(shared_t)); // Удаление shared memory
    close(shm_fd); // Закрываем дескриптор

    // Удаление именованных семафоров
    sem_close(mutex);
    sem_close(rw_mutex);

    return 0;
}
