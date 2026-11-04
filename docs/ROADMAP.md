# minivmm: роадмап

Цель — собственный гипервизор (VMM) на C поверх KVM: от «hello» в real mode до загрузки настоящего Linux с virtio-устройствами, SMP и ACPI. Затем фаззинг, профилирование и vhost-user. Каждый этап покрывает конкретные пункты вакансии Yandex Cloud Core (KVM/QEMU) — таблица соответствия в конце.

Как работать:
- один этап — одна ветка и PR с зелёным CI;
- у каждого этапа есть критерий готовности, и это тест (unit, fuzz или pytest), а не «вроде работает»;
- всё, что приходит от гостя (адреса, индексы, длины, содержимое образов), — недоверенные данные: проверяется по границам и попадает в фаззер.

Сроки грубые, из расчёта 8–10 часов в неделю.

---

## Этап 0. Окружение и чтение (1 неделя)

**Сделать**
- Поднять dev-VM (`scripts/run-dev-vm.sh`): minivmm собирается и запускается внутри неё, его гости работают как L2 под вложенной KVM. Внутри проверить `/dev/kvm` и `/sys/module/kvm_intel/parameters/{ept,unrestricted_guest}`.
- Собрать гостевое ядро и initramfs (`scripts/build-guest-kernel.sh`, `scripts/build-initramfs.sh`), загрузить их в QEMU (`scripts/run-qemu-reference.sh`). Это эталон, с которым потом сравнивается minivmm.

**Прочитать**
- `Documentation/virt/kvm/api.rst` в исходниках ядра (лежат в `guest/out/src/linux-*`): разделы про `KVM_CREATE_VM`, `KVM_SET_USER_MEMORY_REGION`, `KVM_RUN`, `struct kvm_run`.
- Статья LWN «Using the KVM API» (2015) — минимальный VMM на одной странице.
- Исходники kvmtool (`tools/kvm` в его репозитории) и Firecracker (`src/vmm`) — два живых маленьких VMM для подглядывания.
- Intel SDM Vol. 3C, главы про VMX, и AMD APM Vol. 2, глава 15 (SVM): не целиком, а VMCS/VMCB, причины выходов, вложенные таблицы страниц (EPT/NPT). Машина для разработки на Intel (i5-12500H), поэтому VMX в первую очередь, но на собеседовании спросят про оба.

## Этап 1. Real mode: первый выход из гостя (1 неделя)

**Сделать**
- `kvm.c`: открыть `/dev/kvm`, проверить `KVM_GET_API_VERSION`, проверить нужные расширения.
- `vmm.c` и `memory.c`: создать VM, выделить память через `mmap`, зарегистрировать её (`KVM_SET_USER_MEMORY_REGION`).
- `vcpu.c`: создать vCPU, отобразить `struct kvm_run`, написать цикл `KVM_RUN` с разбором `exit_reason`.
- `x86/cpu_setup.c`: начальное состояние real mode (CS:IP, сегменты).
- `boot/raw_loader.c` и `main.c --payload`: загрузить плоский бинарник.
- `guest/payloads/realmode_hello.S`: вывести строку через `out` в порт 0x3f8, затем `hlt`.

**Готово, когда** `test_realmode_hello` в pytest видит приветствие и код выхода 0, а `test_memory` проверяет `gpa_to_hva` на границах и на переполнении `gpa + len`.

**Попробовать**: что будет при обращении к неотображённому адресу (`KVM_EXIT_MMIO`), на invalid opcode, на triple fault (`KVM_EXIT_SHUTDOWN`). Каждый случай — отдельный тест.

## Этап 2. Long mode (1–1,5 недели)

**Сделать**
- Таблицы страниц с identity mapping (PML4 → PDPT → PD, страницы по 2 МиБ), GDT с 64-битным сегментом кода, `CR0.PE|PG`, `CR4.PAE`, `EFER.LME|LMA`, всё через `KVM_SET_SREGS`.
- `x86/cpuid.c`: взять `KVM_GET_SUPPORTED_CPUID`, проставить APIC ID и hypervisor-биты, передать через `KVM_SET_CPUID2`.
- Дамп регистров (`regs`/`sregs`) при фатальном выходе: без него отладка мучительна.

