/*
 * worker_r2.c - Cloudflare R2 Object Storage Bindings
 *
 * Provides a pure-C abstraction over Cloudflare R2 object storage.
 * Mirrors the R2 Workers API: get, put, delete, list, head.
 * For native/test builds an in-memory store simulates the bucket.
 *
 * R2 API surface (mirrors Cloudflare R2 Workers binding):
 *   - get(key)     → R2 object with body
 *   - put(key,body)→ stores object
 *   - delete(key)  → removes object
 *   - head(key)    → metadata only (no body)
 *   - list(opts)   → paginated object listing
 *
 * Copyright (c) 2024 Modern C Web Library
 * Licensed under MIT License
 */

#include "kamran.k"
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ===== Internal R2 object entry ===== */

#define WORKER_R2_MAX_OBJECTS   1024

typedef struct {
    char    *key;
    uint8_t *body;
    size_t   size;
    char    *content_type;
    char    *etag;
    time_t   uploaded;
} r2_entry_t;

struct worker_r2_bucket {
    char        *bucket_name;
    r2_entry_t   objects[WORKER_R2_MAX_OBJECTS];
    int           count;
};

/* ===== Lifecycle ===== */

worker_r2_bucket_t *worker_r2_bucket_create(const char *bucket_name) {
    if (!bucket_name) return NULL;
    worker_r2_bucket_t *b = calloc(1, sizeof(worker_r2_bucket_t));
    if (!b) return NULL;
    b->bucket_name = strdup(bucket_name);
    if (!b->bucket_name) { free(b); return NULL; }
    return b;
}

void worker_r2_bucket_destroy(worker_r2_bucket_t *bucket) {
    if (!bucket) return;
    for (int i = 0; i < bucket->count; i++) {
        free(bucket->objects[i].key);
        free(bucket->objects[i].body);
        free(bucket->objects[i].content_type);
        free(bucket->objects[i].etag);
    }
    free(bucket->bucket_name);
    free(bucket);
}

const char *worker_r2_bucket_get_name(const worker_r2_bucket_t *bucket) {
    return bucket ? bucket->bucket_name : NULL;
}

/* ===== Internal helpers ===== */

static int _r2_find(const worker_r2_bucket_t *b, const char *key) {
    if (!b || !key) return -1;
    for (int i = 0; i < b->count; i++) {
        if (strcmp(b->objects[i].key, key) == 0) return i;
    }
    return -1;
}

/* Simple hash for generating ETags */
static void _r2_make_etag(char *buf, size_t bufsize,
                           const uint8_t *data, size_t len) {
    uint32_t hash = 5381;
    for (size_t i = 0; i < len; i++) {
        hash = ((hash << 5) + hash) + data[i];
    }
    snprintf(buf, bufsize, "\"%08x\"", hash);
}

/* ===== R2 Object helpers ===== */

static worker_r2_object_t *_r2_object_from_entry(const r2_entry_t *e,
                                                  bool include_body) {
    worker_r2_object_t *obj = calloc(1, sizeof(worker_r2_object_t));
    if (!obj) return NULL;

    obj->key = strdup(e->key);
    obj->size = e->size;
    obj->etag = e->etag ? strdup(e->etag) : NULL;
    obj->content_type = e->content_type ? strdup(e->content_type) : NULL;
    obj->uploaded = e->uploaded;

    if (include_body && e->body && e->size > 0) {
        obj->body = malloc(e->size);
        if (obj->body) {
            memcpy(obj->body, e->body, e->size);
            obj->body_len = e->size;
        }
    }
    return obj;
}

void worker_r2_object_destroy(worker_r2_object_t *obj) {
    if (!obj) return;
    free(obj->key);
    free(obj->body);
    free(obj->etag);
    free(obj->content_type);
    free(obj);
}

/* ===== R2 Get ===== */

worker_r2_object_t *worker_r2_get(worker_r2_bucket_t *bucket,
                                  const char *key) {
    int idx = _r2_find(bucket, key);
    if (idx < 0) return NULL;
    return _r2_object_from_entry(&bucket->objects[idx], true);
}

/* ===== R2 Head (metadata only) ===== */

worker_r2_object_t *worker_r2_head(worker_r2_bucket_t *bucket,
                                   const char *key) {
    int idx = _r2_find(bucket, key);
    if (idx < 0) return NULL;
    return _r2_object_from_entry(&bucket->objects[idx], false);
}

