/*
 * test_worker.c - Tests for Cloudflare Worker Runtime
 *
 * Validates the Worker request/response abstraction, fetch handler
 * integration with the router, KV store (including TTL and list),
 * R2 object storage, D1 SQL database, Queues producer, and the
 * env context bindings.
 *
 * These tests run on native builds too, exercising the same code
 * paths that Emscripten/Workers will use.
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

/* Test macros (consistent with test_weblib.c / test_wasm.c) */
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

/* ===== Worker Runtime Info Tests ===== */

static void test_worker_runtime_version(void) {
    TEST("worker_runtime_version");
    const char *ver = worker_runtime_version();
    ASSERT(ver != NULL);
    ASSERT(strstr(ver, "weblib-worker") != NULL);
    PASS();
}

static void test_worker_runtime_is_supported(void) {
    TEST("worker_runtime_is_supported");
    ASSERT(worker_runtime_is_supported() == true);
    PASS();
}

/* ===== Worker Request Tests ===== */

static void test_worker_request_create(void) {
    TEST("worker_request_create");
    worker_request_t *req = worker_request_create("GET", "/api/hello");
    ASSERT(req != NULL);
    ASSERT(strcmp(worker_request_get_method(req), "GET") == 0);
    ASSERT(strcmp(worker_request_get_url(req), "/api/hello") == 0);
    worker_request_destroy(req);
    PASS();
}

static void test_worker_request_null_params(void) {
    TEST("worker_request_create_null_params");
    ASSERT(worker_request_create(NULL, "/api") == NULL);
    ASSERT(worker_request_create("GET", NULL) == NULL);
    ASSERT(worker_request_get_method(NULL) == NULL);
    ASSERT(worker_request_get_url(NULL) == NULL);
    ASSERT(worker_request_get_body(NULL) == NULL);
    ASSERT(worker_request_get_body_len(NULL) == 0);
    PASS();
}

static void test_worker_request_headers(void) {
    TEST("worker_request_headers");
    worker_request_t *req = worker_request_create("POST", "/api/data");
    ASSERT(req != NULL);

    worker_request_set_header(req, "Content-Type", "application/json");
    worker_request_set_header(req, "Authorization", "Bearer token123");

    const char *ct = worker_request_get_header(req, "Content-Type");
    ASSERT(ct != NULL);
    ASSERT(strcmp(ct, "application/json") == 0);

    /* Case-insensitive lookup */
    const char *ct2 = worker_request_get_header(req, "content-type");
    ASSERT(ct2 != NULL);
    ASSERT(strcmp(ct2, "application/json") == 0);

    const char *auth = worker_request_get_header(req, "Authorization");
    ASSERT(auth != NULL);
    ASSERT(strstr(auth, "Bearer") != NULL);

    /* Missing header */
    ASSERT(worker_request_get_header(req, "X-Missing") == NULL);

    worker_request_destroy(req);
    PASS();
}

static void test_worker_request_body(void) {
    TEST("worker_request_body");
    worker_request_t *req = worker_request_create("POST", "/api/data");
    ASSERT(req != NULL);

    const char *body = "{\"key\":\"value\"}";
    worker_request_set_body(req, body, strlen(body));

    const char *got = worker_request_get_body(req);
    ASSERT(got != NULL);
    ASSERT(strcmp(got, body) == 0);
    ASSERT(worker_request_get_body_len(req) == strlen(body));

    /* Replace body */
    const char *body2 = "updated";
    worker_request_set_body(req, body2, strlen(body2));
    ASSERT(strcmp(worker_request_get_body(req), "updated") == 0);

    /* Clear body */
    worker_request_set_body(req, NULL, 0);
    ASSERT(worker_request_get_body(req) == NULL);
    ASSERT(worker_request_get_body_len(req) == 0);

    worker_request_destroy(req);
    PASS();
}

/* ===== Worker Response Tests ===== */

static void test_worker_response_create(void) {
    TEST("worker_response_create");
    worker_response_t *res = worker_response_create();
    ASSERT(res != NULL);
    ASSERT(worker_response_get_status(res) == 200);
    ASSERT(worker_response_get_body(res) == NULL);
    ASSERT(worker_response_get_body_len(res) == 0);
    worker_response_destroy(res);
    PASS();
}

static void test_worker_response_status(void) {
    TEST("worker_response_set_status");
    worker_response_t *res = worker_response_create();
    ASSERT(res != NULL);

    worker_response_set_status(res, 404);
    ASSERT(worker_response_get_status(res) == 404);

    worker_response_set_status(res, 500);
    ASSERT(worker_response_get_status(res) == 500);

    worker_response_destroy(res);
    PASS();
}

