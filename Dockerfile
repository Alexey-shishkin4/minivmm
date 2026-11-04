# Reproducible build + test environment (same packages as CI).
#
#   docker build -t minivmm .
#   docker run --rm -it --device /dev/kvm -v "$PWD:/src" minivmm
#
# Inside: meson setup build/debug && meson test -C build/debug
#         scripts/build-guest-kernel.sh && scripts/build-initramfs.sh
#         uv run pytest

FROM ubuntu:26.04

ARG DEBIAN_FRONTEND=noninteractive
RUN apt-get update \
 && apt-get install -y --no-install-recommends \
      bc bison build-essential ca-certificates clang clang-format clang-tidy \
      cpio curl flex gdb git libelf-dev libssl-dev lld llvm meson ninja-build \
      python3 qemu-system-x86 strace xz-utils \
 && rm -rf /var/lib/apt/lists/*

COPY --from=ghcr.io/astral-sh/uv:0.12.23/uv /uvx /usr/local/bin/

WORKDIR /src
CMD ["bash"]
