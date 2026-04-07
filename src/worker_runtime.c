/*
 * worker_runtime.c - Cloudflare Worker Runtime Layer
 *
 * Provides a WASM-exported API for running the library inside
 * Cloudflare Workers.  Workers receive HTTP requests via a
 * JavaScript "fetch" event; the JS glue deserialises the
 * request, calls into this C/WASM layer for routing and
 * response construction, then returns the result to the
 * Workers runtime.
 *
 * Key abstractions:
 *   - worker_request_t   – incoming HTTP request (method, URL, headers, body)
 *   - worker_response_t  – outgoing HTTP response (status, headers, body)
 *   - worker_kv_t        – KV namespace binding (get/put/delete/list, TTL)
 *   - worker_r2_bucket_t – R2 object-storage binding (get/put/delete/list/head)
 *   - worker_d1_t        – D1 SQL database binding (exec/query)
 *   - worker_queue_t     – Queue producer binding (send/send_batch)
 *   - worker_env_t       – env context (KV, R2, D1, Queue bindings)
 *   - worker_handle_fetch() – routes a request through a router_t
 *
 * All public symbols are WASM_EXPORT so Emscripten keeps them alive.
 *
 * Copyright (c) 2024 Modern C Web Library
 * Licensed under MIT License
 */

#include "weblib_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ------------------------------------------------------------------ */
/*  Internal helpers and limits                                        */
/* ------------------------------------------------------------------ */

#define WORKER_MAX_METHOD_LEN     16
#define WORKER_MAX_URL_LEN        2048
#define WORKER_MAX_HEADERS        64
#define WORKER_MAX_HEADER_NAME    256
#define WORKER_MAX_HEADER_VALUE   4096
#define WORKER_KV_MAX_ENTRIES     1024
#define WORKER_KV_MAX_KEY_LEN     512
#define WORKER_R2_MAX_OBJECTS     256
#define WORKER_R2_MAX_KEY_LEN     1024
#define WORKER_D1_MAX_PARAMS      32
#define WORKER_D1_MAX_ROWS        256
#define WORKER_D1_MAX_COLS        32
#define WORKER_QUEUE_MAX_MSGS     100
#define WORKER_QUEUE_MAX_MSG_LEN  131072  /* 128 KB per Cloudflare limit */
#define WORKER_QUEUE_MAX_PENDING  4096    /* max queued messages in memory */
#define WORKER_ENV_MAX_BINDINGS   32

/*
 * Case-insensitive string comparison (ASCII only).
 * Returns true if a and b are equal ignoring case.
 */
static bool _header_name_eq(const char *a, const char *b) {
    while (*a && *b) {
        char ca = (*a >= 'A' && *a <= 'Z') ? (*a + 32) : *a;
        char cb = (*b >= 'A' && *b <= 'Z') ? (*b + 32) : *b;
        if (ca != cb) return false;
        a++; b++;
    }
    return *a == '\0' && *b == '\0';
}

/*
 * Generic capacity-growth helper for dynamic arrays.
 * Doubles capacity up to max_cap, zeroes the new region, and updates
 * *arr_ptr / *cap_ptr in place.  Returns 0 on success, -1 on failure.
 */
static int _grow_array(void **arr_ptr, int *cap_ptr, int max_cap,
                       size_t elem_size) {
    int old_cap = *cap_ptr;
    if (old_cap >= max_cap) return -1;
    int new_cap = old_cap * 2;
    if (new_cap > max_cap) new_cap = max_cap;
    void *new_arr = realloc(*arr_ptr, (size_t)new_cap * elem_size);
    if (!new_arr) return -1;
    memset((char *)new_arr + (size_t)old_cap * elem_size, 0,
           (size_t)(new_cap - old_cap) * elem_size);
    *arr_ptr = new_arr;
    *cap_ptr = new_cap;
    return 0;
}

/* ------------------------------------------------------------------ */
/*  worker_request_t                                                   */
/* ------------------------------------------------------------------ */

struct worker_request {
    char method[WORKER_MAX_METHOD_LEN];
    char url[WORKER_MAX_URL_LEN];
    struct {
        char name[WORKER_MAX_HEADER_NAME];
        char value[WORKER_MAX_HEADER_VALUE];
    } headers[WORKER_MAX_HEADERS];
    int header_count;
    char *body;
    size_t body_len;
};

WASM_EXPORT
worker_request_t *worker_request_create(const char *method, const char *url) {
    if (!method || !url) return NULL;

    worker_request_t *req = (worker_request_t *)calloc(1, sizeof(worker_request_t));
    if (!req) return NULL;

    snprintf(req->method, sizeof(req->method), "%s", method);
    snprintf(req->url, sizeof(req->url), "%s", url);
    return req;
}

WASM_EXPORT
void worker_request_set_header(worker_request_t *req,
                               const char *name, const char *value) {
    if (!req || !name || !value) return;
    if (req->header_count >= WORKER_MAX_HEADERS) return;

    snprintf(req->headers[req->header_count].name,
             WORKER_MAX_HEADER_NAME, "%s", name);
    snprintf(req->headers[req->header_count].value,
             WORKER_MAX_HEADER_VALUE, "%s", value);
    req->header_count++;
}

WASM_EXPORT
void worker_request_set_body(worker_request_t *req,
                             const char *body, size_t len) {
    if (!req) return;
    free(req->body);
    req->body = NULL;
    req->body_len = 0;
    if (body && len > 0) {
        req->body = (char *)malloc(len + 1);
        if (req->body) {
            memcpy(req->body, body, len);
            req->body[len] = '\0';
            req->body_len = len;
        }
    }
}

WASM_EXPORT
const char *worker_request_get_method(worker_request_t *req) {
    return req ? req->method : NULL;
}

WASM_EXPORT
const char *worker_request_get_url(worker_request_t *req) {
    return req ? req->url : NULL;
}

WASM_EXPORT
const char *worker_request_get_header(worker_request_t *req,
                                      const char *name) {
    if (!req || !name) return NULL;
    for (int i = 0; i < req->header_count; i++) {
        if (_header_name_eq(req->headers[i].name, name))
            return req->headers[i].value;
    }
    return NULL;
}

