#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <stdint.h>

// Флаг, получен ли приёмник
volatile sig_atomic_t ack_received = 0;
// PID приёмника
pid_t pid = 0;

// Обработчик SIGUSR1 от приёмника
void handle_ack(int sig) {
    (void)sig;
    ack_received = 1;
}

// Функция печати 32‑битного числа в двоичном виде
void print_bits32(uint32_t u) {
    for (int i = 31; i >= 0; --i)
        putchar( (u & (1u << i)) ? '1' : '0' );
    putchar('\n');
}

int main(void) {
    // Исходное число
    int32_t number;

    // Печать PID отправителя
    printf("Sender pid = %d\n", getpid());
    // Запрос PID приёмника
    printf("Input receiver PID: ");
    // Считываем PID приёмника из стандартного ввода
    if (scanf("%d", &pid) != 1) {
        // Обработка ошибочного PID
        fprintf(stderr, "Failed to read receiver PID\n");
        return 1;
    }

    // Назначаем функцию handle_ack обработчиком SIGUSR1
    signal(SIGUSR1, handle_ack);

    // Запрашиваем числа у пользователя
    printf("Input decimal integer number: ");
    // Считываем число
    if (scanf("%d", &number) != 1) {
        // Обработка ошибки
        fprintf(stderr, "Failed to read number\n");
        return 1;
    }

    // Перевод в беззнаковое для удобства подстановки
    uint32_t u = (uint32_t)number;

    // Печатаем двоичное представление исходного числа
    print_bits32(u);

    // Цикл отправки 32 бит от старшего к младшему
    for (int i = 31; i >= 0; --i) {
        // Выделяем бит
        int bit = (u >> i) & 1;
        // Выбираем сигнал
        int sig = bit ? SIGUSR2 : SIGUSR1;

        // Сбрасываем флаг подтверждения перед отправкой
        ack_received = 0;

        // Отправляем выбранный сигнал приёмнику
        if (kill(pid, sig) == -1) {
            // Обработка ошибки
            perror("kill");
            return 1;
        }

        // Ждём установки флага = 0
        while (!ack_received)
            // Останавливаемся до прихода следующего сигнала
            pause();
    }

    // После отправки всех битов посылаем SIGINT как сигнал конца передачи
    if (kill(pid, SIGINT) == -1) {
        // Обработка ошибки
        perror("kill SIGINT");
        return 1;
    }

    // Печатаем восстановленное десятичное значение
    printf("Result = %d\n", number);

    return 0;
}
