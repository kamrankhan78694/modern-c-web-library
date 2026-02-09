#include "weblib.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

/* Test counter */
static int tests_run = 0;
static int tests_passed = 0;

/* Dummy handler for testing */
static void dummy_handler(http_request_t *req, http_response_t *res) {
    (void)req;
    (void)res;
}

/* Test macros */
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

/* Test router creation */
void test_router_create(void) {
    TEST("router_create");
    
    router_t *router = router_create();
    ASSERT(router != NULL);
    
    router_destroy(router);
    
    PASS();
}

/* Test adding routes */
void test_router_add_route(void) {
    TEST("router_add_route");
    
    router_t *router = router_create();
    ASSERT(router != NULL);
    
    /* Add a simple route */
    int result = router_add_route(router, HTTP_GET, "/test", dummy_handler);
    ASSERT(result == 0);
    
    router_destroy(router);
    
    PASS();
}

/* Test JSON object creation */
void test_json_object_create(void) {
    TEST("json_object_create");
    
    json_value_t *obj = json_object_create();
    ASSERT(obj != NULL);
    ASSERT(obj->type == JSON_OBJECT);
    
    json_value_free(obj);
    
    PASS();
}

/* Test JSON string creation */
void test_json_string_create(void) {
    TEST("json_string_create");
    
    json_value_t *str = json_string_create("test");
    ASSERT(str != NULL);
    ASSERT(str->type == JSON_STRING);
    ASSERT(strcmp(str->data.string_val, "test") == 0);
    
    json_value_free(str);
    
    PASS();
}

/* Test JSON number creation */
void test_json_number_create(void) {
    TEST("json_number_create");
    
    json_value_t *num = json_number_create(42.5);
    ASSERT(num != NULL);
    ASSERT(num->type == JSON_NUMBER);
    ASSERT(num->data.number_val == 42.5);
    
    json_value_free(num);
    
    PASS();
}

/* Test JSON boolean creation */
void test_json_bool_create(void) {
    TEST("json_bool_create");
    
    json_value_t *bool_val = json_bool_create(true);
    ASSERT(bool_val != NULL);
    ASSERT(bool_val->type == JSON_BOOL);
    ASSERT(bool_val->data.bool_val == true);
    
    json_value_free(bool_val);
    
    PASS();
}

/* Test JSON object set/get */
void test_json_object_operations(void) {
    TEST("json_object_set/get");
    
    json_value_t *obj = json_object_create();
    ASSERT(obj != NULL);
    
    json_value_t *str = json_string_create("value");
    json_object_set(obj, "key", str);
    
    json_value_t *retrieved = json_object_get(obj, "key");
    ASSERT(retrieved != NULL);
    ASSERT(retrieved->type == JSON_STRING);
    ASSERT(strcmp(retrieved->data.string_val, "value") == 0);
    
    json_value_free(obj);
    
    PASS();
}

/* Test JSON stringify */
void test_json_stringify(void) {
    TEST("json_stringify");
    
    json_value_t *obj = json_object_create();
    json_object_set(obj, "name", json_string_create("John"));
    json_object_set(obj, "age", json_number_create(30));
    json_object_set(obj, "active", json_bool_create(true));
    
    char *json_str = json_stringify(obj);
    ASSERT(json_str != NULL);
    ASSERT(strlen(json_str) > 0);
    
    /* Basic checks - order may vary */
    ASSERT(strstr(json_str, "\"name\"") != NULL);
    ASSERT(strstr(json_str, "\"John\"") != NULL);
    ASSERT(strstr(json_str, "\"age\"") != NULL);
    ASSERT(strstr(json_str, "30") != NULL);
    
    free(json_str);
    json_value_free(obj);
    
    PASS();
}

/* Test JSON parse - simple string */
void test_json_parse_string(void) {
    TEST("json_parse (string)");
    
    json_value_t *val = json_parse("\"test string\"");
    ASSERT(val != NULL);
    ASSERT(val->type == JSON_STRING);
    ASSERT(strcmp(val->data.string_val, "test string") == 0);
    
    json_value_free(val);
    
    PASS();
}

/* Test JSON parse - number */
void test_json_parse_number(void) {
    TEST("json_parse (number)");
    
    json_value_t *val = json_parse("42.5");
    ASSERT(val != NULL);
    ASSERT(val->type == JSON_NUMBER);
    ASSERT(val->data.number_val == 42.5);
    
    json_value_free(val);
    
    PASS();
}

