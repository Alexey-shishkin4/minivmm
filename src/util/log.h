/*
 * Logging: levels, timestamps, vCPU id; fatal() with errno.
 *
 * TODO: log(level, fmt, ...) with __attribute__((format(printf, ...)))
 */

#ifndef MINIVMM_UTIL_LOG_H
#define MINIVMM_UTIL_LOG_H

enum log_level {
    LOG_LEVEL_DEBUG,
    LOG_LEVEL_INFO,
    LOG_LEVEL_WARN,
    LOG_LEVEL_ERROR,
};

void log_set_level(enum log_level level);

void log_set_vcpu(int id);

void log_msg(enum log_level level, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));

void log_errno(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

_Noreturn void die(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
_Noreturn void die_errno(const char *fmt, ...)
    __attribute__((format(printf, 1, 2)));

#define log_debug(...) log_msg(LOG_LEVEL_DEBUG, __VA_ARGS__)
#define log_info(...) log_msg(LOG_LEVEL_INFO, __VA_ARGS__)
#define log_warn(...) log_msg(LOG_LEVEL_WARN, __VA_ARGS__)
#define log_error(...) log_msg(LOG_LEVEL_ERROR, __VA_ARGS__)

struct logger;

#endif /* MINIVMM_UTIL_LOG_H */
