# Host-only unit tests: no /dev/kvm needed. Each test is a small program that
# prints TAP; tests needing KVM belong in tests/integration (pytest).
unit_tests = [
  'test_memory',
  'test_bus',
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
