#include "util/log.h"

#include <errno.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define LOG_LINE_MAX 512

static enum log_level min_level = LOG_LEVEL_INFO;
static _Thread_local int vcpu_id = -1;

static const char *const level_names[] = {[LOG_LEVEL_DEBUG] = "debug",
                                          [LOG_LEVEL_INFO] = "info",
                                          [LOG_LEVEL_WARN] = "warn",
                                          [LOG_LEVEL_ERROR] = "error"};

void log_set_level(enum log_level level)
{
    min_level = level;
}

void log_set_vcpu(int id)
{
    vcpu_id = id;
}

struct line {
    char buf[LOG_LINE_MAX];
    size_t len;
};

__attribute__((format(printf, 2, 0))) static void
line_vadd(struct line *l, const char *fmt, va_list ap)
{
    const size_t room = sizeof(l->buf) - 1;
    const int n = vsnprintf(l->buf + l->len, room - l->len, fmt, ap);

    if (n > 0) {
        l->len += (size_t)n;
        if (l->len >= room) {
            l->len = room - 1;
        }
    }
}

__attribute__((format(printf, 2, 3))) static void line_add(struct line *l,
                                                           const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    line_vadd(l, fmt, ap);
    va_end(ap);
}

__attribute__((format(printf, 3, 0))) static void
emit(enum log_level level, int err, const char *fmt, va_list ap)
{
    struct line l;
    struct timespec now;

    l.len = 0;
    clock_gettime(CLOCK_MONOTONIC, &now);
    line_add(&l, "[%6lld.%06ld] ", (long long)now.tv_sec, now.tv_nsec / 1000);
    if (vcpu_id >= 0) {
        line_add(&l, "vcpu%d: ", vcpu_id);
    }
    line_add(&l, "%s: ", level_names[level]);
    line_vadd(&l, fmt, ap);
    if (err != 0) {
        char buf[128];
        line_add(&l, ": %s", strerror_r(err, buf, sizeof(buf)));
    }
    l.buf[l.len++] = '\n';
    fwrite(l.buf, 1, l.len, stderr);
}

void log_msg(enum log_level level, const char *fmt, ...)
{
    const int saved = errno;
    va_list ap;

    if (level < min_level) {
        return;
    }
    va_start(ap, fmt);
    emit(level, 0, fmt, ap);
    va_end(ap);
    errno = saved;
}

void log_errno(const char *fmt, ...)
{
    const int saved = errno;
    va_list ap;

    va_start(ap, fmt);
    emit(LOG_LEVEL_ERROR, saved, fmt, ap);
    va_end(ap);
    errno = saved;
}

_Noreturn static void terminate(void)
{
    fflush(stdout);
    _exit(EXIT_FAILURE);
}

_Noreturn void die(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    emit(LOG_LEVEL_ERROR, 0, fmt, ap);
    va_end(ap);
    terminate();
}

_Noreturn void die_errno(const char *fmt, ...)
{
    const int saved = errno;
    va_list ap;

    va_start(ap, fmt);
    emit(LOG_LEVEL_ERROR, saved, fmt, ap);
    va_end(ap);
    terminate();
}