WASM_EXPORT
const char *worker_request_get_body(worker_request_t *req) {
    return (req && req->body) ? req->body : NULL;
}

WASM_EXPORT
size_t worker_request_get_body_len(worker_request_t *req) {
    return req ? req->body_len : 0;
}

WASM_EXPORT
void worker_request_destroy(worker_request_t *req) {
    if (!req) return;
    free(req->body);
    free(req);
}

/* ------------------------------------------------------------------ */
/*  worker_response_t                                                  */
/* ------------------------------------------------------------------ */

struct worker_response {
    int status;
    struct {
        char name[WORKER_MAX_HEADER_NAME];
        char value[WORKER_MAX_HEADER_VALUE];
    } headers[WORKER_MAX_HEADERS];
    int header_count;
    char *body;
    size_t body_len;
};

WASM_EXPORT
worker_response_t *worker_response_create(void) {
    worker_response_t *res = (worker_response_t *)calloc(1, sizeof(worker_response_t));
    if (res) res->status = 200;
    return res;
}

WASM_EXPORT
void worker_response_set_status(worker_response_t *res, int status) {
    if (res) res->status = status;
}

WASM_EXPORT
void worker_response_set_header(worker_response_t *res,
                                const char *name, const char *value) {
    if (!res || !name || !value) return;

    /* Replace existing header with same name (case-insensitive) */
    for (int i = 0; i < res->header_count; i++) {
        if (_header_name_eq(res->headers[i].name, name)) {
            snprintf(res->headers[i].value,
                     WORKER_MAX_HEADER_VALUE, "%s", value);
            return;
        }
    }

    if (res->header_count >= WORKER_MAX_HEADERS) return;

    snprintf(res->headers[res->header_count].name,
             WORKER_MAX_HEADER_NAME, "%s", name);
    snprintf(res->headers[res->header_count].value,
             WORKER_MAX_HEADER_VALUE, "%s", value);
    res->header_count++;
}

WASM_EXPORT
void worker_response_set_body(worker_response_t *res,
                              const char *body, size_t len) {
    if (!res) return;
    free(res->body);
    res->body = NULL;
    res->body_len = 0;
    if (body && len > 0) {
        res->body = (char *)malloc(len + 1);
        if (res->body) {
            memcpy(res->body, body, len);
            res->body[len] = '\0';
            res->body_len = len;
        }
    }
}

WASM_EXPORT
void worker_response_set_text(worker_response_t *res,
                              int status, const char *text) {
    if (!res || !text) return;
    worker_response_set_status(res, status);
    worker_response_set_header(res, "Content-Type", "text/plain; charset=utf-8");
    worker_response_set_body(res, text, strlen(text));
}

WASM_EXPORT
void worker_response_set_json(worker_response_t *res,
                              int status, json_value_t *json) {
    if (!res || !json) return;
    char *str = json_stringify(json);
    if (!str) return;
    worker_response_set_status(res, status);
    worker_response_set_header(res, "Content-Type", "application/json; charset=utf-8");
    worker_response_set_body(res, str, strlen(str));
    free(str);
}

WASM_EXPORT
int worker_response_get_status(worker_response_t *res) {
    return res ? res->status : 0;
}

WASM_EXPORT
const char *worker_response_get_header(worker_response_t *res,
                                       const char *name) {
    if (!res || !name) return NULL;
    for (int i = 0; i < res->header_count; i++) {
        if (_header_name_eq(res->headers[i].name, name))
            return res->headers[i].value;
    }
    return NULL;
}

WASM_EXPORT
int worker_response_get_header_count(worker_response_t *res) {
    return res ? res->header_count : 0;
}

WASM_EXPORT
const char *worker_response_get_header_name(worker_response_t *res, int index) {
    if (!res || index < 0 || index >= res->header_count) return NULL;
    return res->headers[index].name;
}

WASM_EXPORT
const char *worker_response_get_header_value(worker_response_t *res, int index) {
    if (!res || index < 0 || index >= res->header_count) return NULL;
    return res->headers[index].value;
}

WASM_EXPORT
const char *worker_response_get_body(worker_response_t *res) {
    return (res && res->body) ? res->body : NULL;
}

WASM_EXPORT
size_t worker_response_get_body_len(worker_response_t *res) {
    return res ? res->body_len : 0;
}

WASM_EXPORT
void worker_response_destroy(worker_response_t *res) {
    if (!res) return;
    free(res->body);
    free(res);
}

/* ------------------------------------------------------------------ */
/*  worker_handle_fetch – route a Worker request through a router_t   */
/* ------------------------------------------------------------------ */

/*
 * Map a method string ("GET", "POST", …) to the library's enum.
 * Returns HTTP_GET on unrecognised input so routing can still proceed.
 */
static http_method_t _method_from_string(const char *m) {
    if (!m) return HTTP_GET;
    if (strcmp(m, "GET") == 0)     return HTTP_GET;
    if (strcmp(m, "POST") == 0)    return HTTP_POST;
    if (strcmp(m, "PUT") == 0)     return HTTP_PUT;
    if (strcmp(m, "DELETE") == 0)  return HTTP_DELETE;
    if (strcmp(m, "PATCH") == 0)   return HTTP_PATCH;
    if (strcmp(m, "HEAD") == 0)    return HTTP_HEAD;
    if (strcmp(m, "OPTIONS") == 0) return HTTP_OPTIONS;
    return HTTP_GET;
}

/*
 * Cleanup helper for the stack-allocated lib_req/lib_res used in
 * worker_handle_fetch().  Uses the shared internal helpers from
 * weblib_internal.h so we don't duplicate struct layouts.
 */
static void _cleanup_lib_request(http_request_t *req) {
    free(req->path);
    free(req->query_string);
    free(req->body);
    weblib_header_list_free((http_header_node_t *)req->headers);
    weblib_param_list_free((http_param_node_t *)req->params);
    req->path = NULL;
    req->query_string = NULL;
    req->body = NULL;
    req->headers = NULL;
    req->params = NULL;
}

