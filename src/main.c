/*
 * minivmm command line entry point.
 *
 * TODO: --cpus N, --disk FILE[,ro], --stats (exit counters)
 * TODO: map guest poweroff to exit code 0 once there is a way to power off
 */

#include <ctype.h>
#include <errno.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "boot/linux_loader.h"
#include "boot/raw_loader.h"
#include "util/log.h"
#include "vcpu.h"
#include "vmm.h"
#include "x86/cpu_setup.h"
#include "x86/cpuid.h"

#define EXIT_USAGE 2

#define DEFAULT_RAM_SIZE 0x8000000ULL /* 128 MiB */
#define DEFAULT_CMDLINE                                                        \
    "console=ttyS0 earlyprintk=serial,ttyS0,115200 "                           \
    "rdinit=/init reboot=k panic=-1"

struct options {
    const char *payload;
    const char *kernel;
    const char *initrd;
    const char *cmdline;
    struct vmm_config vm;
    bool long_mode;
};

static void usage(FILE *out, const char *prog)
{
    fprintf(
        out,
        "usage: %s --payload FILE [--mem SIZE] [--mode real|long] [--verbose]\n"
        "       %s --kernel FILE [--initrd FILE] [--cmdline STR] [--mem SIZE]\n"
        "\n"
        "  -p, --payload FILE  flat binary loaded at 0x%x\n"
        "  -k, --kernel FILE   x86-64 bzImage (boot protocol >= 2.12)\n"
        "  -i, --initrd FILE   initramfs for Linux boot\n"
        "  -c, --cmdline STR   Linux command line (default: ttyS0, /init, "
        "reboot=k)\n"
        "  -m, --mem SIZE      guest RAM, suffix K, M or G (default 128M)\n"
        "      --mode MODE     real (default) or long\n"
        "  -v, --verbose       debug logging\n"
        "  -h, --help          show this help\n",
        prog, prog, RAW_LOAD_ADDR);
}

/* "64M" -> 0x4000000. Decimal only, so "010" is not octal 8. */
static int parse_size(const char *s, uint64_t *out)
{
    unsigned long long value;
    unsigned shift = 0;
    char *end;

    /* strtoull() accepts "-1" and returns ULLONG_MAX without an error. */
    if (!isdigit((unsigned char)s[0])) {
        return -1;
    }
    errno = 0;
    value = strtoull(s, &end, 10);
    if (errno != 0) {
        return -1;
    }
    switch (*end) {
    case 'K':
    case 'k':
        shift = 10;
        end++;
        break;
    case 'M':
    case 'm':
        shift = 20;
        end++;
        break;
    case 'G':
    case 'g':
        shift = 30;
        end++;
        break;
    default:
        break;
    }
    if (*end != '\0' || value > (UINT64_MAX >> shift)) {
        return -1;
    }
    *out = (uint64_t)value << shift;
    return 0;
}

