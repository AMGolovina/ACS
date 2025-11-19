#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <time.h>

#define SHM_NAME "/my-shm" // Имя объекта разделяемой памяти в системе POSIX

// Структура разделяемой памяти
typedef struct {
    int value;      // Генерируемое число
    int terminate;  // Флаг завершения
} shm_data_t;

volatile shm_data_t *data = NULL; // Указатель на область памяти
int shm_fd;                       // Дескриптор разделяемой памяти

// Метод для выхода из программы с помощью Ctrl+C
void cleanup_and_exit(int signo) {
    if (data) {
        data->terminate = 1;    // Сигнал завершения
    }
    if (shm_fd != -1) {
        shm_unlink(SHM_NAME);   // Удаляем общий сегмент
    }
    exit(0);
}

int main() {
    shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666); // Открываем объект
    // Обработка ошибки
    if (shm_fd == -1) {
        printf("Opening error\n");
        perror("shm_open");
        exit(1);
    }
    if (ftruncate(shm_fd, sizeof(shm_data_t)) == -1) {
        perror("ftruncate");
        exit(1);
    }

    // Получить доступ к памяти
    data = mmap(NULL, sizeof(shm_data_t), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    // Обработка ошибки
    if (data == MAP_FAILED) {
        perror("mmap");
        exit(1);
    }

    data->terminate = 0; // Инициализация флага завершения

    signal(SIGINT, cleanup_and_exit); // Обработчик сигнала SIGINT

    srand(time(NULL)); // Инициализация генератора чисел
    // Основной цикл генерирует рандомные числа до Ctrl+C
    while (!data->terminate) {
        data->value = rand() % 1000; // Как в семинаре, генерим числа до 999
        printf("Client generated: %d\n", data->value);
        sleep(1);
    }

    close(shm_fd); // Закрыть открытый объект
    return 0;
}
