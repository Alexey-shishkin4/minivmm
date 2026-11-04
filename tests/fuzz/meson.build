# Fuzz targets for code that parses guest-controlled data.
#
#  -Dfuzzing=true (clang): real libFuzzer binaries, run e.g.
#      ./build/fuzz/tests/fuzz/fuzz_virtqueue -max_total_time=60 tests/fuzz/corpus/virtqueue
#  otherwise: the same LLVMFuzzerTestOneInput linked with standalone_main.c
#      and registered as tests that replay tests/fuzz/corpus/<name>/ (crash
#      regressions live there forever).

fuzz_targets = {
  'fuzz_virtqueue': 'virtqueue',
  'fuzz_pci_config': 'pci_config',
  'fuzz_linux_loader': 'linux_loader',
}

fuzzing = get_option('fuzzing')
if fuzzing and cc.get_id() != 'clang'
  error('-Dfuzzing=true requires clang (CC=clang meson setup ...)')
endif

foreach target, corpus : fuzz_targets
  if fuzzing
    executable(target, target + '.c',
      dependencies: minivmm_core_dep,
      c_args: ['-fsanitize=fuzzer'],
      link_args: ['-fsanitize=fuzzer'],
    )
  else
    exe = executable(target, target + '.c', 'standalone_main.c',
      dependencies: minivmm_core_dep,
    )
    test(target, exe,
      args: [meson.current_source_dir() / 'corpus' / corpus],
      suite: 'fuzz-regression',
    )
  endif
endforeach