static void _cleanup_lib_response(http_response_t *res) {
    free(res->body);
    weblib_header_list_free((http_header_node_t *)res->headers);
    res->body = NULL;
    res->headers = NULL;
}

WASM_EXPORT
worker_response_t *worker_handle_fetch(worker_request_t *req,
                                       router_t *router) {
    if (!req || !router) return NULL;

    /* Build a library-level request for the router. */
    http_request_t lib_req;
    memset(&lib_req, 0, sizeof(lib_req));
    lib_req.method = _method_from_string(req->method);

    /* Separate path and query string from the URL */
    char *qs = strchr(req->url, '?');
    if (qs) {
        size_t path_len = (size_t)(qs - req->url);
        lib_req.path = (char *)malloc(path_len + 1);
        if (!lib_req.path) {
            return NULL;
        }
        memcpy(lib_req.path, req->url, path_len);
        lib_req.path[path_len] = '\0';
        lib_req.query_string = strdup(qs + 1);
        if (!lib_req.query_string) {
            free(lib_req.path);
            return NULL;
        }
    } else {
        lib_req.path = strdup(req->url);
        if (!lib_req.path) {
            return NULL;
        }
        lib_req.query_string = NULL;
    }

    /* Copy body */
    if (req->body && req->body_len > 0) {
        lib_req.body = (char *)malloc(req->body_len + 1);
        if (!lib_req.body) {
            free(lib_req.path);
            free(lib_req.query_string);
            return NULL;
        }
        memcpy(lib_req.body, req->body, req->body_len);
        lib_req.body[req->body_len] = '\0';
        lib_req.body_length = req->body_len;
    }

    /* Copy headers into the library request using the same linked-list
     * layout shared by request and response header stores. */
    for (int i = 0; i < req->header_count; i++) {
        http_response_t tmp_res;
        memset(&tmp_res, 0, sizeof(tmp_res));
        tmp_res.headers = lib_req.headers;
        http_response_set_header(&tmp_res, req->headers[i].name,
                                 req->headers[i].value);
        lib_req.headers = tmp_res.headers;
    }

    lib_req.socket_fd = -1;

    /* Build a library-level response */
    http_response_t lib_res;
    memset(&lib_res, 0, sizeof(lib_res));
    lib_res.status = HTTP_OK;

    /* Dispatch through the router */
    router_route(router, &lib_req, &lib_res);

    /* Convert the library response to a worker_response_t */
    worker_response_t *wres = worker_response_create();
    if (!wres) {
        _cleanup_lib_request(&lib_req);
        _cleanup_lib_response(&lib_res);
        return NULL;
    }

    wres->status = (int)lib_res.status;

    /* Copy response body */
    if (lib_res.body && lib_res.body_length > 0) {
        worker_response_set_body(wres, lib_res.body, lib_res.body_length);
    }

    /* Copy response headers using shared internal types */
    for (http_header_node_t *n = (http_header_node_t *)lib_res.headers;
         n; n = n->next) {
        const char *hname = n->raw_name ? n->raw_name : n->name;
        worker_response_set_header(wres, hname, n->value);
    }

    /* Free library-level objects */
    _cleanup_lib_request(&lib_req);
    _cleanup_lib_response(&lib_res);

    return wres;
}

/* ------------------------------------------------------------------ */
/*  worker_kv_t – KV namespace binding (mirrors Cloudflare KV API)    */
/*                                                                     */
/*  Cloudflare KV API methods modeled:                                 */
/*    - get(key)                  → worker_kv_get()                    */
/*    - put(key, value, {ttl})    → worker_kv_put() / _put_with_ttl() */
/*    - delete(key)               → worker_kv_delete()                */
/*    - list({prefix, limit, cursor}) → worker_kv_list()              */
/* ------------------------------------------------------------------ */

struct worker_kv_entry {
    char *key;
    char *value;
    time_t expiration;  /* 0 = no expiration (matches CF KV semantics) */
};

struct worker_kv {
    struct worker_kv_entry *entries;
    int count;
    int capacity;
};

WASM_EXPORT
worker_kv_t *worker_kv_create(void) {
    worker_kv_t *kv = (worker_kv_t *)calloc(1, sizeof(worker_kv_t));
    if (!kv) return NULL;
    kv->capacity = 64;
    kv->entries = (struct worker_kv_entry *)calloc(
        (size_t)kv->capacity, sizeof(struct worker_kv_entry));
    if (!kv->entries) {
        free(kv);
        return NULL;
    }
    return kv;
}

/* Purge expired entries lazily */
static void _kv_purge_expired(worker_kv_t *kv) {
    time_t now = time(NULL);
    int i = 0;
    while (i < kv->count) {
        if (kv->entries[i].expiration > 0 &&
            kv->entries[i].expiration <= now) {
            free(kv->entries[i].key);
            free(kv->entries[i].value);
            if (i < kv->count - 1) {
                kv->entries[i] = kv->entries[kv->count - 1];
            }
            memset(&kv->entries[kv->count - 1], 0,
                   sizeof(struct worker_kv_entry));
            kv->count--;
        } else {
            i++;
        }
    }
}

/* Find an entry by key, returns index or -1. Purges expired first. */
static int _kv_find(worker_kv_t *kv, const char *key) {
    _kv_purge_expired(kv);
    for (int i = 0; i < kv->count; i++) {
        if (strcmp(kv->entries[i].key, key) == 0)
            return i;
    }
    return -1;
}

WASM_EXPORT
int worker_kv_put(worker_kv_t *kv, const char *key, const char *value) {
    if (!kv || !key || !value) return -1;

    /* Reject keys exceeding max length */
    if (strlen(key) >= WORKER_KV_MAX_KEY_LEN) return -1;

    /* Update existing entry */
    int idx = _kv_find(kv, key);
    if (idx >= 0) {
        char *new_val = strdup(value);
        if (!new_val) return -1;
        free(kv->entries[idx].value);
        kv->entries[idx].value = new_val;
        kv->entries[idx].expiration = 0;
        return 0;
    }

    /* Grow if needed */
    if (kv->count >= kv->capacity) {
        if (_grow_array((void **)&kv->entries, &kv->capacity,
                        WORKER_KV_MAX_ENTRIES,
                        sizeof(struct worker_kv_entry)) != 0)
            return -1;
    }

    /* Allocate key and value before committing */
    char *k = strdup(key);
    char *v = strdup(value);
    if (!k || !v) {
        free(k);
        free(v);
        return -1;
    }

    kv->entries[kv->count].key = k;
    kv->entries[kv->count].value = v;
    kv->entries[kv->count].expiration = 0;
    kv->count++;
    return 0;
}

