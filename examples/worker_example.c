/*
 * worker_example.c - Cloudflare Worker Example
 *
 * Demonstrates the Worker runtime API for building HTTP backends
 * that run inside Cloudflare Workers via WASM.
 *
 * Native build (for local testing):
 *   gcc -o worker_example worker_example.c -I../include -L../build -lweblib -lpthread
 *   ./worker_example
 *
 * Emscripten (WASM) build:
 *   emcc -o worker.js worker_example.c -I../include -L../build-wasm -lweblib \
 *        -sEXPORTED_RUNTIME_METHODS=ccall,cwrap \
 *        -sEXPORTED_FUNCTIONS=_worker_init,_worker_fetch,_worker_cleanup \
 *        -sWASM=1
 *
 * Copyright (c) 2024 Modern C Web Library
 * Licensed under MIT License
 */

#include "kamran.k"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ===== Route Handlers ===== */

static void handle_index(http_request_t *req, http_response_t *res) {
    (void)req;
    http_response_send_text(res, HTTP_OK,
        "Welcome to Modern C Web Library on Cloudflare Workers!");
}

static void handle_api_hello(http_request_t *req, http_response_t *res) {
    (void)req;
    json_value_t *json = json_object_create();
    json_object_set(json, "message", json_string_create("Hello from Worker!"));
    json_object_set(json, "runtime", json_string_create(worker_runtime_version()));
    json_object_set(json, "wasm", json_bool_create(true));
    http_response_send_json(res, HTTP_OK, json);
    json_value_free(json);
}

static void handle_api_user(http_request_t *req, http_response_t *res) {
    const char *id = http_request_get_param(req, "id");
    json_value_t *json = json_object_create();
    json_object_set(json, "id", json_string_create(id ? id : "unknown"));
    json_object_set(json, "name", json_string_create("Example User"));
    http_response_send_json(res, HTTP_OK, json);
    json_value_free(json);
}

/* ===== Global State ===== */

static router_t *g_router = NULL;
static worker_kv_t *g_kv = NULL;

/* Exported initialisation — called once when the Worker starts. */
WASM_EXPORT
void worker_init(void) {
    g_router = router_create();
    router_add_route(g_router, HTTP_GET, "/", handle_index);
    router_add_route(g_router, HTTP_GET, "/api/hello", handle_api_hello);
    router_add_route(g_router, HTTP_GET, "/api/users/:id", handle_api_user);

    g_kv = worker_kv_create();
    worker_kv_put(g_kv, "version", "1.0.0");
}

/*
 * Exported fetch handler — called for every incoming request.
 * Returns a worker_response_t* that the JS glue reads and
 * converts into a Response object.
 */
WASM_EXPORT
worker_response_t *worker_fetch(const char *method, const char *url) {
    worker_request_t *req = worker_request_create(method, url);
    if (!req) return NULL;

    worker_response_t *res = worker_handle_fetch(req, g_router);
    worker_request_destroy(req);
    return res;
}

/* Exported cleanup — called on Worker shutdown. */
WASM_EXPORT
void worker_cleanup(void) {
    if (g_kv)     { worker_kv_destroy(g_kv);     g_kv = NULL; }
    if (g_router) { router_destroy(g_router);     g_router = NULL; }
}

/* ===== Native local-test harness ===== */

int main(void) {
    printf("=== Cloudflare Worker Example (native test) ===\n\n");

    worker_init();

    printf("Runtime: %s  (supported: %s)\n\n",
           worker_runtime_version(),
           worker_runtime_is_supported() ? "yes" : "no");

    /* Simulate fetch events */
    const char *urls[] = { "/", "/api/hello", "/api/users/42", "/not-found" };
    const char *methods[] = { "GET", "GET", "GET", "GET" };
    int n = (int)(sizeof(urls) / sizeof(urls[0]));

    for (int i = 0; i < n; i++) {
        printf("--- %s %s ---\n", methods[i], urls[i]);
        worker_response_t *res = worker_fetch(methods[i], urls[i]);
        if (res) {
            printf("  Status: %d\n", worker_response_get_status(res));
            const char *body = worker_response_get_body(res);
            printf("  Body:   %s\n", body ? body : "(empty)");
            int hc = worker_response_get_header_count(res);
            for (int j = 0; j < hc; j++) {
                printf("  Header: %s: %s\n",
                       worker_response_get_header_name(res, j),
                       worker_response_get_header_value(res, j));
            }
            worker_response_destroy(res);
        } else {
            printf("  (null response)\n");
        }
        printf("\n");
    }

    /* KV store demo */
    printf("--- KV Store ---\n");
    const char *ver = worker_kv_get(g_kv, "version");
    printf("  version = %s\n", ver ? ver : "(null)");
    worker_kv_put(g_kv, "hits", "1");
    printf("  hits    = %s\n", worker_kv_get(g_kv, "hits"));

    worker_cleanup();
    printf("\n=== Done ===\n");
    return 0;
}
