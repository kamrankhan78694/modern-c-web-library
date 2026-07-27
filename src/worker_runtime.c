/*
 * worker_runtime.c - Cloudflare Worker Runtime Compatibility Layer
 *
 * Provides the core Worker fetch handler, request/response types, and
 * environment bindings for Cloudflare Workers.  The C code exposes
 * WASM-exported functions that a JavaScript glue layer in the Worker
 * can call.  NOTE: no such glue ships in this repo, the KV/R2/D1/Queues
 * bindings are in-memory simulations in every build, and worker_handle_fetch()
 * does NOT perform route matching -- see its comment below.
 *
 * The intended shape is that a thin JS wrapper in the Worker receives the fetch
 * event and calls into these WASM exports, and the C layer returns a serialised
 * response.  Note the C layer does NOT dispatch through the router (see above);
 * a fetch handler registered with worker_set_fetch_handler() must branch on the
 * URL itself.
 *
 * Copyright (c) 2024 Modern C Web Library
 * Licensed under MIT License
 */

#include "kamran.k"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ===== Internal limits ===== */
#define WORKER_MAX_HEADERS      64
#define WORKER_MAX_BINDINGS     32
#define WORKER_MAX_BODY_SIZE    (1024 * 1024)  /* 1 MiB */

/* ===== Worker Request ===== */

struct worker_request {
    char *method;
    char *url;
    char *body;
    size_t body_len;
    struct {
        char *name;
        char *value;
    } headers[WORKER_MAX_HEADERS];
    int header_count;
};

worker_request_t *worker_request_create(const char *method, const char *url) {
    if (!method || !url) return NULL;

    worker_request_t *req = calloc(1, sizeof(worker_request_t));
    if (!req) return NULL;

    req->method = strdup(method);
    req->url = strdup(url);
    if (!req->method || !req->url) {
        free(req->method);
        free(req->url);
        free(req);
        return NULL;
    }
    return req;
}

void worker_request_destroy(worker_request_t *req) {
    if (!req) return;
    free(req->method);
    free(req->url);
    free(req->body);
    for (int i = 0; i < req->header_count; i++) {
        free(req->headers[i].name);
        free(req->headers[i].value);
    }
    free(req);
}

int worker_request_set_header(worker_request_t *req, const char *name,
                              const char *value) {
    if (!req || !name || !value) return -1;
    if (req->header_count >= WORKER_MAX_HEADERS) return -1;

    req->headers[req->header_count].name = strdup(name);
    req->headers[req->header_count].value = strdup(value);
    if (!req->headers[req->header_count].name ||
        !req->headers[req->header_count].value) {
        free(req->headers[req->header_count].name);
        free(req->headers[req->header_count].value);
        return -1;
    }
    req->header_count++;
    return 0;
}

const char *worker_request_get_header(const worker_request_t *req,
                                      const char *name) {
    if (!req || !name) return NULL;
    for (int i = 0; i < req->header_count; i++) {
        if (strcasecmp(req->headers[i].name, name) == 0) {
            return req->headers[i].value;
        }
    }
    return NULL;
}

int worker_request_set_body(worker_request_t *req, const char *body,
                            size_t len) {
    if (!req) return -1;
    free(req->body);
    req->body = NULL;
    req->body_len = 0;
    if (body && len > 0) {
        if (len > WORKER_MAX_BODY_SIZE) return -1;
        req->body = malloc(len + 1);
        if (!req->body) return -1;
        memcpy(req->body, body, len);
        req->body[len] = '\0';
        req->body_len = len;
    }
    return 0;
}

const char *worker_request_get_method(const worker_request_t *req) {
    return req ? req->method : NULL;
}

const char *worker_request_get_url(const worker_request_t *req) {
    return req ? req->url : NULL;
}

const char *worker_request_get_body(const worker_request_t *req,
                                    size_t *out_len) {
    if (!req) return NULL;
    if (out_len) *out_len = req->body_len;
    return req->body;
}

/* ===== Worker Response ===== */

struct worker_response {
    int status;
    char *body;
    size_t body_len;
    struct {
        char *name;
        char *value;
    } headers[WORKER_MAX_HEADERS];
    int header_count;
};

worker_response_t *worker_response_create(int status) {
    worker_response_t *res = calloc(1, sizeof(worker_response_t));
    if (!res) return NULL;
    res->status = (status > 0) ? status : 200;
    return res;
}

void worker_response_destroy(worker_response_t *res) {
    if (!res) return;
    free(res->body);
    for (int i = 0; i < res->header_count; i++) {
        free(res->headers[i].name);
        free(res->headers[i].value);
    }
    free(res);
}