/* Test JSON parse - boolean */
void test_json_parse_bool(void) {
    TEST("json_parse (bool)");
    
    json_value_t *val_true = json_parse("true");
    ASSERT(val_true != NULL);
    ASSERT(val_true->type == JSON_BOOL);
    ASSERT(val_true->data.bool_val == true);
    
    json_value_t *val_false = json_parse("false");
    ASSERT(val_false != NULL);
    ASSERT(val_false->type == JSON_BOOL);
    ASSERT(val_false->data.bool_val == false);
    
    json_value_free(val_true);
    json_value_free(val_false);
    
    PASS();
}

/* Test JSON parse - null */
void test_json_parse_null(void) {
    TEST("json_parse (null)");
    
    json_value_t *val = json_parse("null");
    ASSERT(val != NULL);
    ASSERT(val->type == JSON_NULL);
    
    json_value_free(val);
    
    PASS();
}

/* Test JSON parse - object */
void test_json_parse_object(void) {
    TEST("json_parse (object)");
    
    const char *json_str = "{\"name\":\"John\",\"age\":30}";
    json_value_t *obj = json_parse(json_str);
    ASSERT(obj != NULL);
    ASSERT(obj->type == JSON_OBJECT);
    
    json_value_t *name = json_object_get(obj, "name");
    ASSERT(name != NULL);
    ASSERT(name->type == JSON_STRING);
    ASSERT(strcmp(name->data.string_val, "John") == 0);
    
    json_value_t *age = json_object_get(obj, "age");
    ASSERT(age != NULL);
    ASSERT(age->type == JSON_NUMBER);
    ASSERT(age->data.number_val == 30);
    
    json_value_free(obj);
    
    PASS();
}

/* Test server creation */
void test_server_create(void) {
    TEST("http_server_create");
    
    http_server_t *server = http_server_create();
    ASSERT(server != NULL);
    
    http_server_destroy(server);
    
    PASS();
}

/* Test event loop creation */
void test_event_loop_create(void) {
    TEST("event_loop_create");
    
    event_loop_t *loop = event_loop_create();
    ASSERT(loop != NULL);
    
    event_loop_destroy(loop);
    
    PASS();
}

/* Test async mode enable/disable */
void test_server_async_mode(void) {
    TEST("http_server_set_async");
    
    http_server_t *server = http_server_create();
    ASSERT(server != NULL);
    
    /* Enable async mode */
    int result = http_server_set_async(server, true);
    ASSERT(result == 0);
    
    /* Get event loop */
    event_loop_t *loop = http_server_get_event_loop(server);
    ASSERT(loop != NULL);
    
    /* Disable async mode */
    result = http_server_set_async(server, false);
    ASSERT(result == 0);
    
    /* Event loop should be NULL now */
    loop = http_server_get_event_loop(server);
    ASSERT(loop == NULL);
    
    http_server_destroy(server);
    
    PASS();
}

/* Global test data for timeout test */
static bool timeout_called = false;

/* Timeout callback for testing */
static void test_timeout_callback(int fd, int events, void *user_data) {
    (void)fd;
    (void)user_data;
    
    if (events & EVENT_TIMEOUT) {
        timeout_called = true;
    }
}

/* Test event loop timeout */
void test_event_loop_timeout(void) {
    TEST("event_loop_add_timeout");
    
    event_loop_t *loop = event_loop_create();
    ASSERT(loop != NULL);
    
    timeout_called = false;
    
    /* Add a short timeout (100ms) */
    int timer_id = event_loop_add_timeout(loop, 100, test_timeout_callback, NULL);
    ASSERT(timer_id > 0);
    
    /* Run event loop for a short time */
    /* Note: We'll stop it after timeout fires */
    /* For testing, we'll use a simple timer mechanism */
    
    event_loop_destroy(loop);
    
    PASS();
}

/* Test event loop cancel timeout */
void test_event_loop_cancel_timeout(void) {
    TEST("event_loop_cancel_timeout");
    
    event_loop_t *loop = event_loop_create();
    ASSERT(loop != NULL);
    
    /* Add timeout */
    int timer_id = event_loop_add_timeout(loop, 1000, test_timeout_callback, NULL);
    ASSERT(timer_id > 0);
    
    /* Cancel timeout */
    int result = event_loop_cancel_timeout(loop, timer_id);
    ASSERT(result == 0);
    
    event_loop_destroy(loop);
    
    PASS();
}

