/*
 * PID 1 for the stage 3 initramfs.
 * Requires /dev/console, /dev, /proc and /sys from build-initramfs.sh.
 * Use reboot=k on the kernel command line to select the i8042 reset path.
 */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/reboot.h>
#include <termios.h>
#include <unistd.h>


static _Noreturn void fail_errno(const char *operation) {
    const int saved = errno;

    (void)dprintf(STDERR_FILENO, "minivmm-init: FAIL: %s: %s\n", operation, strerror(saved));

    for (;;) {
        pause();
    }
}

static void setup_console(void) {
    const int fd = open("/dev/console", O_RDWR | O_NOCTTY);

    if (fd < 0) {
        fail_errno("open /dev/console");
    }

    for (int target = STDIN_FILENO; target <= STDERR_FILENO; target++) {
        if (dup2(fd,target) < 0) {
            fail_errno("dup2 /dev/console");
        }
    }

    if (fd > STDERR_FILENO && close(fd) < 0) {
        fail_errno("close console fd");
    }
}

static void message(const char *text) {
    if (dprintf(STDOUT_FILENO, "minivmm-init: %s\n", text) < 0) {
        fail_errno("write console");
    }
}

int main(void)
{
    const unsigned long dev_flags = MS_NOSUID | MS_NOEXEC;
    const unsigned long pseudo_flags = MS_NOSUID | MS_NODEV | MS_NOEXEC;

    if (getpid() != 1) {
        fputs("minivmm-init: must run as PID 1\n", stderr);
        return EXIT_FAILURE;
    }

    setup_console();
    message("starting");

    if (mount("devtmpfs", "/dev", "devtmpfs", dev_flags, NULL) < 0) {
        fail_errno("mount devtmpfs on /dev");
    }

    if (mount("proc", "/proc", "proc", pseudo_flags, NULL) < 0) {
        fail_errno("mount proc on /proc");
    }

    if (mount("sysfs", "/sys", "sysfs", pseudo_flags, NULL) < 0) {
        fail_errno("mount sysfs on /sys");
    }

    message("ready");
    sync();
    message("reboot");

    /* Do not reboot while the UART still has queued output. */
    while (tcdrain(STDOUT_FILENO) < 0) {
        if (errno != EINTR) {
            fail_errno("tcdrain console");
        }
    }

    if (reboot(RB_AUTOBOOT) < 0) {
        fail_errno("reboot");
    }

    /* A successful RB_AUTOBOOT never returns. */
    errno = EIO;
    fail_errno("reboot returned unexpectedly");
}
