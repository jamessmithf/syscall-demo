/*
 * inject_events.cpp — тестовий інжектор подій клавіатури.
 *
 * Записує struct input_event у файл (може бути FIFO або звичайний файл).
 * kbd_hook читає з цього файлу як якби це був справжній /dev/input/eventN.
 *
 * Використання:
 *   mkfifo /tmp/fake_kbd
 *   ./kbd_hook /tmp/fake_kbd &
 *   ./inject_events /tmp/fake_kbd
 */

#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <sys/time.h>
#include <linux/input.h>

static void emit(int fd, int type, int code, int value)
{
    struct input_event ev{};
    gettimeofday(&ev.time, nullptr);
    ev.type  = (__u16)type;
    ev.code  = (__u16)code;
    ev.value = (__s32)value;
    ssize_t __attribute__((unused)) _n = write(fd, &ev, sizeof(ev));
}

/* Синтетичне натискання + відпускання */
static void press(int fd, int keycode, useconds_t delay_us = 80000)
{
    emit(fd, EV_MSC,  MSC_SCAN, keycode);   /* опціональний scan-код */
    emit(fd, EV_KEY,  keycode, 1);           /* PRESS  */
    emit(fd, EV_SYN,  SYN_REPORT, 0);       /* синхро-подія          */
    usleep(delay_us);
    emit(fd, EV_KEY,  keycode, 0);           /* RELEASE */
    emit(fd, EV_SYN,  SYN_REPORT, 0);
    usleep(delay_us / 4);
}

int main(int argc, char *argv[])
{
    const char *path = argc >= 2 ? argv[1] : "/tmp/fake_kbd";

    int fd = open(path, O_WRONLY);
    if (fd < 0) { perror("open"); return 1; }

    printf("[inject] Sending test key sequence to %s\n", path);
    fflush(stdout);

    /* Симулюємо введення тексту "hello" */
    press(fd, KEY_H);
    press(fd, KEY_E);
    press(fd, KEY_L);
    press(fd, KEY_L);
    press(fd, KEY_O);
    press(fd, KEY_SPACE);

    /* Shift + "W" = "W" */
    emit(fd, EV_KEY, KEY_LEFTSHIFT, 1);
    emit(fd, EV_SYN, SYN_REPORT, 0);
    press(fd, KEY_W);
    emit(fd, EV_KEY, KEY_LEFTSHIFT, 0);
    emit(fd, EV_SYN, SYN_REPORT, 0);

    press(fd, KEY_O);
    press(fd, KEY_R);
    press(fd, KEY_L);
    press(fd, KEY_D);
    press(fd, KEY_ENTER);

    /* Ctrl+C (щоб завершити kbd_hook) */
    usleep(100000);
    emit(fd, EV_KEY, KEY_LEFTCTRL, 1);
    emit(fd, EV_SYN, SYN_REPORT, 0);
    emit(fd, EV_KEY, KEY_C, 1);
    emit(fd, EV_SYN, SYN_REPORT, 0);
    emit(fd, EV_KEY, KEY_C, 0);
    emit(fd, EV_SYN, SYN_REPORT, 0);
    emit(fd, EV_KEY, KEY_LEFTCTRL, 0);
    emit(fd, EV_SYN, SYN_REPORT, 0);

    close(fd);
    printf("[inject] Done.\n");
    return 0;
}
