/*
 * worker_example.c - Cloudflare Worker with KV, R2, D1, and Queues
 *
 * Demonstrates how to use the library's Worker runtime bindings to
 * build a Cloudflare Worker that accesses KV, R2, D1, and Queues.
 *
 * In a real deployment this would be compiled to WASM and loaded by
 * a JavaScript glue layer in the Worker.  This native-build example
 * simulates the Worker lifecycle using in-memory service backends.
 *
 * Usage: ./worker_example
 *
 * Copyright (c) 2024 Modern C Web Library
 * Licensed under MIT License
 */

#include "kamran.k"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

/* ---------- Main ---------- */

int main(void) {
    printf("=== Cloudflare Worker Example (native simulation) ===\n\n");

    /* Create services */
    worker_kv_t *kv = worker_kv_create("CACHE");
    worker_r2_bucket_t *bucket = worker_r2_bucket_create("LOGS");
    worker_d1_t *db = worker_d1_create("APP_DB");
    worker_queue_t *queue = worker_queue_create("EVENTS");

    /* Set up D1 schema and seed data */
    worker_d1_exec(db, "CREATE TABLE users (id, name, email)");

    worker_d1_stmt_t *ins = worker_d1_prepare(db,
        "INSERT INTO users (id, name, email) VALUES (?, ?, ?)");
    worker_d1_stmt_bind(ins, 1, "1");
    worker_d1_stmt_bind(ins, 2, "Alice");
    worker_d1_stmt_bind(ins, 3, "alice@example.com");
    worker_d1_result_t *r = worker_d1_stmt_run(ins);
    worker_d1_result_destroy(r);
    worker_d1_stmt_destroy(ins);

    ins = worker_d1_prepare(db,
        "INSERT INTO users (id, name, email) VALUES (?, ?, ?)");
    worker_d1_stmt_bind(ins, 1, "2");
    worker_d1_stmt_bind(ins, 2, "Bob");
    worker_d1_stmt_bind(ins, 3, "bob@example.com");
    r = worker_d1_stmt_run(ins);
    worker_d1_result_destroy(r);
    worker_d1_stmt_destroy(ins);

    /* Create Worker environment with all bindings */
    worker_env_t *env = worker_env_create();
    worker_env_add_binding(env, "CACHE", WORKER_BINDING_KV, kv);
    worker_env_add_binding(env, "LOGS", WORKER_BINDING_R2, bucket);
    worker_env_add_binding(env, "APP_DB", WORKER_BINDING_D1, db);
    worker_env_add_binding(env, "EVENTS", WORKER_BINDING_QUEUE, queue);

    /* Register handler */
    worker_set_fetch_handler(handle_fetch);

    /* Simulate incoming requests */
    printf("--- Request 1 ---\n");
    worker_request_t *req1 = worker_request_create("GET",
        "https://example.com/");
    worker_response_t *res1 = worker_handle_fetch(req1, env);
    printf("  Response: %d\n\n", worker_response_get_status(res1));
    worker_request_destroy(req1);
    worker_response_destroy(res1);

    printf("--- Request 2 ---\n");
    worker_request_t *req2 = worker_request_create("GET",
        "https://example.com/about");
    worker_response_t *res2 = worker_handle_fetch(req2, env);
    printf("  Response: %d\n\n", worker_response_get_status(res2));
    worker_request_destroy(req2);
    worker_response_destroy(res2);

    /* Consume queued events */
    printf("--- Consuming Queue Events ---\n");
    worker_queue_batch_t *batch = worker_queue_consume(queue, 10, 0);
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
    char *vc = worker_kv_get(kv, "visit_count");
    printf("  visit_count = %s\n", vc ? vc : "(null)");
    free(vc);

    /* Verify R2 state */
    printf("\n--- Final R2 State ---\n");
    worker_r2_object_t *obj = worker_r2_head(bucket, "access.log");
    if (obj) {
        printf("  access.log: %zu bytes, type=%s\n",
               obj->size, obj->content_type ? obj->content_type : "?");
        worker_r2_object_destroy(obj);
    }

    printf("\n=== Done ===\n");

    /* Cleanup */
    worker_set_fetch_handler(NULL);
    worker_env_destroy(env);
    worker_queue_destroy(queue);
    worker_d1_destroy(db);
    worker_r2_bucket_destroy(bucket);
    worker_kv_destroy(kv);

    return 0;
}