**Готово, когда** `test_longmode_hello` проходит, а payload проверяет `cpuid` и пишет в MMIO.

**Понять и записать в `docs/notes/`**: что делает процессор при VM entry/exit, чем вложенная виртуализация (WSL → KVM) отличается от bare metal, почему identity map работает.

## Этап 3. Загрузка Linux до консоли (2 недели)

**Сделать**
- `boot/linux_loader.c`: протокол загрузки x86 (`Documentation/arch/x86/boot.rst`) — `setup_header`, `boot_params`, командная строка, initrd, 64-битная точка входа.
- `boot/e820.c`: карта памяти с дырой под MMIO ниже 4 ГиБ.
- `devices/bus.c`: диспетчеризация PIO и MMIO по диапазонам.
- `devices/serial8250.c`: сначала только передача (TX). Этого хватает для `earlyprintk=serial console=ttyS0`.
- `devices/i8042.c`: сброс через порт 0x64 → выход из VMM. Команда гостя `reboot` даёт чистое завершение.
- `guest/init/init.c`: баннер `minivmm-init: ready`, затем poweroff или reboot.

**Готово, когда** `test_boot_to_init` проходит, `test_linux_loader` и `test_e820` зелёные, а `fuzz_linux_loader` 10 минут работает под ASan без падений.

**Подсказка**: если ядро молчит — `earlyprintk=serial,ttyS0,115200`, `KVM_EXIT_DEBUG` с single-step или `gdb vmlinux`. vmlinux с символами лежит в `guest/out/`.

## Этап 4. Прерывания, таймеры, интерактивная консоль (1–1,5 недели)

**Сделать**
- `KVM_CREATE_IRQCHIP` (PIC и IOAPIC в ядре), `KVM_CREATE_PIT2`, `KVM_IRQ_LINE`.
- UART: приём (RX) из stdin через `util/event_loop.c` (epoll), прерывания THRE/RDA, терминал в raw mode.
- Корректная остановка: SIGINT, выход vCPU из `KVM_RUN` через `immediate_exit`.

**Готово, когда** в госте работает `busybox sh` и pytest-тест отправляет команду и получает ответ.

## Этап 5. PCI (1–1,5 недели)

**Сделать**
- `pci/pci.c`: конфигурационное пространство через 0xCF8/0xCFC, host bridge, маски записываемых битов, определение размера BAR (запись `~0`), список capabilities, выделение BAR в MMIO-дыре.

**Готово, когда** `lspci` в госте видит устройства, `test_pci_config` зелёный, а `fuzz_pci_config` работает в CI.

**Прочитать**: спецификацию PCI Local Bus (конфигурационное пространство, BAR), разделы ECAM/MCFG — понадобятся на этапе 7.

## Этап 6. virtio-pci и virtio-blk (2–3 недели)

Главный этап: здесь появляется основная поверхность атаки.

**Сделать**
- `virtio/virtqueue.c`: split virtqueue по спецификации virtio 1.2 (раздел 2.7). Цепочки дескрипторов, indirect-дескрипторы, used ring, барьеры памяти, `VIRTIO_F_EVENT_IDX`. Каждый индекс и каждая длина проверяются.
- `virtio/virtio_pci.c`: modern-транспорт (раздел 4.1) — capabilities (common/notify/ISR/device cfg), согласование фич, машина состояний device status.
- `virtio/virtio_blk.c`: запросы IN/OUT/FLUSH/GET_ID на файле хоста, сначала синхронно (`pread`/`pwrite`).
- Уведомления через `KVM_IOEVENTFD` (без выхода в userspace на vCPU-потоке) и прерывания через `KVM_IRQFD`.
- `virtio/virtio_console.c` — по желанию, как более быстрая альтернатива UART.
- `docs/security.md`: модель угроз — что контролирует гость, где проверки, что будет при злонамеренном драйвере.