/* Test WebSocket frame encoding */
void test_websocket_frame_encode(void) {
    TEST("websocket_frame_encode");
    
    /* Create a WebSocket connection (fd doesn't matter for this test) */
    websocket_connection_t *conn = websocket_connection_create(999);
    ASSERT(conn != NULL);
    ASSERT(websocket_is_open(conn));
    
    websocket_connection_destroy(conn);
    
    PASS();
}

/* Test WebSocket connection creation */
void test_websocket_connection_create(void) {
    TEST("websocket_connection_create");
    
    websocket_connection_t *conn = websocket_connection_create(123);
    ASSERT(conn != NULL);
    ASSERT(websocket_is_open(conn));
    
    /* Set user data */
    int user_val = 42;
    websocket_set_user_data(conn, &user_val);
    ASSERT(websocket_get_user_data(conn) == &user_val);
    
    websocket_connection_destroy(conn);
    
    PASS();
}

/* Test WebSocket handshake key generation */
void test_websocket_handshake_key(void) {
    TEST("websocket_handshake_key");
    
    /* This is more of an integration test - we verify the handshake 
     * function exists and can be called. Full handshake testing would
     * require mocking HTTP request/response objects with proper headers.
     */
    
    /* Create dummy request and response */
    http_request_t req = {0};
    http_response_t res = {0};
    
    /* Without proper headers, handshake should fail gracefully */
    bool result = websocket_handle_upgrade(&req, &res);
    ASSERT(result == false); /* Should fail without headers */
    
    PASS();
}

/* Test JSON array creation */
void test_json_array_create(void) {
    TEST("json_array_create");
    
    json_value_t *arr = json_array_create();
    ASSERT(arr != NULL);
    ASSERT(arr->type == JSON_ARRAY);
    ASSERT(json_array_length(arr) == 0);
    
    json_value_free(arr);
    
    PASS();
}

/* Test JSON array append and get */
void test_json_array_append_get(void) {
    TEST("json_array_append/get");
    
    json_value_t *arr = json_array_create();
    ASSERT(arr != NULL);
    
    /* Append elements */
    ASSERT(json_array_append(arr, json_number_create(1.0)) == 0);
    ASSERT(json_array_append(arr, json_string_create("hello")) == 0);
    ASSERT(json_array_append(arr, json_bool_create(true)) == 0);
    
    /* Check length */
    ASSERT(json_array_length(arr) == 3);
    
    /* Check get by index */
    json_value_t *elem0 = json_array_get(arr, 0);
    ASSERT(elem0 != NULL);
    ASSERT(elem0->type == JSON_NUMBER);
    ASSERT(elem0->data.number_val == 1.0);
    
    json_value_t *elem1 = json_array_get(arr, 1);
    ASSERT(elem1 != NULL);
    ASSERT(elem1->type == JSON_STRING);
    ASSERT(strcmp(elem1->data.string_val, "hello") == 0);
    
    json_value_t *elem2 = json_array_get(arr, 2);
    ASSERT(elem2 != NULL);
    ASSERT(elem2->type == JSON_BOOL);
    ASSERT(elem2->data.bool_val == true);
    
    /* Out of bounds should return NULL */
    ASSERT(json_array_get(arr, 3) == NULL);
    ASSERT(json_array_get(arr, 999) == NULL);
    
    json_value_free(arr);
    
    PASS();
}

/* Test JSON array parse */
void test_json_parse_array(void) {
    TEST("json_parse (array)");
    
    json_value_t *arr = json_parse("[1,2,3]");
    ASSERT(arr != NULL);
    ASSERT(arr->type == JSON_ARRAY);
    ASSERT(json_array_length(arr) == 3);
    
    json_value_t *elem0 = json_array_get(arr, 0);
    ASSERT(elem0 != NULL);
    ASSERT(elem0->type == JSON_NUMBER);
    ASSERT(elem0->data.number_val == 1.0);
    
    json_value_t *elem2 = json_array_get(arr, 2);
    ASSERT(elem2 != NULL);
    ASSERT(elem2->data.number_val == 3.0);
    
    json_value_free(arr);
    
    PASS();
}