WASM_EXPORT
int worker_kv_put_with_ttl(worker_kv_t *kv, const char *key,
                           const char *value, int ttl_seconds) {
    if (!kv || !key || !value) return -1;
    if (ttl_seconds < 0) return -1;
    if (strlen(key) >= WORKER_KV_MAX_KEY_LEN) return -1;

    /* Update existing entry */
    int idx = _kv_find(kv, key);
    if (idx >= 0) {
        char *new_val = strdup(value);
        if (!new_val) return -1;
        free(kv->entries[idx].value);
        kv->entries[idx].value = new_val;
        kv->entries[idx].expiration =
            ttl_seconds > 0 ? time(NULL) + ttl_seconds : 0;
        return 0;
    }

    /* Grow if needed */
    if (kv->count >= kv->capacity) {
        if (_grow_array((void **)&kv->entries, &kv->capacity,
                        WORKER_KV_MAX_ENTRIES,
                        sizeof(struct worker_kv_entry)) != 0)
            return -1;
    }

    char *k = strdup(key);
    char *v = strdup(value);
    if (!k || !v) {
        free(k);
        free(v);
        return -1;
    }

    kv->entries[kv->count].key = k;
    kv->entries[kv->count].value = v;
    kv->entries[kv->count].expiration =
        ttl_seconds > 0 ? time(NULL) + ttl_seconds : 0;
    kv->count++;
    return 0;
}

WASM_EXPORT
const char *worker_kv_get(worker_kv_t *kv, const char *key) {
    if (!kv || !key) return NULL;
    int idx = _kv_find(kv, key);
    return idx >= 0 ? kv->entries[idx].value : NULL;
}

WASM_EXPORT
int worker_kv_delete(worker_kv_t *kv, const char *key) {
    if (!kv || !key) return -1;
    int idx = _kv_find(kv, key);
    if (idx < 0) return -1;
    free(kv->entries[idx].key);
    free(kv->entries[idx].value);
    if (idx < kv->count - 1) {
        kv->entries[idx] = kv->entries[kv->count - 1];
    }
    memset(&kv->entries[kv->count - 1], 0, sizeof(struct worker_kv_entry));
    kv->count--;
    return 0;
}

WASM_EXPORT
int worker_kv_list(worker_kv_t *kv, const char *prefix, int limit,
                   const char ***out_keys, int *out_count) {
    if (!kv || !out_keys || !out_count) return -1;
    _kv_purge_expired(kv);

    if (limit <= 0 || limit > 1000) limit = 1000;

    /* Count matching keys */
    int match_count = 0;
    size_t prefix_len = prefix ? strlen(prefix) : 0;
    for (int i = 0; i < kv->count && match_count < limit; i++) {
        if (!prefix || strncmp(kv->entries[i].key, prefix, prefix_len) == 0)
            match_count++;
    }

    const char **keys = (const char **)calloc((size_t)match_count,
                                              sizeof(const char *));
    if (!keys && match_count > 0) return -1;

    int idx = 0;
    for (int i = 0; i < kv->count && idx < match_count; i++) {
        if (!prefix || strncmp(kv->entries[i].key, prefix, prefix_len) == 0)
            keys[idx++] = kv->entries[i].key;
    }

    *out_keys = keys;
    *out_count = match_count;
    return 0;
}

WASM_EXPORT
void worker_kv_list_free(const char **keys) {
    free((void *)keys);
}

WASM_EXPORT
void worker_kv_destroy(worker_kv_t *kv) {
    if (!kv) return;
    for (int i = 0; i < kv->count; i++) {
        free(kv->entries[i].key);
        free(kv->entries[i].value);
    }
    free(kv->entries);
    free(kv);
}

/* ------------------------------------------------------------------ */
/*  worker_r2_bucket_t – R2 object storage binding                     */
/*                                                                     */
/*  Cloudflare R2 API methods modeled:                                 */
/*    - put(key, value)  → worker_r2_put()                            */
/*    - get(key)         → worker_r2_get()                            */
/*    - delete(key)      → worker_r2_delete()                         */
/*    - head(key)        → worker_r2_head()                           */
/*    - list({prefix})   → worker_r2_list()                           */
/* ------------------------------------------------------------------ */

struct worker_r2_object {
    char *key;
    char *data;
    size_t size;
    char *content_type;
    time_t uploaded;
};

struct worker_r2_bucket {
    struct worker_r2_object *objects;
    int count;
    int capacity;
};

WASM_EXPORT
worker_r2_bucket_t *worker_r2_create(void) {
    worker_r2_bucket_t *r2 = (worker_r2_bucket_t *)calloc(
        1, sizeof(worker_r2_bucket_t));
    if (!r2) return NULL;
    r2->capacity = 32;
    r2->objects = (struct worker_r2_object *)calloc(
        (size_t)r2->capacity, sizeof(struct worker_r2_object));
    if (!r2->objects) {
        free(r2);
        return NULL;
    }
    return r2;
}

static int _r2_find(worker_r2_bucket_t *r2, const char *key) {
    for (int i = 0; i < r2->count; i++) {
        if (strcmp(r2->objects[i].key, key) == 0) return i;
    }
    return -1;
}

