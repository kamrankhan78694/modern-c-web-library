/*
 * worker_example.c - Cloudflare Worker with KV, R2, D1, and Queues
 *
 * Demonstrates the library's Worker runtime bindings AND the three
 * lifecycle exports that the JavaScript glue (examples/worker.js) drives:
 *
 *   worker_init()                 - create bindings, seed data, register the
 *                                   fetch handler (called once at startup)
 *   worker_fetch(method, url)     - handle one fetch event; returns a
 *                                   worker_response_t* the caller destroys
 *   worker_cleanup()              - tear everything down
 *
 * These are WASM_EXPORTed, so the emcc command in worker.js's header links
 * against this file as printed.  The native build wraps the same three
 * functions in a main() that simulates the Worker lifecycle using the
 * in-memory service backends (which are the implementation in every build -
 * no glue to the real Cloudflare services ships in this repo; worker.js
 * does not pass its `env` into WASM).
 *
 * Usage (native): ./worker_example
 *
 * Copyright (c) 2024 Modern C Web Library
 * Licensed under MIT License
 */

#include "kamran.k"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------- Worker state (set up by worker_init, freed by worker_cleanup) ---------- */

static worker_kv_t        *g_kv;
static worker_r2_bucket_t *g_bucket;
static worker_d1_t        *g_db;
static worker_queue_t     *g_queue;
static worker_env_t       *g_env;

/* ---------- Fetch Handler ---------- */

static worker_response_t *handle_fetch(worker_request_t *req,
                                       worker_env_t *env) {
    const char *url = worker_request_get_url(req);
    const char *method = worker_request_get_method(req);
    printf("  [Worker] %s %s\n", method, url);

    /* ---- KV: cache a visit counter ---- */
    worker_kv_t *kv = worker_env_get_binding(env, "CACHE", WORKER_BINDING_KV);
    if (kv) {
        char *visits_str = worker_kv_get(kv, "visit_count");
        int visits = visits_str ? atoi(visits_str) : 0;
        free(visits_str);
        visits++;
        char buf[16];
        snprintf(buf, sizeof(buf), "%d", visits);
        worker_kv_put(kv, "visit_count", buf, NULL);
        printf("  [KV] Visit count: %d\n", visits);
    }

    /* ---- R2: store a log file ---- */
    worker_r2_bucket_t *bucket = worker_env_get_binding(env, "LOGS",
                                                        WORKER_BINDING_R2);
    if (bucket) {
        const char *log_data = "User visited the site";
        worker_r2_put_options_t opts = { .content_type = "text/plain" };
        worker_r2_put(bucket, "access.log", (const uint8_t *)log_data,
                      strlen(log_data), &opts);
        printf("  [R2] Stored access.log (%zu bytes)\n", strlen(log_data));
    }

    /* ---- D1: query the database ---- */
    worker_d1_t *db = worker_env_get_binding(env, "APP_DB", WORKER_BINDING_D1);
    if (db) {
        /* Query all users */
        worker_d1_stmt_t *stmt = worker_d1_prepare(db,
            "SELECT * FROM users");
        json_value_t *rows = worker_d1_stmt_all(stmt);
        if (rows) {
            char *json_str = json_stringify(rows);
            printf("  [D1] Users: %s\n", json_str ? json_str : "[]");
            free(json_str);
            json_value_free(rows);
        }
        worker_d1_stmt_destroy(stmt);
    }

    /* ---- Queues: emit an event ---- */
    worker_queue_t *q = worker_env_get_binding(env, "EVENTS",
                                               WORKER_BINDING_QUEUE);
    if (q) {
        json_value_t *event = json_object_create();
        json_object_set(event, "type", json_string_create("page.view"));
        json_object_set(event, "url", json_string_create(url));
        worker_queue_send_json(q, event);
        json_value_free(event);
        printf("  [Queue] Emitted page.view event (depth: %d)\n",
               worker_queue_get_depth(q));
    }

    /* Build response */
    worker_response_t *res = worker_response_create(200);
    json_value_t *body = json_object_create();
    json_object_set(body, "status", json_string_create("ok"));
    json_object_set(body, "message",
                    json_string_create("Worker processed request"));
    worker_response_set_json(res, body);
    json_value_free(body);
    return res;
}

/* ---------- Lifecycle exports (the contract worker.js drives) ---------- */

/*
 * Create the service bindings, seed the D1 table, and register the fetch
 * handler.  Safe to call once; worker.js calls it on the first request.
 */
