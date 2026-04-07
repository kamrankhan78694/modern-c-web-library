/*
 * worker_queues.c - Cloudflare Workers Queues Bindings
 *
 * Provides a pure-C abstraction over Cloudflare Queues.
 * Mirrors the Queues Workers API: send, sendBatch, consume.
 * For native/test builds an in-memory FIFO queue is used.
 *
 * Queues API surface (mirrors Cloudflare Queues Workers binding):
 *   - send(body)         → enqueue single message
 *   - send_batch(msgs[]) → enqueue multiple messages
 *   - consume(handler)   → process messages in batches
 *
 * Copyright (c) 2024 Modern C Web Library
 * Licensed under MIT License
 */

#include "kamran.k"
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ===== Internal types ===== */

#define WORKER_QUEUE_MAX_MESSAGES   4096
#define WORKER_QUEUE_MAX_MSG_SIZE   (128 * 1024)  /* 128 KiB per CF limit */
#define WORKER_QUEUE_MAX_BATCH_SIZE 100

typedef struct {
    char    *id;
    char    *body;
    size_t   body_len;
    time_t   timestamp;
    int      attempts;
    bool     acked;
} queue_entry_t;

struct worker_queue {
    char          *queue_name;
    queue_entry_t  messages[WORKER_QUEUE_MAX_MESSAGES];
    int            head;   /* next read position */
    int            tail;   /* next write position */
    int            count;  /* current message count */
    int            total_sent;
    int            total_consumed;
};

/* ===== Lifecycle ===== */

worker_queue_t *worker_queue_create(const char *queue_name) {
    if (!queue_name) return NULL;
    worker_queue_t *q = calloc(1, sizeof(worker_queue_t));
    if (!q) return NULL;
    q->queue_name = strdup(queue_name);
    if (!q->queue_name) { free(q); return NULL; }
    return q;
}

void worker_queue_destroy(worker_queue_t *q) {
    if (!q) return;
    /* Free all remaining messages */
    for (int i = 0; i < WORKER_QUEUE_MAX_MESSAGES; i++) {
        free(q->messages[i].id);
        free(q->messages[i].body);
    }
    free(q->queue_name);
    free(q);
}

const char *worker_queue_get_name(const worker_queue_t *q) {
    return q ? q->queue_name : NULL;
}

int worker_queue_get_depth(const worker_queue_t *q) {
    return q ? q->count : 0;
}

/* ===== Internal helpers ===== */

/* Generate a simple message ID */
static char *_queue_gen_id(int seq) {
    char buf[32];
    snprintf(buf, sizeof(buf), "msg_%d_%ld", seq, (long)time(NULL));
    return strdup(buf);
}

/* ===== Queue Send ===== */

int worker_queue_send(worker_queue_t *q, const char *body, size_t body_len) {
    if (!q || !body) return -1;
    if (body_len > WORKER_QUEUE_MAX_MSG_SIZE) return -1;
    if (q->count >= WORKER_QUEUE_MAX_MESSAGES) return -1;

    queue_entry_t *e = &q->messages[q->tail];
    memset(e, 0, sizeof(queue_entry_t));
    e->body = malloc(body_len + 1);
    if (!e->body) return -1;
    memcpy(e->body, body, body_len);
    e->body[body_len] = '\0';
    e->body_len = body_len;
    e->timestamp = time(NULL);
    e->id = _queue_gen_id(q->total_sent);
    if (!e->id) {
        free(e->body);
        memset(e, 0, sizeof(queue_entry_t));
        return -1;
    }
    e->attempts = 0;
    e->acked = false;

    q->tail = (q->tail + 1) % WORKER_QUEUE_MAX_MESSAGES;
    q->count++;
    q->total_sent++;
    return 0;
}

int worker_queue_send_text(worker_queue_t *q, const char *text) {
    if (!text) return -1;
    return worker_queue_send(q, text, strlen(text));
}

int worker_queue_send_json(worker_queue_t *q, json_value_t *json) {
    if (!q || !json) return -1;
    char *str = json_stringify(json);
    if (!str) return -1;
    int rc = worker_queue_send(q, str, strlen(str));
    free(str);
    return rc;
}

int worker_queue_send_batch(worker_queue_t *q, const char **bodies,
                            const size_t *lengths, int count) {
    if (!q || !bodies || !lengths || count <= 0) return -1;
    if (count > WORKER_QUEUE_MAX_BATCH_SIZE) return -1;

    /* Validate all messages fit before sending any */
    for (int i = 0; i < count; i++) {
        if (!bodies[i] || lengths[i] > WORKER_QUEUE_MAX_MSG_SIZE) return -1;
    }
    if (q->count + count > WORKER_QUEUE_MAX_MESSAGES) return -1;

    for (int i = 0; i < count; i++) {
        int rc = worker_queue_send(q, bodies[i], lengths[i]);
        if (rc != 0) return -1;  /* shouldn't happen after validation */
    }
    return 0;
}

/* ===== Queue Message ===== */

void worker_queue_message_destroy(worker_queue_message_t *msg) {
    if (!msg) return;
    free(msg->id);
    free(msg->body);
    free(msg);
}

void worker_queue_message_ack(worker_queue_message_t *msg) {
    if (msg) msg->acked = true;
}

/* ===== Queue Consume (Batch) ===== */

worker_queue_batch_t *worker_queue_consume(worker_queue_t *q, int max_batch,
                                           int max_wait_seconds) {
    if (!q) return NULL;
    if (max_batch <= 0) max_batch = 10;
    if (max_batch > WORKER_QUEUE_MAX_BATCH_SIZE) max_batch = WORKER_QUEUE_MAX_BATCH_SIZE;
    (void)max_wait_seconds;  /* In-memory impl doesn't need to wait */

    worker_queue_batch_t *batch = calloc(1, sizeof(worker_queue_batch_t));
    if (!batch) return NULL;

    batch->messages = calloc((size_t)max_batch, sizeof(worker_queue_message_t *));
    if (!batch->messages) { free(batch); return NULL; }

    int fetched = 0;
    while (fetched < max_batch && q->count > 0) {
        queue_entry_t *e = &q->messages[q->head];

        worker_queue_message_t *msg = calloc(1, sizeof(worker_queue_message_t));
        if (!msg) break;

        msg->id = e->id ? strdup(e->id) : NULL;
        msg->body = e->body ? strdup(e->body) : NULL;
        msg->body_len = e->body_len;
        msg->timestamp = e->timestamp;
        msg->attempts = e->attempts + 1;
        msg->acked = false;

        /* Remove from queue */
        free(e->id);
        free(e->body);
        memset(e, 0, sizeof(queue_entry_t));
        q->head = (q->head + 1) % WORKER_QUEUE_MAX_MESSAGES;
        q->count--;
        q->total_consumed++;

        batch->messages[fetched] = msg;
        fetched++;
    }

    batch->count = fetched;
    if (q->queue_name) {
        batch->queue_name = strdup(q->queue_name);
        if (!batch->queue_name) {
            worker_queue_batch_destroy(batch);
            return NULL;
        }
    } else {
        batch->queue_name = NULL;
    }
    return batch;
}

void worker_queue_batch_destroy(worker_queue_batch_t *batch) {
    if (!batch) return;
    for (int i = 0; i < batch->count; i++) {
        worker_queue_message_destroy(batch->messages[i]);
    }
    free(batch->messages);
    free((char *)batch->queue_name);
    free(batch);
}
