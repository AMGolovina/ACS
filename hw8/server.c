#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/mman.h>
#include <fcntl.h>

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
        data->terminate = 1; // Сигнал завершения
    }
    exit(0); // Завершение процесса
}

int main() {
    shm_fd = shm_open(SHM_NAME, O_RDWR, 0666); // Открываем объект
    // Обработка ошибки
    if (shm_fd == -1) {
        perror("shm_open");
        exit(1);
    }

    // Получить доступ к памяти
    data = mmap(NULL, sizeof(shm_data_t), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    // Обработка ошибки
    if (data == MAP_FAILED) {
        perror("mmap");
        exit(1);
    }

    signal(SIGINT, cleanup_and_exit); // Обработчик сигнала SIGINT

    // Цикл, выводящий текущее значение value из client, пока terminate = 0
    while (!data->terminate) {
        printf("Server read: %d\n", data->value);
        sleep(1);
    }

    close(shm_fd); // Закрываем открытый объект
    return 0;
}
