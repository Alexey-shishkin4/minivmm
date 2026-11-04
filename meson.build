project('minivmm', 'c',
  version: '0.1.0',
  meson_version: '>=1.3.0',
  default_options: [
    'c_std=gnu17',
    'warning_level=3',
    'buildtype=debug',
    'b_ndebug=if-release',
  ],
)

cc = meson.get_compiler('c')

add_project_arguments(cc.get_supported_arguments(
  '-D_GNU_SOURCE',
  '-Wshadow',
  '-Wconversion',
  '-Wsign-conversion',
  '-Wstrict-prototypes',
  '-Wmissing-prototypes',
  '-Wold-style-definition',
  '-Wpointer-arith',
  '-Wcast-align',
  '-Wwrite-strings',
  '-Wnull-dereference',
  '-Wdouble-promotion',
  '-Wimplicit-fallthrough',
  '-Wvla',
  '-Wformat=2',
  '-Wno-unused-parameter',
  '-fno-strict-aliasing',
  '-fno-omit-frame-pointer',
), language: 'c')

if host_machine.cpu_family() != 'x86_64' or host_machine.system() != 'linux'
  error('minivmm targets x86_64 Linux hosts with KVM')
endif

if not cc.has_header('linux/kvm.h')
  error('linux/kvm.h not found: install kernel UAPI headers (linux-libc-dev)')
endif

threads = dependency('threads')

inc = include_directories('src')

subdir('src')
subdir('guest')
subdir('tests')
