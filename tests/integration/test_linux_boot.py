"""Stages 3+: boot the real guest kernel with the test initramfs."""

import pytest

pytestmark = [pytest.mark.kvm, pytest.mark.guest]


@pytest.mark.skip(reason="TODO: stage 3")
def test_boot_to_init(minivmm, guest_kernel, guest_initramfs):
    """TODO: boot with console=ttyS0, wait for 'minivmm-init: ready', clean poweroff."""


@pytest.mark.skip(reason="TODO: stage 6")
def test_virtio_blk_read_write(minivmm, guest_kernel, guest_initramfs, tmp_path):
    """TODO: attach a disk image, guest writes a pattern to /dev/vda, host verifies the file."""


@pytest.mark.skip(reason="TODO: stage 7")
def test_smp_cpus_visible(minivmm, guest_kernel, guest_initramfs):
    """TODO: --cpus 4 -> guest /proc/cpuinfo lists 4 processors (ACPI MADT)."""