static void test_worker_response_null_params(void) {
    TEST("worker_response_null_params");
    ASSERT(worker_response_get_status(NULL) == 0);
    ASSERT(worker_response_get_body(NULL) == NULL);
    ASSERT(worker_response_get_body_len(NULL) == 0);
    ASSERT(worker_response_get_header(NULL, "X") == NULL);
    ASSERT(worker_response_get_header_count(NULL) == 0);
    ASSERT(worker_response_get_header_name(NULL, 0) == NULL);
    ASSERT(worker_response_get_header_value(NULL, 0) == NULL);
    PASS();
}

static void test_worker_response_headers(void) {
    TEST("worker_response_headers");
    worker_response_t *res = worker_response_create();
    ASSERT(res != NULL);

    worker_response_set_header(res, "X-Custom", "value1");
    worker_response_set_header(res, "X-Another", "value2");

    ASSERT(worker_response_get_header_count(res) == 2);

    const char *v1 = worker_response_get_header(res, "X-Custom");
    ASSERT(v1 != NULL);
    ASSERT(strcmp(v1, "value1") == 0);

    /* Replace existing header */
    worker_response_set_header(res, "X-Custom", "replaced");
    ASSERT(worker_response_get_header_count(res) == 2);
    ASSERT(strcmp(worker_response_get_header(res, "X-Custom"), "replaced") == 0);

    /* Index-based access */
    const char *name0 = worker_response_get_header_name(res, 0);
    const char *val0 = worker_response_get_header_value(res, 0);
    ASSERT(name0 != NULL);
    ASSERT(val0 != NULL);

    /* Out-of-bounds */
    ASSERT(worker_response_get_header_name(res, 99) == NULL);
    ASSERT(worker_response_get_header_value(res, -1) == NULL);

    worker_response_destroy(res);
    PASS();
}

static void test_worker_response_body(void) {
    TEST("worker_response_body");
    worker_response_t *res = worker_response_create();
    ASSERT(res != NULL);

    const char *body = "Hello, Worker!";
    worker_response_set_body(res, body, strlen(body));

    ASSERT(worker_response_get_body(res) != NULL);
    ASSERT(strcmp(worker_response_get_body(res), body) == 0);
    ASSERT(worker_response_get_body_len(res) == strlen(body));

    worker_response_destroy(res);
    PASS();
}

static void test_worker_response_set_text(void) {
    TEST("worker_response_set_text");
    worker_response_t *res = worker_response_create();
    ASSERT(res != NULL);

    worker_response_set_text(res, 201, "Created");
    ASSERT(worker_response_get_status(res) == 201);
    ASSERT(strcmp(worker_response_get_body(res), "Created") == 0);

    const char *ct = worker_response_get_header(res, "Content-Type");
    ASSERT(ct != NULL);
    ASSERT(strstr(ct, "text/plain") != NULL);

    worker_response_destroy(res);
    PASS();
}

static void test_worker_response_set_json(void) {
    TEST("worker_response_set_json");
    worker_response_t *res = worker_response_create();
    ASSERT(res != NULL);

    json_value_t *json = json_object_create();
    json_object_set(json, "status", json_string_create("ok"));
    json_object_set(json, "code", json_number_create(200));

    worker_response_set_json(res, 200, json);
    ASSERT(worker_response_get_status(res) == 200);

    const char *body = worker_response_get_body(res);
    ASSERT(body != NULL);
    ASSERT(strstr(body, "\"status\"") != NULL);
    ASSERT(strstr(body, "\"ok\"") != NULL);

    const char *ct = worker_response_get_header(res, "Content-Type");
    ASSERT(ct != NULL);
    ASSERT(strstr(ct, "application/json") != NULL);

    json_value_free(json);
    worker_response_destroy(res);
    PASS();
}

/* ===== Worker Fetch Handler Tests ===== */

/* Simple handler for testing */
static void hello_handler(http_request_t *req, http_response_t *res) {
    (void)req;
    http_response_send_text(res, HTTP_OK, "Hello from Worker!");
}

/* JSON handler for testing */
static void json_handler(http_request_t *req, http_response_t *res) {
    (void)req;
    json_value_t *json = json_object_create();
    json_object_set(json, "message", json_string_create("worker response"));
    http_response_send_json(res, HTTP_OK, json);
    json_value_free(json);
}

/* Handler that reads route parameters */
static void param_handler(http_request_t *req, http_response_t *res) {
    const char *id = http_request_get_param(req, "id");
    if (id) {
        char buf[256];
        snprintf(buf, sizeof(buf), "User %s", id);
        http_response_send_text(res, HTTP_OK, buf);
    } else {
        http_response_send_text(res, HTTP_NOT_FOUND, "No ID");
    }
}

