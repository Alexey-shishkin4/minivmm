# Этап 3: загрузка Linux

Путь запуска: `main.c` → `vmm_create()` → `linux_load()` → `vcpu_create()` →
`cpuid_setup()` → `cpu_setup_linux()` → `vcpu_run()`.
`guest/init/init.c` в рамках реализации пунктов 2–7 не изменялся: для проверки
пересобран initramfs из уже имеющегося исходника PID 1.

## Загрузчик и размещение памяти

`linux_image_parse()` проверяет размер и поля `setup_header` без I/O и записи
в гостевую память. Поддерживается x86-64 bzImage до 128 МиБ, протокол 2.12+.
Проверяются `0xaa55`, `HdrS`, `LOADED_HIGH`, `XLF_KERNEL_64`, границы setup,
`syssize`, `init_size` и выравнивание. При нулевом `setup_sects` используется
историческое значение 4. Размер строки из заголовка не включает завершающий NUL.

`linux_boot_plan()` рассчитывает адреса и проверяет, что все объекты доступны
через начальную identity map [0, 1 ГиБ). Только после этого `linux_load()`
копирует данные. Чтение ограничено размером обычного файла, EOF до ожидаемой
длины считается ошибкой. Парсер и расчёт размещения используются фаззером.

| GPA | Назначение |
|---|---|
| `0x10000..0x18000` | PML4, PDPT, PD, GDT, стек загрузчика |
| `0x20000..0x21000` | `struct boot_params`, 4096 байт |
| `0x21000..0x22000` | Командная строка, до 4096 байт вместе с NUL |
| `pref_address` | Protected-mode часть bzImage без setup-секторов |
| Конец нижней RAM | Initrd, начало выровнено на 4 КиБ |

У закреплённого ядра `pref_address = 0x1000000`, `init_size = 0xc0b000`.
Вся область `init_size` зарезервирована при расчёте размещения: initrd не может
перекрывать рабочую память распаковщика. Дополнительно соблюдается
`initrd_addr_max`. Высокая RAM допускается в E820, хотя начальные объекты
загрузки размещаются ниже 1 ГиБ.

`e820_build()` описывает только реальную RAM из `guest_memory`, резервирует
workspace `0x10000..0x22000`, EBDA/BIOS `0x9fc00..0x100000` и MMIO-дыру
[3, 4 ГиБ). Таблица отсортирована; переполнение адресов, перекрытие регионов
и нехватка выходного буфера возвращают ошибку.

## Начальное состояние CPU

Для Linux используются `CS = 0x10`, `DS/ES/SS = 0x18`, соответствующие записи
GDT, `RIP = kernel_addr + 0x200`, `RSI = boot_params`, `RFLAGS = 2`.
Страничные таблицы отображают первые 1 ГиБ страницами по 2 МиБ.
Настройка raw payload сохранена отдельно: `RIP = 0x1000`, селекторы 8 и 16.

Основа ABI — раздел
[64-bit Boot Protocol](https://docs.kernel.org/arch/x86/boot.html#bit-boot-protocol-1)
документации Linux; локальная копия лежит в исходниках гостевого ядра.

## Устройства и завершение

В режиме `--kernel` до создания vCPU вызываются `KVM_CREATE_IRQCHIP` и
`KVM_CREATE_PIT2`. PIC/IOAPIC/LAPIC и таймер обслуживает KVM; UART подаёт IRQ4
через `KVM_IRQ_LINE`. Это базовая часть этапа 4, необходимая уже для Linux.
В raw-режиме контроллеры не создаются, поэтому `hlt` по-прежнему завершает
тестовый payload. Linux HLT с irqchip обрабатывается KVM как ожидание
прерывания; неожиданный выход HLT в userspace считается ошибкой.

UART остаётся моделью 16450 без FIFO. IIR выбирает источник по приоритету
LSR → RX → THRE → MSR. OUT2 управляет выходной линией. Чтение IIR с THRE
подтверждает прерывание; чтение LSR не должно его подтверждать. RX пока есть
только в loopback для обнаружения UART ядром. Внешний ввод остаётся этапу 4.

Командная строка по умолчанию:

```text
console=ttyS0 earlyprintk=serial,ttyS0,115200 rdinit=/init reboot=k panic=-1
```

`reboot=k` выбирает i8042: статус порта `0x64` сообщает пустой входной буфер,
команда `0xfe` устанавливает `reset_requested`. Цикл vCPU завершает pending OUT
повторным `KVM_RUN` с `immediate_exit=1`, затем возвращает `VCPU_STOP_REBOOT`.
`main` преобразует явный reset в код 0. Triple fault остаётся ошибкой.
Повторный запуск Linux внутри той же VM и ACPI poweroff пока не реализованы.

## Проверки

В dev-VM, сборка `build/vm-debug` с `-Dwerror=true`:

- 14 Meson-тестов: unit и replay корпуса, включая E820, загрузчик, CPU ABI,
  IRQ UART, i8042 и завершение pending I/O.
- 16 проверок pytest: 7 CLI, 6 payload и Linux с 64/128/512 МиБ RAM.
- 11 unit-тестов дополнительно прошли в Clang-сборке под ASan+UBSan.
- `fuzz_linux_loader`: 173 029 590 входов за 601 секунду, exit code 0,
  без ошибок ASan/UBSan. Прогон выполнен на хосте; `detect_leaks=0` отключал
  только LeakSanitizer из-за ограничения ptrace в sandbox. ASan и UBSan
  оставались включены. Лог: `build/stage3-validation/fuzz-linux-loader.log`.
- `ruff check` и `ruff format --check` проходят. `clang-format` для `src/`
  и `tests/` проходит; общая проверка ещё сообщает о прежнем форматировании
  `guest/init/init.c`, который не изменялся в этой задаче.
- 2 интеграционных теста следующих этапов (virtio-blk, SMP) пока пропущены.

```bash
meson test -C build/vm-debug --print-errorlogs
MINIVMM_BUILD_DIR="$PWD/build/vm-debug" uv run pytest -v tests/integration
```

Команда для десятиминутного фаззинга под ASan+UBSan:

```bash
CC=clang meson setup build/fuzz -Dfuzzing=true \
    -Db_sanitize=address,undefined -Db_lundef=false
meson compile -C build/fuzz
mkdir -p fuzz-work/linux_loader
build/fuzz/tests/fuzz/fuzz_linux_loader -max_total_time=600 \
    fuzz-work/linux_loader tests/fuzz/corpus/linux_loader
```

Инструментируется вся core library через `-fsanitize=fuzzer-no-link`,
иначе libFuzzer не видел бы ветвления внутри загрузчика. Первый каталог
корпуса рабочий; исходные seeds в `tests/fuzz/corpus/linux_loader` не меняются.
