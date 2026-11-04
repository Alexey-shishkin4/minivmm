/*
 * minivmm command line entry point.
 *
 * TODO: options (getopt_long):
 *        --payload FILE   flat binary for stages 1-2 (real/long mode tests)
 *        --kernel FILE    bzImage, --initrd FILE, --cmdline STR
 *        --mem SIZE, --cpus N, --disk FILE[,ro], --stats (exit counters)
 * TODO: build struct vmm from options, run, map guest outcome to exit code
 *        (poweroff -> 0, triple fault / internal error -> non-zero)
 */

int main(void)
{
    return 0;
}
