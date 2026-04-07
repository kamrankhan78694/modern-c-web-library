/*
 * worker_example.c - Cloudflare Worker Example
 *
 * Demonstrates the Worker runtime API for building HTTP backends
 * that run inside Cloudflare Workers via WASM, including:
 *   - Fetch handler with router integration
 *   - KV namespace with TTL support
 *   - R2 object storage
 *   - D1 edge SQL database
 *   - Queues producer
 *   - Environment context with named bindings
 *
 * Native build (for local testing):
 *   gcc -o worker_example worker_example.c -I../include -L../build -lweblib -lpthread
 *   ./worker_example
 *
 * Emscripten (WASM) build:
 *   emcc -o worker.wasm.js worker_example.c -I../include -L../build-wasm -lweblib \
 *        -sEXPORTED_RUNTIME_METHODS=ccall,cwrap,allocateUTF8,UTF8ToString \
 *        -sEXPORTED_FUNCTIONS=_worker_init,_worker_fetch,_worker_cleanup \
 *        -sWASM=1 -sMODULARIZE=1 -sEXPORT_NAME=createModule
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
static worker_env_t *g_env = NULL;
static worker_kv_t *g_kv = NULL;
static worker_r2_bucket_t *g_r2 = NULL;
static worker_d1_t *g_d1 = NULL;
static worker_queue_t *g_queue = NULL;

/* Exported initialisation — called once when the Worker starts. */
WASM_EXPORT
void worker_init(void) {
    g_router = router_create();
    router_add_route(g_router, HTTP_GET, "/", handle_index);
    router_add_route(g_router, HTTP_GET, "/api/hello", handle_api_hello);
    router_add_route(g_router, HTTP_GET, "/api/users/:id", handle_api_user);

    /* Create Cloudflare infrastructure bindings */
    g_kv = worker_kv_create();
    g_r2 = worker_r2_create();
    g_d1 = worker_d1_create();
    g_queue = worker_queue_create();

    /* Register bindings in env (matches wrangler.toml binding names) */
    g_env = worker_env_create();
    worker_env_bind_kv(g_env, "CACHE", g_kv);
    worker_env_bind_r2(g_env, "ASSETS", g_r2);
    worker_env_bind_d1(g_env, "DB", g_d1);
    worker_env_bind_queue(g_env, "JOBS", g_queue);

    /* Seed some data */
    worker_kv_put(g_kv, "version", "1.0.0");
    worker_kv_put_with_ttl(g_kv, "cache:home", "<h1>Welcome</h1>", 3600);

    worker_d1_exec(g_d1, "CREATE TABLE users (id TEXT, name TEXT, email TEXT)");
    worker_d1_exec(g_d1,
        "INSERT INTO users VALUES ('1', 'Alice', 'alice@example.com')");
    worker_d1_exec(g_d1,
        "INSERT INTO users VALUES ('2', 'Bob', 'bob@example.com')");
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
    if (g_env)    { worker_env_destroy(g_env);     g_env = NULL; }
    if (g_queue)  { worker_queue_destroy(g_queue);  g_queue = NULL; }
    if (g_d1)     { worker_d1_destroy(g_d1);        g_d1 = NULL; }
    if (g_r2)     { worker_r2_destroy(g_r2);        g_r2 = NULL; }
    if (g_kv)     { worker_kv_destroy(g_kv);        g_kv = NULL; }
    if (g_router) { router_destroy(g_router);       g_router = NULL; }
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
    printf("  version = %s\n", worker_kv_get(g_kv, "version"));
    printf("  cache   = %s\n", worker_kv_get(g_kv, "cache:home"));
    worker_kv_put(g_kv, "hits", "1");
    printf("  hits    = %s\n", worker_kv_get(g_kv, "hits"));

    /* R2 demo */
    printf("\n--- R2 Object Storage ---\n");
    const char *file_data = "Hello from R2!";
    worker_r2_put(g_r2, "docs/readme.txt", file_data,
                  strlen(file_data), "text/plain");
    size_t sz = 0;
    const char *obj = worker_r2_get(g_r2, "docs/readme.txt", &sz);
    printf("  docs/readme.txt = %s (%zu bytes)\n", obj, sz);

    /* D1 demo */
    printf("\n--- D1 Database ---\n");
    worker_d1_result_t *result = worker_d1_query(g_d1,
        "SELECT * FROM users");
    if (result && worker_d1_result_is_success(result)) {
        int rows = worker_d1_result_get_row_count(result);
        int cols = worker_d1_result_get_col_count(result);
        printf("  %d rows, %d columns\n", rows, cols);
        for (int r = 0; r < rows; r++) {
            printf("  [%d] id=%s name=%s email=%s\n", r,
                   worker_d1_result_get_value(result, r, 0),
                   worker_d1_result_get_value(result, r, 1),
                   worker_d1_result_get_value(result, r, 2));
        }
    }
    worker_d1_result_destroy(result);

    /* Queue demo */
    printf("\n--- Queues ---\n");
    const char *msg = "{\"task\":\"send-welcome-email\"}";
    worker_queue_send(g_queue, msg, strlen(msg));
    printf("  Queue count: %d\n", worker_queue_get_count(g_queue));
    printf("  Peek[0]: %s\n", worker_queue_peek(g_queue, 0, NULL));

    /* Env binding demo */
    printf("\n--- Env Bindings ---\n");
    worker_kv_t *cache = worker_env_get_kv(g_env, "CACHE");
    printf("  env.CACHE = %p (same as g_kv: %s)\n",
           (void *)cache, cache == g_kv ? "yes" : "no");
    worker_d1_t *db = worker_env_get_d1(g_env, "DB");
    printf("  env.DB    = %p (same as g_d1: %s)\n",
           (void *)db, db == g_d1 ? "yes" : "no");

    worker_cleanup();
    printf("\n=== Done ===\n");
    return 0;
}
