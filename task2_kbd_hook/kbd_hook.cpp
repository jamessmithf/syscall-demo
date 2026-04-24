/*
 * kbd_hook.cpp — глобальне перехоплення клавіатури через /dev/input/
 *
 * Принцип роботи:
 *   Linux представляє кожен пристрій вводу як файл /dev/input/eventN.
 *   Ядро записує туди struct input_event при кожному натисканні/відпусканні.
 *   Читаючи цей файл, ми отримуємо ВСІ події незалежно від того, яке вікно
 *   є активним — це і є "hook" клавіатури на рівні ядра.
 *
 * Використовувані системні виклики:
 *   open(2)     — відкрити файл пристрою
 *   read(2)     — прочитати struct input_event
 *   ioctl(2)    — EVIOCGNAME: отримати назву пристрою
 *                 EVIOCGBIT:  перевірити підтримувані типи подій
 *   close(2)    — закрити дескриптор
 *
 * Структура подій (з <linux/input.h>):
 *   struct input_event {
 *       struct timeval time;   // час події
 *       __u16 type;            // EV_KEY, EV_REL, EV_ABS, ...
 *       __u16 code;            // KEY_A, KEY_ENTER, ...
 *       __s32 value;           // 0=відпускання, 1=натискання, 2=утримання
 *   };
 *
 * Запуск:
 *   sudo ./kbd_hook             — авто-пошук клавіатури
 *   sudo ./kbd_hook /dev/input/eventN — конкретний пристрій
 *
 * Демо-сценарій:
 *   Термінал 1: sudo ./kbd_hook   (перехоплювач)
 *   Термінал 2: будь-який текстовий редактор / terminal — набираємо текст
 *   Термінал 1 показує все, що набираємо в Термінал 2.
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#include <csignal>
#include <dirent.h>
#include <sys/ioctl.h>
#include <linux/input.h>

/* ── Таблиця відповідності keycode → рядок ──────────────────────────────── */
static const char *KEY_NAMES[KEY_MAX + 1] = {};