/* ===== R2 Put ===== */

int worker_r2_put(worker_r2_bucket_t *bucket, const char *key,
                  const uint8_t *body, size_t body_len,
                  const worker_r2_put_options_t *opts) {
    char *new_key = NULL;
    uint8_t *new_body = NULL;
    size_t new_size = 0;
    char *new_content_type = NULL;
    char *new_etag = NULL;
    time_t new_uploaded;
    int idx;
    int is_new;

    if (!bucket || !key) return -1;

    /* Find existing entry or reserve the next slot, but do not modify it
     * until all allocations for the replacement object succeed. */
    idx = _r2_find(bucket, key);
    is_new = (idx < 0);
    if (is_new) {
        if (bucket->count >= WORKER_R2_MAX_OBJECTS) return -1;
        idx = bucket->count;
        new_key = strdup(key);
        if (!new_key) return -1;
    }

    if (body && body_len > 0) {
        new_body = malloc(body_len);
        if (!new_body) {
            free(new_key);
            return -1;
        }
        memcpy(new_body, body, body_len);
        new_size = body_len;

        /* Generate ETag */
        char etag_buf[16];
        _r2_make_etag(etag_buf, sizeof(etag_buf), body, body_len);
        new_etag = strdup(etag_buf);
        if (!new_etag) {
            free(new_body);
            free(new_key);
            return -1;
        }
    }

    if (opts && opts->content_type) {
        new_content_type = strdup(opts->content_type);
        if (!new_content_type) {
            free(new_etag);
            free(new_body);
            free(new_key);
            return -1;
        }
    }

    new_uploaded = time(NULL);

    /* All allocations succeeded — commit changes */
    {
        r2_entry_t *e = &bucket->objects[idx];

        if (is_new) {
            e->key = new_key;
            bucket->count++;
        } else {
            free(e->body);
            free(e->content_type);
            free(e->etag);
        }

        e->body = new_body;
        e->size = new_size;
        e->content_type = new_content_type;
        e->etag = new_etag;
        e->uploaded = new_uploaded;
    }

    return 0;
}

/* ===== R2 Delete ===== */

int worker_r2_delete(worker_r2_bucket_t *bucket, const char *key) {
    if (!bucket || !key) return -1;
    int idx = _r2_find(bucket, key);
    if (idx < 0) return -1;

    free(bucket->objects[idx].key);
    free(bucket->objects[idx].body);
    free(bucket->objects[idx].content_type);
    free(bucket->objects[idx].etag);

    /* Shift remaining */
    for (int i = idx; i < bucket->count - 1; i++) {
        bucket->objects[i] = bucket->objects[i + 1];
    }
    bucket->count--;
    memset(&bucket->objects[bucket->count], 0, sizeof(r2_entry_t));
    return 0;
}

/* ===== R2 List ===== */

worker_r2_list_result_t *worker_r2_list(worker_r2_bucket_t *bucket,
                                        const worker_r2_list_options_t *opts) {
    if (!bucket) return NULL;

    worker_r2_list_result_t *result = calloc(1, sizeof(worker_r2_list_result_t));
    if (!result) return NULL;

    const char *prefix = opts ? opts->prefix : NULL;
    int limit = (opts && opts->limit > 0) ? opts->limit : 1000;
    int cursor_start = (opts && opts->cursor > 0) ? opts->cursor : 0;
    if (limit > 1000) limit = 1000;

    result->objects = calloc((size_t)limit, sizeof(worker_r2_object_t *));
    if (!result->objects) { free(result); return NULL; }

    int found = 0;
    int skipped = 0;
    bool has_more = false;

    for (int i = 0; i < bucket->count; i++) {
        if (prefix && strncmp(bucket->objects[i].key, prefix,
                              strlen(prefix)) != 0) continue;
        if (skipped < cursor_start) { skipped++; continue; }

        if (found < limit) {
            result->objects[found] = _r2_object_from_entry(&bucket->objects[i],
                                                           false);
            found++;
        } else {
            has_more = true;
            break;
        }
    }

    result->count = found;
    result->truncated = has_more;
    result->cursor = has_more ? (cursor_start + found) : 0;
    return result;
}

void worker_r2_list_result_destroy(worker_r2_list_result_t *result) {
    if (!result) return;
    for (int i = 0; i < result->count; i++) {
        worker_r2_object_destroy(result->objects[i]);
    }
    free(result->objects);
    free(result);
}