static void test_worker_handle_fetch_text(void) {
    TEST("worker_handle_fetch_text");

    router_t *router = router_create();
    ASSERT(router != NULL);
    router_add_route(router, HTTP_GET, "/hello", hello_handler);

    worker_request_t *req = worker_request_create("GET", "/hello");
    ASSERT(req != NULL);

    worker_response_t *res = worker_handle_fetch(req, router);
    ASSERT(res != NULL);
    ASSERT(worker_response_get_status(res) == 200);

    const char *body = worker_response_get_body(res);
    ASSERT(body != NULL);
    ASSERT(strcmp(body, "Hello from Worker!") == 0);

    worker_response_destroy(res);
    worker_request_destroy(req);
    router_destroy(router);
    PASS();
}

static void test_worker_handle_fetch_json(void) {
    TEST("worker_handle_fetch_json");

    router_t *router = router_create();
    ASSERT(router != NULL);
    router_add_route(router, HTTP_GET, "/api/data", json_handler);

    worker_request_t *req = worker_request_create("GET", "/api/data");
    ASSERT(req != NULL);

    worker_response_t *res = worker_handle_fetch(req, router);
    ASSERT(res != NULL);
    ASSERT(worker_response_get_status(res) == 200);

    const char *body = worker_response_get_body(res);
    ASSERT(body != NULL);
    ASSERT(strstr(body, "worker response") != NULL);

    const char *ct = worker_response_get_header(res, "Content-Type");
    ASSERT(ct != NULL);
    ASSERT(strstr(ct, "application/json") != NULL);

    worker_response_destroy(res);
    worker_request_destroy(req);
    router_destroy(router);
    PASS();
}

static void test_worker_handle_fetch_not_found(void) {
    TEST("worker_handle_fetch_not_found");

    router_t *router = router_create();
    ASSERT(router != NULL);
    router_add_route(router, HTTP_GET, "/exists", hello_handler);

    worker_request_t *req = worker_request_create("GET", "/does-not-exist");
    ASSERT(req != NULL);

    worker_response_t *res = worker_handle_fetch(req, router);
    ASSERT(res != NULL);
    ASSERT(worker_response_get_status(res) == 404);

    worker_response_destroy(res);
    worker_request_destroy(req);
    router_destroy(router);
    PASS();
}

static void test_worker_handle_fetch_with_params(void) {
    TEST("worker_handle_fetch_with_params");

    router_t *router = router_create();
    ASSERT(router != NULL);
    router_add_route(router, HTTP_GET, "/users/:id", param_handler);

    worker_request_t *req = worker_request_create("GET", "/users/42");
    ASSERT(req != NULL);

    worker_response_t *res = worker_handle_fetch(req, router);
    ASSERT(res != NULL);
    ASSERT(worker_response_get_status(res) == 200);

    const char *body = worker_response_get_body(res);
    ASSERT(body != NULL);
    ASSERT(strstr(body, "User 42") != NULL);

    worker_response_destroy(res);
    worker_request_destroy(req);
    router_destroy(router);
    PASS();
}

static void test_worker_handle_fetch_query_string(void) {
    TEST("worker_handle_fetch_query_string");

    router_t *router = router_create();
    ASSERT(router != NULL);
    router_add_route(router, HTTP_GET, "/search", hello_handler);

    /* URL with query string should still match the path */
    worker_request_t *req = worker_request_create("GET", "/search?q=test&page=1");
    ASSERT(req != NULL);

    worker_response_t *res = worker_handle_fetch(req, router);
    ASSERT(res != NULL);
    ASSERT(worker_response_get_status(res) == 200);

    worker_response_destroy(res);
    worker_request_destroy(req);
    router_destroy(router);
    PASS();
}

static void test_worker_handle_fetch_null(void) {
    TEST("worker_handle_fetch_null");
    router_t *router = router_create();
    ASSERT(worker_handle_fetch(NULL, router) == NULL);
    ASSERT(worker_handle_fetch(NULL, NULL) == NULL);
    worker_request_t *req = worker_request_create("GET", "/");
    ASSERT(worker_handle_fetch(req, NULL) == NULL);
    worker_request_destroy(req);
    router_destroy(router);
    PASS();
}

/* ===== Worker KV Store Tests ===== */

static void test_worker_kv_create(void) {
    TEST("worker_kv_create");
    worker_kv_t *kv = worker_kv_create();
    ASSERT(kv != NULL);
    worker_kv_destroy(kv);
    PASS();
}

