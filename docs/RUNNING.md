# Запуск minivmm на другом компьютере

Инструкция воспроизводит окружение, в котором проверялся проект:

```text
Linux-хост → Ubuntu dev VM → minivmm → гостевой Linux с BusyBox
```

QEMU запускает dev VM, а minivmm внутри неё использует вложенный KVM для
выполнения собственного гостя. Все команды ниже разделены на команды хоста,
команды внутри dev VM и команды гостевого shell.

## Что уже доступно

- Один x86-64 vCPU и гостевая RAM.
- Проверочные payload в real mode и long mode.
- Загрузка Linux bzImage и initramfs.
- UART COM1, контроллеры прерываний, PIT и завершение через i8042 reboot.
- Интерактивный BusyBox shell — после применения отдельного патча консоли.

Подключённого гостевого диска и сетевой карты пока нет. Корневая файловая
система гостя находится в RAM: изменения исчезают после завершения VM.

## Что передать другому человеку

1. Доступ к репозиторию и нужную ветку или коммит:
   `https://github.com/Alexey-shishkin4/minivmm.git`.
2. Для интерактивной консоли — файл
   [`interactive-console.patch`](examples/interactive-console.patch), если
   изменения ещё не включены в передаваемую ветку.
3. Эту инструкцию.

Проверь, что инструкция и патч опубликованы в репозитории либо отправлены
отдельными файлами. Локальные неотслеживаемые файлы не передаются через
`git clone`.

Каталоги `build/` и `guest/out/` исключены из Git. На другом компьютере
нужно собрать VMM, ядро и initramfs заново. Локальная папка
`build/stage4-console-example` содержит проверенный пример на машине автора,
но не является способом установки для другого человека.

Если передаются готовые гостевые артефакты, нужны оба файла: `bzImage` и
соответствующий ему `initramfs.cpio.gz`. Для shell в initramfs должна быть
обновлённая реализация PID 1. Приватный SSH-ключ dev VM и её диск передавать
вместе с исходниками не требуется.

## 1. Требования к хосту

- Linux x86-64.
- Intel VT-x или AMD-V, включённая в BIOS/UEFI.
- Рабочий `/dev/kvm`, доступный запускающему пользователю.
- Включённая вложенная виртуализация в модуле KVM.
- Интернет для первоначального скачивания образа, исходников и пакетов.

По умолчанию скрипт dev VM использует **6 ГиБ RAM, 4 vCPU и диск qcow2 на
32 ГиБ**. Дополнительно нужны ресурсы хосту и место под исходники и сборки.
Размер RAM и число vCPU можно менять переменными `MEM` и `SMP`.

Внутри dev VM автоматически устанавливаются Ubuntu 26.04, компилятор,
Meson, Ninja, Python, uv и зависимости для сборки гостя. Вручную устанавливать
гостевую ОС или искать установочный ISO не нужно.

Windows и macOS не являются нативными платформами minivmm. Если Linux-хост
сам работает внутри другой VM, ему тоже должны быть доступны аппаратная
виртуализация и KVM.

## 2. Установка инструментов на хосте

На Ubuntu:

```bash
sudo apt update
sudo apt install -y git curl ca-certificates \
    qemu-system-x86 qemu-utils virtiofsd xorriso openssh-client
```

На Arch Linux:

```bash
sudo pacman -S --needed git curl ca-certificates \
    qemu-system-x86 qemu-img virtiofsd libisoburn openssh
```

Это инструменты для запуска dev VM. Зависимости сборки minivmm будут
установлены внутри неё через cloud-init.

## 3. Проверка KVM и вложенной виртуализации

На хосте проверь, что устройство действительно открывается:

```bash
(exec 3<>/dev/kvm)
```

Команда должна завершиться без ошибки. Самого наличия файла `/dev/kvm`
недостаточно: устройство может существовать без работающего драйвера.

При `Permission denied` добавь пользователя в группу `kvm`:

```bash
sudo usermod -aG kvm "$USER"
```

После этого выйди из пользовательской сессии и войди снова.

Для Intel проверь:

```bash
cat /sys/module/kvm_intel/parameters/nested
```

Для AMD:

```bash
cat /sys/module/kvm_amd/parameters/nested
```

Ожидается `Y` или `1`. Если нужный модуль не загружен, используй
`sudo modprobe kvm_intel` для Intel либо `sudo modprobe kvm_amd` для AMD,
затем повтори проверку.

