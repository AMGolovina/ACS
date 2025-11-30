#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <signal.h>

// Массив имён каналов для разных наблюдателей
const char *fifo_names[4] = {
    "/tmp/idz_observer_fifo1",
    "/tmp/idz_observer_fifo2",
    "/tmp/idz_observer_fifo3",
    "/tmp/idz_observer_fifo4"
};

volatile sig_atomic_t stop = 0; // Флаг завершения

// Завершение программы
void sigint_handler(int signo) {
    stop = 1;
}

int main(int argc, char *argv[]) {
    // Обработчик SIGINT для завершения по Ctrl+C
    signal(SIGINT, sigint_handler);

    // Выбор налюдателя
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <observer_id 1-%d>\n", argv[0], 4);
        return 1;
    }
    int id = atoi(argv[1]);
    if (id < 1 || id > 4) {
        fprintf(stderr, "Observer id must be in 1..%d\n", 4);
        return 1;
    }

    const char *fifo_name = fifo_names[id - 1];

    // Создаем очередь
    mkfifo(fifo_name, 0666);

    // Открываем канал для чтения
    int fd = open(fifo_name, O_RDONLY);
    if (fd == -1) {
        perror("open fifo");
        exit(1);
    }

    printf("Observer %d started, PID=%d, FIFO=%s\n", id, getpid(), fifo_name);

    char buf[256];
    while (!stop) {
        ssize_t n = read(fd, buf, sizeof(buf) - 1);
        if (n > 0) {
            buf[n] = '\0';
            printf("[OBSERVE %d] %s", id, buf);   // Помечаем, какой наблюдатель выводит сообщение
        } else if (n == 0) {
            sleep(1);
        } else {
            perror("read fifo");
            break;
        }
    }

    close(fd); // Закрываем файловый дескриптор fifo
    return 0;
}
