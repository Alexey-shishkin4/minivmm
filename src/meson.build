# Everything except main.c goes into a static library so that unit tests and
# fuzz targets can link the real code.
core_sources = files(
  'kvm.c',
  'vmm.c',
  'memory.c',
  'vcpu.c',
  'x86/cpu_setup.c',
  'x86/cpuid.c',
  'boot/raw_loader.c',
  'boot/linux_loader.c',
  'boot/e820.c',
  'devices/bus.c',
  'devices/serial8250.c',
  'devices/i8042.c',
  'pci/pci.c',
  'virtio/virtqueue.c',
  'virtio/virtio_pci.c',
  'virtio/virtio_blk.c',
  'virtio/virtio_console.c',
  'acpi/acpi.c',
  'util/log.c',
  'util/event_loop.c',
)

minivmm_core = static_library('minivmm_core',
  core_sources,
  include_directories: inc,
  dependencies: threads,
)

minivmm_core_dep = declare_dependency(
  link_with: minivmm_core,
  include_directories: inc,
  dependencies: threads,
)

minivmm_exe = executable('minivmm',
  'main.c',
  dependencies: minivmm_core_dep,
  install: true,
)
