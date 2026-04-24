/*
 * monitor.cpp — читає пам'ять іншого процесу двома методами:
 *
 * Метод А (основний): process_vm_readv(2)
 *   Системний виклик, що з'явився в Linux 3.2.
 *   Читає вміст адресного простору іншого процесу напряму,
 *   без ptrace-прив'язки, через scatter-gather iovec-вектори.
 *   Вимагає: однаковий uid або CAP_SYS_PTRACE, або
 *             prctl(PR_SET_PTRACER_ANY) у цільового процесу.
 *
 * Метод Б (резервний): /proc/[pid]/mem + pread(2)
 *   Псевдофайл ядра, що відображає весь VA-простір процесу.
 *   Зсув у файлі = віртуальна адреса.
 *   pread(2) — позиційне читання без зміни поточної позиції fd.
 *
 * Використовуються системні виклики:
 *   open(2), pread(2), close(2)  — для /proc/[pid]/mem
 *   process_vm_readv(2)          — прямий VM-доступ
 *   usleep(3) / nanosleep(2)     — опитування з паузою
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#include <sys/uio.h>      /* process_vm_readv, iovec  */
#include <sys/types.h>
#include <csignal>

/* Інтервал опитування пам'яті (мікросекунди) */
static const int POLL_US = 100'000;   /* 100 мс */

static volatile sig_atomic_t running = 1;
static void handle_sigint(int) { running = 0; }

/* ── Метод А: process_vm_readv ──────────────────────────────────────────── */
static bool read_via_vm_readv(pid_t pid, unsigned long addr, int *out)
{
    struct iovec local  = { .iov_base = out,                    .iov_len = sizeof(int) };
    struct iovec remote = { .iov_base = (void *)addr,           .iov_len = sizeof(int) };

    /*
     * process_vm_readv(2):
     *   pid        — цільовий процес
     *   &local, 1  — iovec у НАШОМУ адресному просторі (куди писати)
     *   &remote, 1 — iovec у ЧУЖОМУ адресному просторі (звідки читати)
     *   0          — прапори (зарезервовані, мають бути 0)
     *
     * Повертає кількість прочитаних байт або -1 при помилці.
     */
    ssize_t n = process_vm_readv(pid, &local, 1, &remote, 1, 0);
    return n == (ssize_t)sizeof(int);
}

/* ── Метод Б: /proc/[pid]/mem + pread ───────────────────────────────────── */
static bool read_via_proc_mem(int mem_fd, unsigned long addr, int *out)
{
    /*
     * pread(2): читає sizeof(int) байт з файлового дескриптора mem_fd,
     * починаючи з позиції addr (= адреса у VA-просторі цільового процесу).
     * Не змінює поточну позицію fd — важливо для циклічного опитування.
     */
    ssize_t n = pread(mem_fd, out, sizeof(int), (off_t)addr);
    return n == (ssize_t)sizeof(int);
}

/* ── Головний цикл моніторингу ──────────────────────────────────────────── */
int main(int argc, char *argv[])
{
    if (argc != 3) {
        fprintf(stderr,
                "Використання: %s <pid> <hex_address>\n"
                "  pid         — PID цільового процесу\n"
                "  hex_address — адреса змінної (виводиться target-ом)\n",
                argv[0]);
        return 1;
    }

    pid_t        pid  = (pid_t)atoi(argv[1]);
    unsigned long addr = strtoul(argv[2], nullptr, 16);

    signal(SIGINT, handle_sigint);

    /* ── Відкриваємо /proc/[pid]/mem (Метод Б) ──────────────────────────── */
    char mem_path[64];
    snprintf(mem_path, sizeof(mem_path), "/proc/%d/mem", (int)pid);

    int mem_fd = open(mem_path, O_RDONLY);
    if (mem_fd < 0) {
        fprintf(stderr, "[!] open(%s): %s\n", mem_path, strerror(errno));
        fprintf(stderr, "    Переконайтесь, що target викликав prctl(PR_SET_PTRACER_ANY)\n");
        /* Продовжимо — можливо process_vm_readv спрацює */
    }

    printf("=== MONITOR ===\n");
    printf("Цільовий PID      : %d\n",  (int)pid);
    printf("Адреса            : 0x%lx\n", addr);
    printf("Шлях до пам'яті   : %s\n",  mem_path);
    printf("Інтервал опитування: %d мс\n", POLL_US / 1000);
    printf("Натисніть Ctrl+C для зупинки.\n");
    printf("───────────────────────────────────────────\n");
    fflush(stdout);

    int last_value   = 0;
    int current_value = 0;
    bool first_read  = true;

    /* Спочатку спробуємо зчитати початкове значення */
    {
        bool ok = false;
        if (!ok) ok = read_via_vm_readv(pid, addr, &last_value);
        if (!ok && mem_fd >= 0) ok = read_via_proc_mem(mem_fd, addr, &last_value);
        if (ok) {
            printf("[init] початкове значення = %d\n", last_value);
        } else {
            fprintf(stderr, "[!] Не вдалось прочитати початкове значення: %s\n",
                    strerror(errno));
        }
    }

    while (running) {
        bool ok = false;

        /* Спроба 1: process_vm_readv */
        if (!ok) ok = read_via_vm_readv(pid, addr, &current_value);

        /* Спроба 2: /proc/[pid]/mem */
        if (!ok && mem_fd >= 0) ok = read_via_proc_mem(mem_fd, addr, &current_value);

        if (!ok) {
            /* Процес, мабуть, завершився */
            fprintf(stderr, "[!] Не вдалось прочитати пам'ять: %s\n",
                    strerror(errno));
            fprintf(stderr, "    Цільовий процес завершився?\n");
            break;
        }

        if (first_read || current_value != last_value) {
            printf("[monitor] значення: %d → %d\n", last_value, current_value);
            fflush(stdout);
            last_value = current_value;
            first_read = false;
        }

        usleep(POLL_US);
    }

    if (mem_fd >= 0) close(mem_fd);
    printf("[monitor] завершення.\n");
    return 0;
}
