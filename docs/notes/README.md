# Заметки по реализации minivmm

Заметки описывают long mode, загрузку Linux и текущую среду разработки minivmm.

- [VM entry/exit и цикл KVM_RUN](vm-entry-exit.md): что переключает процессор,
  что делает KVM и когда управление возвращается в userspace VMM.
- [Вложенная виртуализация](nested-virtualization.md): уровни L0/L1/L2,
  отличие текущей dev-VM от прежней конфигурации WSL2 и ограничения измерений.
- [Long mode и identity mapping](long-mode-memory.md): адреса GVA/GPA/HVA/HPA,
  конкретные таблицы страниц, GDT и путь MMIO в тестовом payload.

- [Загрузка Linux](linux-boot.md): проверка bzImage, E820, boot ABI, IRQ UART
  и завершение по i8042 reset.

Проверяемый результат этапа 2 —
[`test_longmode_hello`](../../tests/integration/test_payloads.py).
Он запускает гостя с `--mode long --mem 1M`, проверяет вывод о 64-битной
арифметике и CPUID, сообщения о MMIO и код завершения 0.

В прогоне внутри dev-VM от 2026-11-08 прошли все 6 payload-тестов и все
9 Meson-тестов. Этот результат не означает, что будущие устройства уже
протестированы: unit-тесты остальных модулей и fuzz targets пока содержат
заглушки.
