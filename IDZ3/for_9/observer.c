#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <signal.h>

const char *fifo_name = "/tmp/idz_observer_fifo"; // Общий именованный канал

volatile sig_atomic_t stop = 0; // Флаг завершения

// Завершение программы
void sigint_handler(int signo) {
    stop = 1;
}

int main(void) {
    // Обработчик SIGINT для завершения по Ctrl+C
    signal(SIGINT, sigint_handler);

    // Создаем очередь
    mkfifo(fifo_name, 0666);

    // Открываем канал для чтения
    int fd = open(fifo_name, O_RDONLY);
    if (fd == -1) {
        perror("open fifo");
        exit(1);
    }

    printf("Observer started, PID=%d\n", getpid());
    printf("Waiting for messages from readers and writers...\n");

    char buf[256]; // Буфер, куда читаются данные
    // Основной цикл наблюдателя
    while (!stop) {
        ssize_t n = read(fd, buf, sizeof(buf) - 1); // Читаем из fifo до 255 байт
        if (n > 0) {
            buf[n] = '\0';      // Завершаем строку
            printf("[OBSERVE] %s", buf); // Выводим то, что прислали
        } else if (n == 0) { //Пауза
            sleep(1);
        } else { // Ошибка
            perror("read fifo");
            break;
        }
    }

    close(fd); // Закрываем файловый дескриптор fifo
    return 0;
}
