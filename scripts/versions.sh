# Pinned guest component versions and checksums (sourced by other scripts).

LINUX_VERSION=6.18.59
LINUX_URL="https://cdn.kernel.org/pub/linux/kernel/v6.x/linux-${LINUX_VERSION}.tar.xz"
LINUX_SHA256=53a55ac9863a5547e292b8e381d4e4c2240dd1127d5428a7c44dbc550be23963

BUSYBOX_VERSION=1.38.0
BUSYBOX_URL="https://busybox.net/downloads/busybox-${BUSYBOX_VERSION}.tar.bz2"
BUSYBOX_SHA256=34f9ea6ff8636f2c9241153b9114eefa9e65674a45318ae1ef95bb5f31c53bb2

# Development VM (L1) that builds and runs minivmm, see scripts/run-dev-vm.sh.
# Same distribution as the Dockerfile; uv version kept in sync with it too.
DEVVM_IMAGE_URL="https://cloud-images.ubuntu.com/releases/26.04/release-20261016/ubuntu-26.04-server-cloudimg-amd64.img"
DEVVM_IMAGE_SHA256=5645d0318ad21f643ebb1ac4567514620e9893380b07b0201fda163baa5b7eb5
DEVVM_UV_VERSION=0.12.23

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
GUEST_OUT="${GUEST_OUT:-${REPO_ROOT}/guest/out}"