WASM_EXPORT void worker_init(void) {
    g_kv = worker_kv_create("CACHE");
    g_bucket = worker_r2_bucket_create("LOGS");
    g_db = worker_d1_create("APP_DB");
    g_queue = worker_queue_create("EVENTS");

    /* Set up D1 schema and seed data.  worker_d1_exec() returns an owned
     * result even for DDL - discard it and it leaks (the same class PR #75
     * fixed in the D1 tests). */
    worker_d1_result_t *ddl = worker_d1_exec(g_db,
        "CREATE TABLE users (id, name, email)");
    worker_d1_result_destroy(ddl);

    worker_d1_stmt_t *ins = worker_d1_prepare(g_db,
        "INSERT INTO users (id, name, email) VALUES (?, ?, ?)");
    worker_d1_stmt_bind(ins, 1, "1");
    worker_d1_stmt_bind(ins, 2, "Alice");
    worker_d1_stmt_bind(ins, 3, "alice@example.com");
    worker_d1_result_t *r = worker_d1_stmt_run(ins);
    worker_d1_result_destroy(r);
    worker_d1_stmt_destroy(ins);

    ins = worker_d1_prepare(g_db,
        "INSERT INTO users (id, name, email) VALUES (?, ?, ?)");
    worker_d1_stmt_bind(ins, 1, "2");
    worker_d1_stmt_bind(ins, 2, "Bob");
    worker_d1_stmt_bind(ins, 3, "bob@example.com");
    r = worker_d1_stmt_run(ins);
    worker_d1_result_destroy(r);
    worker_d1_stmt_destroy(ins);

    /* Create Worker environment with all bindings */
    g_env = worker_env_create();
    worker_env_add_binding(g_env, "CACHE", WORKER_BINDING_KV, g_kv);
    worker_env_add_binding(g_env, "LOGS", WORKER_BINDING_R2, g_bucket);
    worker_env_add_binding(g_env, "APP_DB", WORKER_BINDING_D1, g_db);
    worker_env_add_binding(g_env, "EVENTS", WORKER_BINDING_QUEUE, g_queue);

    /* Register handler */
    worker_set_fetch_handler(handle_fetch);
}

/*
 * Handle one fetch event.  worker.js allocates `method` and `url` in WASM
 * memory and frees them after this returns; the response pointer is read via
 * worker_response_get_status()/get_body() and freed by the caller with
 * worker_response_destroy().  Returns NULL on failure (the glue maps that to
 * a 500).
 */
WASM_EXPORT worker_response_t *worker_fetch(const char *method,
                                            const char *url) {
    worker_request_t *req = worker_request_create(method, url);
    if (!req) {
        return NULL;
    }
    worker_response_t *res = worker_handle_fetch(req, g_env);
    worker_request_destroy(req);
    return res;
}

/* Tear down everything worker_init() created. */
WASM_EXPORT void worker_cleanup(void) {
    worker_set_fetch_handler(NULL);
    worker_env_destroy(g_env);
    worker_queue_destroy(g_queue);
    worker_d1_destroy(g_db);
    worker_r2_bucket_destroy(g_bucket);
    worker_kv_destroy(g_kv);
    g_env = NULL;
    g_queue = NULL;
    g_db = NULL;
    g_bucket = NULL;
    g_kv = NULL;
}

/* ---------- Main (native simulation of the Worker lifecycle) ---------- */

#ifdef __EMSCRIPTEN__
/*
 * Under Emscripten the JavaScript glue drives the Worker entirely through the
 * exports above; main() is not part of the contract, but Emscripten links a
 * trivial one so the printed emcc command works without extra flags.
 */
int main(void) {
    return 0;
}
#else
int main(void) {
    printf("=== Cloudflare Worker Example (native simulation) ===\n\n");

    /* Same call sequence the JS glue performs */
    worker_init();

    /* Simulate incoming fetch events through the exported entry point */
    printf("--- Request 1 ---\n");
    worker_response_t *res1 = worker_fetch("GET", "https://example.com/");
    printf("  Response: %d\n\n", worker_response_get_status(res1));
    worker_response_destroy(res1);

    printf("--- Request 2 ---\n");
    worker_response_t *res2 = worker_fetch("GET", "https://example.com/about");
    printf("  Response: %d\n\n", worker_response_get_status(res2));
    worker_response_destroy(res2);

    /* Consume queued events (native demo only - the glue has no consumer) */
    printf("--- Consuming Queue Events ---\n");
    worker_queue_batch_t *batch = worker_queue_consume(g_queue, 10, 0);
    if (batch) {
        printf("  Consumed %d events:\n", batch->count);
        for (int i = 0; i < batch->count; i++) {
            printf("    [%d] %s\n", i, batch->messages[i]->body);
            worker_queue_message_ack(batch->messages[i]);
        }
        worker_queue_batch_destroy(batch);
    }

    /* Verify KV state */
    printf("\n--- Final KV State ---\n");
    char *vc = worker_kv_get(g_kv, "visit_count");
    printf("  visit_count = %s\n", vc ? vc : "(null)");
    free(vc);

    /* Verify R2 state */
    printf("\n--- Final R2 State ---\n");
    worker_r2_object_t *obj = worker_r2_head(g_bucket, "access.log");
    if (obj) {
        printf("  access.log: %zu bytes, type=%s\n",
               obj->size, obj->content_type ? obj->content_type : "?");
        worker_r2_object_destroy(obj);
    }

    printf("\n=== Done ===\n");

    worker_cleanup();
    return 0;
}
#endif /* __EMSCRIPTEN__ */
