/*
 * worker_kv.c - Cloudflare Workers KV Namespace Bindings
 *
 * Provides a pure-C abstraction over Cloudflare Workers KV storage.
 * NOTE: an in-memory hash map IS the implementation, in every build --
 * native, test and WASM/Workers alike.  Reaching a real env.MY_KV binding
 * would require a JS glue layer, and none ships in this repo, so the fixed
 * capacity below is a real limit everywhere, not a local-testing artefact.
 *
 * KV API surface (mirrors Cloudflare KV Workers binding):
 *   - get(key)              → value (string)
 *   - get_with_metadata(key)→ value + metadata JSON
 *   - put(key, value, opts) → stores key with optional TTL/metadata
 *   - delete(key)           → removes key
 *   - list(opts)            → paginated key listing
 *
 * Copyright (c) 2024 Modern C Web Library
 * Licensed under MIT License
 */

#include "kamran.k"
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ===== Internal KV entry ===== */

#define WORKER_KV_MAX_ENTRIES   1024
#define WORKER_KV_MAX_KEY_LEN   512
#define WORKER_KV_MAX_VALUE_LEN (25 * 1024 * 1024)  /* 25 MiB per CF limit */
#define WORKER_KV_MAX_META_LEN  1024

typedef struct {
    char   *key;
    char   *value;
    size_t  value_len;
    char   *metadata;      /* JSON string, may be NULL */
    time_t  expiration;    /* 0 = no expiration */
} kv_entry_t;

struct worker_kv {
    char        *namespace_name;
    kv_entry_t   entries[WORKER_KV_MAX_ENTRIES];
    int           count;
};

/* ===== Lifecycle ===== */

worker_kv_t *worker_kv_create(const char *namespace_name) {
    if (!namespace_name) return NULL;
    worker_kv_t *kv = calloc(1, sizeof(worker_kv_t));
    if (!kv) return NULL;
    kv->namespace_name = strdup(namespace_name);
    if (!kv->namespace_name) { free(kv); return NULL; }
    return kv;
}

void worker_kv_destroy(worker_kv_t *kv) {
    if (!kv) return;
    for (int i = 0; i < kv->count; i++) {
        free(kv->entries[i].key);
        free(kv->entries[i].value);
        free(kv->entries[i].metadata);
    }
    free(kv->namespace_name);
    free(kv);
}

const char *worker_kv_get_namespace(const worker_kv_t *kv) {
    return kv ? kv->namespace_name : NULL;
}

/* ===== Internal helpers ===== */

static void _kv_remove_at(worker_kv_t *kv, int idx) {
    free(kv->entries[idx].key);
    free(kv->entries[idx].value);
    free(kv->entries[idx].metadata);
    /* Shift remaining entries */
    for (int i = idx; i < kv->count - 1; i++) {
        kv->entries[i] = kv->entries[i + 1];
    }
    kv->count--;
    memset(&kv->entries[kv->count], 0, sizeof(kv_entry_t));
}

static int _kv_is_expired(const kv_entry_t *entry, time_t now) {
    return entry && entry->expiration > 0 && entry->expiration <= now;
}

static int _kv_find(worker_kv_t *kv, const char *key) {
    if (!kv || !key) return -1;
    time_t now = time(NULL);
    for (int i = 0; i < kv->count; ) {
        if (_kv_is_expired(&kv->entries[i], now)) {
            _kv_remove_at(kv, i);
            continue;
        }
        if (strcmp(kv->entries[i].key, key) == 0) {
            return i;
        }
        i++;
    }
    return -1;
}

/* ===== KV Get ===== */

char *worker_kv_get(worker_kv_t *kv, const char *key) {
    int idx = _kv_find(kv, key);
    if (idx < 0) return NULL;
    return strdup(kv->entries[idx].value);
}

char *worker_kv_get_with_metadata(worker_kv_t *kv, const char *key,
                                  char **out_metadata) {
    int idx = _kv_find(kv, key);
    if (idx < 0) {
        if (out_metadata) *out_metadata = NULL;
        return NULL;
    }
    if (out_metadata) {
        *out_metadata = kv->entries[idx].metadata
                        ? strdup(kv->entries[idx].metadata)
                        : NULL;
    }
    return strdup(kv->entries[idx].value);
}

/* ===== KV Put ===== */