static void test_worker_kv_put_get(void) {
    TEST("worker_kv_put_get");
    worker_kv_t *kv = worker_kv_create();
    ASSERT(kv != NULL);

    ASSERT(worker_kv_put(kv, "name", "Kamran") == 0);
    ASSERT(worker_kv_put(kv, "role", "developer") == 0);

    const char *name = worker_kv_get(kv, "name");
    ASSERT(name != NULL);
    ASSERT(strcmp(name, "Kamran") == 0);

    const char *role = worker_kv_get(kv, "role");
    ASSERT(role != NULL);
    ASSERT(strcmp(role, "developer") == 0);

    /* Missing key */
    ASSERT(worker_kv_get(kv, "missing") == NULL);

    worker_kv_destroy(kv);
    PASS();
}

static void test_worker_kv_update(void) {
    TEST("worker_kv_update");
    worker_kv_t *kv = worker_kv_create();
    ASSERT(kv != NULL);

    ASSERT(worker_kv_put(kv, "counter", "1") == 0);
    ASSERT(strcmp(worker_kv_get(kv, "counter"), "1") == 0);

    /* Overwrite */
    ASSERT(worker_kv_put(kv, "counter", "2") == 0);
    ASSERT(strcmp(worker_kv_get(kv, "counter"), "2") == 0);

    worker_kv_destroy(kv);
    PASS();
}

static void test_worker_kv_delete(void) {
    TEST("worker_kv_delete");
    worker_kv_t *kv = worker_kv_create();
    ASSERT(kv != NULL);

    ASSERT(worker_kv_put(kv, "temp", "data") == 0);
    ASSERT(worker_kv_get(kv, "temp") != NULL);

    ASSERT(worker_kv_delete(kv, "temp") == 0);
    ASSERT(worker_kv_get(kv, "temp") == NULL);

    /* Delete non-existent key */
    ASSERT(worker_kv_delete(kv, "nope") == -1);

    worker_kv_destroy(kv);
    PASS();
}

static void test_worker_kv_null_params(void) {
    TEST("worker_kv_null_params");
    worker_kv_t *kv = worker_kv_create();
    ASSERT(kv != NULL);

    ASSERT(worker_kv_put(NULL, "k", "v") == -1);
    ASSERT(worker_kv_put(kv, NULL, "v") == -1);
    ASSERT(worker_kv_put(kv, "k", NULL) == -1);
    ASSERT(worker_kv_get(NULL, "k") == NULL);
    ASSERT(worker_kv_get(kv, NULL) == NULL);
    ASSERT(worker_kv_delete(NULL, "k") == -1);
    ASSERT(worker_kv_delete(kv, NULL) == -1);

    worker_kv_destroy(kv);
    /* Destroy NULL should be safe */
    worker_kv_destroy(NULL);
    PASS();
}

static void test_worker_kv_key_length_validation(void) {
    TEST("worker_kv_key_length_validation");
    worker_kv_t *kv = worker_kv_create();
    ASSERT(kv != NULL);

    /* Build a key that exceeds max length */
    char long_key[600];
    memset(long_key, 'k', 599);
    long_key[599] = '\0';

    ASSERT(worker_kv_put(kv, long_key, "value") == -1);

    worker_kv_destroy(kv);
    PASS();
}

static void test_worker_kv_put_with_ttl(void) {
    TEST("worker_kv_put_with_ttl");
    worker_kv_t *kv = worker_kv_create();
    ASSERT(kv != NULL);

    /* TTL=0 means no expiration */
    ASSERT(worker_kv_put_with_ttl(kv, "persist", "data", 0) == 0);
    ASSERT(worker_kv_get(kv, "persist") != NULL);

    /* TTL=3600 (1 hour) should still be visible */
    ASSERT(worker_kv_put_with_ttl(kv, "session", "abc", 3600) == 0);
    ASSERT(worker_kv_get(kv, "session") != NULL);
    ASSERT(strcmp(worker_kv_get(kv, "session"), "abc") == 0);

    /* Invalid TTL */
    ASSERT(worker_kv_put_with_ttl(kv, "bad", "val", -1) == -1);

    /* NULL params */
    ASSERT(worker_kv_put_with_ttl(NULL, "k", "v", 60) == -1);
    ASSERT(worker_kv_put_with_ttl(kv, NULL, "v", 60) == -1);
    ASSERT(worker_kv_put_with_ttl(kv, "k", NULL, 60) == -1);

    worker_kv_destroy(kv);
    PASS();
}

