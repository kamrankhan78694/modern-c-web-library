/*
 * test_worker.c - Tests for Cloudflare Worker Runtime
 *
 * Validates the Worker request/response abstraction, fetch handler
 * integration with the router, and the in-memory KV store.
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

    printf("\n=== Results: %d/%d tests passed ===\n", tests_passed, tests_run);

    return (tests_passed == tests_run) ? 0 : 1;
}
