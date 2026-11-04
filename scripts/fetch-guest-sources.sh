#!/usr/bin/env bash
# Download and verify pinned Linux and BusyBox sources into guest/out/src.
set -euo pipefail
source "$(dirname "$0")/versions.sh"

SRC="${GUEST_OUT}/src"
mkdir -p "${SRC}"

fetch() { # url sha256 dest_dir_name
    local url=$1 sha=$2 name=$3
    local archive="${SRC}/$(basename "${url}")"
    if [[ -d "${SRC}/${name}" ]]; then
        echo "ok: ${name} already extracted"
        return
    fi
    if [[ ! -f "${archive}" ]]; then
        echo "downloading ${url}"
        curl -fL --retry 3 -o "${archive}.part" "${url}"
        mv "${archive}.part" "${archive}"
    fi
    echo "${sha}  ${archive}" | sha256sum -c -
    tar -C "${SRC}" -xf "${archive}"
}

fetch "${LINUX_URL}" "${LINUX_SHA256}" "linux-${LINUX_VERSION}"
fetch "${BUSYBOX_URL}" "${BUSYBOX_SHA256}" "busybox-${BUSYBOX_VERSION}"
