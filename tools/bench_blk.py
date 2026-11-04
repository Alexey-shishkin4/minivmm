"""Block I/O benchmark: minivmm virtio-blk vs. QEMU virtio-blk on the same guest.

TODO: boot each VMM with the same kernel/initramfs and disk image, run fio
      (or a small built-in dd-like test in the guest init) for 4k randread /
      128k seqwrite, collect IOPS / bandwidth / latency percentiles,
      emit a markdown table for docs/performance.md.
"""

import argparse


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.parse_args()
    raise SystemExit("TODO: not implemented yet")


if __name__ == "__main__":
    main()
