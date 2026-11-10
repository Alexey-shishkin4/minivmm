# Host-only unit tests: no /dev/kvm needed. Each test is a small program that
# prints TAP; tests needing KVM belong in tests/integration (pytest).
unit_tests = [
  'test_memory',
  'test_bus',
  'test_serial8250',
  'test_serial8250_irq',
  'test_i8042',
  'test_e820',
  'test_linux_loader',
  'test_pci_config',
  'test_virtqueue',
]

foreach t : unit_tests
  test(t,
    executable(t, t + '.c', dependencies: minivmm_core_dep),
    protocol: 'tap',
    suite: 'unit',
  )
endforeach

# Simulate KVM_RUN exits to verify PIO/MMIO routing without /dev/kvm.
test('test_vcpu_io',
  executable('test_vcpu_io', 'test_vcpu_io.c',
    dependencies: minivmm_core_dep,
    link_args: ['-Wl,--wrap=ioctl'],
  ),
  protocol: 'tap',
  suite: 'unit',
)

test('test_cpu_setup',
  executable('test_cpu_setup', 'test_cpu_setup.c',
    dependencies: minivmm_core_dep,
    link_args: ['-Wl,--wrap=ioctl'],
  ),
  protocol: 'tap',
  suite: 'unit',
)
