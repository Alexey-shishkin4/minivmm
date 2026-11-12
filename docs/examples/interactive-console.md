# Interactive console example

This is a reviewed example for stage 4, prepared against the current external
RX UART and event_loop implementations. The active src/ and guest/init/ files
were not replaced. The complete changes are in [interactive-console.patch](interactive-console.patch).

A built copy of the example is available locally in
`build/stage4-console-example/`. The patch is the reusable source artifact;
the built copy and its guest artifacts are gitignored.

## File changes

- `main.c`: `--interactive` (Linux only), default `minivmm.shell=1` boot argument,
  call to `vmm_run()` and successful host-requested termination.
- `vmm.h`: UART mutex, temporary console context and run/stop-query interfaces.
- `vmm.c`: vCPU worker, epoll callbacks for input, RX-space notifications and
  SIGINT/SIGTERM; bounded 4096-byte input queue; UART access serialization;
  raw terminal setup/restoration; orderly stop and join before releasing fds.
- `vcpu.c/.h`: host-requested stop reason, immediate_exit reset before checking
  the stop flag, and a compiler signal fence to preserve that order.
- `init.c`: optional BusyBox shell, selected by `minivmm.shell=1` in
  `/proc/cmdline`; PID 1 keeps running and reaps children. The shell obtains
  a session and controlling `/dev/ttyS0` for foreground job control. After
  shell exit, PID 1 reboots using the existing path.
- `test_console.py`: standard-library PTY test, no pexpect dependency.
- `event_loop.c/.h`: formatting only, including removing the extra blank EOF line.

## Runtime details

The input queue belongs to the event-loop thread. UART state and `rx_waiting`
are protected by `serial_lock`. After a guest UART access frees RBR, the vCPU
thread writes `space_fd`; its callback retries pending input. When the input
queue fills, stdin is removed from epoll, then re-registered after space appears.

Input uses a separate open file description via `/proc/self/fd/0`. Applying
O_NONBLOCK to a dup of stdin could also change stdout when both originated
from the same PTY slave. The UART TX path remains synchronous.

SIGINT/SIGTERM are blocked before the worker is created and consumed through
signalfd in the event loop. SIGUSR1 is reserved for kicking that worker; its
handler sets the worker's immediate_exit byte. eventfd alone does not wake
KVM_RUN. The worker is joined before its console context or VM can be released.

Ctrl-C is forwarded to the guest. Ctrl-] exits the host VMM. External SIGINT
and SIGTERM request an orderly host stop. The host terminal attributes are
restored on normal and initialization-error exits.

## Applying and running

Review the patch before applying it:

```bash
git apply --check docs/examples/interactive-console.patch
git apply docs/examples/interactive-console.patch
```

Inside the dev VM, rebuild the initramfs as well as the VMM:

```bash
scripts/build-initramfs.sh
meson compile -C build/vm-debug
meson test -C build/vm-debug --print-errorlogs
MINIVMM_BUILD_DIR="$PWD/build/vm-debug" uv run pytest -v tests/integration/test_console.py
build/vm-debug/src/minivmm --kernel guest/out/bzImage \
    --initrd guest/out/initramfs.cpio.gz --interactive
```

The interactive default appends `minivmm.shell=1`. When providing a custom
`--cmdline`, include this argument yourself if you want the shell mode.
Without `--interactive`, the existing stage 3 boot-to-init-and-reboot test
continues to work.

To try the already built example inside the dev VM:

```bash
cd /home/zxcking/oss/minivmm/build/stage4-console-example
build/vm-check/src/minivmm --kernel guest/out/bzImage \
    --initrd guest/out/initramfs.cpio.gz --interactive
MINIVMM_BUILD_DIR="$PWD/build/vm-check" \
    UV_PROJECT_ENVIRONMENT=/home/zxcking/.venv-minivmm \
    uv run --offline pytest -v tests/integration/test_console.py
```

## Validation on 2026-11-12

- Original event_loop compiled with strict warnings; standalone checks covered
  callbacks, add/mod/remove, stale event batches, stop-before-run, cross-thread
  stop, source capacity and pipe data followed by HUP.
- GCC builds with `-Dwerror=true` passed on the host and in the dev VM.
- 14 Meson unit/replay tests passed.
- 20 pytest checks passed, 2 future-stage tests skipped.
- PTY cases: guest shell exit/reboot, external SIGINT, external SIGTERM,
  Ctrl-] host exit. The guest-exit case also checks Ctrl-C interrupting a
  foreground sleep and 200 pasted commands exceeding the 4096-byte input queue.
- PTY checks compare terminal attributes and file flags before/after execution.
  Expected output markers cannot be satisfied by terminal echo alone.
- ruff and clang-format passed for the example; git apply --check passed.

## References

- [KVM immediate_exit and signal kicks](https://docs.kernel.org/virt/kvm/api.html#the-kvm-run-structure)
- [signalfd and blocked signal masks](https://man7.org/linux/man-pages/man2/signalfd.2.html)
- [Controlling terminal requirements](https://man7.org/linux/man-pages/man2/TIOCSCTTY.2const.html)
