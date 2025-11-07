#include "weblib.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/time.h>

#ifdef __linux__
#include <sys/epoll.h>
#define USE_EPOLL 1
#elif defined(__APPLE__) || defined(__FreeBSD__)
#include <sys/event.h>
#define USE_KQUEUE 1
#else
#include <poll.h>
#define USE_POLL 1
#endif

#define MAX_EVENTS 1024
#define MAX_TIMERS 64

/* Event handler structure */
struct event_handler {
    int fd;
    int events;
    event_callback_t callback;
    void *user_data;
    bool active;
};

/* Timer structure */
typedef struct {
    int id;
    struct timeval expiry;
    event_callback_t callback;
    void *user_data;
    bool active;
} event_timer_t;

/* Event loop structure */
struct event_loop {
    bool running;
    int next_timer_id;
    
#ifdef USE_EPOLL
    int epoll_fd;
#elif defined(USE_KQUEUE)
    int kqueue_fd;
#elif defined(USE_POLL)
    struct pollfd *poll_fds;
    int poll_count;
    int poll_capacity;
#endif
    
    event_handler_t *handlers;
    int handler_count;
    int handler_capacity;
    
    event_timer_t timers[MAX_TIMERS];
    int timer_count;
};

/* Forward declarations */
static int find_handler_index(event_loop_t *loop, int fd);
static void process_timers(event_loop_t *loop);
static int get_next_timeout(event_loop_t *loop);

/* Create event loop */
event_loop_t *event_loop_create(void) {
    event_loop_t *loop = (event_loop_t *)calloc(1, sizeof(event_loop_t));
    if (!loop) {
        return NULL;
    }
    
    loop->running = false;
    loop->next_timer_id = 1;
    loop->handler_count = 0;
    loop->handler_capacity = 16;
    loop->timer_count = 0;
    
    /* Allocate handlers array */
    loop->handlers = (event_handler_t *)calloc(loop->handler_capacity, sizeof(event_handler_t));
    if (!loop->handlers) {
        free(loop);
        return NULL;
    }
    
#ifdef USE_EPOLL
    /* Create epoll instance */
    loop->epoll_fd = epoll_create1(0);
    if (loop->epoll_fd < 0) {
        perror("epoll_create1 failed");
        free(loop->handlers);
        free(loop);
        return NULL;
    }
#elif defined(USE_KQUEUE)
    /* Create kqueue instance */
    loop->kqueue_fd = kqueue();
    if (loop->kqueue_fd < 0) {
        perror("kqueue failed");
        free(loop->handlers);
        free(loop);
        return NULL;
    }
#elif defined(USE_POLL)
    /* Initialize poll structures */
    loop->poll_capacity = 16;
    loop->poll_count = 0;
    loop->poll_fds = (struct pollfd *)calloc(loop->poll_capacity, sizeof(struct pollfd));
    if (!loop->poll_fds) {
        free(loop->handlers);
        free(loop);
        return NULL;
    }
#endif
    
    return loop;
}

