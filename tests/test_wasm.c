/*
 * test_wasm.c - Tests for WASM-compatible library features
 *
 * Validates that the WASM-safe subset of the library (JSON, router,
 * template engine, input validation) works correctly.  These tests
 * run on native builds too, exercising the same code paths that
 * Emscripten will compile.
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

/* Test macros (consistent with test_weblib.c) */
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

/* ===== WASM Capability Tests ===== */

static void test_wasm_version(void) {
    TEST("wasm_weblib_version");
    const char *ver = wasm_weblib_version();
    ASSERT(ver != NULL);
    ASSERT(strstr(ver, "weblib") != NULL);
    PASS();
}

static void test_wasm_capabilities(void) {
    TEST("wasm_weblib_capabilities");
    const char *caps = wasm_weblib_capabilities();
    ASSERT(caps != NULL);
    ASSERT(strstr(caps, "json") != NULL);
    ASSERT(strstr(caps, "router") != NULL);
    ASSERT(strstr(caps, "template") != NULL);
    ASSERT(strstr(caps, "validation") != NULL);
    PASS();
}

static void test_wasm_has_capability(void) {
    TEST("wasm_weblib_has_capability");
    ASSERT(wasm_weblib_has_capability("json") == true);
    ASSERT(wasm_weblib_has_capability("router") == true);
    ASSERT(wasm_weblib_has_capability("template") == true);
    ASSERT(wasm_weblib_has_capability("validation") == true);
    ASSERT(wasm_weblib_has_capability("cookie") == true);
    ASSERT(wasm_weblib_has_capability("body_parser") == true);
    ASSERT(wasm_weblib_has_capability("compression") == true);
    ASSERT(wasm_weblib_has_capability("nonexistent") == false);
    ASSERT(wasm_weblib_has_capability(NULL) == false);
    PASS();
}

/* ===== WASM JSON Tests ===== */

static void test_wasm_json_object(void) {
    TEST("wasm_json_object_create_and_set");
    json_value_t *obj = wasm_json_object_create();
    ASSERT(obj != NULL);

    wasm_json_object_set(obj, "name", wasm_json_string_create("test"));
    wasm_json_object_set(obj, "count", wasm_json_number_create(42));
    wasm_json_object_set(obj, "active", wasm_json_bool_create(true));

    json_value_t *name = wasm_json_object_get(obj, "name");
    ASSERT(name != NULL);

    json_value_t *count = wasm_json_object_get(obj, "count");
    ASSERT(count != NULL);

    wasm_json_free(obj);
    PASS();
}

static void test_wasm_json_array(void) {
    TEST("wasm_json_array_create_and_push");
    json_value_t *arr = wasm_json_array_create();
    ASSERT(arr != NULL);

    wasm_json_array_push(arr, wasm_json_number_create(1));
    wasm_json_array_push(arr, wasm_json_number_create(2));
    wasm_json_array_push(arr, wasm_json_string_create("three"));

    char *str = wasm_json_stringify(arr);
    ASSERT(str != NULL);
    ASSERT(strstr(str, "1") != NULL);
    ASSERT(strstr(str, "\"three\"") != NULL);

    wasm_free(str);
    wasm_json_free(arr);
    PASS();
}

static void test_wasm_json_parse(void) {
    TEST("wasm_json_parse");
    const char *input = "{\"key\":\"value\",\"num\":123}";
    json_value_t *parsed = wasm_json_parse(input);
    ASSERT(parsed != NULL);

    json_value_t *key = wasm_json_object_get(parsed, "key");
    ASSERT(key != NULL);

    json_value_t *num = wasm_json_object_get(parsed, "num");
    ASSERT(num != NULL);

    wasm_json_free(parsed);
    PASS();
}

static void test_wasm_json_stringify(void) {
    TEST("wasm_json_stringify");
    json_value_t *obj = wasm_json_object_create();
    wasm_json_object_set(obj, "hello", wasm_json_string_create("world"));

    char *str = wasm_json_stringify(obj);
    ASSERT(str != NULL);
    ASSERT(strstr(str, "\"hello\"") != NULL);
    ASSERT(strstr(str, "\"world\"") != NULL);

    wasm_free(str);
    wasm_json_free(obj);
    PASS();
}

static void test_wasm_json_null(void) {
    TEST("wasm_json_null_create");
    json_value_t *val = wasm_json_null_create();
    ASSERT(val != NULL);

    char *str = wasm_json_stringify(val);
    ASSERT(str != NULL);
    ASSERT(strcmp(str, "null") == 0);

    wasm_free(str);
    wasm_json_free(val);
    PASS();
}