/* Returns 0 to run, 1 if --help was printed, -1 on a usage error. */
static int parse_options(int argc, char **argv, struct options *opts)
{
    enum { OPT_MODE = 256 };

    static const struct option long_options[] = {
        {"mode", required_argument, NULL, OPT_MODE},
        {"payload", required_argument, NULL, 'p'},
        {"kernel", required_argument, NULL, 'k'},
        {"initrd", required_argument, NULL, 'i'},
        {"cmdline", required_argument, NULL, 'c'},
        {"mem", required_argument, NULL, 'm'},
        {"verbose", no_argument, NULL, 'v'},
        {"help", no_argument, NULL, 'h'},
        {NULL, 0, NULL, 0},
    };
    int c;
    bool mode_given = false;
    bool cmdline_given = false;

    *opts = (struct options){.cmdline = DEFAULT_CMDLINE,
                             .vm = {.ram_size = DEFAULT_RAM_SIZE}};

    /* getopt keeps its state in globals: fine before any vCPU thread runs. */
    /* NOLINTNEXTLINE(concurrency-mt-unsafe) */
    while ((c = getopt_long(argc, argv, "p:k:i:c:m:vh", long_options, NULL)) !=
           -1) {
        switch (c) {
        case 'p':
            opts->payload = optarg;
            break;
        case 'k':
            opts->kernel = optarg;
            break;
        case 'i':
            opts->initrd = optarg;
            break;
        case 'c':
            opts->cmdline = optarg;
            cmdline_given = true;
            break;
        case 'm':
            if (parse_size(optarg, &opts->vm.ram_size) < 0) {
                log_error("--mem: bad size '%s'", optarg);
                return -1;
            }
            break;
        case OPT_MODE:
            mode_given = true;
            if (strcmp(optarg, "real") == 0) {
                opts->long_mode = false;
            } else if (strcmp(optarg, "long") == 0) {
                opts->long_mode = true;
            } else {
                log_error("--mode: expected real or long, got '%s'", optarg);
                return -1;
            }
            break;
        case 'v':
            log_set_level(LOG_LEVEL_DEBUG);
            break;
        case 'h':
            usage(stdout, argv[0]);
            return 1;
        default: /* getopt_long() has already printed what is wrong */
            usage(stderr, argv[0]);
            return -1;
        }
    }
    if (optind < argc) {
        log_error("unexpected argument '%s'", argv[optind]);
        return -1;
    }
    if ((opts->payload == NULL) == (opts->kernel == NULL)) {
        log_error("specify exactly one of --payload and --kernel");
        usage(stderr, argv[0]);
        return -1;
    }
    if ((opts->kernel == NULL && (opts->initrd != NULL || cmdline_given)) ||
        (opts->kernel != NULL && mode_given)) {
        log_error(
            "--initrd/--cmdline require --kernel; --mode requires --payload");
        return -1;
    }
    opts->vm.linux_boot = opts->kernel != NULL;
    return 0;
}

/* Create the VM, run the selected guest and tear everything down again. */
static int run_guest(const struct options *opts)
{
    struct vmm vm;
    struct vcpu vcpu;
    struct linux_boot boot;
    int ret = EXIT_FAILURE;

    if (vmm_create(&vm, &opts->vm) < 0) {
        return EXIT_FAILURE;
    }

    if (opts->vm.linux_boot ? linux_load(&vm.mem, opts->kernel, opts->initrd,
                                         opts->cmdline, &boot) < 0
                            : raw_load(&vm.mem, opts->payload) < 0) {
        goto out_vm;
    }
    /* TODO: struct vmm owns its vCPUs once there is more than one */
    if (vcpu_create(&vcpu, &vm, 0) < 0) {
        goto out_vm;
    }
    if (opts->long_mode || opts->vm.linux_boot) {
        if (cpuid_setup(&vm.kvm, &vcpu) < 0) {
            goto out_vcpu;
        }
        if (opts->vm.linux_boot ? cpu_setup_linux(&vcpu, &vm.mem, &boot) < 0
                                : cpu_setup_long_mode(&vcpu, &vm.mem) < 0) {
            goto out_vcpu;
        }
    } else if (cpu_setup_real_mode(&vcpu, RAW_LOAD_ADDR) < 0) {
        goto out_vcpu;
    }
    const enum vcpu_stop stop = vcpu_run(&vcpu);
    if (opts->vm.linux_boot ? stop == VCPU_STOP_REBOOT
                            : stop == VCPU_STOP_HLT) {
        ret = EXIT_SUCCESS;
    }

out_vcpu:
    vcpu_destroy(&vcpu);
out_vm:
    vmm_destroy(&vm);
    return ret;
}

int main(int argc, char **argv)
{
    struct options opts;
    const int parsed = parse_options(argc, argv, &opts);

    if (parsed != 0) {
        return parsed > 0 ? EXIT_SUCCESS : EXIT_USAGE;
    }
    return run_guest(&opts);
}