/* Add file descriptor to event loop */
int event_loop_add_fd(event_loop_t *loop, int fd, int events, event_callback_t callback, void *user_data) {
    if (!loop || fd < 0 || !callback) {
        return -1;
    }
    
    /* Check if handler already exists */
    if (find_handler_index(loop, fd) >= 0) {
        fprintf(stderr, "File descriptor %d already registered\n", fd);
        return -1;
    }
    
    /* Expand handlers array if needed */
    if (loop->handler_count >= loop->handler_capacity) {
        int new_capacity = loop->handler_capacity * 2;
        event_handler_t *new_handlers = (event_handler_t *)realloc(loop->handlers, 
                                                                    new_capacity * sizeof(event_handler_t));
        if (!new_handlers) {
            fprintf(stderr, "Failed to expand handlers array\n");
            return -1;
        }
        loop->handlers = new_handlers;
        loop->handler_capacity = new_capacity;
    }
    
    /* Add handler to array */
    event_handler_t *handler = &loop->handlers[loop->handler_count];
    handler->fd = fd;
    handler->events = events;
    handler->callback = callback;
    handler->user_data = user_data;
    handler->active = true;
    loop->handler_count++;
    
#ifdef USE_EPOLL
    /* Add to epoll */
    struct epoll_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.events = 0;
    if (events & EVENT_READ) ev.events |= EPOLLIN;
    if (events & EVENT_WRITE) ev.events |= EPOLLOUT;
    ev.data.fd = fd;
    
    if (epoll_ctl(loop->epoll_fd, EPOLL_CTL_ADD, fd, &ev) < 0) {
        perror("epoll_ctl EPOLL_CTL_ADD failed");
        loop->handler_count--;
        return -1;
    }
#elif defined(USE_KQUEUE)
    /* Add to kqueue */
    struct kevent kev[2];
    int nchanges = 0;
    
    if (events & EVENT_READ) {
        EV_SET(&kev[nchanges++], fd, EVFILT_READ, EV_ADD | EV_ENABLE, 0, 0, user_data);
    }
    if (events & EVENT_WRITE) {
        EV_SET(&kev[nchanges++], fd, EVFILT_WRITE, EV_ADD | EV_ENABLE, 0, 0, user_data);
    }
    
    if (kevent(loop->kqueue_fd, kev, nchanges, NULL, 0, NULL) < 0) {
        perror("kevent failed");
        loop->handler_count--;
        return -1;
    }
#elif defined(USE_POLL)
    /* Expand poll_fds if needed */
    if (loop->poll_count >= loop->poll_capacity) {
        int new_capacity = loop->poll_capacity * 2;
        struct pollfd *new_fds = (struct pollfd *)realloc(loop->poll_fds,
                                                          new_capacity * sizeof(struct pollfd));
        if (!new_fds) {
            loop->handler_count--;
            return -1;
        }
        loop->poll_fds = new_fds;
        loop->poll_capacity = new_capacity;
    }
    
    /* Add to poll_fds */
    struct pollfd *pfd = &loop->poll_fds[loop->poll_count];
    pfd->fd = fd;
    pfd->events = 0;
    if (events & EVENT_READ) pfd->events |= POLLIN;
    if (events & EVENT_WRITE) pfd->events |= POLLOUT;
    pfd->revents = 0;
    loop->poll_count++;
#endif
    
    return 0;
}

/* Modify events for a file descriptor */
int event_loop_modify_fd(event_loop_t *loop, int fd, int events) {
    if (!loop || fd < 0) {
        return -1;
    }
    
    int idx = find_handler_index(loop, fd);
    if (idx < 0) {
        fprintf(stderr, "File descriptor %d not found\n", fd);
        return -1;
    }
    
    loop->handlers[idx].events = events;
    
#ifdef USE_EPOLL
    struct epoll_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.events = 0;
    if (events & EVENT_READ) ev.events |= EPOLLIN;
    if (events & EVENT_WRITE) ev.events |= EPOLLOUT;
    ev.data.fd = fd;
    
    if (epoll_ctl(loop->epoll_fd, EPOLL_CTL_MOD, fd, &ev) < 0) {
        perror("epoll_ctl EPOLL_CTL_MOD failed");
        return -1;
    }
#elif defined(USE_KQUEUE)
    /* Modify kqueue events */
    struct kevent kev[2];
    int nchanges = 0;
    
    /* Remove old events and add new ones */
    EV_SET(&kev[nchanges++], fd, EVFILT_READ, EV_DELETE, 0, 0, NULL);
    EV_SET(&kev[nchanges++], fd, EVFILT_WRITE, EV_DELETE, 0, 0, NULL);
    kevent(loop->kqueue_fd, kev, nchanges, NULL, 0, NULL);
    
    nchanges = 0;
    if (events & EVENT_READ) {
        EV_SET(&kev[nchanges++], fd, EVFILT_READ, EV_ADD | EV_ENABLE, 0, 0, loop->handlers[idx].user_data);
    }
    if (events & EVENT_WRITE) {
        EV_SET(&kev[nchanges++], fd, EVFILT_WRITE, EV_ADD | EV_ENABLE, 0, 0, loop->handlers[idx].user_data);
    }
    
    if (kevent(loop->kqueue_fd, kev, nchanges, NULL, 0, NULL) < 0) {
        perror("kevent failed");
        return -1;
    }
#elif defined(USE_POLL)
    /* Find and modify poll_fds entry */
    for (int i = 0; i < loop->poll_count; i++) {
        if (loop->poll_fds[i].fd == fd) {
            loop->poll_fds[i].events = 0;
            if (events & EVENT_READ) loop->poll_fds[i].events |= POLLIN;
            if (events & EVENT_WRITE) loop->poll_fds[i].events |= POLLOUT;
            break;
        }
    }
#endif
    
    return 0;
}

