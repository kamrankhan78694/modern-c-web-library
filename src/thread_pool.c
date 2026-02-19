/**
 * thread_pool.c - Thread Pool for Modern C Web Library
 *
 * Bounded thread pool with FIFO work queue using POSIX threads.
 * Worker threads block on a condition variable until work is available.
 * Shutdown drains the queue before joining threads.
 *
 * Copyright (c) 2024 Modern C Web Library
 * Licensed under MIT License
 */

#include "thread_pool.h"
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

/* Work item in the queue */
typedef struct {
    thread_pool_work_fn_t fn;
    void *arg;
} work_item_t;

/* Thread pool internal structure */
struct thread_pool {
    pthread_t *threads;
    int thread_count;

    /* Circular work queue */
    work_item_t *queue;
    int queue_size;       /* capacity */
    int queue_head;       /* next item to dequeue */
    int queue_tail;       /* next slot to enqueue */
    int queue_count;      /* current items in queue */

    /* Synchronization */
    pthread_mutex_t mutex;
    pthread_cond_t not_empty;   /* signaled when work is added */
    pthread_cond_t not_full;    /* signaled when work is removed */

    bool shutdown;
};

/* Worker thread function */
static void *worker_thread(void *arg) {
    thread_pool_t *pool = (thread_pool_t *)arg;

    for (;;) {
        pthread_mutex_lock(&pool->mutex);

        /* Wait for work or shutdown */
        while (pool->queue_count == 0 && !pool->shutdown) {
            pthread_cond_wait(&pool->not_empty, &pool->mutex);
        }

        /* If shutting down and queue is empty, exit */
        if (pool->shutdown && pool->queue_count == 0) {
            pthread_mutex_unlock(&pool->mutex);
            break;
        }

        /* Dequeue work item */
        work_item_t item = pool->queue[pool->queue_head];
        pool->queue_head = (pool->queue_head + 1) % pool->queue_size;
        pool->queue_count--;

        /* Signal that there's space in the queue */
        pthread_cond_signal(&pool->not_full);

        pthread_mutex_unlock(&pool->mutex);

        /* Execute work item outside the lock */
        if (item.fn) {
            item.fn(item.arg);
        }
    }

    return NULL;
}

/* Create thread pool */
thread_pool_t *thread_pool_create(int thread_count, int queue_size) {
    /* Clamp thread count */
    if (thread_count < THREAD_POOL_MIN_SIZE) {
        thread_count = THREAD_POOL_MIN_SIZE;
    }
    if (thread_count > THREAD_POOL_MAX_SIZE) {
        thread_count = THREAD_POOL_MAX_SIZE;
    }

    /* Default queue size */
    if (queue_size <= 0) {
        queue_size = THREAD_POOL_DEFAULT_QUEUE_SIZE;
    }

    thread_pool_t *pool = (thread_pool_t *)calloc(1, sizeof(thread_pool_t));
    if (!pool) {
        return NULL;
    }

    pool->thread_count = thread_count;
    pool->queue_size = queue_size;
    pool->queue_head = 0;
    pool->queue_tail = 0;
    pool->queue_count = 0;
    pool->shutdown = false;

    /* Allocate queue */
    pool->queue = (work_item_t *)calloc((size_t)queue_size, sizeof(work_item_t));
    if (!pool->queue) {
        free(pool);
        return NULL;
    }

    /* Initialize synchronization primitives */
    if (pthread_mutex_init(&pool->mutex, NULL) != 0) {
        free(pool->queue);
        free(pool);
        return NULL;
    }

    if (pthread_cond_init(&pool->not_empty, NULL) != 0) {
        pthread_mutex_destroy(&pool->mutex);
        free(pool->queue);
        free(pool);
        return NULL;
    }

    if (pthread_cond_init(&pool->not_full, NULL) != 0) {
        pthread_cond_destroy(&pool->not_empty);
        pthread_mutex_destroy(&pool->mutex);
        free(pool->queue);
        free(pool);
        return NULL;
    }

    /* Allocate thread array */
    pool->threads = (pthread_t *)calloc((size_t)thread_count, sizeof(pthread_t));
    if (!pool->threads) {
        pthread_cond_destroy(&pool->not_full);
        pthread_cond_destroy(&pool->not_empty);
        pthread_mutex_destroy(&pool->mutex);
        free(pool->queue);
        free(pool);
        return NULL;
    }

    /* Start worker threads */
    for (int i = 0; i < thread_count; i++) {
        if (pthread_create(&pool->threads[i], NULL, worker_thread, pool) != 0) {
            /* Shutdown threads already created */
            pthread_mutex_lock(&pool->mutex);
            pool->shutdown = true;
            pthread_cond_broadcast(&pool->not_empty);
            pthread_mutex_unlock(&pool->mutex);

            for (int j = 0; j < i; j++) {
                pthread_join(pool->threads[j], NULL);
            }

            pthread_cond_destroy(&pool->not_full);
            pthread_cond_destroy(&pool->not_empty);
            pthread_mutex_destroy(&pool->mutex);
            free(pool->threads);
            free(pool->queue);
            free(pool);
            return NULL;
        }
    }

    return pool;
}

/* Submit work to the pool */
int thread_pool_submit(thread_pool_t *pool, thread_pool_work_fn_t work_fn, void *arg) {
    if (!pool || !work_fn) {
        return -1;
    }

    pthread_mutex_lock(&pool->mutex);

    /* Reject if shutting down */
    if (pool->shutdown) {
        pthread_mutex_unlock(&pool->mutex);
        return -1;
    }

    /* If queue is full, reject (non-blocking) */
    if (pool->queue_count >= pool->queue_size) {
        pthread_mutex_unlock(&pool->mutex);
        return -1;
    }

    /* Enqueue work item */
    pool->queue[pool->queue_tail].fn = work_fn;
    pool->queue[pool->queue_tail].arg = arg;
    pool->queue_tail = (pool->queue_tail + 1) % pool->queue_size;
    pool->queue_count++;

    /* Signal a waiting worker */
    pthread_cond_signal(&pool->not_empty);

    pthread_mutex_unlock(&pool->mutex);

    return 0;
}

/* Destroy thread pool (waits for queued work to complete) */
void thread_pool_destroy(thread_pool_t *pool) {
    if (!pool) {
        return;
    }

    pthread_mutex_lock(&pool->mutex);
    pool->shutdown = true;
    pthread_cond_broadcast(&pool->not_empty);
    pthread_mutex_unlock(&pool->mutex);

    /* Join all worker threads */
    for (int i = 0; i < pool->thread_count; i++) {
        pthread_join(pool->threads[i], NULL);
    }

    pthread_cond_destroy(&pool->not_full);
    pthread_cond_destroy(&pool->not_empty);
    pthread_mutex_destroy(&pool->mutex);
    free(pool->threads);
    free(pool->queue);
    free(pool);
}

/* Get pending work count */
size_t thread_pool_pending(thread_pool_t *pool) {
    if (!pool) {
        return 0;
    }

    pthread_mutex_lock(&pool->mutex);
    size_t count = (size_t)pool->queue_count;
    pthread_mutex_unlock(&pool->mutex);

    return count;
}
