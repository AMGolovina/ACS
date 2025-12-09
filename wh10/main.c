#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>

// Глобальные переменные
int buffer[200]; // Буфер
int count = 0; // Кол-во чисел в буфере
int active_src = 100; // Активные источники
int active_add = 0; // Активные суматоры

pthread_mutex_t m; // Мьютекс
pthread_cond_t c; // Оповещение основного потока

// Поток-источник
void* source(void* arg) {
    long id = (long)arg; // ID потока
    unsigned int seed = time(NULL) ^ id; // Уникальный сид

    // Задержка от 1 до 7 секунд
    sleep(1 + rand_r(&seed) % 7);
    // Случайного числа от 1 до 100
    int val = 1 + rand_r(&seed) % 100;

    // Блокируем доступ к общим данным перед записью
    pthread_mutex_lock(&m);

    buffer[count++] = val; // Кладем число в буфер
    active_src--;          // Уменьшаем счетчик оставшихся источников

    // Выводим информацию в консоль
    printf("[Источник %3ld] Поступило: %d. (В буфере: %d)\n", id, val, count);

    // Сигнал главному потоку, что появились данные
    pthread_cond_signal(&c);

    // Освобождаем доступ
    pthread_mutex_unlock(&m);
    return NULL;
}

// Поток-сумматор
void* adder(void* arg) {
    long packed = (long)arg;
    // Распаковываем два числа из одного long (битовые операции)
    int a = packed >> 16;
    int b = packed & 0xFFFF;

    unsigned int seed = time(NULL) ^ a;
    sleep(3 + rand_r(&seed) % 4); // Задержка от 3 до 6 секунд
    int sum = a + b;

    // Блокируем доступ перед записью результата
    pthread_mutex_lock(&m);

    buffer[count++] = sum; // Возвращаем сумму в общий буфер
    active_add--; // Уменьшаем счетчик активных сумматоров

    printf("[Сумматор] %d + %d = %d. (В буфере: %d)\n", a, b, sum, count);

    // Сигнал главному потоку
    pthread_cond_signal(&c);

    pthread_mutex_unlock(&m);
    return NULL;
}

int main() {
    // Инициализация мьютекса и условной переменной
    pthread_mutex_init(&m, NULL);
    pthread_cond_init(&c, NULL);

    pthread_t t;
    // Запускаем 100 потоков-источников
    // Передаем i как аргумент (ID источника)
    for(long i = 1; i <= 100; i++)
        pthread_create(&t, NULL, source, (void*)i);

    pthread_mutex_lock(&m);

    // Бесконечный цикл главного потока
    while(1) {
        // Пока в буфере меньше 2 чисел, мы не можем запустить суммирование
        while(count < 2) {
            // Проверка условия завершения программы
            if(active_src == 0 && active_add == 0 && count == 1) {
                printf("ИТОГ: %d\n", buffer[0]);

                // Корректное завершение и выход из программы
                pthread_mutex_unlock(&m);
                pthread_mutex_destroy(&m);
                pthread_cond_destroy(&c);
                return 0;
            }
            // Ждем сигнала от потоков, если данных мало
            pthread_cond_wait(&c, &m);
        }

        // Берем два последних числа из буфера
        int a = buffer[--count];
        int b = buffer[--count];
        active_add++; // Увеличиваем счетчик запущенных сумматоров

        // Упаковываем два числа в одну переменную
        long packed = (a << 16) | b;

        // Разблокируем мьютекс, чтобы создать поток
        pthread_mutex_unlock(&m);

        // Запускаем поток-сумматор
        pthread_create(&t, NULL, adder, (void*)packed);
        pthread_detach(t);

        // Блокируем мьютекс для следующей итерации
        pthread_mutex_lock(&m);
    }
}