**Готово, когда** `test_virtio_blk_read_write` проходит (гость пишет шаблон в `/dev/vda`, хост проверяет файл), а `fuzz_virtqueue` под ASan+UBSan отработал в сумме не меньше часа без падений. Найденные падения минимизируются и кладутся в `tests/fuzz/corpus/virtqueue/`.

## Этап 7. SMP и ACPI (2 недели)

**Сделать**
- Несколько vCPU-потоков, блокировки на шинах и устройствах, отдельный пресет сборки с `-Db_sanitize=thread`.
- `acpi/acpi.c`: RSDP, XSDT, FADT, MADT (LAPIC на каждый vCPU, IOAPIC, ISA overrides), минимальный DSDT на AML (PCI root bridge). Контрольные суммы.
- Poweroff через ACPI (PM1a control) → чистое завершение VMM.
- Вместо 0xCF8 можно перейти на ECAM + MCFG.

**Готово, когда** `test_smp_cpus_visible` видит 4 процессора, а `poweroff` в госте завершает minivmm с кодом 0. Сравнить вывод `acpidump`/`dmesg` с эталонным запуском под QEMU.

## Этап 8. Производительность (1,5–2 недели)

**Сделать**
- Счётчики выходов по причинам и времени (`--stats`), `tools/exit_stats.py`.
- `perf kvm stat`, tracepoint `kvm:kvm_exit`, flamegraph потоков VMM.
- Эксперименты (каждый — гипотеза, замер, вывод):
  - уведомления virtio через PIO-выход против ioeventfd;
  - прерывания через `KVM_IRQ_LINE` против irqfd;
  - синхронный I/O против io_uring в отдельном потоке;
  - polling против eventfd;
  - huge pages для памяти гостя;
  - kvmclock / PV EOI на стороне гостя.
- `tools/bench_blk.py`: minivmm против QEMU на одном госте и одном образе диска.
- `docs/performance.md`: таблицы и выводы.

**Важно**: замеры делать на bare-metal Linux-машине, не под вложенной виртуализацией. У i5-12500H гибридная архитектура (P- и E-ядра с разной производительностью): закреплять vCPU-потоки и бенчмарки на P-ядрах (`taskset`), фиксировать частоту (`cpupower frequency-set -g performance`), иначе разброс съест разницу.

## Этап 9. Безопасность: фаззинг, статический анализ, CVE (2 недели, частично параллельно с другими этапами)

**Сделать**
- Длительные прогоны libFuzzer и отчёт о покрытии (`llvm-profdata`/`llvm-cov`) по virtqueue, PCI и загрузчику. Добавить фаззер для разбора virtio-blk запросов.
- Статический анализ: clang-tidy (уже в CI), `gcc -fanalyzer`, `scan-build`; по желанию Coverity Scan (бесплатен для open source).
- Песочница процесса VMM в духе Firecracker: seccomp-bpf фильтр системных вызовов после инициализации, сброс привилегий.
- Разборы CVE в `docs/cve/`, 3–4 штуки: корневая причина, патч, как бы это поймал фаззер или проверка в minivmm. Кандидаты:
  - VENOM (CVE-2015-3456, FDC в QEMU) — классика эмуляции устройств;
  - любая свежая CVE в virtio или vhost в QEMU либо в ядре;
  - аппаратные: L1TF / MDS и BHI / RFDS (Intel — актуально для машины разработки), Retbleed и SRSO/Inception (AMD): что видно в `/sys/devices/system/cpu/vulnerabilities/`, какие mitigations включает KVM, сколько они стоят на частых VM exit (замер из этапа 8).

## Этап 10. vhost-user (2–3 недели)

Прямо под пункт вакансии «библиотека на базе vhost-user».