WASM_EXPORT
int worker_r2_put(worker_r2_bucket_t *r2, const char *key,
                  const char *data, size_t size,
                  const char *content_type) {
    if (!r2 || !key || !data) return -1;
    if (strlen(key) >= WORKER_R2_MAX_KEY_LEN) return -1;

    /* Update existing object */
    int idx = _r2_find(r2, key);
    if (idx >= 0) {
        char *new_data = (char *)malloc(size + 1);
        if (!new_data) return -1;
        memcpy(new_data, data, size);
        new_data[size] = '\0';
        free(r2->objects[idx].data);
        r2->objects[idx].data = new_data;
        r2->objects[idx].size = size;
        r2->objects[idx].uploaded = time(NULL);
        if (content_type) {
            char *ct = strdup(content_type);
            if (ct) {
                free(r2->objects[idx].content_type);
                r2->objects[idx].content_type = ct;
            }
        }
        return 0;
    }

    /* Grow if needed */
    if (r2->count >= r2->capacity) {
        if (_grow_array((void **)&r2->objects, &r2->capacity,
                        WORKER_R2_MAX_OBJECTS,
                        sizeof(struct worker_r2_object)) != 0)
            return -1;
    }

    /* Pre-allocate then commit */
    char *k = strdup(key);
    char *d = (char *)malloc(size + 1);
    char *ct = content_type ? strdup(content_type) : strdup("application/octet-stream");
    if (!k || !d || !ct) {
        free(k); free(d); free(ct);
        return -1;
    }
    memcpy(d, data, size);
    d[size] = '\0';

    r2->objects[r2->count].key = k;
    r2->objects[r2->count].data = d;
    r2->objects[r2->count].size = size;
    r2->objects[r2->count].content_type = ct;
    r2->objects[r2->count].uploaded = time(NULL);
    r2->count++;
    return 0;
}

WASM_EXPORT
const char *worker_r2_get(worker_r2_bucket_t *r2, const char *key,
                          size_t *out_size) {
    if (!r2 || !key) return NULL;
    int idx = _r2_find(r2, key);
    if (idx < 0) return NULL;
    if (out_size) *out_size = r2->objects[idx].size;
    return r2->objects[idx].data;
}

WASM_EXPORT
int worker_r2_head(worker_r2_bucket_t *r2, const char *key,
                   size_t *out_size, const char **out_content_type) {
    if (!r2 || !key) return -1;
    int idx = _r2_find(r2, key);
    if (idx < 0) return -1;
    if (out_size) *out_size = r2->objects[idx].size;
    if (out_content_type) *out_content_type = r2->objects[idx].content_type;
    return 0;
}

WASM_EXPORT
int worker_r2_delete(worker_r2_bucket_t *r2, const char *key) {
    if (!r2 || !key) return -1;
    int idx = _r2_find(r2, key);
    if (idx < 0) return -1;
    free(r2->objects[idx].key);
    free(r2->objects[idx].data);
    free(r2->objects[idx].content_type);
    if (idx < r2->count - 1) {
        r2->objects[idx] = r2->objects[r2->count - 1];
    }
    memset(&r2->objects[r2->count - 1], 0, sizeof(struct worker_r2_object));
    r2->count--;
    return 0;
}

WASM_EXPORT
int worker_r2_list(worker_r2_bucket_t *r2, const char *prefix, int limit,
                   const char ***out_keys, int *out_count) {
    if (!r2 || !out_keys || !out_count) return -1;
    if (limit <= 0 || limit > 1000) limit = 1000;

    size_t prefix_len = prefix ? strlen(prefix) : 0;
    int match = 0;
    for (int i = 0; i < r2->count && match < limit; i++) {
        if (!prefix || strncmp(r2->objects[i].key, prefix, prefix_len) == 0)
            match++;
    }

    const char **keys = (const char **)calloc((size_t)match,
                                              sizeof(const char *));
    if (!keys && match > 0) return -1;

    int idx = 0;
    for (int i = 0; i < r2->count && idx < match; i++) {
        if (!prefix || strncmp(r2->objects[i].key, prefix, prefix_len) == 0)
            keys[idx++] = r2->objects[i].key;
    }

    *out_keys = keys;
    *out_count = match;
    return 0;
}

WASM_EXPORT
void worker_r2_list_free(const char **keys) {
    free((void *)keys);
}

WASM_EXPORT
void worker_r2_destroy(worker_r2_bucket_t *r2) {
    if (!r2) return;
    for (int i = 0; i < r2->count; i++) {
        free(r2->objects[i].key);
        free(r2->objects[i].data);
        free(r2->objects[i].content_type);
    }
    free(r2->objects);
    free(r2);
}

/* ------------------------------------------------------------------ */
/*  worker_d1_t – D1 edge SQL database binding                        */
/*                                                                     */
/*  Cloudflare D1 API methods modeled:                                 */
/*    - prepare(sql).bind(...).run()  → worker_d1_exec()              */
/*    - prepare(sql).bind(...).all()  → worker_d1_query()             */
/* ------------------------------------------------------------------ */

struct worker_d1_row {
    char *columns[WORKER_D1_MAX_COLS];
    int col_count;
};

struct worker_d1_result {
    char *col_names[WORKER_D1_MAX_COLS];
    int col_count;
    struct worker_d1_row rows[WORKER_D1_MAX_ROWS];
    int row_count;
    int changes;
    bool success;
    char *error;
};

/*
 * Simple in-memory table store for D1 emulation.
 * Uses an array of key-value JSON rows for simplicity.
 */
struct worker_d1_table {
    char *name;
    char *col_names[WORKER_D1_MAX_COLS];
    int col_count;
    struct worker_d1_row rows[WORKER_D1_MAX_ROWS];
    int row_count;
};

struct worker_d1 {
    struct worker_d1_table *tables;
    int table_count;
    int table_capacity;
};

WASM_EXPORT
worker_d1_t *worker_d1_create(void) {
    worker_d1_t *d1 = (worker_d1_t *)calloc(1, sizeof(worker_d1_t));
    if (!d1) return NULL;
    d1->table_capacity = 8;
    d1->tables = (struct worker_d1_table *)calloc(
        (size_t)d1->table_capacity, sizeof(struct worker_d1_table));
    if (!d1->tables) {
        free(d1);
        return NULL;
    }
    return d1;
}

static int _d1_find_table(worker_d1_t *d1, const char *name) {
    for (int i = 0; i < d1->table_count; i++) {
        if (d1->tables[i].name && strcmp(d1->tables[i].name, name) == 0)
            return i;
    }
    return -1;
}

