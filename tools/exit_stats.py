"""VM-exit statistics for performance work.

TODO:
  * parse `minivmm --stats` output (per-exit-reason counts and time);
  * alternatively parse `perf kvm stat record/report` or the kvm:kvm_exit
    tracepoint (`perf record -e kvm:kvm_exit -a`) for the same workload;
  * print a table and optionally compare two runs (before/after an optimization,
    e.g. ioeventfd vs. plain PIO exits for virtio notifications).
"""

import argparse


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.parse_args()
    raise SystemExit("TODO: not implemented yet")


if __name__ == "__main__":
    main()