/* Remove file descriptor from event loop */
int event_loop_remove_fd(event_loop_t *loop, int fd) {
    if (!loop || fd < 0) {
        return -1;
    }
    
    int idx = find_handler_index(loop, fd);
    if (idx < 0) {
        return -1;
    }
    
#ifdef USE_EPOLL
    epoll_ctl(loop->epoll_fd, EPOLL_CTL_DEL, fd, NULL);
#elif defined(USE_KQUEUE)
    struct kevent kev[2];
    EV_SET(&kev[0], fd, EVFILT_READ, EV_DELETE, 0, 0, NULL);
    EV_SET(&kev[1], fd, EVFILT_WRITE, EV_DELETE, 0, 0, NULL);
    kevent(loop->kqueue_fd, kev, 2, NULL, 0, NULL);
#elif defined(USE_POLL)
    /* Remove from poll_fds */
    for (int i = 0; i < loop->poll_count; i++) {
        if (loop->poll_fds[i].fd == fd) {
            /* Move last element to this position */
            if (i < loop->poll_count - 1) {
                loop->poll_fds[i] = loop->poll_fds[loop->poll_count - 1];
            }
            loop->poll_count--;
            break;
        }
    }
#endif
    
    /* Mark handler as inactive and compact array */
    loop->handlers[idx].active = false;
    if (idx < loop->handler_count - 1) {
        loop->handlers[idx] = loop->handlers[loop->handler_count - 1];
    }
    loop->handler_count--;
    
    return 0;
}

/* Run the event loop */
int event_loop_run(event_loop_t *loop) {
    if (!loop) {
        return -1;
    }
    
    loop->running = true;
    
    while (loop->running) {
        int timeout_ms = get_next_timeout(loop);
        
#ifdef USE_EPOLL
        struct epoll_event events[MAX_EVENTS];
        int nfds = epoll_wait(loop->epoll_fd, events, MAX_EVENTS, timeout_ms);
        
        if (nfds < 0) {
            if (errno == EINTR) continue;
            perror("epoll_wait failed");
            return -1;
        }
        
        /* Process events */
        for (int i = 0; i < nfds; i++) {
            int fd = events[i].data.fd;
            int idx = find_handler_index(loop, fd);
            if (idx >= 0 && loop->handlers[idx].active) {
                int event_flags = 0;
                if (events[i].events & EPOLLIN) event_flags |= EVENT_READ;
                if (events[i].events & EPOLLOUT) event_flags |= EVENT_WRITE;
                if (events[i].events & (EPOLLERR | EPOLLHUP)) event_flags |= EVENT_ERROR;
                
                loop->handlers[idx].callback(fd, event_flags, loop->handlers[idx].user_data);
            }
        }
        
#elif defined(USE_KQUEUE)
        struct kevent events[MAX_EVENTS];
        struct timespec ts;
        struct timespec *ts_ptr = NULL;
        
        if (timeout_ms >= 0) {
            ts.tv_sec = timeout_ms / 1000;
            ts.tv_nsec = (timeout_ms % 1000) * 1000000;
            ts_ptr = &ts;
        }
        
        int nfds = kevent(loop->kqueue_fd, NULL, 0, events, MAX_EVENTS, ts_ptr);
        
        if (nfds < 0) {
            if (errno == EINTR) continue;
            perror("kevent failed");
            return -1;
        }
        
        /* Process events */
        for (int i = 0; i < nfds; i++) {
            int fd = (int)events[i].ident;
            int idx = find_handler_index(loop, fd);
            if (idx >= 0 && loop->handlers[idx].active) {
                int event_flags = 0;
                if (events[i].filter == EVFILT_READ) event_flags |= EVENT_READ;
                if (events[i].filter == EVFILT_WRITE) event_flags |= EVENT_WRITE;
                if (events[i].flags & EV_ERROR) event_flags |= EVENT_ERROR;
                
                loop->handlers[idx].callback(fd, event_flags, loop->handlers[idx].user_data);
            }
        }
        
#elif defined(USE_POLL)
        int nfds = poll(loop->poll_fds, loop->poll_count, timeout_ms);
        
        if (nfds < 0) {
            if (errno == EINTR) continue;
            perror("poll failed");
            return -1;
        }
        
        /* Process events */
        for (int i = 0; i < loop->poll_count && nfds > 0; i++) {
            if (loop->poll_fds[i].revents == 0) continue;
            
            nfds--;
            int fd = loop->poll_fds[i].fd;
            int idx = find_handler_index(loop, fd);
            if (idx >= 0 && loop->handlers[idx].active) {
                int event_flags = 0;
                if (loop->poll_fds[i].revents & POLLIN) event_flags |= EVENT_READ;
                if (loop->poll_fds[i].revents & POLLOUT) event_flags |= EVENT_WRITE;
                if (loop->poll_fds[i].revents & (POLLERR | POLLHUP | POLLNVAL)) event_flags |= EVENT_ERROR;
                
                loop->handlers[idx].callback(fd, event_flags, loop->handlers[idx].user_data);
            }
            
            loop->poll_fds[i].revents = 0;
        }
#endif
        
        /* Process timers */
        process_timers(loop);
    }
    
    return 0;
}

