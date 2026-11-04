#!/usr/bin/env bash
# Development VM (L1): minivmm is built and run inside it, so its guests are
# L2 under nested KVM. The repository is shared over virtiofs at the same path
# as on the host: edit on the host, build and run inside the VM.
#
#   scripts/run-dev-vm.sh [QEMU args]   boot; the first boot provisions the VM
#                                       with cloud-init (packages, user, mount)
#   scripts/run-dev-vm.sh ssh [cmd]     log in from another terminal
#
# Environment: MEM (6G), SMP (4), DISK_SIZE (32G), SSH_PORT (2222),
# DEVVM_DIR (guest/out/devvm), RESET=1 recreates the disk from the base image.
# Extra QEMU args, e.g. -s, give a gdbstub on the L1 kernel (KVM itself).
# The serial console is on stdio: quit with Ctrl-A X.
set -euo pipefail
source "$(dirname "$0")/versions.sh"

DEVVM_DIR="${DEVVM_DIR:-${GUEST_OUT}/devvm}"
MEM="${MEM:-6G}"
SMP="${SMP:-4}"
DISK_SIZE="${DISK_SIZE:-32G}"
SSH_PORT="${SSH_PORT:-2222}"

KEY="${DEVVM_DIR}/id_ed25519"
BASE="${DEVVM_DIR}/$(basename "${DEVVM_IMAGE_URL}")"
DISK="${DEVVM_DIR}/disk.qcow2"
SEED="${DEVVM_DIR}/seed.iso"
SOCK="${DEVVM_DIR}/virtiofs.sock"

die() {
    echo "run-dev-vm: $*" >&2
    exit 1
}

need() { # program hint
    command -v "$1" >/dev/null || die "$1 not found ($2)"
}

if [[ "${1:-}" == ssh ]]; then
    shift
    [[ -f "${KEY}" ]] || die "no VM yet: boot it first"
    exec ssh -i "${KEY}" -p "${SSH_PORT}" \
        -o StrictHostKeyChecking=accept-new \
        -o "UserKnownHostsFile=${DEVVM_DIR}/known_hosts" \
        "${USER}@127.0.0.1" "$@"
fi

need qemu-system-x86_64 "pacman -S qemu-system-x86"
need qemu-img "pacman -S qemu-img"
need xorriso "pacman -S libisoburn"
need ssh-keygen "pacman -S openssh"
VIRTIOFSD="${VIRTIOFSD:-$(command -v virtiofsd || echo /usr/libexec/virtiofsd)}"
[[ -x "${VIRTIOFSD}" ]] || die "virtiofsd not found (pacman -S virtiofsd)"

# Same check as run-qemu-reference.sh: the node may exist without a driver.
(exec 3<>/dev/kvm) 2>/dev/null || die "/dev/kvm cannot be opened"
nested="$(cat /sys/module/kvm_intel/parameters/nested \
    /sys/module/kvm_amd/parameters/nested 2>/dev/null || true)"
[[ "${nested}" == *[Y1]* ]] || die "nested virtualization is disabled in the host KVM module"

mkdir -p "${DEVVM_DIR}"

if [[ ! -f "${BASE}" ]]; then
    echo "downloading ${DEVVM_IMAGE_URL}"
    curl -fL --retry 3 -o "${BASE}.part" "${DEVVM_IMAGE_URL}"
    echo "${DEVVM_IMAGE_SHA256}  ${BASE}.part" | sha256sum -c -
    mv "${BASE}.part" "${BASE}"
fi

if [[ "${RESET:-0}" == 1 ]]; then
    rm -f "${DISK}" "${SEED}" "${DEVVM_DIR}/known_hosts"
fi

[[ -f "${KEY}" ]] || ssh-keygen -q -t ed25519 -N '' -C minivmm-devvm -f "${KEY}"

if [[ ! -f "${DISK}" ]]; then
    qemu-img create -q -f qcow2 -F qcow2 -b "${BASE}" "${DISK}" "${DISK_SIZE}"
    rm -f "${SEED}" "${DEVVM_DIR}/known_hosts"
fi

