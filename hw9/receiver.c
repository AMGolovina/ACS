#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <stdint.h>

// Флаг завершения приёма
volatile sig_atomic_t done = 0;
// Принятые биты
volatile uint32_t value = 0;
// PID процесса‑отправителя
pid_t pid = 0;

// Обработчик сигналов SIGUSR1 и SIGUSR2
void handle_bit(int sig) {
    int bit = (sig == SIGUSR2) ? 1 : 0;
    value = (value << 1) | (uint32_t)bit;

    if (pid)
        kill(pid, SIGUSR1);
}

// Обработчик сигнала SIGINT
void handle_end(int sig) {
    (void)sig;
    done = 1;
}

// Функция для печати числа в двоичном коде
void print_bits32(uint32_t u) {
    for (int i = 31; i >= 0; --i)
        putchar( (u & (1u << i)) ? '1' : '0' );
    putchar('\n');
}

int main(void) {
    // Печать PID приёмника
    printf("Receiver PID: %d\n", getpid());
    // Запрос PID отправителя
    printf("Input sender PID: ");
    // Считываем PID отправителя
    if (scanf("%d", &pid) != 1) {
        // Обработка ошибочного PID
        fprintf(stderr, "Failed to read sender PID\n");
        return 1;
    }

    // Обработчик SIGUSR1
    signal(SIGUSR1, handle_bit);
    // Обработчик SIGUSR2
    signal(SIGUSR2, handle_bit);
    // Обработчик SIGINT для окончания передачи
    signal(SIGINT,  handle_end);

    // Ждем SIGINT
    while (!done)
        // Остановка
        pause();

    // Из беззнакового в знаковое
    int32_t result = (int32_t)value;

    // Печатаем принятые биты
    print_bits32(value);
    // Печатаем восстановленное десятичное значение
    printf("Result = %d\n", result);

    return 0;
}
