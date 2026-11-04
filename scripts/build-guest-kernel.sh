#!/usr/bin/env bash
# Build the minimal guest kernel: tinyconfig + guest/kernel/minivmm.config.
# Output: guest/out/bzImage, guest/out/vmlinux (for gdb / perf symbols).
set -euo pipefail
source "$(dirname "$0")/versions.sh"

"$(dirname "$0")/fetch-guest-sources.sh"

KSRC="${GUEST_OUT}/src/linux-${LINUX_VERSION}"
KBUILD="${GUEST_OUT}/kernel-build"
FRAGMENT="${REPO_ROOT}/guest/kernel/minivmm.config"
mkdir -p "${KBUILD}"

make -C "${KSRC}" O="${KBUILD}" tinyconfig
"${KSRC}/scripts/kconfig/merge_config.sh" -m -O "${KBUILD}" "${KBUILD}/.config" "${FRAGMENT}"
make -C "${KSRC}" O="${KBUILD}" olddefconfig

# Fail loudly if Kconfig silently dropped something we asked for.
missing=0
while IFS= read -r line; do
    [[ "${line}" =~ ^CONFIG_ ]] || continue
    if ! grep -qx "${line}" "${KBUILD}/.config"; then
        echo "warning: not applied: ${line}" >&2
        missing=1
    fi
done < "${FRAGMENT}"
if [[ ${missing} -ne 0 && "${ALLOW_MISSING_CONFIG:-0}" != 1 ]]; then
    echo "error: config fragment not fully applied (set ALLOW_MISSING_CONFIG=1 to continue)" >&2
    exit 1
fi

make -C "${KSRC}" O="${KBUILD}" -j"$(nproc)" bzImage
cp "${KBUILD}/arch/x86/boot/bzImage" "${KBUILD}/vmlinux" "${GUEST_OUT}/"
echo "built ${GUEST_OUT}/bzImage"
