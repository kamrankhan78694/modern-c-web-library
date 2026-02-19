#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include <stdbool.h>
#include <stddef.h>

/**
 * thread_pool.h - Thread Pool for Modern C Web Library
 *
 * Bounded thread pool with work queue for handling concurrent connections.
 * Replaces thread-per-connection model with a fixed pool of worker threads.
 *
 * Copyright (c) 2024 Modern C Web Library
 * Licensed under MIT License
 */

#ifdef __cplusplus
extern "C" {
#endif

/* Limits */
#define THREAD_POOL_DEFAULT_SIZE 16
#define THREAD_POOL_MIN_SIZE 1
#define THREAD_POOL_MAX_SIZE 256
#define THREAD_POOL_DEFAULT_QUEUE_SIZE 256

/* Opaque thread pool type */
typedef struct thread_pool thread_pool_t;

/* Work function signature */
typedef void (*thread_pool_work_fn_t)(void *arg);

/**
 * Create a new thread pool
 * @param thread_count Number of worker threads (clamped to [1, 256])
 * @param queue_size Maximum pending work items (0 = default 256)
 * @return Thread pool instance or NULL on failure
 */
thread_pool_t *thread_pool_create(int thread_count, int queue_size);

/**
 * Submit work to the thread pool
 * @param pool Thread pool instance
 * @param work_fn Function to execute
 * @param arg Argument passed to work_fn
 * @return 0 on success, -1 if pool is shutting down or queue is full
 */
int thread_pool_submit(thread_pool_t *pool, thread_pool_work_fn_t work_fn, void *arg);

/**
 * Destroy the thread pool, waiting for all queued work to complete
 * @param pool Thread pool instance
 */
void thread_pool_destroy(thread_pool_t *pool);

/**
 * Get number of pending work items in queue
 * @param pool Thread pool instance
 * @return Number of pending items, or 0 if pool is NULL
 */
size_t thread_pool_pending(thread_pool_t *pool);

#ifdef __cplusplus
}
#endif

#endif /* THREAD_POOL_H */