WASM_EXPORT
int worker_d1_exec(worker_d1_t *d1, const char *sql) {
    if (!d1 || !sql) return -1;

    /* Simple CREATE TABLE parsing */
    if (strncmp(sql, "CREATE TABLE", 12) == 0 ||
        strncmp(sql, "create table", 12) == 0) {
        /* Extract table name: CREATE TABLE name (...) */
        const char *p = sql + 12;
        while (*p == ' ') p++;

        /* Skip IF NOT EXISTS */
        if (strncmp(p, "IF NOT EXISTS", 13) == 0 ||
            strncmp(p, "if not exists", 13) == 0) {
            p += 13;
            while (*p == ' ') p++;
        }

        char tname[128];
        int ti = 0;
        while (*p && *p != ' ' && *p != '(' && ti < 127) {
            tname[ti++] = *p++;
        }
        tname[ti] = '\0';

        /* Check if table already exists */
        if (_d1_find_table(d1, tname) >= 0) return 0;

        if (d1->table_count >= d1->table_capacity) return -1;

        d1->tables[d1->table_count].name = strdup(tname);
        if (!d1->tables[d1->table_count].name) return -1;

        /* Parse column names from parentheses */
        const char *paren = strchr(p, '(');
        if (paren) {
            paren++;
            int col = 0;
            while (*paren && *paren != ')' && col < WORKER_D1_MAX_COLS) {
                while (*paren == ' ' || *paren == '\n' || *paren == '\t') paren++;
                if (*paren == ')') break;
                char cname[128];
                int ci = 0;
                while (*paren && *paren != ' ' && *paren != ','
                       && *paren != ')' && ci < 127) {
                    cname[ci++] = *paren++;
                }
                cname[ci] = '\0';
                if (ci > 0) {
                    d1->tables[d1->table_count].col_names[col] = strdup(cname);
                    col++;
                }
                /* Skip column type and constraints */
                while (*paren && *paren != ',' && *paren != ')') paren++;
                if (*paren == ',') paren++;
            }
            d1->tables[d1->table_count].col_count = col;
        }

        d1->table_count++;
        return 0;
    }

    /* Simple INSERT parsing: INSERT INTO table (cols) VALUES (vals) */
    if (strncmp(sql, "INSERT INTO", 11) == 0 ||
        strncmp(sql, "insert into", 11) == 0) {
        const char *p = sql + 11;
        while (*p == ' ') p++;

        char tname[128];
        int ti = 0;
        while (*p && *p != ' ' && *p != '(' && ti < 127) {
            tname[ti++] = *p++;
        }
        tname[ti] = '\0';

        int tidx = _d1_find_table(d1, tname);
        if (tidx < 0) return -1;

        struct worker_d1_table *tbl = &d1->tables[tidx];
        if (tbl->row_count >= WORKER_D1_MAX_ROWS) return -1;

        /* Find VALUES section */
        const char *vals = strstr(p, "VALUES");
        if (!vals) vals = strstr(p, "values");
        if (!vals) return -1;

        const char *vp = strchr(vals, '(');
        if (!vp) return -1;
        vp++;

        struct worker_d1_row *row = &tbl->rows[tbl->row_count];
        int col = 0;
        while (*vp && *vp != ')' && col < tbl->col_count) {
            while (*vp == ' ') vp++;
            if (*vp == ')') break;

            char val[1024];
            int vi = 0;
            bool quoted = (*vp == '\'');
            if (quoted) {
                vp++; /* skip opening quote */
                while (*vp && *vp != '\'' && vi < 1023) {
                    val[vi++] = *vp++;
                }
                if (*vp == '\'') vp++;
            } else {
                while (*vp && *vp != ',' && *vp != ')' && *vp != ' ' && vi < 1023) {
                    val[vi++] = *vp++;
                }
            }
            val[vi] = '\0';
            row->columns[col] = strdup(val);
            col++;
            while (*vp == ' ' || *vp == ',') vp++;
        }
        row->col_count = col;
        tbl->row_count++;
        return 0;
    }

    return 0; /* Unknown SQL - no-op (no error) */
}

WASM_EXPORT
worker_d1_result_t *worker_d1_query(worker_d1_t *d1, const char *sql) {
    if (!d1 || !sql) return NULL;

    worker_d1_result_t *result = (worker_d1_result_t *)calloc(
        1, sizeof(worker_d1_result_t));
    if (!result) return NULL;

    /* Simple SELECT * FROM table [WHERE col = 'val'] parsing */
    if (strncmp(sql, "SELECT", 6) == 0 || strncmp(sql, "select", 6) == 0) {
        /* Skip to FROM */
        const char *from = strstr(sql, "FROM");
        if (!from) from = strstr(sql, "from");
        if (!from) {
            result->success = false;
            result->error = strdup("Missing FROM clause");
            return result;
        }
        from += 4;
        while (*from == ' ') from++;

        char tname[128];
        int ti = 0;
        while (*from && *from != ' ' && *from != ';' && ti < 127) {
            tname[ti++] = *from++;
        }
        tname[ti] = '\0';

        int tidx = _d1_find_table(d1, tname);
        if (tidx < 0) {
            result->success = false;
            result->error = strdup("Table not found");
            return result;
        }

        struct worker_d1_table *tbl = &d1->tables[tidx];

        /* Copy column names */
        result->col_count = tbl->col_count;
        for (int c = 0; c < tbl->col_count; c++) {
            result->col_names[c] = tbl->col_names[c] ?
                strdup(tbl->col_names[c]) : NULL;
        }

        /* Check for WHERE clause */
        const char *where = strstr(from, "WHERE");
        if (!where) where = strstr(from, "where");

        char where_col[128] = {0};
        char where_val[256] = {0};
        bool has_where = false;
        if (where) {
            where += 5;
            while (*where == ' ') where++;
            int wi = 0;
            while (*where && *where != ' ' && *where != '=' && wi < 127) {
                where_col[wi++] = *where++;
            }
            where_col[wi] = '\0';
            while (*where == ' ' || *where == '=') where++;
            if (*where == '\'') {
                where++;
                wi = 0;
                while (*where && *where != '\'' && wi < 255) {
                    where_val[wi++] = *where++;
                }
                where_val[wi] = '\0';
            } else {
                wi = 0;
                while (*where && *where != ' ' && *where != ';' && wi < 255) {
                    where_val[wi++] = *where++;
                }
                where_val[wi] = '\0';
            }
            has_where = true;
        }

        /* Find WHERE column index */
        int where_col_idx = -1;
        if (has_where) {
            for (int c = 0; c < tbl->col_count; c++) {
                if (tbl->col_names[c] &&
                    strcmp(tbl->col_names[c], where_col) == 0) {
                    where_col_idx = c;
                    break;
                }
            }
        }

        /* Copy matching rows */
        int rc = 0;
        for (int r = 0; r < tbl->row_count && rc < WORKER_D1_MAX_ROWS; r++) {
            if (has_where && where_col_idx >= 0) {
                if (!tbl->rows[r].columns[where_col_idx] ||
                    strcmp(tbl->rows[r].columns[where_col_idx], where_val) != 0) {
                    continue;
                }
            }
            for (int c = 0; c < tbl->rows[r].col_count; c++) {
                result->rows[rc].columns[c] = tbl->rows[r].columns[c] ?
                    strdup(tbl->rows[r].columns[c]) : NULL;
            }
            result->rows[rc].col_count = tbl->rows[r].col_count;
            rc++;
        }
        result->row_count = rc;
        result->success = true;
    } else {
        /* Non-SELECT: execute and report changes */
        int ret = worker_d1_exec(d1, sql);
        result->success = (ret == 0);
        result->changes = ret == 0 ? 1 : 0;
    }

    return result;
}

