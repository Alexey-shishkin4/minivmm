# minivmm

A small virtual machine monitor for x86-64 Linux hosts, written in C on top of
the KVM API: from a 16-bit "hello" payload to booting a real Linux kernel with
virtio devices, SMP and ACPI. Built to understand, measure and harden every
layer between `ioctl(KVM_RUN)` and a guest driver.

> Status: skeleton. Build, tests, fuzzing harness and CI are in place; the VMM
> is implemented stage by stage, see [docs/ROADMAP.md](docs/ROADMAP.md).

## Architecture

```mermaid
flowchart TD
    main["main.c<br/>CLI"] --> vmm
    vmm["vmm<br/>VM lifecycle"] --> kvm["kvm<br/>/dev/kvm ioctls"]
    vmm --> mem["memory<br/>memslots, GPA→HVA (bounds-checked)"]
    vmm --> vcpu["vcpu<br/>KVM_RUN exit loop, per-exit stats"]
    vmm --> boot["boot<br/>raw payloads, bzImage + boot_params, e820"]
    vmm --> acpi["acpi<br/>RSDP/XSDT/MADT/FADT/DSDT"]
    vcpu -->|PIO / MMIO exits| bus["devices/bus"]
    bus --> uart["8250 UART"]
    bus --> i8042["i8042 (reset)"]
    bus --> pci["pci<br/>config space, BARs"]
    pci --> vpci["virtio-pci"]
    vpci --> vq["virtqueue<br/>(untrusted input, fuzzed)"]
    vq --> blk["virtio-blk"]
    vq --> con["virtio-console"]
    vpci -.->|ioeventfd / irqfd| kvm
```

| Path | Contents |
|---|---|
| `src/` | The VMM. Everything except `main.c` is built into `libminivmm_core.a` so tests and fuzzers link the real code. |
| `guest/payloads/` | Flat-binary test guests (real mode, long mode). |
| `guest/kernel/minivmm.config` | Kconfig fragment for the minimal guest kernel. |
| `guest/init/` | Static PID 1 for the test initramfs. |
| `tests/unit/` | Host-only unit tests (TAP), no KVM required. |
| `tests/fuzz/` | libFuzzer targets for guest-controlled parsers + corpus. |
| `tests/integration/` | pytest: boot payloads / Linux under minivmm (needs `/dev/kvm`). |
| `tools/` | Python: VM-exit statistics, block I/O benchmark vs. QEMU. |
| `scripts/` | Fetch/build the pinned guest kernel and BusyBox initramfs; QEMU reference run. |

## Build

Requirements: Linux x86-64, GCC or Clang, Meson ≥ 1.3, Ninja, kernel UAPI
headers (`linux/kvm.h`). Running VMs needs `/dev/kvm` (user in group `kvm`).

```bash
meson setup build/debug
meson compile -C build/debug
meson test -C build/debug                 # unit tests + fuzz corpus replay

meson setup build/asan -Db_sanitize=address,undefined
meson setup build/release -Dbuildtype=release
CC=clang meson setup build/fuzz -Dfuzzing=true -Db_sanitize=address,undefined -Db_lundef=false
```

Guest kernel and initramfs (pinned versions and SHA256 in
[`scripts/versions.sh`](scripts/versions.sh)):

```bash
scripts/build-guest-kernel.sh   # needs flex, bison, bc, libelf-dev, libssl-dev
scripts/build-initramfs.sh
scripts/run-qemu-reference.sh   # same guest under QEMU, for comparison
```

Integration tests:

```bash
uv run pytest
```

Development VM: minivmm can be built and run inside a QEMU VM (nested KVM),
with the repository shared over virtiofs at the same path. Needs `qemu-img`,
`xorriso`, `virtiofsd` and nested virtualization enabled in the host KVM module.

```bash
scripts/run-dev-vm.sh            # boot; the first boot provisions it via cloud-init
scripts/run-dev-vm.sh ssh        # from another terminal
```

Inside the VM `MINIVMM_BUILD_DIR` points at `build/vm-debug` and uv uses a
separate venv, so host and VM build trees do not clash:
`meson setup build/vm-debug && meson test -C build/vm-debug && uv run pytest`.

Docker (same packages as CI): `docker build -t minivmm .` and
`docker run --rm -it --device /dev/kvm -v "$PWD:/src" minivmm`.

## Fuzzing

Every parser of guest-controlled data has a libFuzzer target in
`tests/fuzz/`. Without `-Dfuzzing=true` the same targets are linked with a
standalone driver and replay `tests/fuzz/corpus/<target>/` as regular tests,
so every crash found once stays a regression test.

```bash
# libFuzzer writes new inputs into the FIRST directory: keep it outside git,
# commit only minimized crashers / interesting inputs to tests/fuzz/corpus/.
mkdir -p fuzz-work/virtqueue
./build/fuzz/tests/fuzz/fuzz_virtqueue -max_total_time=300 \
    fuzz-work/virtqueue tests/fuzz/corpus/virtqueue
```

## Code quality

```bash
git ls-files '*.c' '*.h' | xargs clang-format --dry-run --Werror
run-clang-tidy.py -p build/debug -quiet "$(pwd)/(src|tests)/.*\.c$"
```

CI ([`.github/workflows/ci.yml`](.github/workflows/ci.yml)): GCC/Clang ×
plain/ASan+UBSan builds with `-Werror`, 60 s fuzzing per target, clang-format,
clang-tidy, ruff, and integration tests on a KVM-enabled runner.
