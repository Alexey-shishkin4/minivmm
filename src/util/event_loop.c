#include "util/event_loop.h"

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <unistd.h>

#define EVENT_BATCH_SIZE 16

static bool valid_events(uint32_t events) {
    const uint32_t allowed = EPOLLIN | EPOLLOUT | EPOLLPRI | EPOLLRDHUP;

    return events != 0 && (events & ~allowed) == 0;
}

static struct event_loop_source *find_fd(struct event_loop *loop, int fd) {
    for (unsigned i = 0; i < EVENT_LOOP_MAX_SOURCES; i++) {
        if (loop->sources[i].fd == fd) {
            return &loop->sources[i];
        }
    }

    return NULL;
}

static struct event_loop_source *find_token(struct event_loop *loop, uint64_t token) {
    for (unsigned i = 0; i < EVENT_LOOP_MAX_SOURCES; i++) {
        if (loop->sources[i].fd >= 0 &&
            loop->sources[i].token == token) {
            return &loop->sources[i];
        }
    }

    return NULL;
}


int event_loop_init(struct event_loop *loop)
{
    struct epoll_event event = {
        .events = EPOLLIN,
        .data.u64 = 0,
    };
    int saved;

    if (loop == NULL) {
        errno = EINVAL;
        return -1;
    }

    *loop = (struct event_loop){
        .epoll_fd = -1,
        .wake_fd = -1,
        .next_token = 1,
    };

    atomic_init(&loop->stop_requested, false);

    for (unsigned i = 0; i < EVENT_LOOP_MAX_SOURCES; i++) {
        loop->sources[i].fd = -1;
    }

    loop->epoll_fd = epoll_create1(EPOLL_CLOEXEC);
    if (loop->epoll_fd < 0) {
        goto fail;
    }

    loop->wake_fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (loop->wake_fd < 0) {
        goto fail;
    }

    if (epoll_ctl(loop->epoll_fd, EPOLL_CTL_ADD,
                  loop->wake_fd, &event) < 0) {
        goto fail;
    }

    return 0;

fail:
    saved = errno;
    event_loop_destroy(loop);
    errno = saved;
    return -1;
}

int event_loop_add(struct event_loop *loop, int fd, uint32_t events,
                    event_loop_fn fn, void *opaque)
{
    struct event_loop_source *source;
    struct epoll_event event;

    if (loop == NULL || loop->epoll_fd < 0 || fd < 0 ||
        fd == loop->epoll_fd || fd == loop->wake_fd ||
        fn == NULL || !valid_events(events)) {
        errno = EINVAL;
        return -1;
    }

    if (find_fd(loop, fd) != NULL) {
        errno = EEXIST;
        return -1;
    }

    source = find_fd(loop, -1);
    if (source == NULL) {
        errno = ENOSPC;
        return -1;
    }

    if (loop->next_token == UINT64_MAX) {
        errno = EOVERFLOW;
        return -1;
    }

    event = (struct epoll_event){
        .events = events,
        .data.u64 = loop->next_token,
    };

    if (epoll_ctl(loop->epoll_fd, EPOLL_CTL_ADD, fd, &event) < 0) {
        return -1;
    }

    *source = (struct event_loop_source){
        .fd = fd,
        .token = loop->next_token++,
        .fn = fn,
        .opaque = opaque,
    };

    return 0;
}

int event_loop_mod(struct event_loop *loop, int fd, uint32_t events)
{
    struct event_loop_source *source;
    struct epoll_event event;

    if (loop == NULL || loop->epoll_fd < 0 || fd < 0 ||
        !valid_events(events)) {
        errno = EINVAL;
        return -1;
    }

    source = find_fd(loop, fd);
    if (source == NULL) {
        errno = ENOENT;
        return -1;
    }

    event = (struct epoll_event){
        .events = events,
        .data.u64 = source->token,
    };

    return epoll_ctl(loop->epoll_fd, EPOLL_CTL_MOD, fd, &event);
}

int event_loop_remove(struct event_loop *loop, int fd)
{
    struct event_loop_source *source;

    if (loop == NULL || loop->epoll_fd < 0 || fd < 0) {
        errno = EINVAL;
        return -1;
    }

    source = find_fd(loop, fd);
    if (source == NULL) {
        errno = ENOENT;
        return -1;
    }

    if (epoll_ctl(loop->epoll_fd, EPOLL_CTL_DEL, fd, NULL) < 0) {
        return -1;
    }

    *source = (struct event_loop_source){
        .fd = -1,
    };

    return 0;
}

static int dispatch(struct event_loop *loop)
{
    struct epoll_event events[EVENT_BATCH_SIZE];

    while (!atomic_load_explicit(&loop->stop_requested,
                                  memory_order_acquire)) {
        const int n =
            epoll_wait(loop->epoll_fd, events, EVENT_BATCH_SIZE, -1);

        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }

            return -1;
        }

        for (int i = 0; i < n; i++) {
            struct event_loop_source *source;

            if (atomic_load_explicit(&loop->stop_requested,
                                     memory_order_acquire)) {
                return 0;
            }

            /*
             * Token 0 is reserved for the stop eventfd.
             * A stop is permanent, so the counter does not need
             * draining before destruction.
             */
            if (events[i].data.u64 == 0) {
                continue;
            }

            source = find_token(loop, events[i].data.u64);
            if (source == NULL) {
                /* A previous callback removed this registration. */
                continue;
            }

            const struct event_loop_source current = *source;

            if (current.fn(loop, current.fd, events[i].events,
                           current.opaque) < 0) {
                return -1;
            }
        }
    }

    return 0;
}

int event_loop_run(struct event_loop *loop)
{
    int ret;

    if (loop == NULL || loop->epoll_fd < 0 || loop->wake_fd < 0) {
        errno = EINVAL;
        return -1;
    }

    if (loop->running) {
        errno = EBUSY;
        return -1;
    }

    loop->running = true;
    ret = dispatch(loop);
    loop->running = false;

    return ret;
}

int event_loop_stop(struct event_loop *loop)
{
    const uint64_t one = 1;

    if (loop == NULL || loop->wake_fd < 0) {
        errno = EINVAL;
        return -1;
    }

    atomic_store_explicit(&loop->stop_requested, true,
                          memory_order_release);

    for (;;) {
        const ssize_t n = write(loop->wake_fd, &one, sizeof(one));

        if (n == (ssize_t)sizeof(one)) {
            return 0;
        }

        if (n < 0 && errno == EINTR) {
            continue;
        }

        if (n < 0 && errno == EAGAIN) {
            /* A saturated eventfd is already readable. */
            return 0;
        }

        if (n >= 0) {
            errno = EIO;
        }

        return -1;
    }
}

void event_loop_destroy(struct event_loop *loop)
{
    if (loop == NULL) {
        return;
    }

    if (loop->epoll_fd >= 0) {
        close(loop->epoll_fd);
        loop->epoll_fd = -1;
    }

    if (loop->wake_fd >= 0) {
        close(loop->wake_fd);
        loop->wake_fd = -1;
    }
}

