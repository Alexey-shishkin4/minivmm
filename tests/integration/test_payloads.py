"""Stages 1-2: flat-binary payloads in real and long mode."""

import subprocess

import pytest

pytestmark = pytest.mark.kvm


def run_minivmm(minivmm, *args):
    return subprocess.run([minivmm, *args], capture_output=True, text=True, timeout=10, check=False)


def test_realmode_hello(minivmm, payloads):
    """The guest prints through port 0x3f8, then hlt ends the run with 0."""
    result = run_minivmm(minivmm, "--payload", payloads / "realmode_hello.bin")
    assert result.returncode == 0, result.stderr
    assert result.stdout == "Hello from real mode\n"


def test_missing_payload(minivmm, tmp_path):
    result = run_minivmm(minivmm, "--payload", tmp_path / "nope.bin")
    assert result.returncode == 1
    assert "No such file or directory" in result.stderr


@pytest.mark.skip(reason="TODO: stage 2")
def test_longmode_hello(minivmm, payloads):
    """TODO: same in 64-bit mode; also check reported CPUID / MMIO exit handling."""
