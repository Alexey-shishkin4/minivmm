#!/usr/bin/env bash
# Build guest/out/initramfs.cpio.gz: our static /init + static BusyBox.
# Uses the kernel's usr/gen_init_cpio (built by build-guest-kernel.sh), so
# device nodes can be created without root.
set -euo pipefail
source "$(dirname "$0")/versions.sh"

"$(dirname "$0")/fetch-guest-sources.sh"

BBSRC="${GUEST_OUT}/src/busybox-${BUSYBOX_VERSION}"
BBBUILD="${GUEST_OUT}/busybox-build"
GEN_INIT_CPIO="${GUEST_OUT}/kernel-build/usr/gen_init_cpio"
ROOTFS="${GUEST_OUT}/rootfs"

if [[ ! -x "${GEN_INIT_CPIO}" ]]; then
    echo "error: ${GEN_INIT_CPIO} missing, run scripts/build-guest-kernel.sh first" >&2
    exit 1
fi

# --- BusyBox (static) ---------------------------------------------------------
mkdir -p "${BBBUILD}"
if [[ ! -f "${BBBUILD}/.config" ]]; then
    make -C "${BBSRC}" O="${BBBUILD}" defconfig
    sed -i \
        -e 's/^# CONFIG_STATIC is not set/CONFIG_STATIC=y/' \
        -e 's/^CONFIG_TC=y/# CONFIG_TC is not set/' \
        "${BBBUILD}/.config"
    make -C "${BBSRC}" O="${BBBUILD}" oldconfig </dev/null >/dev/null
fi
make -C "${BBBUILD}" -j"$(nproc)" busybox

# --- /init --------------------------------------------------------------------
rm -rf "${ROOTFS}"
mkdir -p "${ROOTFS}"
gcc -static -O2 -Wall -Wextra -o "${ROOTFS}/init" "${REPO_ROOT}/guest/init/init.c"

# --- cpio list ----------------------------------------------------------------
LIST="${GUEST_OUT}/initramfs.list"
{
    echo "dir /dev 0755 0 0"
    echo "nod /dev/console 0600 0 0 c 5 1"
    echo "nod /dev/null 0666 0 0 c 1 3"
    echo "dir /proc 0755 0 0"
    echo "dir /sys 0755 0 0"
    echo "dir /tmp 1777 0 0"
    echo "dir /bin 0755 0 0"
    echo "file /init ${ROOTFS}/init 0755 0 0"
    echo "file /bin/busybox ${BBBUILD}/busybox 0755 0 0"
    for applet in $("${BBBUILD}/busybox" --list); do
        [[ "${applet}" == busybox ]] && continue
        echo "slink /bin/${applet} busybox 0777 0 0"
    done
} > "${LIST}"

"${GEN_INIT_CPIO}" "${LIST}" | gzip -9 > "${GUEST_OUT}/initramfs.cpio.gz"
echo "built ${GUEST_OUT}/initramfs.cpio.gz"