int worker_response_set_header(worker_response_t *res, const char *name,
                               const char *value) {
    if (!res || !name || !value) return -1;
    if (res->header_count >= WORKER_MAX_HEADERS) return -1;

    res->headers[res->header_count].name = strdup(name);
    res->headers[res->header_count].value = strdup(value);
    if (!res->headers[res->header_count].name ||
        !res->headers[res->header_count].value) {
        free(res->headers[res->header_count].name);
        free(res->headers[res->header_count].value);
        return -1;
    }
    res->header_count++;
    return 0;
}

const char *worker_response_get_header(const worker_response_t *res,
                                       const char *name) {
    if (!res || !name) return NULL;
    for (int i = 0; i < res->header_count; i++) {
        if (strcasecmp(res->headers[i].name, name) == 0) {
            return res->headers[i].value;
        }
    }
    return NULL;
}

int worker_response_set_body(worker_response_t *res, const char *body,
                             size_t len) {
    if (!res) return -1;
    free(res->body);
    res->body = NULL;
    res->body_len = 0;
    if (body && len > 0) {
        res->body = malloc(len + 1);
        if (!res->body) return -1;
        memcpy(res->body, body, len);
        res->body[len] = '\0';
        res->body_len = len;
    }
    return 0;
}

int worker_response_set_body_text(worker_response_t *res, const char *text) {
    if (!text) return worker_response_set_body(res, NULL, 0);
    return worker_response_set_body(res, text, strlen(text));
}

int worker_response_set_json(worker_response_t *res, json_value_t *json) {
    if (!res || !json) return -1;
    char *str = json_stringify(json);
    if (!str) return -1;
    worker_response_set_header(res, "Content-Type", "application/json");
    int rc = worker_response_set_body(res, str, strlen(str));
    free(str);
    return rc;
}

int worker_response_get_status(const worker_response_t *res) {
    return res ? res->status : 0;
}

const char *worker_response_get_body(const worker_response_t *res,
                                     size_t *out_len) {
    if (!res) return NULL;
    if (out_len) *out_len = res->body_len;
    return res->body;
}

/* ===== Worker Environment (bindings container) ===== */

struct worker_env {
    struct {
        char *name;
        worker_binding_type_t type;
        void *handle;  /* opaque pointer to the binding */
    } bindings[WORKER_MAX_BINDINGS];
    int binding_count;
};

worker_env_t *worker_env_create(void) {
    return calloc(1, sizeof(worker_env_t));
}

void worker_env_destroy(worker_env_t *env) {
    if (!env) return;
    for (int i = 0; i < env->binding_count; i++) {
        free(env->bindings[i].name);
        /* handles are NOT owned by env — caller manages them */
    }
    free(env);
}

int worker_env_add_binding(worker_env_t *env, const char *name,
                           worker_binding_type_t type, void *handle) {
    if (!env || !name || !handle) return -1;
    if (env->binding_count >= WORKER_MAX_BINDINGS) return -1;

    env->bindings[env->binding_count].name = strdup(name);
    if (!env->bindings[env->binding_count].name) return -1;
    env->bindings[env->binding_count].type = type;
    env->bindings[env->binding_count].handle = handle;
    env->binding_count++;
    return 0;
}

void *worker_env_get_binding(const worker_env_t *env, const char *name,
                             worker_binding_type_t type) {
    if (!env || !name) return NULL;
    for (int i = 0; i < env->binding_count; i++) {
        if (env->bindings[i].type == type &&
            strcmp(env->bindings[i].name, name) == 0) {
            return env->bindings[i].handle;
        }
    }
    return NULL;
}

int worker_env_binding_count(const worker_env_t *env) {
    return env ? env->binding_count : 0;
}

/* ===== Fetch Handler ===== */

static worker_fetch_handler_t _global_fetch_handler = NULL;
static router_t               *_global_worker_router = NULL;

void worker_set_fetch_handler(worker_fetch_handler_t handler) {
    _global_fetch_handler = handler;
}

void worker_set_router(router_t *router) {
    _global_worker_router = router;
}

WASM_EXPORT
worker_response_t *worker_handle_fetch(worker_request_t *req,
                                       worker_env_t *env) {
    if (!req) return worker_response_create(400);

    /* If a custom fetch handler is registered, delegate entirely */
    if (_global_fetch_handler) {
        return _global_fetch_handler(req, env);
    }

    /* Otherwise, build a response and try the router */
    worker_response_t *res = worker_response_create(200);
    if (!res) return NULL;

    if (_global_worker_router) {
        /* The native simulation in this file does not inspect the URL
         * path or consult the router's route table, so avoid claiming
         * that a route was matched.  Report only that a router is
         * configured for this runtime. */
        worker_response_set_header(res, "X-Worker-Routed", "configured");
        worker_response_set_body_text(
            res,
            "Router configured; native worker simulation does not perform route matching");
    } else {
        res->status = 503;
        worker_response_set_body_text(res, "No router or handler configured");
    }

    return res;
}