static void test_worker_kv_list(void) {
    TEST("worker_kv_list");
    worker_kv_t *kv = worker_kv_create();
    ASSERT(kv != NULL);

    worker_kv_put(kv, "user:1", "Alice");
    worker_kv_put(kv, "user:2", "Bob");
    worker_kv_put(kv, "session:abc", "data");
    worker_kv_put(kv, "user:3", "Charlie");

    /* List all keys */
    const char **keys = NULL;
    int count = 0;
    ASSERT(worker_kv_list(kv, NULL, 0, &keys, &count) == 0);
    ASSERT(count == 4);
    worker_kv_list_free(keys);

    /* List with prefix */
    ASSERT(worker_kv_list(kv, "user:", 0, &keys, &count) == 0);
    ASSERT(count == 3);
    worker_kv_list_free(keys);

    ASSERT(worker_kv_list(kv, "session:", 0, &keys, &count) == 0);
    ASSERT(count == 1);
    worker_kv_list_free(keys);

    /* List with limit */
    ASSERT(worker_kv_list(kv, "user:", 2, &keys, &count) == 0);
    ASSERT(count == 2);
    worker_kv_list_free(keys);

    /* NULL params */
    ASSERT(worker_kv_list(NULL, NULL, 0, &keys, &count) == -1);
    ASSERT(worker_kv_list(kv, NULL, 0, NULL, &count) == -1);

    worker_kv_destroy(kv);
    PASS();
}

/* ===== Worker R2 Object Storage Tests ===== */

static void test_worker_r2_create(void) {
    TEST("worker_r2_create");
    worker_r2_bucket_t *r2 = worker_r2_create();
    ASSERT(r2 != NULL);
    worker_r2_destroy(r2);
    PASS();
}

static void test_worker_r2_put_get(void) {
    TEST("worker_r2_put_get");
    worker_r2_bucket_t *r2 = worker_r2_create();
    ASSERT(r2 != NULL);

    const char *data = "Hello, R2!";
    ASSERT(worker_r2_put(r2, "file.txt", data, strlen(data), "text/plain") == 0);

    size_t size = 0;
    const char *got = worker_r2_get(r2, "file.txt", &size);
    ASSERT(got != NULL);
    ASSERT(size == strlen(data));
    ASSERT(strcmp(got, data) == 0);

    /* Missing object */
    ASSERT(worker_r2_get(r2, "missing.txt", NULL) == NULL);

    worker_r2_destroy(r2);
    PASS();
}

static void test_worker_r2_head(void) {
    TEST("worker_r2_head");
    worker_r2_bucket_t *r2 = worker_r2_create();
    ASSERT(r2 != NULL);

    const char *data = "image data";
    ASSERT(worker_r2_put(r2, "pic.png", data, strlen(data), "image/png") == 0);

    size_t size = 0;
    const char *ct = NULL;
    ASSERT(worker_r2_head(r2, "pic.png", &size, &ct) == 0);
    ASSERT(size == strlen(data));
    ASSERT(ct != NULL);
    ASSERT(strcmp(ct, "image/png") == 0);

    /* Missing */
    ASSERT(worker_r2_head(r2, "nope", &size, &ct) == -1);

    worker_r2_destroy(r2);
    PASS();
}

static void test_worker_r2_delete(void) {
    TEST("worker_r2_delete");
    worker_r2_bucket_t *r2 = worker_r2_create();
    ASSERT(r2 != NULL);

    ASSERT(worker_r2_put(r2, "tmp.txt", "data", 4, NULL) == 0);
    ASSERT(worker_r2_get(r2, "tmp.txt", NULL) != NULL);

    ASSERT(worker_r2_delete(r2, "tmp.txt") == 0);
    ASSERT(worker_r2_get(r2, "tmp.txt", NULL) == NULL);

    /* Delete non-existent */
    ASSERT(worker_r2_delete(r2, "nope") == -1);

    worker_r2_destroy(r2);
    PASS();
}

static void test_worker_r2_list(void) {
    TEST("worker_r2_list");
    worker_r2_bucket_t *r2 = worker_r2_create();
    ASSERT(r2 != NULL);

    worker_r2_put(r2, "images/a.png", "a", 1, "image/png");
    worker_r2_put(r2, "images/b.png", "b", 1, "image/png");
    worker_r2_put(r2, "docs/readme.md", "c", 1, "text/markdown");

    const char **keys = NULL;
    int count = 0;

    /* List all */
    ASSERT(worker_r2_list(r2, NULL, 0, &keys, &count) == 0);
    ASSERT(count == 3);
    worker_r2_list_free(keys);

    /* List with prefix */
    ASSERT(worker_r2_list(r2, "images/", 0, &keys, &count) == 0);
    ASSERT(count == 2);
    worker_r2_list_free(keys);

    /* NULL params */
    ASSERT(worker_r2_list(NULL, NULL, 0, &keys, &count) == -1);

    worker_r2_destroy(r2);
    PASS();
}

