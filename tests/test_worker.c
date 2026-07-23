/*
 * test_worker.c - Tests for Cloudflare Worker Runtime & Infrastructure Bindings
 *
 * Validates the Worker fetch handler, environment bindings, and all
 * Cloudflare infrastructure services: KV, R2, D1, and Queues.
 * Runs on native builds using the in-memory simulation backends.
 *
 * Copyright (c) 2024 Modern C Web Library
 * Licensed under MIT License
 */

#include "kamran.k"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Test counters */
static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) \
    printf("Testing %s... ", name); \
    tests_run++;

#define ASSERT(condition) \
    if (!(condition)) { \
        printf("FAILED at line %d: %s\n", __LINE__, #condition); \
        return; \
    }

#define PASS() \
    printf("PASSED\n"); \
    tests_passed++;

/* ========================================================
 * Worker Request Tests
 * ======================================================== */

static void test_worker_request_create(void) {
    TEST("worker_request_create");
    worker_request_t *req = worker_request_create("GET", "https://example.com/api");
    ASSERT(req != NULL);
    ASSERT(strcmp(worker_request_get_method(req), "GET") == 0);
    ASSERT(strcmp(worker_request_get_url(req), "https://example.com/api") == 0);
    worker_request_destroy(req);
    PASS();
}

static void test_worker_request_null_args(void) {
    TEST("worker_request_null_args");
    ASSERT(worker_request_create(NULL, "url") == NULL);
    ASSERT(worker_request_create("GET", NULL) == NULL);
    PASS();
}

static void test_worker_request_headers(void) {
    TEST("worker_request_headers");
    worker_request_t *req = worker_request_create("POST", "/data");
    ASSERT(req != NULL);
    ASSERT(worker_request_set_header(req, "Content-Type", "application/json") == 0);
    ASSERT(worker_request_set_header(req, "Authorization", "Bearer tok") == 0);
    ASSERT(strcmp(worker_request_get_header(req, "content-type"), "application/json") == 0);
    ASSERT(strcmp(worker_request_get_header(req, "AUTHORIZATION"), "Bearer tok") == 0);
    ASSERT(worker_request_get_header(req, "X-Missing") == NULL);
    worker_request_destroy(req);
    PASS();
}

static void test_worker_request_body(void) {
    TEST("worker_request_body");
    worker_request_t *req = worker_request_create("POST", "/upload");
    ASSERT(req != NULL);
    ASSERT(worker_request_set_body(req, "hello world", 11) == 0);
    size_t len = 0;
    const char *body = worker_request_get_body(req, &len);
    ASSERT(body != NULL);
    ASSERT(len == 11);
    ASSERT(memcmp(body, "hello world", 11) == 0);
    worker_request_destroy(req);
    PASS();
}

/* ========================================================
 * Worker Response Tests
 * ======================================================== */

static void test_worker_response_create(void) {
    TEST("worker_response_create");
    worker_response_t *res = worker_response_create(200);
    ASSERT(res != NULL);
    ASSERT(worker_response_get_status(res) == 200);
    worker_response_destroy(res);
    PASS();
}

static void test_worker_response_body_text(void) {
    TEST("worker_response_body_text");
    worker_response_t *res = worker_response_create(200);
    ASSERT(worker_response_set_body_text(res, "OK") == 0);
    size_t len = 0;
    const char *body = worker_response_get_body(res, &len);
    ASSERT(body != NULL);
    ASSERT(len == 2);
    ASSERT(memcmp(body, "OK", 2) == 0);
    worker_response_destroy(res);
    PASS();
}

static void test_worker_response_json(void) {
    TEST("worker_response_json");
    worker_response_t *res = worker_response_create(200);
    json_value_t *j = json_object_create();
    json_object_set(j, "status", json_string_create("ok"));
    ASSERT(worker_response_set_json(res, j) == 0);
    ASSERT(strcmp(worker_response_get_header(res, "Content-Type"),
                  "application/json") == 0);
    size_t len = 0;
    const char *body = worker_response_get_body(res, &len);
    ASSERT(body != NULL);
    ASSERT(strstr(body, "status") != NULL);
    json_value_free(j);
    worker_response_destroy(res);
    PASS();
}

/* ========================================================
 * Worker Environment Tests
 * ======================================================== */

static void test_worker_env_bindings(void) {
    TEST("worker_env_bindings");
    worker_env_t *env = worker_env_create();
    ASSERT(env != NULL);
    ASSERT(worker_env_binding_count(env) == 0);

    worker_kv_t *kv = worker_kv_create("MY_KV");
    ASSERT(worker_env_add_binding(env, "MY_KV", WORKER_BINDING_KV, kv) == 0);
    ASSERT(worker_env_binding_count(env) == 1);

    void *got = worker_env_get_binding(env, "MY_KV", WORKER_BINDING_KV);
    ASSERT(got == kv);
    ASSERT(worker_env_get_binding(env, "MISSING", WORKER_BINDING_KV) == NULL);

    worker_kv_destroy(kv);
    worker_env_destroy(env);
    PASS();
}

/* ========================================================
 * Worker Fetch Handler Tests
 * ======================================================== */

static void test_worker_fetch_no_router(void) {
    TEST("worker_fetch_no_router");
    worker_set_fetch_handler(NULL);
    worker_set_router(NULL);

    worker_request_t *req = worker_request_create("GET", "/test");
    worker_response_t *res = worker_handle_fetch(req, NULL);
    ASSERT(res != NULL);
    ASSERT(worker_response_get_status(res) == 503);
    worker_request_destroy(req);
    worker_response_destroy(res);
    PASS();
}

static void _dummy_handler(http_request_t *req, http_response_t *res) {
    (void)req; (void)res;
}

static void test_worker_fetch_with_router(void) {
    TEST("worker_fetch_with_router");
    router_t *router = router_create();
    router_add_route(router, HTTP_GET, "/hello", _dummy_handler);
    worker_set_router(router);
    worker_set_fetch_handler(NULL);

    worker_request_t *req = worker_request_create("GET", "https://example.com/hello");
    worker_response_t *res = worker_handle_fetch(req, NULL);
    ASSERT(res != NULL);
    ASSERT(worker_response_get_status(res) == 200);
    ASSERT(strcmp(worker_response_get_header(res, "X-Worker-Routed"), "configured") == 0);

    worker_request_destroy(req);
    worker_response_destroy(res);

    worker_set_router(NULL);
    router_destroy(router);
    PASS();
}

static worker_response_t *_custom_handler(worker_request_t *req,
                                          worker_env_t *env) {
    (void)env;
    worker_response_t *res = worker_response_create(201);
    const char *method = worker_request_get_method(req);
    worker_response_set_body_text(res, method);
    return res;
}

static void test_worker_fetch_custom_handler(void) {
    TEST("worker_fetch_custom_handler");
    worker_set_fetch_handler(_custom_handler);

    worker_request_t *req = worker_request_create("POST", "/anything");
    worker_response_t *res = worker_handle_fetch(req, NULL);
    ASSERT(res != NULL);
    ASSERT(worker_response_get_status(res) == 201);
    size_t len = 0;
    const char *body = worker_response_get_body(res, &len);
    ASSERT(body != NULL && strcmp(body, "POST") == 0);

    worker_request_destroy(req);
    worker_response_destroy(res);
    worker_set_fetch_handler(NULL);
    PASS();
}

/* ========================================================
 * KV Namespace Tests
 * ======================================================== */

static void test_kv_create_destroy(void) {
    TEST("kv_create_destroy");
    worker_kv_t *kv = worker_kv_create("TEST_NS");
    ASSERT(kv != NULL);
    ASSERT(strcmp(worker_kv_get_namespace(kv), "TEST_NS") == 0);
    worker_kv_destroy(kv);
    ASSERT(worker_kv_create(NULL) == NULL);
    PASS();
}

static void test_kv_put_get(void) {
    TEST("kv_put_get");
    worker_kv_t *kv = worker_kv_create("NS");
    ASSERT(worker_kv_put(kv, "key1", "value1", NULL) == 0);
    char *val = worker_kv_get(kv, "key1");
    ASSERT(val != NULL);
    ASSERT(strcmp(val, "value1") == 0);
    free(val);

    /* Overwrite */
    ASSERT(worker_kv_put(kv, "key1", "updated", NULL) == 0);
    val = worker_kv_get(kv, "key1");
    ASSERT(strcmp(val, "updated") == 0);
    free(val);

    /* Not found */
    ASSERT(worker_kv_get(kv, "missing") == NULL);

    worker_kv_destroy(kv);
    PASS();
}

static void test_kv_metadata(void) {
    TEST("kv_metadata");
    worker_kv_t *kv = worker_kv_create("NS");
    worker_kv_put_options_t opts = {0};
    opts.metadata = "{\"user\": 42}";
    ASSERT(worker_kv_put(kv, "k", "v", &opts) == 0);

    char *meta = NULL;
    char *val = worker_kv_get_with_metadata(kv, "k", &meta);
    ASSERT(val != NULL && strcmp(val, "v") == 0);
    ASSERT(meta != NULL && strstr(meta, "42") != NULL);
    free(val);
    free(meta);

    worker_kv_destroy(kv);
    PASS();
}

static void test_kv_delete(void) {
    TEST("kv_delete");
    worker_kv_t *kv = worker_kv_create("NS");
    worker_kv_put(kv, "a", "1", NULL);
    worker_kv_put(kv, "b", "2", NULL);
    ASSERT(worker_kv_delete(kv, "a") == 0);
    ASSERT(worker_kv_get(kv, "a") == NULL);

    char *val = worker_kv_get(kv, "b");
    ASSERT(val != NULL && strcmp(val, "2") == 0);
    free(val);

    ASSERT(worker_kv_delete(kv, "nonexistent") == -1);

    worker_kv_destroy(kv);
    PASS();
}

static void test_kv_list(void) {
    TEST("kv_list");
    worker_kv_t *kv = worker_kv_create("NS");
    worker_kv_put(kv, "users:1", "alice", NULL);
    worker_kv_put(kv, "users:2", "bob", NULL);
    worker_kv_put(kv, "posts:1", "hello", NULL);

    /* List all */
    worker_kv_list_result_t *all = worker_kv_list(kv, NULL);
    ASSERT(all != NULL);
    ASSERT(all->count == 3);
    ASSERT(all->list_complete == true);
    worker_kv_list_result_destroy(all);

    /* List with prefix */
    worker_kv_list_options_t opts = {0};
    opts.prefix = "users:";
    worker_kv_list_result_t *users = worker_kv_list(kv, &opts);
    ASSERT(users != NULL);
    ASSERT(users->count == 2);
    worker_kv_list_result_destroy(users);

    /* List with limit */
    worker_kv_list_options_t opts2 = {0};
    opts2.limit = 1;
    worker_kv_list_result_t *limited = worker_kv_list(kv, &opts2);
    ASSERT(limited != NULL);
    ASSERT(limited->count == 1);
    ASSERT(limited->list_complete == false);
    worker_kv_list_result_destroy(limited);

    worker_kv_destroy(kv);
    PASS();
}

/* ========================================================
 * R2 Object Storage Tests
 * ======================================================== */

static void test_r2_create_destroy(void) {
    TEST("r2_create_destroy");
    worker_r2_bucket_t *b = worker_r2_bucket_create("my-bucket");
    ASSERT(b != NULL);
    ASSERT(strcmp(worker_r2_bucket_get_name(b), "my-bucket") == 0);
    worker_r2_bucket_destroy(b);
    ASSERT(worker_r2_bucket_create(NULL) == NULL);
    PASS();
}

static void test_r2_put_get(void) {
    TEST("r2_put_get");
    worker_r2_bucket_t *b = worker_r2_bucket_create("bucket");
    const char *data = "Hello R2!";
    worker_r2_put_options_t opts = { .content_type = "text/plain" };
    ASSERT(worker_r2_put(b, "greeting.txt", (const uint8_t *)data, 9, &opts) == 0);

    worker_r2_object_t *obj = worker_r2_get(b, "greeting.txt");
    ASSERT(obj != NULL);
    ASSERT(obj->body != NULL);
    ASSERT(obj->body_len == 9);
    ASSERT(memcmp(obj->body, "Hello R2!", 9) == 0);
    ASSERT(obj->size == 9);
    ASSERT(obj->etag != NULL);
    ASSERT(obj->content_type != NULL && strcmp(obj->content_type, "text/plain") == 0);
    worker_r2_object_destroy(obj);

    /* Not found */
    ASSERT(worker_r2_get(b, "missing") == NULL);

    worker_r2_bucket_destroy(b);
    PASS();
}

static void test_r2_head(void) {
    TEST("r2_head");
    worker_r2_bucket_t *b = worker_r2_bucket_create("bucket");
    worker_r2_put(b, "file.bin", (const uint8_t *)"data", 4, NULL);

    worker_r2_object_t *obj = worker_r2_head(b, "file.bin");
    ASSERT(obj != NULL);
    ASSERT(obj->size == 4);
    ASSERT(obj->body == NULL);  /* head: no body */
    worker_r2_object_destroy(obj);

    ASSERT(worker_r2_head(b, "nope") == NULL);
    worker_r2_bucket_destroy(b);
    PASS();
}

static void test_r2_delete(void) {
    TEST("r2_delete");
    worker_r2_bucket_t *b = worker_r2_bucket_create("bucket");
    worker_r2_put(b, "temp.txt", (const uint8_t *)"tmp", 3, NULL);
    ASSERT(worker_r2_delete(b, "temp.txt") == 0);
    ASSERT(worker_r2_get(b, "temp.txt") == NULL);
    ASSERT(worker_r2_delete(b, "temp.txt") == -1);
    worker_r2_bucket_destroy(b);
    PASS();
}

static void test_r2_list(void) {
    TEST("r2_list");
    worker_r2_bucket_t *b = worker_r2_bucket_create("bucket");
    worker_r2_put(b, "img/a.png", (const uint8_t *)"A", 1, NULL);
    worker_r2_put(b, "img/b.png", (const uint8_t *)"B", 1, NULL);
    worker_r2_put(b, "doc/c.pdf", (const uint8_t *)"C", 1, NULL);

    /* List all */
    worker_r2_list_result_t *all = worker_r2_list(b, NULL);
    ASSERT(all != NULL);
    ASSERT(all->count == 3);
    worker_r2_list_result_destroy(all);

    /* Prefix filter */
    worker_r2_list_options_t opts = { .prefix = "img/" };
    worker_r2_list_result_t *imgs = worker_r2_list(b, &opts);
    ASSERT(imgs != NULL);
    ASSERT(imgs->count == 2);
    worker_r2_list_result_destroy(imgs);

    worker_r2_bucket_destroy(b);
    PASS();
}

/* ========================================================
 * D1 Database Tests
 * ======================================================== */

static void test_d1_create_destroy(void) {
    TEST("d1_create_destroy");
    worker_d1_t *db = worker_d1_create("test-db");
    ASSERT(db != NULL);
    ASSERT(strcmp(worker_d1_get_name(db), "test-db") == 0);
    worker_d1_destroy(db);
    ASSERT(worker_d1_create(NULL) == NULL);
    PASS();
}

static void test_d1_create_table_and_insert(void) {
    TEST("d1_create_table_and_insert");
    worker_d1_t *db = worker_d1_create("testdb");

    /* CREATE TABLE */
    worker_d1_result_t *r = worker_d1_exec(db,
        "CREATE TABLE users (id, name, email)");
    ASSERT(r != NULL);
    ASSERT(r->success == true);
    worker_d1_result_destroy(r);

    /* INSERT */
    worker_d1_stmt_t *stmt = worker_d1_prepare(db,
        "INSERT INTO users (id, name, email) VALUES (?, ?, ?)");
    ASSERT(stmt != NULL);
    worker_d1_stmt_bind(stmt, 1, "1");
    worker_d1_stmt_bind(stmt, 2, "Alice");
    worker_d1_stmt_bind(stmt, 3, "alice@test.com");
    r = worker_d1_stmt_run(stmt);
    ASSERT(r != NULL);
    ASSERT(r->success == true);
    ASSERT(r->meta_changes == 1);
    worker_d1_result_destroy(r);
    worker_d1_stmt_destroy(stmt);

    worker_d1_destroy(db);
    PASS();
}

static void test_d1_select(void) {
    TEST("d1_select");
    worker_d1_t *db = worker_d1_create("testdb");
    worker_d1_result_destroy(worker_d1_exec(db, "CREATE TABLE items (id, title)"));

    /* Insert two rows */
    worker_d1_stmt_t *ins = worker_d1_prepare(db,
        "INSERT INTO items (id, title) VALUES (?, ?)");
    worker_d1_stmt_bind(ins, 1, "1");
    worker_d1_stmt_bind(ins, 2, "First");
    worker_d1_result_t *r1 = worker_d1_stmt_run(ins);
    worker_d1_result_destroy(r1);
    worker_d1_stmt_destroy(ins);

    ins = worker_d1_prepare(db, "INSERT INTO items (id, title) VALUES (?, ?)");
    worker_d1_stmt_bind(ins, 1, "2");
    worker_d1_stmt_bind(ins, 2, "Second");
    r1 = worker_d1_stmt_run(ins);
    worker_d1_result_destroy(r1);
    worker_d1_stmt_destroy(ins);

    /* SELECT all */
    worker_d1_stmt_t *sel = worker_d1_prepare(db, "SELECT * FROM items");
    json_value_t *all = worker_d1_stmt_all(sel);
    ASSERT(all != NULL);
    /* Should have two rows */
    char *str = json_stringify(all);
    ASSERT(str != NULL);
    ASSERT(strstr(str, "First") != NULL);
    ASSERT(strstr(str, "Second") != NULL);
    free(str);
    json_value_free(all);
    worker_d1_stmt_destroy(sel);

    /* SELECT with WHERE */
    sel = worker_d1_prepare(db, "SELECT * FROM items WHERE id = ?");
    worker_d1_stmt_bind(sel, 1, "1");
    json_value_t *first = worker_d1_stmt_first(sel);
    ASSERT(first != NULL);
    str = json_stringify(first);
    ASSERT(strstr(str, "First") != NULL);
    free(str);
    json_value_free(first);
    worker_d1_stmt_destroy(sel);

    worker_d1_destroy(db);
    PASS();
}

static void test_d1_delete_rows(void) {
    TEST("d1_delete_rows");
    worker_d1_t *db = worker_d1_create("testdb");
    worker_d1_result_destroy(worker_d1_exec(db, "CREATE TABLE logs (id, msg)"));

    worker_d1_stmt_t *ins = worker_d1_prepare(db,
        "INSERT INTO logs (id, msg) VALUES (?, ?)");
    worker_d1_stmt_bind(ins, 1, "1");
    worker_d1_stmt_bind(ins, 2, "log1");
    worker_d1_result_t *r = worker_d1_stmt_run(ins);
    worker_d1_result_destroy(r);
    worker_d1_stmt_destroy(ins);

    ins = worker_d1_prepare(db, "INSERT INTO logs (id, msg) VALUES (?, ?)");
    worker_d1_stmt_bind(ins, 1, "2");
    worker_d1_stmt_bind(ins, 2, "log2");
    r = worker_d1_stmt_run(ins);
    worker_d1_result_destroy(r);
    worker_d1_stmt_destroy(ins);

    /* DELETE one row */
    worker_d1_stmt_t *del = worker_d1_prepare(db,
        "DELETE FROM logs WHERE id = ?");
    worker_d1_stmt_bind(del, 1, "1");
    r = worker_d1_stmt_run(del);
    ASSERT(r != NULL && r->success && r->meta_changes == 1);
    worker_d1_result_destroy(r);
    worker_d1_stmt_destroy(del);

    /* Verify only one row remains */
    worker_d1_stmt_t *sel = worker_d1_prepare(db, "SELECT * FROM logs");
    json_value_t *rows = worker_d1_stmt_all(sel);
    char *str = json_stringify(rows);
    ASSERT(strstr(str, "log2") != NULL);
    ASSERT(strstr(str, "log1") == NULL);
    free(str);
    json_value_free(rows);
    worker_d1_stmt_destroy(sel);

    worker_d1_destroy(db);
    PASS();
}

static void test_d1_batch(void) {
    TEST("d1_batch");
    worker_d1_t *db = worker_d1_create("testdb");
    worker_d1_result_destroy(worker_d1_exec(db, "CREATE TABLE batch_test (id, val)"));

    worker_d1_stmt_t *s1 = worker_d1_prepare(db,
        "INSERT INTO batch_test (id, val) VALUES (?, ?)");
    worker_d1_stmt_bind(s1, 1, "A");
    worker_d1_stmt_bind(s1, 2, "100");

    worker_d1_stmt_t *s2 = worker_d1_prepare(db,
        "INSERT INTO batch_test (id, val) VALUES (?, ?)");
    worker_d1_stmt_bind(s2, 1, "B");
    worker_d1_stmt_bind(s2, 2, "200");

    worker_d1_stmt_t *stmts[] = {s1, s2};
    int out_count = 0;
    worker_d1_result_t **results = worker_d1_batch(db, stmts, 2, &out_count);
    ASSERT(results != NULL);
    ASSERT(out_count == 2);
    ASSERT(results[0]->success == true);
    ASSERT(results[1]->success == true);

    for (int i = 0; i < out_count; i++) worker_d1_result_destroy(results[i]);
    free(results);
    worker_d1_stmt_destroy(s1);
    worker_d1_stmt_destroy(s2);
    worker_d1_destroy(db);
    PASS();
}

/* Security regression: INSERT must map values to NAMED columns (not physical
 * order), and a WHERE clause with an unknown column must NOT become a
 * DELETE-all / SELECT-all. */
static void test_d1_sql_safety(void) {
    TEST("d1_sql_safety (named-column INSERT, unknown-WHERE guard)");
    worker_d1_t *db = worker_d1_create("safedb");
    ASSERT(db != NULL);
    worker_d1_result_destroy(worker_d1_exec(db, "CREATE TABLE t (a, b)"));

    /* Column list is REORDERED (b, a): values must land in the named columns. */
    worker_d1_stmt_t *ins = worker_d1_prepare(db, "INSERT INTO t (b, a) VALUES (?, ?)");
    ASSERT(ins != NULL);
    worker_d1_stmt_bind(ins, 1, "bee");   /* -> column b */
    worker_d1_stmt_bind(ins, 2, "aye");   /* -> column a */
    worker_d1_result_t *ir = worker_d1_stmt_run(ins);
    ASSERT(ir != NULL && ir->success);
    worker_d1_result_destroy(ir);
    worker_d1_stmt_destroy(ins);

    worker_d1_stmt_t *sel = worker_d1_prepare(db, "SELECT * FROM t");
    json_value_t *rows = worker_d1_stmt_all(sel);
    ASSERT(rows != NULL);
    char *js = json_stringify(rows);
    ASSERT(js != NULL);
    ASSERT(strstr(js, "\"a\":\"aye\"") != NULL);   /* mapped to named col a */
    ASSERT(strstr(js, "\"b\":\"bee\"") != NULL);   /* mapped to named col b */
    free(js);
    json_value_free(rows);
    worker_d1_stmt_destroy(sel);

    /* DELETE with an unknown WHERE column must be refused, not delete-all. */
    worker_d1_stmt_t *del = worker_d1_prepare(db, "DELETE FROM t WHERE nosuchcol = ?");
    worker_d1_stmt_bind(del, 1, "x");
    worker_d1_result_t *dr = worker_d1_stmt_run(del);
    ASSERT(dr != NULL);
    ASSERT(dr->success == false || dr->meta_changes == 0);
    worker_d1_result_destroy(dr);
    worker_d1_stmt_destroy(del);

    /* The row must still be present (was NOT wiped). */
    sel = worker_d1_prepare(db, "SELECT * FROM t");
    rows = worker_d1_stmt_all(sel);
    ASSERT(rows != NULL);
    js = json_stringify(rows);
    ASSERT(js != NULL && strstr(js, "aye") != NULL);
    free(js);
    json_value_free(rows);
    worker_d1_stmt_destroy(sel);

    /* SELECT with an unknown WHERE column must fail, not return every row. */
    worker_d1_stmt_t *bsel = worker_d1_prepare(db, "SELECT * FROM t WHERE nosuchcol = ?");
    worker_d1_stmt_bind(bsel, 1, "x");
    json_value_t *brows = worker_d1_stmt_all(bsel);
    if (brows != NULL) {
        char *bjs = json_stringify(brows);
        ASSERT(bjs == NULL || strstr(bjs, "aye") == NULL);
        free(bjs);
        json_value_free(brows);
    }
    worker_d1_stmt_destroy(bsel);

    worker_d1_destroy(db);
    PASS();
}

/* ========================================================
 * Queues Tests
 * ======================================================== */

static void test_queue_create_destroy(void) {
    TEST("queue_create_destroy");
    worker_queue_t *q = worker_queue_create("my-queue");
    ASSERT(q != NULL);
    ASSERT(strcmp(worker_queue_get_name(q), "my-queue") == 0);
    ASSERT(worker_queue_get_depth(q) == 0);
    worker_queue_destroy(q);
    ASSERT(worker_queue_create(NULL) == NULL);
    PASS();
}

static void test_queue_send_consume(void) {
    TEST("queue_send_consume");
    worker_queue_t *q = worker_queue_create("test-q");

    ASSERT(worker_queue_send_text(q, "msg1") == 0);
    ASSERT(worker_queue_send_text(q, "msg2") == 0);
    ASSERT(worker_queue_send_text(q, "msg3") == 0);
    ASSERT(worker_queue_get_depth(q) == 3);

    worker_queue_batch_t *batch = worker_queue_consume(q, 2, 0);
    ASSERT(batch != NULL);
    ASSERT(batch->count == 2);
    ASSERT(batch->messages[0]->body != NULL);
    ASSERT(strcmp(batch->messages[0]->body, "msg1") == 0);
    ASSERT(strcmp(batch->messages[1]->body, "msg2") == 0);

    /* Ack first message */
    worker_queue_message_ack(batch->messages[0]);
    ASSERT(batch->messages[0]->acked == true);

    worker_queue_batch_destroy(batch);
    ASSERT(worker_queue_get_depth(q) == 1);

    /* Consume remaining */
    batch = worker_queue_consume(q, 10, 0);
    ASSERT(batch != NULL);
    ASSERT(batch->count == 1);
    ASSERT(strcmp(batch->messages[0]->body, "msg3") == 0);
    worker_queue_batch_destroy(batch);

    ASSERT(worker_queue_get_depth(q) == 0);
    worker_queue_destroy(q);
    PASS();
}

static void test_queue_send_batch(void) {
    TEST("queue_send_batch");
    worker_queue_t *q = worker_queue_create("batch-q");

    const char *bodies[] = {"alpha", "beta", "gamma"};
    size_t lengths[] = {5, 4, 5};
    ASSERT(worker_queue_send_batch(q, bodies, lengths, 3) == 0);
    ASSERT(worker_queue_get_depth(q) == 3);

    worker_queue_batch_t *batch = worker_queue_consume(q, 10, 0);
    ASSERT(batch->count == 3);
    ASSERT(strcmp(batch->messages[0]->body, "alpha") == 0);
    ASSERT(strcmp(batch->messages[2]->body, "gamma") == 0);
    worker_queue_batch_destroy(batch);

    worker_queue_destroy(q);
    PASS();
}

static void test_queue_send_json(void) {
    TEST("queue_send_json");
    worker_queue_t *q = worker_queue_create("json-q");

    json_value_t *j = json_object_create();
    json_object_set(j, "event", json_string_create("order.created"));
    json_object_set(j, "id", json_number_create(42));
    ASSERT(worker_queue_send_json(q, j) == 0);
    json_value_free(j);

    ASSERT(worker_queue_get_depth(q) == 1);
    worker_queue_batch_t *batch = worker_queue_consume(q, 1, 0);
    ASSERT(batch->count == 1);
    ASSERT(strstr(batch->messages[0]->body, "order.created") != NULL);
    worker_queue_batch_destroy(batch);

    worker_queue_destroy(q);
    PASS();
}

/* ========================================================
 * Integration Test: Worker + KV + R2 + D1 + Queues
 * ======================================================== */

static worker_response_t *_integration_handler(worker_request_t *req,
                                               worker_env_t *env) {
    (void)req;
    worker_response_t *res = worker_response_create(200);

    /* Access KV from env */
    worker_kv_t *kv = worker_env_get_binding(env, "CACHE", WORKER_BINDING_KV);
    if (kv) {
        worker_kv_put(kv, "visited", "true", NULL);
    }

    /* Access R2 from env */
    worker_r2_bucket_t *bucket = worker_env_get_binding(env, "ASSETS",
                                                        WORKER_BINDING_R2);
    if (bucket) {
        worker_r2_put(bucket, "log.txt", (const uint8_t *)"visit", 5, NULL);
    }

    worker_response_set_body_text(res, "integrated");
    return res;
}

static void test_integration(void) {
    TEST("integration_worker_with_services");

    /* Set up services */
    worker_kv_t *kv = worker_kv_create("CACHE");
    worker_r2_bucket_t *bucket = worker_r2_bucket_create("ASSETS");
    worker_d1_t *db = worker_d1_create("APP_DB");
    worker_queue_t *queue = worker_queue_create("EVENTS");

    /* Create env with all bindings */
    worker_env_t *env = worker_env_create();
    worker_env_add_binding(env, "CACHE", WORKER_BINDING_KV, kv);
    worker_env_add_binding(env, "ASSETS", WORKER_BINDING_R2, bucket);
    worker_env_add_binding(env, "APP_DB", WORKER_BINDING_D1, db);
    worker_env_add_binding(env, "EVENTS", WORKER_BINDING_QUEUE, queue);
    ASSERT(worker_env_binding_count(env) == 4);

    /* Run fetch */
    worker_set_fetch_handler(_integration_handler);
    worker_request_t *req = worker_request_create("GET", "/");
    worker_response_t *res = worker_handle_fetch(req, env);
    ASSERT(res != NULL);
    ASSERT(worker_response_get_status(res) == 200);

    /* Verify side effects */
    char *cached = worker_kv_get(kv, "visited");
    ASSERT(cached != NULL && strcmp(cached, "true") == 0);
    free(cached);

    worker_r2_object_t *obj = worker_r2_get(bucket, "log.txt");
    ASSERT(obj != NULL && obj->body_len == 5);
    worker_r2_object_destroy(obj);

    /* Cleanup */
    worker_request_destroy(req);
    worker_response_destroy(res);
    worker_set_fetch_handler(NULL);

    worker_queue_destroy(queue);
    worker_d1_destroy(db);
    worker_r2_bucket_destroy(bucket);
    worker_kv_destroy(kv);
    worker_env_destroy(env);
    PASS();
}

/* ======================================================== */

int main(void) {
    printf("\n=== Cloudflare Worker Runtime & Infrastructure Tests ===\n\n");

    /* Worker Request */
    test_worker_request_create();
    test_worker_request_null_args();
    test_worker_request_headers();
    test_worker_request_body();

    /* Worker Response */
    test_worker_response_create();
    test_worker_response_body_text();
    test_worker_response_json();

    /* Worker Environment */
    test_worker_env_bindings();

    /* Fetch Handler */
    test_worker_fetch_no_router();
    test_worker_fetch_with_router();
    test_worker_fetch_custom_handler();

    /* KV Namespace */
    test_kv_create_destroy();
    test_kv_put_get();
    test_kv_metadata();
    test_kv_delete();
    test_kv_list();

    /* R2 Object Storage */
    test_r2_create_destroy();
    test_r2_put_get();
    test_r2_head();
    test_r2_delete();
    test_r2_list();

    /* D1 Database */
    test_d1_create_destroy();
    test_d1_create_table_and_insert();
    test_d1_select();
    test_d1_delete_rows();
    test_d1_batch();
    test_d1_sql_safety();

    /* Queues */
    test_queue_create_destroy();
    test_queue_send_consume();
    test_queue_send_batch();
    test_queue_send_json();

    /* Integration */
    test_integration();

    printf("\n=== Results: %d/%d tests passed ===\n", tests_passed, tests_run);

    return (tests_passed == tests_run) ? 0 : 1;
}