Если вложенная виртуализация отключена, добавь настройку своего модуля.

Для Intel:

```bash
echo 'options kvm_intel nested=1' | sudo tee /etc/modprobe.d/minivmm-kvm.conf
```

Для AMD вместо Intel-команды:

```bash
echo 'options kvm_amd nested=1' | sudo tee /etc/modprobe.d/minivmm-kvm.conf
```

Перезагрузи хост и повтори проверку. Подробнее — в
[документации Linux по nested KVM](https://docs.kernel.org/virt/kvm/x86/running-nested-guests.html).

## 4. Получение исходников

На хосте, от обычного пользователя:

```bash
mkdir -p "$HOME/oss"
cd "$HOME/oss"
git clone https://github.com/Alexey-shishkin4/minivmm.git
cd minivmm
```

Используй ветку или коммит, переданные автором проекта.

Если нужна интерактивная консоль и изменения ещё не включены в эту версию,
положи полученный патч в корень клона под именем `interactive-console.patch`:

```bash
git apply --check interactive-console.patch
git apply interactive-console.patch
```

Если файл уже находится в `docs/examples/`, используй этот путь:

```bash
git apply --check docs/examples/interactive-console.patch
git apply docs/examples/interactive-console.patch
```

Выбери один вариант. Патч применяется один раз, поверх подходящей реализации
внешнего RX UART и event_loop. Если `--check` сообщает конфликт, нужно получить
совместимую ревизию, а не принудительно применять патч.

## 5. Запуск dev VM

В первом терминале хоста, из корня репозитория:

```bash
scripts/run-dev-vm.sh
```

При первом запуске скрипт скачает закреплённый образ Ubuntu, создаст диск,
SSH-ключ и запустит установку пакетов. Этот терминал остаётся занят, пока
работает VM.

Репозиторий доступен внутри dev VM по тому же абсолютному пути благодаря
virtiofs.

Во втором терминале хоста:

```bash
cd "$HOME/oss/minivmm"
scripts/run-dev-vm.sh ssh cloud-init status --wait
scripts/run-dev-vm.sh ssh
```

Если SSH ещё не отвечает, дождись загрузки VM и повтори команду. По умолчанию
используется `127.0.0.1:2222`; скрипт сам выбирает созданный SSH-ключ.

## 6. Сборка minivmm внутри dev VM

Дальнейшие команды сборки и запуска выполняются **в Ubuntu dev VM**:

```bash
cd "$HOME/oss/minivmm"

export UV_PROJECT_ENVIRONMENT="$HOME/.venv-minivmm"
export MINIVMM_BUILD_DIR="$PWD/build/vm-debug"

uv sync --frozen
meson setup build/vm-debug -Dwerror=true
meson compile -C build/vm-debug
meson test -C build/vm-debug --print-errorlogs
```

`meson setup` нужен при первой сборке. Для изменения настроек существующей
сборки используй `meson setup --reconfigure build/vm-debug`. При обычном
повторном запуске достаточно `meson compile`.

## 7. Быстрая проверка payload

Внутри dev VM:

```bash
build/vm-debug/src/minivmm \
    --payload build/vm-debug/guest/realmode_hello.bin

build/vm-debug/src/minivmm \
    --payload build/vm-debug/guest/longmode_hello.bin \
    --mode long --mem 1M
```

Первый гость выводит `Hello from real mode`. Второй выводит результаты
проверок long mode. Оба завершаются автоматически. Long-mode payload также
намеренно обращается к неотображённому MMIO и выводит диагностические сообщения.

## 8. Сборка гостевого ядра и initramfs

```bash
scripts/build-guest-kernel.sh
scripts/build-initramfs.sh
```

Скрипты скачивают закреплённые версии Linux и BusyBox и проверяют SHA256.
Версии и контрольные суммы находятся в `scripts/versions.sh`.

Результат:

| Файл | Назначение |
|---|---|
| `guest/out/bzImage` | Гостевое ядро |
| `guest/out/vmlinux` | Ядро с символами для отладки |
| `guest/out/initramfs.cpio.gz` | BusyBox и реализация PID 1 |

Если проверка контрольной суммы не прошла, нужно проверить скачивание или
закреплённые версии. Отключать проверку для продолжения сборки не нужно.

## 9. Запуск Linux

Обычная проверка загрузки работает и без интерактивного патча:

```bash
build/vm-debug/src/minivmm \
    --kernel guest/out/bzImage \
    --initrd guest/out/initramfs.cpio.gz \
    --mem 128M
```

Ожидаемый вывод содержит:

```text
minivmm-init: starting
minivmm-init: ready
minivmm-init: reboot
```

Гость выполняет reboot через i8042, после чего minivmm завершается с кодом 0.

Для интерактивной консоли, после применения патча и пересборки initramfs:

```bash
build/vm-debug/src/minivmm \
    --kernel guest/out/bzImage \
    --initrd guest/out/initramfs.cpio.gz \
    --mem 128M \
    --interactive
```

Появится приглашение `minivmm# `. Это shell гостевого Linux.

```sh
uname -a
ls /
cat /proc/cpuinfo
cat /proc/interrupts
echo 'hello from the guest'
busybox --list
```

Управление:

- **Ctrl-C** прерывает текущую foreground-команду в госте.
- **Ctrl-]** завершает minivmm и возвращает в shell dev VM.
- **`exit`** выходит из гостевого shell; PID 1 выполняет reboot, minivmm завершается.