WASM_EXPORT
int worker_d1_result_get_row_count(worker_d1_result_t *result) {
    return result ? result->row_count : 0;
}

WASM_EXPORT
int worker_d1_result_get_col_count(worker_d1_result_t *result) {
    return result ? result->col_count : 0;
}

WASM_EXPORT
const char *worker_d1_result_get_col_name(worker_d1_result_t *result,
                                          int col) {
    if (!result || col < 0 || col >= result->col_count) return NULL;
    return result->col_names[col];
}

WASM_EXPORT
const char *worker_d1_result_get_value(worker_d1_result_t *result,
                                       int row, int col) {
    if (!result || row < 0 || row >= result->row_count) return NULL;
    if (col < 0 || col >= result->rows[row].col_count) return NULL;
    return result->rows[row].columns[col];
}

WASM_EXPORT
bool worker_d1_result_is_success(worker_d1_result_t *result) {
    return result ? result->success : false;
}

WASM_EXPORT
const char *worker_d1_result_get_error(worker_d1_result_t *result) {
    return result ? result->error : NULL;
}

WASM_EXPORT
void worker_d1_result_destroy(worker_d1_result_t *result) {
    if (!result) return;
    for (int c = 0; c < result->col_count; c++) {
        free(result->col_names[c]);
    }
    for (int r = 0; r < result->row_count; r++) {
        for (int c = 0; c < result->rows[r].col_count; c++) {
            free(result->rows[r].columns[c]);
        }
    }
    free(result->error);
    free(result);
}

WASM_EXPORT
void worker_d1_destroy(worker_d1_t *d1) {
    if (!d1) return;
    for (int t = 0; t < d1->table_count; t++) {
        free(d1->tables[t].name);
        for (int c = 0; c < d1->tables[t].col_count; c++) {
            free(d1->tables[t].col_names[c]);
        }
        for (int r = 0; r < d1->tables[t].row_count; r++) {
            for (int c = 0; c < d1->tables[t].rows[r].col_count; c++) {
                free(d1->tables[t].rows[r].columns[c]);
            }
        }
    }
    free(d1->tables);
    free(d1);
}

/* ------------------------------------------------------------------ */
/*  worker_queue_t – Queues producer binding                           */
/*                                                                     */
/*  Cloudflare Queues API methods modeled:                             */
/*    - send(body)        → worker_queue_send()                       */
/*    - sendBatch(msgs)   → worker_queue_send_batch()                 */
/* ------------------------------------------------------------------ */

struct worker_queue_msg {
    char *body;
    size_t len;
    time_t timestamp;
};

struct worker_queue {
    struct worker_queue_msg *messages;
    int count;
    int capacity;
};

WASM_EXPORT
worker_queue_t *worker_queue_create(void) {
    worker_queue_t *q = (worker_queue_t *)calloc(1, sizeof(worker_queue_t));
    if (!q) return NULL;
    q->capacity = 32;
    q->messages = (struct worker_queue_msg *)calloc(
        (size_t)q->capacity, sizeof(struct worker_queue_msg));
    if (!q->messages) {
        free(q);
        return NULL;
    }
    return q;
}

WASM_EXPORT
int worker_queue_send(worker_queue_t *q, const char *body, size_t len) {
    if (!q || !body) return -1;
    if (len > WORKER_QUEUE_MAX_MSG_LEN) return -1;

    if (q->count >= q->capacity) {
        if (_grow_array((void **)&q->messages, &q->capacity,
                        WORKER_QUEUE_MAX_PENDING,
                        sizeof(struct worker_queue_msg)) != 0)
            return -1;
    }

    char *b = (char *)malloc(len + 1);
    if (!b) return -1;
    memcpy(b, body, len);
    b[len] = '\0';

    q->messages[q->count].body = b;
    q->messages[q->count].len = len;
    q->messages[q->count].timestamp = time(NULL);
    q->count++;
    return 0;
}

WASM_EXPORT
int worker_queue_send_batch(worker_queue_t *q,
                            const char **bodies, const size_t *lengths,
                            int count) {
    if (!q || !bodies || !lengths || count <= 0) return -1;
    if (count > WORKER_QUEUE_MAX_MSGS) return -1;

    /* Validate total batch size (256 KB limit per Cloudflare docs) */
    size_t total = 0;
    for (int i = 0; i < count; i++) {
        if (lengths[i] > WORKER_QUEUE_MAX_MSG_LEN) return -1;
        total += lengths[i];
    }
    if (total > 262144) return -1; /* 256 KB */

    for (int i = 0; i < count; i++) {
        if (worker_queue_send(q, bodies[i], lengths[i]) != 0)
            return -1;
    }
    return 0;
}