if [[ ! -f "${SEED}" ]]; then
    seed_dir="$(mktemp -d "${DEVVM_DIR}/seed.XXXXXX")"
    trap 'rm -rf "${seed_dir}"' EXIT
    cat >"${seed_dir}/meta-data" <<EOF
instance-id: minivmm-dev-$(date +%s)
local-hostname: minivmm-dev
EOF
    # Same uid as on the host, so files on the virtiofs share have the right
    # owner. The share is mounted from runcmd rather than "mounts:" because
    # that module runs before users are created and would leave a root-owned
    # home directory behind. The VM keeps its own venv and build tree: the
    # host's ones live in the same shared directory.
    cat >"${seed_dir}/user-data" <<EOF
#cloud-config
hostname: minivmm-dev
users:
  - name: ${USER}
    uid: $(id -u)
    groups: [kvm, sudo]
    shell: /bin/bash
    sudo: "ALL=(ALL) NOPASSWD:ALL"
    lock_passwd: true
    ssh_authorized_keys:
      - $(cat "${KEY}.pub")
package_update: true
packages: [bc, bison, build-essential, ca-certificates, clang, clang-format,
           clang-tidy, cpio, curl, flex, gdb, git, libelf-dev, libssl-dev, lld,
           llvm, meson, ninja-build, python3, qemu-system-x86, strace, xz-utils]
write_files:
  - path: /etc/environment
    append: true
    content: |
      UV_PROJECT_ENVIRONMENT=/home/${USER}/.venv-minivmm
      MINIVMM_BUILD_DIR=${REPO_ROOT}/build/vm-debug
runcmd:
  - runuser -u ${USER} -- mkdir -p "${REPO_ROOT}" || mkdir -p "${REPO_ROOT}"
  - echo "minivmm ${REPO_ROOT} virtiofs defaults,nofail 0 0" >> /etc/fstab
  - mount "${REPO_ROOT}"
  - curl -fsSL https://github.com/astral-sh/uv/releases/download/${DEVVM_UV_VERSION}/uv-x86_64-unknown-linux-gnu.tar.gz | tar -xz -C /usr/local/bin --strip-components=1
EOF
    xorriso -as mkisofs -quiet -output "${SEED}" -volid cidata -joliet -rock \
        "${seed_dir}/user-data" "${seed_dir}/meta-data"
    rm -rf "${seed_dir}"
    trap - EXIT
    echo "run-dev-vm: first boot, provisioning takes a few minutes;" \
        "'$0 ssh cloud-init status --wait' tells when it is done" >&2
fi

# virtiofsd is itself a vhost-user backend: QEMU hands it the guest memory
# (hence the shared memfd backend) and the virtqueues, the same protocol as
# in stage 10 of the roadmap.
rm -f "${SOCK}"
"${VIRTIOFSD}" --socket-path="${SOCK}" --shared-dir="${REPO_ROOT}" \
    --sandbox=none --cache=auto 2>"${DEVVM_DIR}/virtiofsd.log" &
virtiofsd_pid=$!
trap 'kill "${virtiofsd_pid}" 2>/dev/null || true' EXIT
for _ in $(seq 50); do
    [[ -S "${SOCK}" ]] && break
    sleep 0.1
done
[[ -S "${SOCK}" ]] || die "virtiofsd did not start, see ${DEVVM_DIR}/virtiofsd.log"

echo "run-dev-vm: ssh with '$0 ssh' (port ${SSH_PORT})" >&2
qemu-system-x86_64 \
    -accel kvm -cpu host -M q35,memory-backend=mem \
    -smp "${SMP}" -m "${MEM}" \
    -object memory-backend-memfd,id=mem,size="${MEM}",share=on \
    -drive file="${DISK}",if=virtio,format=qcow2,discard=unmap \
    -drive file="${SEED}",if=virtio,format=raw,readonly=on \
    -chardev socket,id=virtiofs,path="${SOCK}" \
    -device vhost-user-fs-pci,chardev=virtiofs,tag=minivmm \
    -netdev user,id=net0,hostfwd=tcp:127.0.0.1:"${SSH_PORT}"-:22 \
    -device virtio-net-pci,netdev=net0 \
    -device virtio-rng-pci \
    -nographic \
    "$@"