static void init_key_names()
{
    /* Заповнюємо тільки "видимі" клавіші — решта буде nullptr */
    KEY_NAMES[KEY_ESC]        = "ESC";
    KEY_NAMES[KEY_1]          = "1";       KEY_NAMES[KEY_2]  = "2";
    KEY_NAMES[KEY_3]          = "3";       KEY_NAMES[KEY_4]  = "4";
    KEY_NAMES[KEY_5]          = "5";       KEY_NAMES[KEY_6]  = "6";
    KEY_NAMES[KEY_7]          = "7";       KEY_NAMES[KEY_8]  = "8";
    KEY_NAMES[KEY_9]          = "9";       KEY_NAMES[KEY_0]  = "0";
    KEY_NAMES[KEY_MINUS]      = "-";       KEY_NAMES[KEY_EQUAL]     = "=";
    KEY_NAMES[KEY_BACKSPACE]  = "⌫";
    KEY_NAMES[KEY_TAB]        = "⇥";
    KEY_NAMES[KEY_Q]="q"; KEY_NAMES[KEY_W]="w"; KEY_NAMES[KEY_E]="e";
    KEY_NAMES[KEY_R]="r"; KEY_NAMES[KEY_T]="t"; KEY_NAMES[KEY_Y]="y";
    KEY_NAMES[KEY_U]="u"; KEY_NAMES[KEY_I]="i"; KEY_NAMES[KEY_O]="o";
    KEY_NAMES[KEY_P]="p";
    KEY_NAMES[KEY_LEFTBRACE]  = "[";       KEY_NAMES[KEY_RIGHTBRACE] = "]";
    KEY_NAMES[KEY_ENTER]      = "↵ ENTER";
    KEY_NAMES[KEY_LEFTCTRL]   = "CTRL_L";
    KEY_NAMES[KEY_A]="a"; KEY_NAMES[KEY_S]="s"; KEY_NAMES[KEY_D]="d";
    KEY_NAMES[KEY_F]="f"; KEY_NAMES[KEY_G]="g"; KEY_NAMES[KEY_H]="h";
    KEY_NAMES[KEY_J]="j"; KEY_NAMES[KEY_K]="k"; KEY_NAMES[KEY_L]="l";
    KEY_NAMES[KEY_SEMICOLON]  = ";";       KEY_NAMES[KEY_APOSTROPHE] = "'";
    KEY_NAMES[KEY_GRAVE]      = "`";
    KEY_NAMES[KEY_LEFTSHIFT]  = "SHIFT_L";
    KEY_NAMES[KEY_BACKSLASH]  = "\\";
    KEY_NAMES[KEY_Z]="z"; KEY_NAMES[KEY_X]="x"; KEY_NAMES[KEY_C]="c";
    KEY_NAMES[KEY_V]="v"; KEY_NAMES[KEY_B]="b"; KEY_NAMES[KEY_N]="n";
    KEY_NAMES[KEY_M]="m";
    KEY_NAMES[KEY_COMMA]      = ",";       KEY_NAMES[KEY_DOT]       = ".";
    KEY_NAMES[KEY_SLASH]      = "/";
    KEY_NAMES[KEY_RIGHTSHIFT] = "SHIFT_R";
    KEY_NAMES[KEY_KPASTERISK] = "KP*";
    KEY_NAMES[KEY_LEFTALT]    = "ALT_L";
    KEY_NAMES[KEY_SPACE]      = "SPACE";
    KEY_NAMES[KEY_CAPSLOCK]   = "CAPS";
    KEY_NAMES[KEY_F1] ="F1";  KEY_NAMES[KEY_F2] ="F2";
    KEY_NAMES[KEY_F3] ="F3";  KEY_NAMES[KEY_F4] ="F4";
    KEY_NAMES[KEY_F5] ="F5";  KEY_NAMES[KEY_F6] ="F6";
    KEY_NAMES[KEY_F7] ="F7";  KEY_NAMES[KEY_F8] ="F8";
    KEY_NAMES[KEY_F9] ="F9";  KEY_NAMES[KEY_F10]="F10";
    KEY_NAMES[KEY_NUMLOCK]    = "NUMLOCK";
    KEY_NAMES[KEY_SCROLLLOCK] = "SCROLLOCK";
    KEY_NAMES[KEY_KP7]="KP7"; KEY_NAMES[KEY_KP8]="KP8";
    KEY_NAMES[KEY_KP9]="KP9"; KEY_NAMES[KEY_KPMINUS]="KP-";
    KEY_NAMES[KEY_KP4]="KP4"; KEY_NAMES[KEY_KP5]="KP5";
    KEY_NAMES[KEY_KP6]="KP6"; KEY_NAMES[KEY_KPPLUS]="KP+";
    KEY_NAMES[KEY_KP1]="KP1"; KEY_NAMES[KEY_KP2]="KP2";
    KEY_NAMES[KEY_KP3]="KP3"; KEY_NAMES[KEY_KP0]="KP0";
    KEY_NAMES[KEY_KPDOT]      = "KP.";
    KEY_NAMES[KEY_F11]="F11"; KEY_NAMES[KEY_F12]="F12";
    KEY_NAMES[KEY_KPENTER]    = "KP_ENTER";
    KEY_NAMES[KEY_RIGHTCTRL]  = "CTRL_R";
    KEY_NAMES[KEY_KPSLASH]    = "KP/";
    KEY_NAMES[KEY_SYSRQ]      = "SYSRQ";
    KEY_NAMES[KEY_RIGHTALT]   = "ALT_R";
    KEY_NAMES[KEY_HOME]       = "HOME";
    KEY_NAMES[KEY_UP]         = "↑";
    KEY_NAMES[KEY_PAGEUP]     = "PGUP";
    KEY_NAMES[KEY_LEFT]       = "←";
    KEY_NAMES[KEY_RIGHT]      = "→";
    KEY_NAMES[KEY_END]        = "END";
    KEY_NAMES[KEY_DOWN]       = "↓";
    KEY_NAMES[KEY_PAGEDOWN]   = "PGDN";
    KEY_NAMES[KEY_INSERT]     = "INS";
    KEY_NAMES[KEY_DELETE]     = "DEL";
    KEY_NAMES[KEY_LEFTMETA]   = "META_L";
    KEY_NAMES[KEY_RIGHTMETA]  = "META_R";
    KEY_NAMES[KEY_COMPOSE]    = "MENU";
}

/* ── Перевірка: чи підтримує пристрій EV_KEY (клавіатурні події) ─────── */
static bool has_key_events(int fd)
{
    /*
     * ioctl EVIOCGBIT(EV_KEY, ...) — повертає бітову маску підтримуваних
     * кодів клавіш.  Нас цікавить лише наявність EV_KEY у типах подій.
     */
    unsigned char evbits[(EV_MAX + 7) / 8] = {};
    if (ioctl(fd, EVIOCGBIT(0, sizeof(evbits)), evbits) < 0)
        return false;
    return (evbits[EV_KEY / 8] >> (EV_KEY % 8)) & 1;
}

/* ── Отримати назву пристрою через ioctl EVIOCGNAME ──────────────────── */
static void get_device_name(int fd, char *buf, size_t sz)
{
    if (ioctl(fd, EVIOCGNAME(sz - 1), buf) < 0)
        strncpy(buf, "невідомо", sz);
}