static void test_worker_r2_null_safety(void) {
    TEST("worker_r2_null_safety");
    ASSERT(worker_r2_put(NULL, "k", "d", 1, NULL) == -1);
    ASSERT(worker_r2_get(NULL, "k", NULL) == NULL);
    ASSERT(worker_r2_head(NULL, "k", NULL, NULL) == -1);
    ASSERT(worker_r2_delete(NULL, "k") == -1);
    worker_r2_destroy(NULL); /* should not crash */
    PASS();
}

/* ===== Worker D1 Database Tests ===== */

static void test_worker_d1_create(void) {
    TEST("worker_d1_create");
    worker_d1_t *d1 = worker_d1_create();
    ASSERT(d1 != NULL);
    worker_d1_destroy(d1);
    PASS();
}

static void test_worker_d1_create_table_and_insert(void) {
    TEST("worker_d1_create_table_and_insert");
    worker_d1_t *d1 = worker_d1_create();
    ASSERT(d1 != NULL);

    ASSERT(worker_d1_exec(d1,
        "CREATE TABLE users (id TEXT, name TEXT, email TEXT)") == 0);

    ASSERT(worker_d1_exec(d1,
        "INSERT INTO users VALUES ('1', 'Alice', 'alice@example.com')") == 0);
    ASSERT(worker_d1_exec(d1,
        "INSERT INTO users VALUES ('2', 'Bob', 'bob@example.com')") == 0);

    /* Query all rows */
    worker_d1_result_t *result = worker_d1_query(d1, "SELECT * FROM users");
    ASSERT(result != NULL);
    ASSERT(worker_d1_result_is_success(result));
    ASSERT(worker_d1_result_get_row_count(result) == 2);
    ASSERT(worker_d1_result_get_col_count(result) == 3);

    /* Check column names */
    ASSERT(strcmp(worker_d1_result_get_col_name(result, 0), "id") == 0);
    ASSERT(strcmp(worker_d1_result_get_col_name(result, 1), "name") == 0);

    /* Check row values */
    ASSERT(strcmp(worker_d1_result_get_value(result, 0, 0), "1") == 0);
    ASSERT(strcmp(worker_d1_result_get_value(result, 0, 1), "Alice") == 0);
    ASSERT(strcmp(worker_d1_result_get_value(result, 1, 0), "2") == 0);
    ASSERT(strcmp(worker_d1_result_get_value(result, 1, 1), "Bob") == 0);

    worker_d1_result_destroy(result);
    worker_d1_destroy(d1);
    PASS();
}

static void test_worker_d1_query_with_where(void) {
    TEST("worker_d1_query_with_where");
    worker_d1_t *d1 = worker_d1_create();
    ASSERT(d1 != NULL);

    worker_d1_exec(d1, "CREATE TABLE items (id TEXT, name TEXT)");
    worker_d1_exec(d1, "INSERT INTO items VALUES ('1', 'Widget')");
    worker_d1_exec(d1, "INSERT INTO items VALUES ('2', 'Gadget')");
    worker_d1_exec(d1, "INSERT INTO items VALUES ('3', 'Widget')");

    /* WHERE filter */
    worker_d1_result_t *result = worker_d1_query(d1,
        "SELECT * FROM items WHERE name = 'Widget'");
    ASSERT(result != NULL);
    ASSERT(worker_d1_result_is_success(result));
    ASSERT(worker_d1_result_get_row_count(result) == 2);

    worker_d1_result_destroy(result);
    worker_d1_destroy(d1);
    PASS();
}

static void test_worker_d1_null_safety(void) {
    TEST("worker_d1_null_safety");
    ASSERT(worker_d1_exec(NULL, "SELECT 1") == -1);
    ASSERT(worker_d1_query(NULL, "SELECT 1") == NULL);

    worker_d1_t *d1 = worker_d1_create();
    ASSERT(worker_d1_exec(d1, NULL) == -1);
    ASSERT(worker_d1_query(d1, NULL) == NULL);

    /* Result getters on NULL */
    ASSERT(worker_d1_result_get_row_count(NULL) == 0);
    ASSERT(worker_d1_result_get_col_count(NULL) == 0);
    ASSERT(worker_d1_result_get_col_name(NULL, 0) == NULL);
    ASSERT(worker_d1_result_get_value(NULL, 0, 0) == NULL);
    ASSERT(worker_d1_result_is_success(NULL) == false);
    ASSERT(worker_d1_result_get_error(NULL) == NULL);

    worker_d1_result_destroy(NULL); /* should not crash */
    worker_d1_destroy(d1);
    worker_d1_destroy(NULL);
    PASS();
}

/* ===== Worker Queue Tests ===== */

