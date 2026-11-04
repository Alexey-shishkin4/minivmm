/*
 * PID 1 for the test initramfs (built statically by scripts/build-initramfs.sh).
 *
 * TODO: mount proc, sysfs, devtmpfs; print a well-known banner the
 *        integration tests wait for ("minivmm-init: ready");
 * TODO: self-checks driven by the kernel command line (minivmm.test=...):
 *        read /proc/cpuinfo, read/write /dev/vda,
 *        report "minivmm-test: PASS" or "minivmm-test: FAIL";
 * TODO: exec busybox sh when minivmm.shell=1, otherwise reboot(RB_POWER_OFF).
 */

int main(void)
{
    return 0;
}