int worker_kv_put(worker_kv_t *kv, const char *key, const char *value,
                  const worker_kv_put_options_t *opts) {
    if (!kv || !key || !value) return -1;
    if (strlen(key) > WORKER_KV_MAX_KEY_LEN) return -1;
    size_t vlen = strlen(value);
    if (vlen > WORKER_KV_MAX_VALUE_LEN) return -1;

    /* Enforce metadata size limit per Cloudflare docs (1024 B) */
    if (opts && opts->metadata &&
        strlen(opts->metadata) > WORKER_KV_MAX_META_LEN) return -1;

    /* Pre-allocate all buffers before modifying any entry state */
    char *new_value = strdup(value);
    if (!new_value) return -1;

    char *new_metadata = NULL;
    if (opts && opts->metadata) {
        new_metadata = strdup(opts->metadata);
        if (!new_metadata) { free(new_value); return -1; }
    }

    /* Check if key already exists — update in place */
    int idx = -1;
    for (int i = 0; i < kv->count; i++) {
        if (strcmp(kv->entries[i].key, key) == 0) {
            idx = i;
            break;
        }
    }

    bool is_new = (idx < 0);
    char *new_key = NULL;
    if (is_new) {
        if (kv->count >= WORKER_KV_MAX_ENTRIES) {
            free(new_value);
            free(new_metadata);
            return -1;
        }
        new_key = strdup(key);
        if (!new_key) {
            free(new_value);
            free(new_metadata);
            return -1;
        }
        idx = kv->count;
    }

    /* All allocations succeeded — commit changes */
    if (is_new) {
        kv->entries[idx].key = new_key;
        kv->count++;
    } else {
        free(kv->entries[idx].value);
        free(kv->entries[idx].metadata);
    }

    kv->entries[idx].value = new_value;
    kv->entries[idx].value_len = vlen;
    kv->entries[idx].metadata = new_metadata;
    kv->entries[idx].expiration = 0;

    if (opts) {
        if (opts->expiration_ttl > 0) {
            kv->entries[idx].expiration = time(NULL) + opts->expiration_ttl;
        } else if (opts->expiration > 0) {
            kv->entries[idx].expiration = opts->expiration;
        }
    }

    return 0;
}

/* ===== KV Delete ===== */

int worker_kv_delete(worker_kv_t *kv, const char *key) {
    if (!kv || !key) return -1;
    for (int i = 0; i < kv->count; i++) {
        if (strcmp(kv->entries[i].key, key) == 0) {
            _kv_remove_at(kv, i);
            return 0;
        }
    }
    return -1;  /* key not found */
}

/* ===== KV List ===== */

worker_kv_list_result_t *worker_kv_list(worker_kv_t *kv,
                                        const worker_kv_list_options_t *opts) {
    if (!kv) return NULL;

    worker_kv_list_result_t *result = calloc(1, sizeof(worker_kv_list_result_t));
    if (!result) return NULL;

    const char *prefix = opts ? opts->prefix : NULL;
    int limit = (opts && opts->limit > 0) ? opts->limit : 1000;
    int cursor_start = (opts && opts->cursor > 0) ? opts->cursor : 0;

    if (limit > 1000) limit = 1000;

    /* Allocate max possible keys */
    result->keys = calloc((size_t)limit, sizeof(char *));
    if (!result->keys) { free(result); return NULL; }

    time_t now = time(NULL);
    int found = 0;
    bool has_more = false;
    /* Use raw index-based cursor for consistency across paginated calls.
     * cursor_start is the raw array index to resume from. */
    int start_index = (cursor_start >= 0 && cursor_start <= kv->count) ? cursor_start : 0;
    int resume_index = 0;

    for (int i = start_index; i < kv->count; i++) {
        /* Skip expired */
        if (_kv_is_expired(&kv->entries[i], now)) continue;

        /* Prefix filter */
        if (prefix && strncmp(kv->entries[i].key, prefix,
                              strlen(prefix)) != 0) continue;

        if (found < limit) {
            result->keys[found] = strdup(kv->entries[i].key);
            found++;
            resume_index = i + 1;
        } else {
            has_more = true;
            break;
        }
    }

    result->count = found;
    result->list_complete = !has_more;
    result->cursor = has_more ? resume_index : 0;

    return result;
}

void worker_kv_list_result_destroy(worker_kv_list_result_t *result) {
    if (!result) return;
    for (int i = 0; i < result->count; i++) {
        free(result->keys[i]);
    }
    free(result->keys);
    free(result);
}