Если используется собственный `--cmdline`, для этого режима PID 1 укажи
`console=ttyS0 rdinit=/init reboot=k minivmm.shell=1`.

## 10. Проверки

Из корня репозитория внутри dev VM:

```bash
meson test -C build/vm-debug --print-errorlogs
MINIVMM_BUILD_DIR="$PWD/build/vm-debug" uv run pytest -v
```

Только PTY-тест интерактивной консоли, если патч применён:

```bash
MINIVMM_BUILD_DIR="$PWD/build/vm-debug" \
    uv run pytest -v tests/integration/test_console.py
```

Проверенный интерактивный пример прошёл 14 Meson-тестов и 20 pytest-проверок;
2 теста будущих этапов были пропущены. PTY-сценарии проверяют shell, Ctrl-C,
ввод больше размера очереди, reboot, SIGINT/SIGTERM, Ctrl-] и восстановление
терминала.

Если KVM-тесты пропущены, прочитай причину пропуска: отсутствие `/dev/kvm`
или гостевых артефактов не означает успешную проверку загрузки.

## 11. Завершение работы

Сначала выйди из гостевого minivmm через `exit` или Ctrl-]. Затем в shell
**Ubuntu dev VM** выполни:

```bash
sudo poweroff
```

SSH отключится, а `scripts/run-dev-vm.sh` в первом терминале хоста завершится.
Обычный выход из SSH оставляет dev VM включённой.

## Частые проблемы

| Симптом | Что проверить |
|---|---|
| `/dev/kvm` отсутствует или не открывается | Виртуализацию в BIOS/UEFI, модуль KVM, права пользователя |
| `nested virtualization is disabled` | Параметр nested модуля KVM на хосте; доступность виртуализации, если хост сам является VM |
| `virtiofsd not found` | Установку virtiofsd; скрипт также проверяет `/usr/libexec/virtiofsd` |
| SSH: `Connection refused` | Загрузку VM, установку пакетов при первом запуске и ошибки в первом терминале |
| Порт 2222 занят | Запуск с `SSH_PORT=2223 scripts/run-dev-vm.sh`; ту же переменную указать для SSH-команд |
| Неизвестная опция `--interactive` | Применение патча и пересборку VMM |
| Linux сразу перезагружается | Это штатно без интерактивного режима; для собственного cmdline проверить `minivmm.shell=1` |
| После изменения init.c нет shell | Пересобрать initramfs вместе с VMM |
| Pytest запускает другой бинарник | Значение `MINIVMM_BUILD_DIR` |
| Файлы гостя исчезли | Корневая файловая система гостя находится в RAM |
| Все интеграционные тесты пропущены | Причины пропуска, доступ к KVM и наличие артефактов |

## Ссылки

- [Описание примера интерактивной консоли](examples/interactive-console.md).
- [Патч интерактивной консоли](examples/interactive-console.patch).
- [Вложенная виртуализация KVM](https://docs.kernel.org/virt/kvm/x86/running-nested-guests.html).
- [Пакет virtiofsd для Ubuntu](https://packages.ubuntu.com/resolute/virtiofsd).
- [Пакет qemu-system-x86 для Arch](https://archlinux.org/packages/extra/x86_64/qemu-system-x86/).
- [Пакет virtiofsd для Arch](https://archlinux.org/packages/extra/x86_64/virtiofsd/).