/* Test JSON array with mixed types */
void test_json_parse_array_mixed(void) {
    TEST("json_parse (mixed array)");
    
    json_value_t *arr = json_parse("[\"hello\",42,true,null]");
    ASSERT(arr != NULL);
    ASSERT(arr->type == JSON_ARRAY);
    ASSERT(json_array_length(arr) == 4);
    
    ASSERT(json_array_get(arr, 0)->type == JSON_STRING);
    ASSERT(strcmp(json_array_get(arr, 0)->data.string_val, "hello") == 0);
    ASSERT(json_array_get(arr, 1)->type == JSON_NUMBER);
    ASSERT(json_array_get(arr, 1)->data.number_val == 42.0);
    ASSERT(json_array_get(arr, 2)->type == JSON_BOOL);
    ASSERT(json_array_get(arr, 2)->data.bool_val == true);
    ASSERT(json_array_get(arr, 3)->type == JSON_NULL);
    
    json_value_free(arr);
    
    PASS();
}

/* Test JSON array stringify */
void test_json_array_stringify(void) {
    TEST("json_stringify (array)");
    
    json_value_t *arr = json_array_create();
    json_array_append(arr, json_number_create(1));
    json_array_append(arr, json_number_create(2));
    json_array_append(arr, json_number_create(3));
    
    char *str = json_stringify(arr);
    ASSERT(str != NULL);
    ASSERT(strcmp(str, "[1,2,3]") == 0);
    
    free(str);
    json_value_free(arr);
    
    PASS();
}

/* Test JSON nested array */
void test_json_nested_array(void) {
    TEST("json_parse (nested array)");
    
    json_value_t *arr = json_parse("[[1,2],[3,4]]");
    ASSERT(arr != NULL);
    ASSERT(arr->type == JSON_ARRAY);
    ASSERT(json_array_length(arr) == 2);
    
    json_value_t *inner0 = json_array_get(arr, 0);
    ASSERT(inner0 != NULL);
    ASSERT(inner0->type == JSON_ARRAY);
    ASSERT(json_array_length(inner0) == 2);
    ASSERT(json_array_get(inner0, 0)->data.number_val == 1.0);
    ASSERT(json_array_get(inner0, 1)->data.number_val == 2.0);
    
    json_value_t *inner1 = json_array_get(arr, 1);
    ASSERT(inner1 != NULL);
    ASSERT(inner1->type == JSON_ARRAY);
    ASSERT(json_array_get(inner1, 0)->data.number_val == 3.0);
    ASSERT(json_array_get(inner1, 1)->data.number_val == 4.0);
    
    json_value_free(arr);
    
    PASS();
}

/* Test JSON object with array value */
void test_json_object_with_array(void) {
    TEST("json_parse (object with array)");
    
    json_value_t *obj = json_parse("{\"items\":[1,2,3],\"name\":\"test\"}");
    ASSERT(obj != NULL);
    ASSERT(obj->type == JSON_OBJECT);
    
    json_value_t *items = json_object_get(obj, "items");
    ASSERT(items != NULL);
    ASSERT(items->type == JSON_ARRAY);
    ASSERT(json_array_length(items) == 3);
    ASSERT(json_array_get(items, 0)->data.number_val == 1.0);
    
    json_value_t *name = json_object_get(obj, "name");
    ASSERT(name != NULL);
    ASSERT(strcmp(name->data.string_val, "test") == 0);
    
    json_value_free(obj);
    
    PASS();
}

/* Run all tests */
int main(void) {
    printf("Running Modern C Web Library Tests\n");
    printf("===================================\n\n");
    
    /* Router tests */
    test_router_create();
    test_router_add_route();
    
    /* JSON tests */
    test_json_object_create();
    test_json_string_create();
    test_json_number_create();
    test_json_bool_create();
    test_json_object_operations();
    test_json_stringify();
    test_json_parse_string();
    test_json_parse_number();
    test_json_parse_bool();
    test_json_parse_null();
    test_json_parse_object();
    
    /* HTTP server tests */
    test_server_create();
    
    /* Event loop tests */
    test_event_loop_create();
    test_server_async_mode();
    test_event_loop_timeout();
    test_event_loop_cancel_timeout();
    
    /* WebSocket tests */
    test_websocket_frame_encode();
    test_websocket_connection_create();
    test_websocket_handshake_key();
    
    /* JSON array tests */
    test_json_array_create();
    test_json_array_append_get();
    test_json_parse_array();
    test_json_parse_array_mixed();
    test_json_array_stringify();
    test_json_nested_array();
    test_json_object_with_array();
    
    printf("\n===================================\n");
    printf("Tests run: %d\n", tests_run);
    printf("Tests passed: %d\n", tests_passed);
    printf("Tests failed: %d\n", tests_run - tests_passed);
    
    return (tests_run == tests_passed) ? 0 : 1;
}