**Сделать**
1. Память гостя через `memfd` с `MAP_SHARED`, чтобы её можно было передать другому процессу.
2. Отдельный бинарник `vhost-user-blk`: протокол vhost-user через unix-сокет (`GET_FEATURES`, `SET_MEM_TABLE` с передачей fd через `SCM_RIGHTS`, `SET_VRING_*`, kick/call eventfd), переиспользует `virtqueue.c`.
3. Проверить backend с настоящим QEMU (`-object memory-backend-memfd,share=on -chardev socket -device vhost-user-blk-pci`).
4. Научить minivmm быть frontend'ом vhost-user: устройство живёт в другом процессе, minivmm только прокидывает eventfd в KVM.
5. Переподключение backend'а без остановки гостя, фаззер для сообщений протокола.

**Готово, когда** тест virtio-blk проходит и с встроенным устройством, и с внешним backend'ом (под QEMU и под minivmm), а в `docs/performance.md` есть сравнение встроенного устройства с vhost-user.

## Этап 11. Исследования (по желанию, 2+ недели)

Под пункт «исследовать новые подходы к работе в облаке». Выбрать одно-два:
- **Snapshot/restore**: сохранить регистры, состояние irqchip и память в файл, затем восстановить. Замерить время старта VM из снапшота против холодной загрузки (идея Firecracker snapshots). Ленивая подгрузка памяти через `userfaultfd`.
- **Dirty logging**: `KVM_GET_DIRTY_LOG` или dirty ring, прототип pre-copy миграции между двумя процессами.
- **Сторона ядра**: пройти путь обработки выхода в `arch/x86/kvm/vmx/vmx.c` (и для сравнения `svm/svm.c`), написать тест в `tools/testing/selftests/kvm`, отправить его или найденную ошибку в апстрим (даже маленький принятый патч в KVM или QEMU сильно весит).

## Этап 12. Документация и упаковка (3–4 вечера)

- README: asciinema/GIF загрузки Linux под minivmm, бейджи CI.
- `docs/architecture.md`, `docs/security.md`, `docs/performance.md`, `docs/cve/*`.
- 3–4 пункта для резюме с цифрами (время загрузки, IOPS против QEMU, часы фаззинга, найденные ошибки).

---

## Соответствие вакансии

| Требование | Где в проекте |
|---|---|
| Понимание Linux | этапы 3–4, 8 (boot protocol, epoll, eventfd, io_uring, perf/ftrace), 10 (memfd, SCM_RIGHTS) |
| Понимание x86 и железа | этапы 1–2 (режимы, страницы, GDT, CPUID/MSR), 4 (PIC/IOAPIC/PIT), 7 (LAPIC, ACPI) |
| Виртуальные машины | весь проект + эталонные запуски под QEMU |
| C и Python | VMM на C; тесты, анализ и бенчмарки на Python |
| Гипервизор и ядро, тесты | этапы 1–8, 11 (KVM selftests) |
| Производительность | этап 8 |
| Безопасность: CVE, аппаратные уязвимости, статический анализ, фаззинг | этапы 6, 9 |
| vhost-user | этап 10 |
| Исследования | этап 11 |
| Плюсы: PCI, ACPI, virtio, VMX/SVM | этапы 5–7, 0–2 |

Примерно 20–25 недель в этом темпе. Показывать можно уже после этапа 3 (Linux загружается под своим гипервизором), а после этапа 6 проект закрывает основные пункты вакансии.

## Справочник

- KVM API: `Documentation/virt/kvm/api.rst`.
- Протокол загрузки x86: `Documentation/arch/x86/boot.rst`.
- Спецификация virtio 1.2/1.3 (OASIS): разделы 2 (базовые механизмы), 4.1 (PCI), 5.2 (block).
- Протокол vhost-user: `docs/interop/vhost-user.rst` в исходниках QEMU.
- ACPI Specification 6.5: таблицы RSDP, XSDT, FADT, MADT, DSDT.
- OSDev Wiki: 8250 UART, PIT, PIC/APIC, long mode.
- Код для сравнения: kvmtool, Firecracker, cloud-hypervisor, QEMU (`hw/virtio/`, `hw/char/serial.c`, `hw/i386/x86.c`).
