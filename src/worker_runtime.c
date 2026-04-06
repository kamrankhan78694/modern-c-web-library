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
 *   - worker_request_t  – incoming HTTP request (method, URL, headers, body)
 *   - worker_response_t – outgoing HTTP response (status, headers, body)
 *   - worker_kv_t       – in-memory key-value store
 *   - worker_handle_fetch() – routes a request through a router_t
 *
 * All public symbols are WASM_EXPORT so Emscripten keeps them alive.
 *
 * Copyright (c) 2024 Modern C Web Library
 * Licensed under MIT License
 */

#include "kamran.k"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/*  Internal helpers                                                   */
/* ------------------------------------------------------------------ */

#define WORKER_MAX_METHOD_LEN     16
#define WORKER_MAX_URL_LEN        2048
#define WORKER_MAX_HEADERS        64
#define WORKER_MAX_HEADER_NAME    256
#define WORKER_MAX_HEADER_VALUE   4096
#define WORKER_KV_MAX_ENTRIES     256
#define WORKER_KV_MAX_KEY_LEN     256

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
        if (lib_req.path) {
            memcpy(lib_req.path, req->url, path_len);
            lib_req.path[path_len] = '\0';
        }
        lib_req.query_string = strdup(qs + 1);
    } else {
        lib_req.path = strdup(req->url);
        lib_req.query_string = NULL;
    }

    /* Copy body */
    if (req->body && req->body_len > 0) {
        lib_req.body = (char *)malloc(req->body_len + 1);
        if (lib_req.body) {
            memcpy(lib_req.body, req->body, req->body_len);
            lib_req.body[req->body_len] = '\0';
        }
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
        free(lib_req.path);
        free(lib_req.query_string);
        free(lib_req.body);
        return NULL;
    }

    wres->status = (int)lib_res.status;

    /* Copy response body */
    if (lib_res.body && lib_res.body_length > 0) {
        worker_response_set_body(wres, lib_res.body, lib_res.body_length);
    }

    /* Copy response headers – iterate the linked list via the
     * public header_node structure.  The void* in http_response_t
     * points to the same http_header_node_t list. */
    typedef struct _hn {
        char *name;
        char *raw_name;
        char *value;
        struct _hn *next;
    } _hn_t;

    for (_hn_t *n = (_hn_t *)lib_res.headers; n; n = n->next) {
        const char *hname = n->raw_name ? n->raw_name : n->name;
        worker_response_set_header(wres, hname, n->value);
    }

    /* Free library-level objects (stack-allocated structs, heap fields) */
    free(lib_req.path);
    free(lib_req.query_string);
    free(lib_req.body);

    /* Free header linked lists */
    _hn_t *h = (_hn_t *)lib_req.headers;
    while (h) {
        _hn_t *next = h->next;
        free(h->name);
        free(h->raw_name);
        free(h->value);
        free(h);
        h = next;
    }

    h = (_hn_t *)lib_res.headers;
    while (h) {
        _hn_t *next = h->next;
        free(h->name);
        free(h->raw_name);
        free(h->value);
        free(h);
        h = next;
    }

    /* Free param list */
    typedef struct _pn {
        char *key;
        char *value;
        struct _pn *next;
    } _pn_t;

    _pn_t *p = (_pn_t *)lib_req.params;
    while (p) {
        _pn_t *next = p->next;
        free(p->key);
        free(p->value);
        free(p);
        p = next;
    }

    return wres;
}

/* ------------------------------------------------------------------ */
/*  worker_kv_t – in-memory key-value store                           */
/* ------------------------------------------------------------------ */

struct worker_kv_entry {
    char key[WORKER_KV_MAX_KEY_LEN];
    char *value;
};

struct worker_kv {
    struct worker_kv_entry entries[WORKER_KV_MAX_ENTRIES];
    int count;
};

WASM_EXPORT
worker_kv_t *worker_kv_create(void) {
    worker_kv_t *kv = (worker_kv_t *)calloc(1, sizeof(worker_kv_t));
    return kv;
}

WASM_EXPORT
int worker_kv_put(worker_kv_t *kv, const char *key, const char *value) {
    if (!kv || !key || !value) return -1;

    /* Update existing entry */
    for (int i = 0; i < kv->count; i++) {
        if (strcmp(kv->entries[i].key, key) == 0) {
            char *new_val = strdup(value);
            if (!new_val) return -1;
            free(kv->entries[i].value);
            kv->entries[i].value = new_val;
            return 0;
        }
    }

    /* Add new entry */
    if (kv->count >= WORKER_KV_MAX_ENTRIES) return -1;
    snprintf(kv->entries[kv->count].key, WORKER_KV_MAX_KEY_LEN, "%s", key);
    kv->entries[kv->count].value = strdup(value);
    if (!kv->entries[kv->count].value) return -1;
    kv->count++;
    return 0;
}

WASM_EXPORT
const char *worker_kv_get(worker_kv_t *kv, const char *key) {
    if (!kv || !key) return NULL;
    for (int i = 0; i < kv->count; i++) {
        if (strcmp(kv->entries[i].key, key) == 0)
            return kv->entries[i].value;
    }
    return NULL;
}

WASM_EXPORT
int worker_kv_delete(worker_kv_t *kv, const char *key) {
    if (!kv || !key) return -1;
    for (int i = 0; i < kv->count; i++) {
        if (strcmp(kv->entries[i].key, key) == 0) {
            free(kv->entries[i].value);
            /* Move last entry into the gap */
            if (i < kv->count - 1) {
                kv->entries[i] = kv->entries[kv->count - 1];
            }
            memset(&kv->entries[kv->count - 1], 0,
                   sizeof(kv->entries[0]));
            kv->count--;
            return 0;
        }
    }
    return -1; /* key not found */
}

WASM_EXPORT
void worker_kv_destroy(worker_kv_t *kv) {
    if (!kv) return;
    for (int i = 0; i < kv->count; i++) {
        free(kv->entries[i].value);
    }
    free(kv);
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
