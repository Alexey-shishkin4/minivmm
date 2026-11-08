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


def test_realmode_mmio(minivmm, payloads):
    """Accesses above RAM exit to the VMM; an unassigned read returns all ones."""
    result = run_minivmm(minivmm, "--mem", "1M", "--payload", payloads / "realmode_mmio.bin")
    assert result.returncode == 0, result.stderr
    assert result.stdout == "mmio read 0xff\n"
    assert "unhandled mmio write 0x100000, len 1, value 0x42" in result.stderr
    assert "unhandled mmio read 0x100000, len 1" in result.stderr


def test_realmode_ud2(minivmm, payloads):
    """#UD goes through the guest's own IVT: no exit, the handler prints."""
    result = run_minivmm(minivmm, "--payload", payloads / "realmode_ud2.bin")
    assert result.returncode == 0, result.stderr
    assert result.stdout == "#UD at ud2\n"


def test_realmode_triple_fault(minivmm, payloads):
    """An IDT with limit 0 turns int3 into a triple fault: KVM_EXIT_SHUTDOWN."""
    result = run_minivmm(minivmm, "--payload", payloads / "realmode_triple.bin")
    assert result.returncode == 1
    assert result.stdout == ""
    assert "triple fault" in result.stderr


def test_missing_payload(minivmm, tmp_path):
    result = run_minivmm(minivmm, "--payload", tmp_path / "nope.bin")
    assert result.returncode == 1
    assert "No such file or directory" in result.stderr


def test_longmode_hello(minivmm, payloads):
    """The 64-bit guest checks arithmetic, CPUID and MMIO, then halts."""
    result = run_minivmm(
        minivmm, "--mode", "long", "--mem", "1M", "--payload", payloads / "longmode_hello.bin"
    )
    assert result.returncode == 0, result.stdout + result.stderr
    assert result.stdout == (
        "Hello from long mode\n64-bit arithmetic: OK\nCPUID: OK\nMMIO: OK\nlongmode: PASS\n"
    )
    assert "unhandled mmio write 0x200000, len 1, value 0x42" in result.stderr
    assert "unhandled mmio read 0x200000, len 1" in result.stderr
