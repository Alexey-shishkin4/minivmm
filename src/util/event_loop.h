#ifndef MINIVMM_UTIL_EVENT_LOOP_H
#define MINIVMM_UTIL_EVENT_LOOP_H

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

#define EVENT_LOOP_MAX_SOURCES 32

struct event_loop;

/*
 * Return 0 to continue, or -1 with errno set to fail event_loop_run().
 * The callback may add, modify or remove sources, including itself.
 */
typedef int (*event_loop_fn)(struct event_loop *loop, int fd,
                             uint32_t events, void *opaque);

struct event_loop_source {
    int fd; /* borrowed; -1 means unused */
    uint64_t token;
    event_loop_fn fn;
    void *opaque;
};

struct event_loop {
    int epoll_fd;
    int wake_fd;

    atomic_bool stop_requested;
    bool running;

    uint64_t next_token;
    struct event_loop_source sources[EVENT_LOOP_MAX_SOURCES];
};

/*
 * Initialize before publishing to another thread. Do not copy a live loop.
 * On failure the object is safe to destroy.
 */
int event_loop_init(struct event_loop *loop);

/*
 * Owner-thread operations: before run(), or inside one of its callbacks.
 *
 * LT only: events may contain EPOLLIN/OUT/PRI/RDHUP.
 * Fds must be nonblocking.
 *
 * Caller owns fds/opaque. Remove a source before closing its fd or freeing
 * its opaque object.
 *
 * No concurrent table operations or nested/concurrent run() calls.
 */
int event_loop_add(struct event_loop *loop, int fd, uint32_t events,
                    event_loop_fn fn, void *opaque);

int event_loop_mod(struct event_loop *loop, int fd, uint32_t events);

int event_loop_remove(struct event_loop *loop, int fd);

/*
 * Block in epoll_wait(); return 0 on stop, -1 with errno on failure.
 * A stop request is permanent until destroy/init, even before run().
 */
int event_loop_run(struct event_loop *loop);

/*
 * Thread-safe after init and until destroy; not a signal-handler API.
 * Wakes epoll_wait(), not KVM_RUN.
 * Returns 0 or -1 with errno.
 */
int event_loop_stop(struct event_loop *loop);

/*
 * Only after run() AND all stop() calls have finished.
 * Join workers first.
 *
 * Closes internal fds only.
 * Safe after failed init and safe to call twice.
 */
void event_loop_destroy(struct event_loop *loop);

#endif /* MINIVMM_UTIL_EVENT_LOOP_H */
