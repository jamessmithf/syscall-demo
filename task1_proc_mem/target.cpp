/*
 * target.cpp — "жертва": процес, чию пам'ять будемо читати.
 *
 * Системні виклики, що використовуються:
 *   getpid(2)   — отримати власний PID
 *   prctl(2)    — PR_SET_PTRACER_ANY: дозволити будь-якому процесу читати нашу
 *                 пам'ять через /proc/[pid]/mem та process_vm_readv(2) навіть
 *                 при ptrace_scope=1 (Yama LSM).
 *
 * Як запустити демонстрацію:
 *   Термінал 1:  ./target
 *   Термінал 2:  ./monitor <pid> <hex_addr>
 *                (pid та hex_addr виводяться на екран target-ом)
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <sys/prctl.h>
#include <csignal>
#include <atomic>

/* Змінна, за якою стежитиме monitor.
 * volatile — компілятор не оптимізує звертання; доступна ззовні. */
volatile int monitored_value = 0;

/* Прапор завершення (встановлюється обробником SIGINT). */
static volatile sig_atomic_t running = 1;

static void handle_sigint(int) { running = 0; }

int main()
{
    signal(SIGINT, handle_sigint);

    /*
     * PR_SET_PTRACER_ANY — системний виклик prctl(2).
     * Дозволяє будь-якому процесу читати пам'ять цього процесу
     * через process_vm_readv(2) / /proc/[pid]/mem навіть при
     * Yama ptrace_scope=1.  Без цього монітор отримає EPERM.
     */
    if (prctl(PR_SET_PTRACER, PR_SET_PTRACER_ANY, 0, 0, 0) != 0) {
        perror("prctl(PR_SET_PTRACER_ANY)");
        /* Не фатально — якщо ptrace_scope=0 або запуск під root,
         * monіtор і так зможе читати пам'ять. */
    }

    printf("=== TARGET PROCESS ===\n");
    printf("PID              : %d\n", (int)getpid());
    printf("&monitored_value : %p\n", (void *)&monitored_value);
    printf("======================\n");
    printf("Запустіть монітор:\n");
    printf("  ./monitor %d %p\n\n", (int)getpid(), (void *)&monitored_value);
    fflush(stdout);

    while (running) {
        printf("Введіть нове значення (Ctrl+C для виходу): ");
        fflush(stdout);

        int val;
        int ret = scanf("%d", &val);
        if (ret == EOF) {
            /* stdin закритий — чекаємо сигналу завершення */
            pause();
            break;
        }
        if (ret != 1) {
            /* Некоректний ввід — очищаємо рядок */
            int c;
            while ((c = getchar()) != '\n' && c != EOF) {}
            if (!running) break;
            continue;
        }

        monitored_value = val;
        printf("[target] monitored_value = %d\n\n", monitored_value);
        fflush(stdout);
    }

    printf("\n[target] завершення.\n");
    return 0;
}