static void test_worker_queue_create(void) {
    TEST("worker_queue_create");
    worker_queue_t *q = worker_queue_create();
    ASSERT(q != NULL);
    ASSERT(worker_queue_get_count(q) == 0);
    worker_queue_destroy(q);
    PASS();
}

static void test_worker_queue_send(void) {
    TEST("worker_queue_send");
    worker_queue_t *q = worker_queue_create();
    ASSERT(q != NULL);

    const char *msg = "{\"type\":\"email\",\"to\":\"user@example.com\"}";
    ASSERT(worker_queue_send(q, msg, strlen(msg)) == 0);
    ASSERT(worker_queue_get_count(q) == 1);

    size_t len = 0;
    const char *peek = worker_queue_peek(q, 0, &len);
    ASSERT(peek != NULL);
    ASSERT(len == strlen(msg));
    ASSERT(strcmp(peek, msg) == 0);

    /* Send another */
    ASSERT(worker_queue_send(q, "msg2", 4) == 0);
    ASSERT(worker_queue_get_count(q) == 2);

    worker_queue_destroy(q);
    PASS();
}

static void test_worker_queue_send_batch(void) {
    TEST("worker_queue_send_batch");
    worker_queue_t *q = worker_queue_create();
    ASSERT(q != NULL);

    const char *bodies[] = { "msg1", "msg2", "msg3" };
    size_t lengths[] = { 4, 4, 4 };

    ASSERT(worker_queue_send_batch(q, bodies, lengths, 3) == 0);
    ASSERT(worker_queue_get_count(q) == 3);

    /* Verify order */
    ASSERT(strcmp(worker_queue_peek(q, 0, NULL), "msg1") == 0);
    ASSERT(strcmp(worker_queue_peek(q, 1, NULL), "msg2") == 0);
    ASSERT(strcmp(worker_queue_peek(q, 2, NULL), "msg3") == 0);

    worker_queue_destroy(q);
    PASS();
}

static void test_worker_queue_null_safety(void) {
    TEST("worker_queue_null_safety");
    ASSERT(worker_queue_send(NULL, "msg", 3) == -1);
    ASSERT(worker_queue_get_count(NULL) == 0);
    ASSERT(worker_queue_peek(NULL, 0, NULL) == NULL);

    worker_queue_t *q = worker_queue_create();
    ASSERT(worker_queue_send(q, NULL, 0) == -1);

    /* Batch with invalid count */
    ASSERT(worker_queue_send_batch(q, NULL, NULL, 0) == -1);
    ASSERT(worker_queue_send_batch(q, NULL, NULL, -1) == -1);

    worker_queue_destroy(q);
    worker_queue_destroy(NULL); /* should not crash */
    PASS();
}

/* ===== Worker Env Context Tests ===== */

static void test_worker_env_create(void) {
    TEST("worker_env_create");
    worker_env_t *env = worker_env_create();
    ASSERT(env != NULL);
    worker_env_destroy(env);
    PASS();
}

static void test_worker_env_kv_binding(void) {
    TEST("worker_env_kv_binding");
    worker_env_t *env = worker_env_create();
    worker_kv_t *kv = worker_kv_create();
    ASSERT(env != NULL && kv != NULL);

    ASSERT(worker_env_bind_kv(env, "MY_KV", kv) == 0);

    worker_kv_t *got = worker_env_get_kv(env, "MY_KV");
    ASSERT(got == kv);

    /* Non-existent binding */
    ASSERT(worker_env_get_kv(env, "OTHER") == NULL);

    worker_env_destroy(env);
    worker_kv_destroy(kv);
    PASS();
}

static void test_worker_env_r2_binding(void) {
    TEST("worker_env_r2_binding");
    worker_env_t *env = worker_env_create();
    worker_r2_bucket_t *r2 = worker_r2_create();
    ASSERT(env != NULL && r2 != NULL);

    ASSERT(worker_env_bind_r2(env, "MY_BUCKET", r2) == 0);
    ASSERT(worker_env_get_r2(env, "MY_BUCKET") == r2);
    ASSERT(worker_env_get_r2(env, "OTHER") == NULL);

    worker_env_destroy(env);
    worker_r2_destroy(r2);
    PASS();
}

static void test_worker_env_d1_binding(void) {
    TEST("worker_env_d1_binding");
    worker_env_t *env = worker_env_create();
    worker_d1_t *d1 = worker_d1_create();
    ASSERT(env != NULL && d1 != NULL);

    ASSERT(worker_env_bind_d1(env, "DB", d1) == 0);
    ASSERT(worker_env_get_d1(env, "DB") == d1);
    ASSERT(worker_env_get_d1(env, "OTHER") == NULL);

    worker_env_destroy(env);
    worker_d1_destroy(d1);
    PASS();
}