/* ── Автоматичний пошук клавіатури у /dev/input/ ──────────────────────── */
static int find_keyboard(char *found_path, size_t path_sz)
{
    DIR *dir = opendir("/dev/input");
    if (!dir) {
        perror("opendir(/dev/input)");
        return -1;
    }

    int best_fd = -1;
    struct dirent *ent;
    while ((ent = readdir(dir)) != nullptr) {
        if (strncmp(ent->d_name, "event", 5) != 0) continue;

        char path[11 + NAME_MAX + 1];
        snprintf(path, sizeof(path), "/dev/input/%s", ent->d_name);

        int fd = open(path, O_RDONLY | O_NONBLOCK);
        if (fd < 0) continue;

        char name[256] = {};
        get_device_name(fd, name, sizeof(name));

        /* Шукаємо пристрій з "keyboard" у назві або KEY-bitmap з літерами */
        if (has_key_events(fd)) {
            /* Перевіряємо, чи є серед підтримуваних клавіш літери (KEY_A..KEY_Z) */
            unsigned char keybits[(KEY_MAX + 7) / 8] = {};
            ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keybits)), keybits);
            bool has_alpha = (keybits[KEY_A / 8] >> (KEY_A % 8)) & 1;

            if (has_alpha) {
                printf("[пошук] Знайдено клавіатуру: %s → \"%s\"\n", path, name);
                if (best_fd >= 0) close(best_fd);
                best_fd = fd;
                snprintf(found_path, path_sz, "%s", path);
                /* Якщо явно "keyboard" — зупиняємось */
                if (strcasestr(name, "keyboard") != nullptr) break;
                continue;
            }
        }
        close(fd);
    }
    closedir(dir);
    return best_fd;
}

/* ── Прапор завершення ───────────────────────────────────────────────── */
static volatile sig_atomic_t running = 1;
static void handle_sigint(int) { running = 0; }

/* ══════════════════════════════════════════════════════════════════════ */
int main(int argc, char *argv[])
{
    init_key_names();
    signal(SIGINT, handle_sigint);

    int  kbd_fd   = -1;
    char dev_path[11 + NAME_MAX + 1] = {};

    if (argc >= 2) {
        /* Пристрій вказано явно */
        strncpy(dev_path, argv[1], sizeof(dev_path) - 1);
        kbd_fd = open(dev_path, O_RDONLY);
        if (kbd_fd < 0) {
            fprintf(stderr, "open(%s): %s\n", dev_path, strerror(errno));
            return 1;
        }
    } else {
        /* Автоматичний пошук */
        kbd_fd = find_keyboard(dev_path, sizeof(dev_path));
        if (kbd_fd < 0) {
            fprintf(stderr,
                    "[!] Не вдалось знайти клавіатуру.\n"
                    "    Переконайтесь, що запущено від root або є доступ до /dev/input/\n"
                    "    Спробуйте: sudo ./kbd_hook /dev/input/eventN\n");
            return 1;
        }
    }

    /* Переключаємо fd у блокуючий режим (для циклу read) */
    {
        int flags = fcntl(kbd_fd, F_GETFL);
        fcntl(kbd_fd, F_SETFL, flags & ~O_NONBLOCK);
    }

    char dev_name[256] = {};
    get_device_name(kbd_fd, dev_name, sizeof(dev_name));

    printf("╔════════════════════════════════════════════╗\n");
    printf("║        KEYBOARD HOOK via /dev/input/       ║\n");
    printf("╚════════════════════════════════════════════╝\n");
    printf("Пристрій : %s\n", dev_path);
    printf("Назва    : %s\n", dev_name);
    printf("Набирайте текст у будь-якому вікні.\n");
    printf("Ctrl+C — зупинити.\n");
    printf("────────────────────────────────────────────\n");
    fflush(stdout);

    struct input_event ev;
    bool shift_held = false;   /* відстежуємо Shift для верхнього регістру */

    /*
     * Основний цикл:
     *   read(2) блокується до появи нової події від ядра.
     *   Ядро записує struct input_event у /dev/input/eventN при кожному
     *   натисканні/відпусканні будь-якої клавіші.
     */
    while (running) {
        ssize_t n = read(kbd_fd, &ev, sizeof(ev));

        if (n < 0) {
            if (errno == EINTR) continue;   /* перервано сигналом */
            perror("read");
            break;
        }
        if (n != (ssize_t)sizeof(ev)) continue;  /* частковий запис ядра */

        /*
         * Фільтруємо:
         *   ev.type  == EV_KEY  — клавіатурна подія
         *   ev.value == 1       — натискання (press)
         *   ev.value == 2       — утримання (repeat)
         *   ev.value == 0       — відпускання (release)
         */
        if (ev.type != EV_KEY) continue;

        /* Відстежуємо стан Shift */
        if (ev.code == KEY_LEFTSHIFT || ev.code == KEY_RIGHTSHIFT) {
            shift_held = (ev.value != 0);
        }

        /* Виводимо лише press + repeat */
        if (ev.value != 1 && ev.value != 2) continue;

        const char *name = (ev.code <= KEY_MAX) ? KEY_NAMES[ev.code] : nullptr;
        const char *action = (ev.value == 2) ? "(repeat)" : "";

        if (name) {
            /* Верхній регістр для звичайних літер при Shift */
            char upper[8] = {};
            const char *display = name;
            if (shift_held && strlen(name) == 1 && name[0] >= 'a' && name[0] <= 'z') {
                upper[0] = (char)(name[0] - 32);
                display  = upper;
            }
            printf("  KEY [%3u] %-12s  %s\n", ev.code, display, action);
        } else {
            printf("  KEY [%3u] <невідомо>       %s\n", ev.code, action);
        }
        fflush(stdout);
    }

    close(kbd_fd);
    printf("\n[kbd_hook] завершення.\n");
    return 0;
}