WASM_EXPORT
int worker_queue_get_count(worker_queue_t *q) {
    return q ? q->count : 0;
}

WASM_EXPORT
const char *worker_queue_peek(worker_queue_t *q, int index, size_t *out_len) {
    if (!q || index < 0 || index >= q->count) return NULL;
    if (out_len) *out_len = q->messages[index].len;
    return q->messages[index].body;
}

WASM_EXPORT
void worker_queue_destroy(worker_queue_t *q) {
    if (!q) return;
    for (int i = 0; i < q->count; i++) {
        free(q->messages[i].body);
    }
    free(q->messages);
    free(q);
}

/* ------------------------------------------------------------------ */
/*  worker_env_t – env context (models CF Workers env object)          */
/*                                                                     */
/*  The env context holds named bindings so handlers can access        */
/*  KV namespaces, R2 buckets, D1 databases, and Queues by name,      */
/*  matching the CF Workers pattern: env.MY_KV, env.MY_BUCKET, etc.   */
/* ------------------------------------------------------------------ */

typedef enum {
    WORKER_BINDING_KV,
    WORKER_BINDING_R2,
    WORKER_BINDING_D1,
    WORKER_BINDING_QUEUE
} worker_binding_type_t;

struct worker_env_binding {
    char *name;
    worker_binding_type_t type;
    void *binding;
};

struct worker_env {
    struct worker_env_binding bindings[WORKER_ENV_MAX_BINDINGS];
    int count;
};

WASM_EXPORT
worker_env_t *worker_env_create(void) {
    return (worker_env_t *)calloc(1, sizeof(worker_env_t));
}

WASM_EXPORT
int worker_env_bind_kv(worker_env_t *env, const char *name,
                       worker_kv_t *kv) {
    if (!env || !name || !kv) return -1;
    if (env->count >= WORKER_ENV_MAX_BINDINGS) return -1;
    char *n = strdup(name);
    if (!n) return -1;
    env->bindings[env->count].name = n;
    env->bindings[env->count].type = WORKER_BINDING_KV;
    env->bindings[env->count].binding = kv;
    env->count++;
    return 0;
}

WASM_EXPORT
int worker_env_bind_r2(worker_env_t *env, const char *name,
                       worker_r2_bucket_t *r2) {
    if (!env || !name || !r2) return -1;
    if (env->count >= WORKER_ENV_MAX_BINDINGS) return -1;
    char *n = strdup(name);
    if (!n) return -1;
    env->bindings[env->count].name = n;
    env->bindings[env->count].type = WORKER_BINDING_R2;
    env->bindings[env->count].binding = r2;
    env->count++;
    return 0;
}

WASM_EXPORT
int worker_env_bind_d1(worker_env_t *env, const char *name,
                       worker_d1_t *d1) {
    if (!env || !name || !d1) return -1;
    if (env->count >= WORKER_ENV_MAX_BINDINGS) return -1;
    char *n = strdup(name);
    if (!n) return -1;
    env->bindings[env->count].name = n;
    env->bindings[env->count].type = WORKER_BINDING_D1;
    env->bindings[env->count].binding = d1;
    env->count++;
    return 0;
}

WASM_EXPORT
int worker_env_bind_queue(worker_env_t *env, const char *name,
                          worker_queue_t *q) {
    if (!env || !name || !q) return -1;
    if (env->count >= WORKER_ENV_MAX_BINDINGS) return -1;
    char *n = strdup(name);
    if (!n) return -1;
    env->bindings[env->count].name = n;
    env->bindings[env->count].type = WORKER_BINDING_QUEUE;
    env->bindings[env->count].binding = q;
    env->count++;
    return 0;
}

WASM_EXPORT
worker_kv_t *worker_env_get_kv(worker_env_t *env, const char *name) {
    if (!env || !name) return NULL;
    for (int i = 0; i < env->count; i++) {
        if (env->bindings[i].type == WORKER_BINDING_KV &&
            strcmp(env->bindings[i].name, name) == 0)
            return (worker_kv_t *)env->bindings[i].binding;
    }
    return NULL;
}

WASM_EXPORT
worker_r2_bucket_t *worker_env_get_r2(worker_env_t *env, const char *name) {
    if (!env || !name) return NULL;
    for (int i = 0; i < env->count; i++) {
        if (env->bindings[i].type == WORKER_BINDING_R2 &&
            strcmp(env->bindings[i].name, name) == 0)
            return (worker_r2_bucket_t *)env->bindings[i].binding;
    }
    return NULL;
}

WASM_EXPORT
worker_d1_t *worker_env_get_d1(worker_env_t *env, const char *name) {
    if (!env || !name) return NULL;
    for (int i = 0; i < env->count; i++) {
        if (env->bindings[i].type == WORKER_BINDING_D1 &&
            strcmp(env->bindings[i].name, name) == 0)
            return (worker_d1_t *)env->bindings[i].binding;
    }
    return NULL;
}

WASM_EXPORT
worker_queue_t *worker_env_get_queue(worker_env_t *env, const char *name) {
    if (!env || !name) return NULL;
    for (int i = 0; i < env->count; i++) {
        if (env->bindings[i].type == WORKER_BINDING_QUEUE &&
            strcmp(env->bindings[i].name, name) == 0)
            return (worker_queue_t *)env->bindings[i].binding;
    }
    return NULL;
}

WASM_EXPORT
void worker_env_destroy(worker_env_t *env) {
    if (!env) return;
    for (int i = 0; i < env->count; i++) {
        free(env->bindings[i].name);
        /* Note: bindings themselves are NOT freed - they are owned
         * by the caller.  The env is just a lookup table. */
    }
    free(env);
}

/* ------------------------------------------------------------------ */
/*  Worker capability query                                            */
/* ------------------------------------------------------------------ */

WASM_EXPORT
const char *worker_runtime_version(void) {
    return "weblib-worker/1.0";
}

WASM_EXPORT
bool worker_runtime_is_supported(void) {
    return true;
}