/* Stop the event loop */
void event_loop_stop(event_loop_t *loop) {
    if (loop) {
        loop->running = false;
    }
}

/* Destroy event loop */
void event_loop_destroy(event_loop_t *loop) {
    if (!loop) {
        return;
    }
    
#ifdef USE_EPOLL
    if (loop->epoll_fd >= 0) {
        close(loop->epoll_fd);
    }
#elif defined(USE_KQUEUE)
    if (loop->kqueue_fd >= 0) {
        close(loop->kqueue_fd);
    }
#elif defined(USE_POLL)
    free(loop->poll_fds);
#endif
    
    free(loop->handlers);
    free(loop);
}

/* Add timeout */
int event_loop_add_timeout(event_loop_t *loop, int timeout_ms, event_callback_t callback, void *user_data) {
    if (!loop || timeout_ms < 0 || !callback) {
        return -1;
    }
    
    if (loop->timer_count >= MAX_TIMERS) {
        fprintf(stderr, "Max timers exceeded\n");
        return -1;
    }
    
    event_timer_t *timer = &loop->timers[loop->timer_count];
    timer->id = loop->next_timer_id++;
    timer->callback = callback;
    timer->user_data = user_data;
    timer->active = true;
    
    /* Calculate expiry time */
    gettimeofday(&timer->expiry, NULL);
    timer->expiry.tv_sec += timeout_ms / 1000;
    timer->expiry.tv_usec += (timeout_ms % 1000) * 1000;
    if (timer->expiry.tv_usec >= 1000000) {
        timer->expiry.tv_sec++;
        timer->expiry.tv_usec -= 1000000;
    }
    
    loop->timer_count++;
    
    return timer->id;
}

/* Cancel timeout */
int event_loop_cancel_timeout(event_loop_t *loop, int timer_id) {
    if (!loop) {
        return -1;
    }
    
    for (int i = 0; i < loop->timer_count; i++) {
        if (loop->timers[i].id == timer_id && loop->timers[i].active) {
            loop->timers[i].active = false;
            /* Compact timers array */
            if (i < loop->timer_count - 1) {
                loop->timers[i] = loop->timers[loop->timer_count - 1];
            }
            loop->timer_count--;
            return 0;
        }
    }
    
    return -1;
}

/* Find handler index by fd */
static int find_handler_index(event_loop_t *loop, int fd) {
    for (int i = 0; i < loop->handler_count; i++) {
        if (loop->handlers[i].fd == fd && loop->handlers[i].active) {
            return i;
        }
    }
    return -1;
}

/* Process expired timers */
static void process_timers(event_loop_t *loop) {
    struct timeval now;
    gettimeofday(&now, NULL);
    
    for (int i = 0; i < loop->timer_count; i++) {
        if (!loop->timers[i].active) continue;
        
        if (now.tv_sec > loop->timers[i].expiry.tv_sec ||
            (now.tv_sec == loop->timers[i].expiry.tv_sec && 
             now.tv_usec >= loop->timers[i].expiry.tv_usec)) {
            /* Timer expired */
            loop->timers[i].callback(-1, EVENT_TIMEOUT, loop->timers[i].user_data);
            loop->timers[i].active = false;
            /* Compact timers array */
            if (i < loop->timer_count - 1) {
                loop->timers[i] = loop->timers[loop->timer_count - 1];
                i--; /* Re-check this position */
            }
            loop->timer_count--;
        }
    }
}

/* Get next timeout in milliseconds */
static int get_next_timeout(event_loop_t *loop) {
    if (loop->timer_count == 0) {
        return -1; /* No timeout, block indefinitely */
    }
    
    struct timeval now;
    gettimeofday(&now, NULL);
    
    int min_timeout = -1;
    
    for (int i = 0; i < loop->timer_count; i++) {
        if (!loop->timers[i].active) continue;
        
        int timeout_ms = (loop->timers[i].expiry.tv_sec - now.tv_sec) * 1000 +
                        (loop->timers[i].expiry.tv_usec - now.tv_usec) / 1000;
        
        if (timeout_ms < 0) timeout_ms = 0;
        
        if (min_timeout < 0 || timeout_ms < min_timeout) {
            min_timeout = timeout_ms;
        }
    }
    
    return min_timeout;
}
