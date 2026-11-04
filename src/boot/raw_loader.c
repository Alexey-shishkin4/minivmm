#include "boot/raw_loader.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <sys/stat.h>
#include <unistd.h>

#include "util/log.h"

/* read() may return less than asked for; loop until len bytes are in. */
static int read_full(int fd, uint8_t *buf, size_t len)
{
    while (len > 0) {
        const ssize_t n = read(fd, buf, len);

        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (n == 0) {
            errno = EIO; /* the file got shorter since fstat() */
            return -1;
        }
        buf += n;
        len -= (size_t)n;
    }
    return 0;
}

int raw_load(const struct guest_memory *mem, const char *path)
{
    struct stat st;
    uint8_t *dst;
    size_t size;
    int saved_errno;
    int fd;

    fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        log_errno("open %s", path);
        return -1;
    }
    if (fstat(fd, &st) < 0) {
        log_errno("stat %s", path);
        goto fail;
    }
    if (!S_ISREG(st.st_mode) || st.st_size == 0) {
        errno = EINVAL;
        log_error("%s: not a regular non-empty file", path);
        goto fail;
    }
    if (st.st_size > RAW_MAX_SIZE) {
        errno = EFBIG;
        log_error("%s: %lld bytes, a payload may have at most %d", path,
                  (long long)st.st_size, RAW_MAX_SIZE);
        goto fail;
    }
    size = (size_t)st.st_size;

    dst = gpa_to_hva(mem, RAW_LOAD_ADDR, size);
    if (dst == NULL) {
        errno = ENOMEM;
        log_error("%s: does not fit in guest RAM at 0x%x", path, RAW_LOAD_ADDR);
        goto fail;
    }
    if (read_full(fd, dst, size) < 0) {
        log_errno("read %s", path);
        goto fail;
    }
    close(fd);
    log_debug("loaded %s: %zu bytes at 0x%x", path, size, RAW_LOAD_ADDR);
    return 0;

fail:
    saved_errno = errno;
    close(fd);
    errno = saved_errno;
    return -1;
}
