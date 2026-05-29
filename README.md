# Lab 5 — Системні виклики Linux (System Call API)

Два завдання на пряму роботу з ядром Linux:

1. **Читання пам'яті іншого процесу** — `process_vm_readv(2)` + `/proc/[pid]/mem`
2. **Перехоплення клавіатури** — `/dev/input/eventN`

## Вимоги

- Linux (Ubuntu / Debian / Arch)
- g++ з підтримкою C++17
- make
- Для task2: `sudo` або членство в групі `input`

```bash
# Ubuntu / Debian
sudo apt update && sudo apt install build-essential
```

## Збірка

```bash
make        # зібрати все
make clean  # очистити
```

## Task 1 — Читання пам'яті процесу

Потрібно два термінали.

**Термінал 1:**
```bash
./task1_proc_mem/target
```

Виведе PID і адресу змінної:
```
PID              : 12345
&monitored_value : 0x601060
Запустіть монітор:
  ./monitor 12345 0x601060
```

**Термінал 2** (скопіювати команду з виводу target):
```bash
./task1_proc_mem/monitor 12345 0x601060
```

Тепер вводьте числа в Терміналі 1 — Термінал 2 миттєво покаже зміни:
```
[monitor] значення: 0 → 42
[monitor] значення: 42 → 100
```

Зупинка: `Ctrl+C` в будь-якому терміналі.

## Task 2 — Перехоплення клавіатури

### Варіант A — реальна клавіатура (sudo)

**Термінал 1:**
```bash
sudo ./task2_kbd_hook/kbd_hook
```

Набирайте текст у **будь-якому** вікні — Термінал 1 покаже всі натискання:
```
  KEY [ 35] h
  KEY [ 18] e
  KEY [ 38] l
  KEY [ 38] l
  KEY [ 24] o
```

### Варіант Б — тест через FIFO (без sudo)

**Термінал 1:**
```bash
mkfifo /tmp/fake_kbd
./task2_kbd_hook/kbd_hook /tmp/fake_kbd
```

**Термінал 2:**
```bash
./task2_kbd_hook/inject_events /tmp/fake_kbd
```

inject_events симулює введення `hello World` — kbd_hook покаже перехоплені клавіші.

## Структура

```
Lab5/
├── Makefile
├── task1_proc_mem/
│   ├── target.cpp      — процес-"жертва"
│   └── monitor.cpp     — читач пам'яті
└── task2_kbd_hook/
    ├── kbd_hook.cpp     — перехоплювач клавіатури
    └── inject_events.cpp — тестовий інжектор подій
```
