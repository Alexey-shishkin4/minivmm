"""Shared fixtures: locate the minivmm binary and guest artifacts, skip cleanly
when the host cannot run them (no /dev/kvm, guest not built)."""

import os
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
BUILD_DIR = Path(os.environ.get("MINIVMM_BUILD_DIR", ROOT / "build" / "debug"))
GUEST_OUT = ROOT / "guest" / "out"


def kvm_usable() -> bool:
    """True if /dev/kvm can really be opened: the node may exist without a
    loaded driver (ENODEV), e.g. in WSL without nested virtualization."""
    try:
        fd = os.open("/dev/kvm", os.O_RDWR | os.O_CLOEXEC)
    except OSError:
        return False
    os.close(fd)
    return True


def pytest_collection_modifyitems(config, items):
    kvm_ok = kvm_usable()
    for item in items:
        if "kvm" in item.keywords and not kvm_ok:
            item.add_marker(pytest.mark.skip(reason="/dev/kvm not accessible"))


@pytest.fixture(scope="session")
def minivmm() -> Path:
    exe = BUILD_DIR / "src" / "minivmm"
    if not exe.exists():
        pytest.skip(f"{exe} not built (meson compile -C {BUILD_DIR})")
    return exe


@pytest.fixture(scope="session")
def payloads() -> Path:
    path = BUILD_DIR / "guest"
    if not (path / "realmode_hello.bin").exists():
        pytest.skip("guest payloads not built")
    return path


@pytest.fixture(scope="session")
def guest_kernel() -> Path:
    path = GUEST_OUT / "bzImage"
    if not path.exists():
        pytest.skip("guest kernel not built (scripts/build-guest-kernel.sh)")
    return path


@pytest.fixture(scope="session")
def guest_initramfs() -> Path:
    path = GUEST_OUT / "initramfs.cpio.gz"
    if not path.exists():
        pytest.skip("initramfs not built (scripts/build-initramfs.sh)")
    return path


# TODO: a `vm` fixture/helper class: start minivmm with a timeout, stream the
#       serial console, expect(pattern) like pexpect, collect exit code and
#       --stats output; always kill the process on test teardown.