static void test_worker_env_queue_binding(void) {
    TEST("worker_env_queue_binding");
    worker_env_t *env = worker_env_create();
    worker_queue_t *q = worker_queue_create();
    ASSERT(env != NULL && q != NULL);

    ASSERT(worker_env_bind_queue(env, "TASK_QUEUE", q) == 0);
    ASSERT(worker_env_get_queue(env, "TASK_QUEUE") == q);
    ASSERT(worker_env_get_queue(env, "OTHER") == NULL);

    worker_env_destroy(env);
    worker_queue_destroy(q);
    PASS();
}

static void test_worker_env_multi_bindings(void) {
    TEST("worker_env_multi_bindings");
    worker_env_t *env = worker_env_create();
    worker_kv_t *kv = worker_kv_create();
    worker_r2_bucket_t *r2 = worker_r2_create();
    worker_d1_t *d1 = worker_d1_create();
    worker_queue_t *q = worker_queue_create();

    ASSERT(worker_env_bind_kv(env, "CACHE", kv) == 0);
    ASSERT(worker_env_bind_r2(env, "ASSETS", r2) == 0);
    ASSERT(worker_env_bind_d1(env, "DB", d1) == 0);
    ASSERT(worker_env_bind_queue(env, "JOBS", q) == 0);

    /* All bindings accessible */
    ASSERT(worker_env_get_kv(env, "CACHE") == kv);
    ASSERT(worker_env_get_r2(env, "ASSETS") == r2);
    ASSERT(worker_env_get_d1(env, "DB") == d1);
    ASSERT(worker_env_get_queue(env, "JOBS") == q);

    /* Cross-type lookups return NULL */
    ASSERT(worker_env_get_kv(env, "ASSETS") == NULL);
    ASSERT(worker_env_get_r2(env, "CACHE") == NULL);

    worker_env_destroy(env);
    worker_kv_destroy(kv);
    worker_r2_destroy(r2);
    worker_d1_destroy(d1);
    worker_queue_destroy(q);
    PASS();
}

static void test_worker_env_null_safety(void) {
    TEST("worker_env_null_safety");
    ASSERT(worker_env_bind_kv(NULL, "k", NULL) == -1);
    ASSERT(worker_env_get_kv(NULL, "k") == NULL);
    ASSERT(worker_env_get_r2(NULL, "k") == NULL);
    ASSERT(worker_env_get_d1(NULL, "k") == NULL);
    ASSERT(worker_env_get_queue(NULL, "k") == NULL);
    worker_env_destroy(NULL); /* should not crash */
    PASS();
}

/* ===== Main ===== */

int main(void) {
    printf("=== Cloudflare Worker Runtime Tests ===\n\n");

    /* Runtime info */
    test_worker_runtime_version();
    test_worker_runtime_is_supported();

    /* Request */
    test_worker_request_create();
    test_worker_request_null_params();
    test_worker_request_headers();
    test_worker_request_body();

    /* Response */
    test_worker_response_create();
    test_worker_response_status();
    test_worker_response_null_params();
    test_worker_response_headers();
    test_worker_response_body();
    test_worker_response_set_text();
    test_worker_response_set_json();

    /* Fetch handler */
    test_worker_handle_fetch_text();
    test_worker_handle_fetch_json();
    test_worker_handle_fetch_not_found();
    test_worker_handle_fetch_with_params();
    test_worker_handle_fetch_query_string();
    test_worker_handle_fetch_null();

    /* KV store */
    test_worker_kv_create();
    test_worker_kv_put_get();
    test_worker_kv_update();
    test_worker_kv_delete();
    test_worker_kv_null_params();
    test_worker_kv_key_length_validation();
    test_worker_kv_put_with_ttl();
    test_worker_kv_list();

    /* R2 object storage */
    test_worker_r2_create();
    test_worker_r2_put_get();
    test_worker_r2_head();
    test_worker_r2_delete();
    test_worker_r2_list();
    test_worker_r2_null_safety();

    /* D1 database */
    test_worker_d1_create();
    test_worker_d1_create_table_and_insert();
    test_worker_d1_query_with_where();
    test_worker_d1_null_safety();

    /* Queue */
    test_worker_queue_create();
    test_worker_queue_send();
    test_worker_queue_send_batch();
    test_worker_queue_null_safety();

    /* Env context */
    test_worker_env_create();
    test_worker_env_kv_binding();
    test_worker_env_r2_binding();
    test_worker_env_d1_binding();
    test_worker_env_queue_binding();
    test_worker_env_multi_bindings();
    test_worker_env_null_safety();

    printf("\n=== Results: %d/%d tests passed ===\n", tests_passed, tests_run);

    return (tests_passed == tests_run) ? 0 : 1;
}
