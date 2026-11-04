"""Stages 1-2: flat-binary payloads in real and long mode."""

import pytest

pytestmark = pytest.mark.kvm


@pytest.mark.skip(reason="TODO: stage 1")
def test_realmode_hello(minivmm, payloads):
    """TODO: run --payload realmode_hello.bin, expect the greeting on stdout and exit 0 on hlt."""


@pytest.mark.skip(reason="TODO: stage 2")
def test_longmode_hello(minivmm, payloads):
    """TODO: same in 64-bit mode; also check reported CPUID / MMIO exit handling."""
