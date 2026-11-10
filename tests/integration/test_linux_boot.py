"""Stages 3+: boot the real guest kernel with the test initramfs."""

import subprocess

import pytest

pytestmark = [pytest.mark.kvm, pytest.mark.guest]


@pytest.mark.parametrize("memory", ["64M", "128M", "512M"])
def test_boot_to_init(minivmm, guest_kernel, guest_initramfs, memory):
    """Boot the real kernel to PID 1 and finish through the i8042 reset path."""
    result = subprocess.run(
        [
            str(minivmm),
            "--kernel",
            str(guest_kernel),
            "--initrd",
            str(guest_initramfs),
            "--mem",
            memory,
            "--cmdline",
            "console=ttyS0 earlyprintk=serial,ttyS0,115200 rdinit=/init reboot=k panic=-1",
        ],
        capture_output=True,
        text=True,
        timeout=30,
        check=False,
    )
    log = result.stdout + result.stderr
    assert result.returncode == 0, log
    assert "Linux version" in result.stdout, log
    for message in ("starting", "ready", "reboot"):
        assert f"minivmm-init: {message}" in result.stdout, log
    assert "guest requested reboot via i8042" in result.stderr, log
    assert "Kernel panic" not in log and "minivmm-init: FAIL" not in log, log


@pytest.mark.skip(reason="TODO: stage 6")
def test_virtio_blk_read_write(minivmm, guest_kernel, guest_initramfs, tmp_path):
    """TODO: attach a disk image, guest writes a pattern to /dev/vda, host verifies the file."""


@pytest.mark.skip(reason="TODO: stage 7")
def test_smp_cpus_visible(minivmm, guest_kernel, guest_initramfs):
    """TODO: --cpus 4 -> guest /proc/cpuinfo lists 4 processors (ACPI MADT)."""
