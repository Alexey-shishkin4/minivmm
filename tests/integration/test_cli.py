"""Argument validation does not require KVM or guest artifacts."""

import subprocess

import pytest


@pytest.mark.parametrize(
    "args",
    [
        [],
        ["--payload", "payload.bin", "--kernel", "bzImage"],
        ["--payload", "payload.bin", "--initrd", "initrd"],
        ["--payload", "payload.bin", "--cmdline", "console=ttyS0"],
        ["--kernel", "bzImage", "--mode", "long"],
        ["--payload", "payload.bin", "--mode", "invalid"],
    ],
)
def test_invalid_boot_arguments(minivmm, args):
    result = subprocess.run(
        [str(minivmm), *args], capture_output=True, text=True, timeout=5, check=False
    )
    assert result.returncode == 2, result.stderr
    assert "/dev/kvm" not in result.stderr


def test_help_lists_linux_boot(minivmm):
    result = subprocess.run(
        [str(minivmm), "--help"], capture_output=True, text=True, timeout=5, check=False
    )
    assert result.returncode == 0, result.stderr
    for option in ("--payload", "--kernel", "--initrd", "--cmdline"):
        assert option in result.stdout
