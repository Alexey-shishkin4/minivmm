#!/usr/bin/env bash
# Boot the same guest kernel + initramfs under QEMU: the reference to compare
# minivmm against (console output, /proc/cpuinfo, lspci, performance).
# Extra arguments are passed to QEMU, e.g. -drive file=disk.img,if=virtio
set -euo pipefail
source "$(dirname "$0")/versions.sh"

# The node may exist without a driver behind it (open() -> ENODEV), so try
# to actually open it instead of checking permissions.
accel=tcg
if (exec 3<>/dev/kvm) 2>/dev/null; then
    accel=kvm
fi
echo "run-qemu-reference: accel=${accel}" >&2

exec qemu-system-x86_64 \
    -accel "${accel}" \
    -M pc -m 256M -smp "${SMP:-1}" \
    -kernel "${GUEST_OUT}/bzImage" \
    -initrd "${GUEST_OUT}/initramfs.cpio.gz" \
    -append "console=ttyS0 earlyprintk=serial panic=-1 ${CMDLINE:-}" \
    -nographic -no-reboot \
    "$@"