static void test_wasm_json_null_input(void) {
    TEST("wasm_json_parse_null_input");
    ASSERT(wasm_json_parse(NULL) == NULL);
    ASSERT(wasm_json_stringify(NULL) == NULL);
    PASS();
}

/* ===== WASM Router Tests ===== */

static void dummy_handler(http_request_t *req, http_response_t *res) {
    (void)req;
    (void)res;
}

static void test_wasm_router(void) {
    TEST("wasm_router_create_and_add_route");
    router_t *router = wasm_router_create();
    ASSERT(router != NULL);

    int ret = wasm_router_add_route(router, HTTP_GET, "/api/test", dummy_handler);
    ASSERT(ret == 0);

    ret = wasm_router_add_route(router, HTTP_POST, "/api/data", dummy_handler);
    ASSERT(ret == 0);

    wasm_router_destroy(router);
    PASS();
}

/* ===== WASM Input Validation Tests ===== */

static void test_wasm_validate_email(void) {
    TEST("wasm_validate_email");
    ASSERT(wasm_validate_email("user@example.com") == true);
    ASSERT(wasm_validate_email("invalid") == false);
    ASSERT(wasm_validate_email(NULL) == false);
    PASS();
}

static void test_wasm_validate_integer(void) {
    TEST("wasm_validate_integer");
    int val = 0;
    ASSERT(wasm_validate_integer("42", &val) == true);
    ASSERT(val == 42);
    ASSERT(wasm_validate_integer("abc", &val) == false);
    ASSERT(wasm_validate_integer(NULL, &val) == false);
    PASS();
}

static void test_wasm_validate_string_length(void) {
    TEST("wasm_validate_string_length");
    ASSERT(wasm_validate_string_length("hello", 1, 10) == true);
    ASSERT(wasm_validate_string_length("hi", 5, 10) == false);
    ASSERT(wasm_validate_string_length(NULL, 0, 10) == false);
    PASS();
}

/* ===== WASM Template Tests ===== */

static void test_wasm_template(void) {
    TEST("wasm_template_render");
    template_context_t *ctx = wasm_template_context_create();
    ASSERT(ctx != NULL);

    wasm_template_context_set(ctx, "name", "World");
    wasm_template_context_set(ctx, "title", "Test");

    char *result = wasm_template_render("Hello {{name}}! Title: {{title}}", ctx);
    ASSERT(result != NULL);
    ASSERT(strstr(result, "Hello World!") != NULL);
    ASSERT(strstr(result, "Title: Test") != NULL);

    wasm_free(result);
    wasm_template_context_destroy(ctx);
    PASS();
}

/* ===== WASM Memory Management Tests ===== */

static void test_wasm_free(void) {
    TEST("wasm_free");
    /* wasm_free should handle NULL safely */
    wasm_free(NULL);

    /* Allocate and free through JSON stringify */
    json_value_t *val = wasm_json_string_create("test");
    char *str = wasm_json_stringify(val);
    ASSERT(str != NULL);
    wasm_free(str);
    wasm_json_free(val);
    PASS();
}

/* ===== WASM Export Macro Test ===== */

static void test_wasm_export_macro(void) {
    TEST("WASM_EXPORT macro defined");
    /* WEBLIB_WASM is always defined: 1 under Emscripten, 0 otherwise */
#if WEBLIB_WASM
    ASSERT(WEBLIB_WASM == 1);
#else
    ASSERT(WEBLIB_WASM == 0);
#endif
    PASS();
}

/* ===== Main ===== */

int main(void) {
    printf("=== WASM Capability Tests ===\n\n");

    /* Capability queries */
    test_wasm_version();
    test_wasm_capabilities();
    test_wasm_has_capability();

    /* JSON */
    test_wasm_json_object();
    test_wasm_json_array();
    test_wasm_json_parse();
    test_wasm_json_stringify();
    test_wasm_json_null();
    test_wasm_json_null_input();

    /* Router */
    test_wasm_router();

    /* Input validation */
    test_wasm_validate_email();
    test_wasm_validate_integer();
    test_wasm_validate_string_length();

    /* Template engine */
    test_wasm_template();

    /* Memory */
    test_wasm_free();

    /* Export macro */
    test_wasm_export_macro();

    printf("\n=== Results: %d/%d tests passed ===\n", tests_passed, tests_run);

    return (tests_passed == tests_run) ? 0 : 1;
}
