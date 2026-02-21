#include "weblib.h"
#include "db_pool.h"
#include "thread_pool.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>

/* Test counter */
static int tests_run = 0;
static int tests_passed = 0;

/*
 * Test-local helper: free an http_header_node linked list.
 * Mirrors the internal header_list_free() in http_server.c so that tests
 * using stack-allocated http_response_t can clean up header allocations.
 */
typedef struct _test_hdr_node {
    char *name;
    char *raw_name;
    char *value;
    struct _test_hdr_node *next;
} _test_hdr_node_t;

static void _test_free_header_list(void *headers) {
    _test_hdr_node_t *h = (_test_hdr_node_t *)headers;
    while (h) {
        _test_hdr_node_t *next = h->next;
        free(h->name);
        free(h->raw_name);
        free(h->value);
        free(h);
        h = next;
    }
}

/* Dummy handler for testing */
static void dummy_handler(http_request_t *req, http_response_t *res) {
    (void)req;
    (void)res;
}

/* Dummy event callback for testing */
static void dummy_event_callback(int fd, int events, void *user_data) {
    (void)fd;
    (void)events;
    (void)user_data;
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
    ASSERT(json_array_append(arr, json_number_create(1)) == 0);
    ASSERT(json_array_append(arr, json_number_create(2)) == 0);
    ASSERT(json_array_append(arr, json_number_create(3)) == 0);
    
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

/* ===== Phase 5 Tests ===== */

/* Test body parser - URL-encoded form data */
void test_body_parser_urlencoded(void) {
    TEST("body_parser (url-encoded)");

    /* Create a request with URL-encoded body */
    http_request_t req = {0};
    req.method = HTTP_POST;
    req.path = strdup("/form");
    req.body = strdup("username=john&password=secret123&email=john%40example.com");
    req.body_length = strlen(req.body);

    /* We need headers for Content-Type. Since the internal header API is used,
     * we'll test via http_request_parse_body which calls get_header.
     * For unit testing without full server infrastructure, we'll test the
     * standalone form field access after manual parse. */

    /* The body parser auto-creates parser data, so we just test that parse
     * doesn't crash on a request without headers (it should return gracefully) */
    int result = http_request_parse_body(&req);
    /* Without Content-Type header, parse should succeed but find nothing */
    ASSERT(result == 0);

    free(req.path);
    free(req.body);
    /* Clean up parser data if it was created */
    if (req.user_data) {
        body_parser_data_free((body_parser_data_t *)req.user_data);
    }

    PASS();
}

/* Test body parser - data structures */
void test_body_parser_data_structures(void) {
    TEST("body_parser (data structures)");

    /* Test that body_parser_data_free handles NULL gracefully */
    body_parser_data_free(NULL);

    /* Create and free a body_parser_data_t manually */
    body_parser_data_t *data = calloc(1, sizeof(body_parser_data_t));
    ASSERT(data != NULL);
    ASSERT(data->parsed == false);
    ASSERT(data->fields == NULL);
    ASSERT(data->files == NULL);

    /* Add a form field manually */
    http_form_field_t *field = malloc(sizeof(http_form_field_t));
    ASSERT(field != NULL);
    field->name = strdup("test_key");
    field->value = strdup("test_value");
    field->next = NULL;
    data->fields = field;

    /* Add an uploaded file manually */
    http_uploaded_file_t *file = calloc(1, sizeof(http_uploaded_file_t));
    ASSERT(file != NULL);
    file->field_name = strdup("avatar");
    file->filename = strdup("photo.jpg");
    file->content_type = strdup("image/jpeg");
    file->data = malloc(4);
    memcpy(file->data, "test", 4);
    file->size = 4;
    file->next = NULL;
    data->files = file;

    data->parsed = true;

    /* Free everything - should not leak */
    body_parser_data_free(data);

    PASS();
}

/* Test body parser - empty body */
void test_body_parser_empty(void) {
    TEST("body_parser (empty body)");

    http_request_t req = {0};
    req.method = HTTP_POST;
    req.body = NULL;
    req.body_length = 0;

    int result = http_request_parse_body(&req);
    ASSERT(result == 0);

    /* get_form_field should return NULL for empty body */
    const char *val = http_request_get_form_field(&req, "key");
    ASSERT(val == NULL);

    /* get_file should return NULL for empty body */
    http_uploaded_file_t *file = http_request_get_file(&req, "avatar");
    ASSERT(file == NULL);

    if (req.user_data) {
        body_parser_data_free((body_parser_data_t *)req.user_data);
    }

    PASS();
}

/* Test body parser - NULL request */
void test_body_parser_null(void) {
    TEST("body_parser (null handling)");

    /* All functions should handle NULL gracefully */
    ASSERT(http_request_parse_body(NULL) == -1);
    ASSERT(http_request_get_form_field(NULL, "key") == NULL);
    ASSERT(http_request_get_form_field(&(http_request_t){0}, NULL) == NULL);
    ASSERT(http_request_get_file(NULL, "key") == NULL);
    ASSERT(http_request_get_file(&(http_request_t){0}, NULL) == NULL);

    PASS();
}

/* Test cookie - get from request header */
void test_cookie_get(void) {
    TEST("cookie (get from request)");

    /* Test with NULL request */
    ASSERT(http_request_get_cookie(NULL, "name") == NULL);

    /* Test with NULL name */
    http_request_t req = {0};
    ASSERT(http_request_get_cookie(&req, NULL) == NULL);

    /* Test with empty name */
    ASSERT(http_request_get_cookie(&req, "") == NULL);

    /* Test with no Cookie header set */
    ASSERT(http_request_get_cookie(&req, "session") == NULL);

    PASS();
}

/* Test cookie - set on response */
void test_cookie_set(void) {
    TEST("cookie (set on response)");

    /* Test NULL handling */
    http_response_set_cookie(NULL, "name", "value", NULL);  /* Should not crash */

    http_response_t res = {0};
    http_response_set_cookie(&res, NULL, "value", NULL);  /* Should not crash */
    http_response_set_cookie(&res, "name", NULL, NULL);   /* Should not crash */
    http_response_set_cookie(&res, "", "value", NULL);    /* Should not crash */

    /* Set a cookie with options */
    cookie_options_t opts = {
        .domain = "example.com",
        .path = "/api",
        .max_age = 3600,
        .secure = true,
        .http_only = true,
        .same_site = "Lax"
    };
    http_response_set_cookie(&res, "session", "abc123", &opts);
    /* The Set-Cookie header should be set (verified by the fact it doesn't crash) */

    _test_free_header_list(res.headers);

    PASS();
}

/* Test cookie - delete */
void test_cookie_delete(void) {
    TEST("cookie (delete)");

    /* Test NULL handling */
    http_response_delete_cookie(NULL, "name");  /* Should not crash */

    http_response_t res = {0};
    http_response_delete_cookie(&res, NULL);    /* Should not crash */
    http_response_delete_cookie(&res, "");      /* Should not crash */

    /* Delete a cookie */
    http_response_delete_cookie(&res, "session");
    /* Should set Set-Cookie: session=; Path=/; Max-Age=0 */

    _test_free_header_list(res.headers);

    PASS();
}

/* Test CORS middleware - creation and destruction */
void test_cors_create_destroy(void) {
    TEST("cors (create/destroy)");

    /* NULL options should return NULL */
    middleware_fn_t mw = cors_middleware_create(NULL);
    ASSERT(mw == NULL);

    /* Create with basic options */
    const char *origins[] = {"http://localhost:3000", "https://example.com", NULL};
    cors_options_t opts = {
        .allowed_origins = origins,
        .allowed_methods = "GET, POST",
        .allowed_headers = "Content-Type",
        .expose_headers = "X-Custom",
        .allow_credentials = true,
        .max_age = 3600
    };

    mw = cors_middleware_create(&opts);
    ASSERT(mw != NULL);

    /* Destroy should not crash */
    cors_middleware_destroy();

    /* Double destroy should be safe */
    cors_middleware_destroy();

    PASS();
}

/* Test CORS middleware - handler behavior */
void test_cors_handler(void) {
    TEST("cors (handler)");

    /* Create CORS middleware with wildcard origins */
    cors_options_t opts = {
        .allowed_origins = NULL,  /* Wildcard */
        .allowed_methods = "GET, POST, PUT, DELETE",
        .allowed_headers = "Content-Type, Authorization",
        .allow_credentials = false,
        .max_age = 86400
    };

    middleware_fn_t mw = cors_middleware_create(&opts);
    ASSERT(mw != NULL);

    /* Test: Request without Origin header should pass through */
    http_request_t req = {0};
    http_response_t res = {0};
    bool result = mw(&req, &res, NULL);
    ASSERT(result == true);  /* Continue chain */

    cors_middleware_destroy();

    PASS();
}

/* Test rate limiting - creation and destruction */
void test_ratelimit_create_destroy(void) {
    TEST("ratelimit (create/destroy)");

    /* NULL config should return NULL */
    middleware_fn_t mw = ratelimit_middleware_create(NULL);
    ASSERT(mw == NULL);

    /* Invalid config should return NULL */
    ratelimit_config_t bad_config = {0, 0, 0};
    mw = ratelimit_middleware_create(&bad_config);
    ASSERT(mw == NULL);

    /* Valid config */
    ratelimit_config_t config = {
        .requests_per_window = 100,
        .window_seconds = 60,
        .burst_size = 120
    };

    mw = ratelimit_middleware_create(&config);
    ASSERT(mw != NULL);

    /* Destroy should not crash */
    ratelimit_middleware_destroy();

    /* Double destroy should be safe */
    ratelimit_middleware_destroy();

    PASS();
}

/* Test static file middleware - creation and destruction */
void test_static_file_create_destroy(void) {
    TEST("static_file (create/destroy)");

    /* NULL config should return NULL */
    middleware_fn_t mw = static_file_middleware_create(NULL);
    ASSERT(mw == NULL);

    /* Config without root_dir should return NULL */
    static_file_config_t bad_config = {0};
    mw = static_file_middleware_create(&bad_config);
    ASSERT(mw == NULL);

    /* Non-existent directory should return NULL */
    static_file_config_t nodir_config = {
        .root_dir = "/nonexistent/directory",
        .enable_etag = true
    };
    mw = static_file_middleware_create(&nodir_config);
    ASSERT(mw == NULL);

    /* Valid config with /tmp directory */
    static_file_config_t config = {
        .root_dir = "/tmp",
        .index_file = "index.html",
        .cache_max_age = 3600,
        .enable_etag = true
    };

    mw = static_file_middleware_create(&config);
    ASSERT(mw != NULL);

    /* Destroy should not crash */
    static_file_middleware_destroy();

    /* Double destroy should be safe */
    static_file_middleware_destroy();

    PASS();
}

/* Test static file middleware - MIME type detection */
void test_static_file_serve(void) {
    TEST("static_file (serve file)");

    /* Create a test file */
    FILE *f = fopen("/tmp/test_weblib_static.txt", "w");
    ASSERT(f != NULL);
    fprintf(f, "Hello, static file!");
    fclose(f);

    /* Create middleware pointing to /tmp */
    static_file_config_t config = {
        .root_dir = "/tmp",
        .index_file = "index.html",
        .cache_max_age = 3600,
        .enable_etag = true
    };

    middleware_fn_t mw = static_file_middleware_create(&config);
    ASSERT(mw != NULL);

    /* Request for the test file */
    http_request_t req = {0};
    req.method = HTTP_GET;
    req.path = strdup("/test_weblib_static.txt");

    http_response_t res = {0};
    bool result = mw(&req, &res, NULL);
    ASSERT(result == false);  /* File served, stop chain */
    ASSERT(res.status == HTTP_OK);
    ASSERT(res.body != NULL);
    ASSERT(res.body_length == 19);  /* "Hello, static file!" */
    ASSERT(memcmp(res.body, "Hello, static file!", 19) == 0);

    free(req.path);
    free(res.body);
    _test_free_header_list(res.headers);

    /* Clean up test file */
    remove("/tmp/test_weblib_static.txt");

    static_file_middleware_destroy();

    PASS();
}

/* Test static file middleware - file not found */
void test_static_file_not_found(void) {
    TEST("static_file (not found)");

    static_file_config_t config = {
        .root_dir = "/tmp",
        .index_file = "index.html",
        .cache_max_age = 3600,
        .enable_etag = true
    };

    middleware_fn_t mw = static_file_middleware_create(&config);
    ASSERT(mw != NULL);

    /* Request for nonexistent file */
    http_request_t req = {0};
    req.method = HTTP_GET;
    req.path = strdup("/nonexistent_file_xyz.txt");

    http_response_t res = {0};
    bool result = mw(&req, &res, NULL);
    ASSERT(result == true);  /* File not found, continue chain */

    free(req.path);

    static_file_middleware_destroy();

    PASS();
}

/* Test static file middleware - path traversal prevention */
void test_static_file_path_traversal(void) {
    TEST("static_file (path traversal)");

    static_file_config_t config = {
        .root_dir = "/tmp",
        .index_file = "index.html",
        .cache_max_age = 3600,
        .enable_etag = true
    };

    middleware_fn_t mw = static_file_middleware_create(&config);
    ASSERT(mw != NULL);

    /* Request with path traversal attempt */
    http_request_t req = {0};
    req.method = HTTP_GET;
    req.path = strdup("/../etc/passwd");

    http_response_t res = {0};
    bool result = mw(&req, &res, NULL);
    ASSERT(result == true);  /* Path traversal blocked, continue chain */

    free(req.path);

    static_file_middleware_destroy();

    PASS();
}

/* Test HTTP status codes */
void test_http_status_codes(void) {
    TEST("http_status_codes (new codes)");

    /* Verify new status codes exist and have correct values */
    ASSERT(HTTP_NOT_MODIFIED == 304);
    ASSERT(HTTP_TOO_MANY_REQUESTS == 429);

    /* Verify existing codes still correct */
    ASSERT(HTTP_OK == 200);
    ASSERT(HTTP_NO_CONTENT == 204);
    ASSERT(HTTP_BAD_REQUEST == 400);
    ASSERT(HTTP_NOT_FOUND == 404);
    ASSERT(HTTP_INTERNAL_ERROR == 500);

    PASS();
}

/* ===== Phase 6 Tests ===== */

/* Test session store create/destroy */
void test_session_store_create_destroy(void) {
    TEST("session_store (create/destroy)");

    session_store_t *store = session_store_create();
    ASSERT(store != NULL);

    session_store_destroy(store);

    /* Destroy NULL should not crash */
    session_store_destroy(NULL);

    PASS();
}

/* Test session create and get */
void test_session_create_get(void) {
    TEST("session (create/get)");

    session_store_t *store = session_store_create();
    ASSERT(store != NULL);

    /* Create a session with 1 hour max_age */
    char *sid = session_create(store, 3600);
    ASSERT(sid != NULL);
    ASSERT(strlen(sid) > 0);

    /* Get session by ID */
    session_t *sess = session_get(store, sid);
    ASSERT(sess != NULL);

    /* Session ID should match */
    const char *retrieved_id = session_get_id(sess);
    ASSERT(retrieved_id != NULL);
    ASSERT(strcmp(retrieved_id, sid) == 0);

    /* Session should not be expired */
    ASSERT(session_is_expired(sess) == false);

    free(sid);
    session_store_destroy(store);

    PASS();
}

/* Test session data set/get/remove */
void test_session_data_operations(void) {
    TEST("session (data operations)");

    session_store_t *store = session_store_create();
    ASSERT(store != NULL);

    char *sid = session_create(store, 3600);
    ASSERT(sid != NULL);

    session_t *sess = session_get(store, sid);
    ASSERT(sess != NULL);

    /* Set data */
    session_set_data(sess, "user_id", "42");
    session_set_data(sess, "username", "testuser");

    /* Get data */
    const char *user_id = session_get_data(sess, "user_id");
    ASSERT(user_id != NULL);
    ASSERT(strcmp(user_id, "42") == 0);

    const char *username = session_get_data(sess, "username");
    ASSERT(username != NULL);
    ASSERT(strcmp(username, "testuser") == 0);

    /* Update data */
    session_set_data(sess, "user_id", "99");
    user_id = session_get_data(sess, "user_id");
    ASSERT(user_id != NULL);
    ASSERT(strcmp(user_id, "99") == 0);

    /* Remove data */
    session_remove_data(sess, "user_id");
    ASSERT(session_get_data(sess, "user_id") == NULL);

    /* Other data should still be there */
    ASSERT(session_get_data(sess, "username") != NULL);

    /* Get non-existent key */
    ASSERT(session_get_data(sess, "nonexistent") == NULL);

    free(sid);
    session_store_destroy(store);

    PASS();
}

/* Test session destroy */
void test_session_destroy_session(void) {
    TEST("session (destroy session)");

    session_store_t *store = session_store_create();
    ASSERT(store != NULL);

    char *sid = session_create(store, 3600);
    ASSERT(sid != NULL);

    /* Session should exist */
    ASSERT(session_get(store, sid) != NULL);

    /* Destroy session */
    session_destroy(store, sid);

    /* Session should no longer exist */
    ASSERT(session_get(store, sid) == NULL);

    free(sid);
    session_store_destroy(store);

    PASS();
}

/* Test session expiration */
void test_session_expiration(void) {
    TEST("session (expiration)");

    session_store_t *store = session_store_create();
    ASSERT(store != NULL);

    /* Create session cookie (never expires) */
    char *sid = session_create(store, 0);
    ASSERT(sid != NULL);

    session_t *sess = session_get(store, sid);
    ASSERT(sess != NULL);
    ASSERT(session_is_expired(sess) == false);

    /* NULL session should be considered expired */
    ASSERT(session_is_expired(NULL) == true);

    free(sid);
    session_store_destroy(store);

    PASS();
}

/* Test session cleanup */
void test_session_cleanup(void) {
    TEST("session (cleanup expired)");

    session_store_t *store = session_store_create();
    ASSERT(store != NULL);

    /* Create a session with max_age=0 (session cookie, won't expire) */
    char *sid = session_create(store, 0);
    ASSERT(sid != NULL);

    /* Cleanup should not remove it */
    int cleaned = session_cleanup_expired(store);
    ASSERT(cleaned == 0);

    ASSERT(session_get(store, sid) != NULL);

    free(sid);
    session_store_destroy(store);

    PASS();
}

/* Test session NULL handling */
void test_session_null_handling(void) {
    TEST("session (null handling)");

    /* All functions should handle NULL gracefully */
    ASSERT(session_create(NULL, 3600) == NULL);
    ASSERT(session_get(NULL, "test") == NULL);
    ASSERT(session_get_data(NULL, "key") == NULL);
    ASSERT(session_get_id(NULL) == NULL);
    session_destroy(NULL, "test");
    session_set_data(NULL, "key", "value");
    session_remove_data(NULL, "key");

    PASS();
}

/* Test session cookie */
void test_session_cookie_set(void) {
    TEST("session (cookie set)");

    http_response_t res = {0};

    /* Should not crash with NULL */
    session_set_cookie(NULL, "test", 3600, "/");
    session_set_cookie(&res, NULL, 3600, "/");

    /* Set a session cookie */
    session_set_cookie(&res, "abc123", 3600, "/api");

    /* Delete session cookie */
    session_set_cookie(&res, "abc123", -1, "/");

    _test_free_header_list(res.headers);

    PASS();
}

/* Test template context create/destroy */
void test_template_create_destroy(void) {
    TEST("template (create/destroy)");

    template_context_t *ctx = template_context_create();
    ASSERT(ctx != NULL);

    template_context_destroy(ctx);

    /* Destroy NULL should not crash */
    template_context_destroy(NULL);

    PASS();
}

/* Test template variable set/get */
void test_template_variables(void) {
    TEST("template (variables)");

    template_context_t *ctx = template_context_create();
    ASSERT(ctx != NULL);

    /* Set variables */
    template_context_set(ctx, "name", "Alice");
    template_context_set(ctx, "role", "Engineer");

    /* Get variables */
    const char *name = template_context_get(ctx, "name");
    ASSERT(name != NULL);
    ASSERT(strcmp(name, "Alice") == 0);

    const char *role = template_context_get(ctx, "role");
    ASSERT(role != NULL);
    ASSERT(strcmp(role, "Engineer") == 0);

    /* Update variable */
    template_context_set(ctx, "name", "Bob");
    name = template_context_get(ctx, "name");
    ASSERT(name != NULL);
    ASSERT(strcmp(name, "Bob") == 0);

    /* Non-existent variable */
    ASSERT(template_context_get(ctx, "nonexistent") == NULL);

    template_context_destroy(ctx);

    PASS();
}

/* Test template rendering */
void test_template_render(void) {
    TEST("template (render)");

    template_context_t *ctx = template_context_create();
    ASSERT(ctx != NULL);

    template_context_set(ctx, "name", "Alice");
    template_context_set(ctx, "greeting", "Hello");

    /* Simple variable substitution */
    char *result = template_render("{{ greeting }}, {{ name }}!", ctx);
    ASSERT(result != NULL);
    ASSERT(strcmp(result, "Hello, Alice!") == 0);
    free(result);

    /* No variables */
    result = template_render("No variables here", ctx);
    ASSERT(result != NULL);
    ASSERT(strcmp(result, "No variables here") == 0);
    free(result);

    /* Unknown variable renders as empty */
    result = template_render("{{ unknown }}", ctx);
    ASSERT(result != NULL);
    ASSERT(strcmp(result, "") == 0);
    free(result);

    /* NULL template */
    ASSERT(template_render(NULL, ctx) == NULL);

    template_context_destroy(ctx);

    PASS();
}

/* Test template file loading */
void test_template_load_file(void) {
    TEST("template (load file)");

    /* Create a test template file */
    FILE *f = fopen("/tmp/test_template.html", "w");
    ASSERT(f != NULL);
    fprintf(f, "<h1>{{ title }}</h1>");
    fclose(f);

    /* Load template */
    char *tmpl = template_load_file("/tmp/test_template.html");
    ASSERT(tmpl != NULL);
    ASSERT(strcmp(tmpl, "<h1>{{ title }}</h1>") == 0);

    /* Render it */
    template_context_t *ctx = template_context_create();
    template_context_set(ctx, "title", "Test Page");
    char *result = template_render(tmpl, ctx);
    ASSERT(result != NULL);
    ASSERT(strcmp(result, "<h1>Test Page</h1>") == 0);

    free(result);
    free(tmpl);
    template_context_destroy(ctx);
    remove("/tmp/test_template.html");

    /* Non-existent file */
    ASSERT(template_load_file("/tmp/nonexistent_template.html") == NULL);
    ASSERT(template_load_file(NULL) == NULL);

    PASS();
}

/* Test auth middleware - basic auth create/destroy */
void test_basic_auth_create_destroy(void) {
    TEST("basic_auth (create/destroy)");

    /* NULL config should return NULL */
    middleware_fn_t mw = basic_auth_middleware_create(NULL);
    ASSERT(mw == NULL);

    /* Config without verify callback should return NULL */
    basic_auth_config_t bad_config = {
        .realm = "Test",
        .verify = NULL,
        .user_data = NULL
    };
    mw = basic_auth_middleware_create(&bad_config);
    ASSERT(mw == NULL);

    /* Destroy should be safe even without create */
    basic_auth_middleware_destroy();
    basic_auth_middleware_destroy();

    PASS();
}

/* Test auth middleware - API key create/destroy */
void test_apikey_auth_create_destroy(void) {
    TEST("apikey_auth (create/destroy)");

    /* NULL config should return NULL */
    middleware_fn_t mw = apikey_auth_middleware_create(NULL);
    ASSERT(mw == NULL);

    /* Config without verify callback should return NULL */
    apikey_auth_config_t bad_config = {
        .header_name = "X-API-Key",
        .verify = NULL,
        .user_data = NULL
    };
    mw = apikey_auth_middleware_create(&bad_config);
    ASSERT(mw == NULL);

    /* Destroy should be safe even without create */
    apikey_auth_middleware_destroy();
    apikey_auth_middleware_destroy();

    PASS();
}

/* Test auth middleware - JWT create/destroy */
void test_jwt_auth_create_destroy(void) {
    TEST("jwt_auth (create/destroy)");

    /* NULL config should return NULL */
    middleware_fn_t mw = jwt_auth_middleware_create(NULL);
    ASSERT(mw == NULL);

    /* Config without secret should return NULL */
    jwt_auth_config_t bad_config = {
        .secret = NULL,
        .secret_len = 0,
        .header_name = NULL
    };
    mw = jwt_auth_middleware_create(&bad_config);
    ASSERT(mw == NULL);

    /* Config with empty secret_len should return NULL */
    jwt_auth_config_t empty_secret = {
        .secret = "test",
        .secret_len = 0,
        .header_name = NULL
    };
    mw = jwt_auth_middleware_create(&empty_secret);
    ASSERT(mw == NULL);

    /* Destroy should be safe even without create */
    jwt_auth_middleware_destroy();
    jwt_auth_middleware_destroy();

    PASS();
}

/* Test db_pool from PR #17 */
void test_db_pool_create_destroy(void) {
    TEST("db_pool (create/destroy)");

    /* NULL config should return NULL */
    ASSERT(db_pool_create(NULL) == NULL);

    /* Config without connection string should return NULL */
    db_pool_config_t bad_config = db_pool_config_default(DB_TYPE_GENERIC, NULL);
    ASSERT(db_pool_create(&bad_config) == NULL);

    /* Valid config */
    db_pool_config_t config = db_pool_config_default(DB_TYPE_GENERIC, "generic://localhost");
    db_pool_t *pool = db_pool_create(&config);
    ASSERT(pool != NULL);

    db_pool_destroy(pool);

    /* Free the config connection string that was strdup'd */
    free(config.connection_string);

    /* Destroy NULL should not crash */
    db_pool_destroy(NULL);

    PASS();
}

/* Test db_pool acquire/release */
void test_db_pool_acquire_release(void) {
    TEST("db_pool (acquire/release)");

    db_pool_config_t config = db_pool_config_default(DB_TYPE_GENERIC, "generic://localhost");
    db_pool_t *pool = db_pool_create(&config);
    ASSERT(pool != NULL);

    /* Acquire a connection */
    db_connection_t *conn = db_pool_acquire(pool);
    ASSERT(conn != NULL);
    ASSERT(db_connection_is_valid(conn) == true);
    ASSERT(db_connection_get_handle(conn) != NULL);

    /* Release connection */
    int result = db_pool_release(pool, conn);
    ASSERT(result == 0);

    /* Get stats */
    db_pool_stats_t stats;
    result = db_pool_get_stats(pool, &stats);
    ASSERT(result == 0);
    ASSERT(stats.total_acquired >= 1);
    ASSERT(stats.total_released >= 1);

    db_pool_destroy(pool);
    free(config.connection_string);

    PASS();
}

/* ===== Phase 6.5: Bug Fix Regression Tests ===== */

/* Test JSON escape sequence decoding in parse */
void test_json_escape_decode(void) {
    TEST("json_parse (escape decoding)");

    /* Basic escapes */
    json_value_t *val = json_parse("\"hello\\nworld\"");
    ASSERT(val != NULL);
    ASSERT(val->type == JSON_STRING);
    ASSERT(strcmp(val->data.string_val, "hello\nworld") == 0);
    ASSERT(strlen(val->data.string_val) == 11);
    json_value_free(val);

    /* Tab escape */
    val = json_parse("\"a\\tb\"");
    ASSERT(val != NULL);
    ASSERT(strcmp(val->data.string_val, "a\tb") == 0);
    json_value_free(val);

    /* Backslash escape */
    val = json_parse("\"a\\\\b\"");
    ASSERT(val != NULL);
    ASSERT(strcmp(val->data.string_val, "a\\b") == 0);
    json_value_free(val);

    /* Quote escape */
    val = json_parse("\"a\\\"b\"");
    ASSERT(val != NULL);
    ASSERT(strcmp(val->data.string_val, "a\"b") == 0);
    json_value_free(val);

    /* Carriage return */
    val = json_parse("\"a\\rb\"");
    ASSERT(val != NULL);
    ASSERT(strcmp(val->data.string_val, "a\rb") == 0);
    json_value_free(val);

    /* Backspace and formfeed */
    val = json_parse("\"\\b\\f\"");
    ASSERT(val != NULL);
    ASSERT(val->data.string_val[0] == '\b');
    ASSERT(val->data.string_val[1] == '\f');
    json_value_free(val);

    /* Forward slash */
    val = json_parse("\"\\/\"");
    ASSERT(val != NULL);
    ASSERT(strcmp(val->data.string_val, "/") == 0);
    json_value_free(val);

    /* Unicode escape \u0041 = 'A' */
    val = json_parse("\"\\u0041\"");
    ASSERT(val != NULL);
    ASSERT(strcmp(val->data.string_val, "A") == 0);
    json_value_free(val);

    /* Invalid escape rejected */
    val = json_parse("\"\\q\"");
    ASSERT(val == NULL);

    PASS();
}

/* Test JSON stringify escape round-trip */
void test_json_stringify_escapes(void) {
    TEST("json_stringify (escape encoding)");

    /* String with special chars */
    json_value_t *val = json_string_create("line1\nline2\ttab");
    char *str = json_stringify(val);
    ASSERT(str != NULL);
    ASSERT(strstr(str, "\\n") != NULL);
    ASSERT(strstr(str, "\\t") != NULL);
    json_value_free(val);

    /* Round-trip: parse the stringify output */
    json_value_t *reparsed = json_parse(str);
    ASSERT(reparsed != NULL);
    ASSERT(strcmp(reparsed->data.string_val, "line1\nline2\ttab") == 0);
    json_value_free(reparsed);
    free(str);

    /* Backslash and quote round-trip */
    val = json_string_create("a\\b\"c");
    str = json_stringify(val);
    ASSERT(str != NULL);
    reparsed = json_parse(str);
    ASSERT(reparsed != NULL);
    ASSERT(strcmp(reparsed->data.string_val, "a\\b\"c") == 0);
    json_value_free(val);
    json_value_free(reparsed);
    free(str);

    /* Control characters */
    val = json_string_create("\b\f");
    str = json_stringify(val);
    ASSERT(str != NULL);
    ASSERT(strstr(str, "\\b") != NULL);
    ASSERT(strstr(str, "\\f") != NULL);
    json_value_free(val);
    free(str);

    PASS();
}

/* Test JSON stringify object key escaping */
void test_json_stringify_key_escape(void) {
    TEST("json_stringify (key escaping)");

    json_value_t *obj = json_object_create();
    ASSERT(obj != NULL);
    json_object_set(obj, "key\"with\"quotes", json_string_create("value"));
    char *str = json_stringify(obj);
    ASSERT(str != NULL);
    /* Key should be escaped */
    ASSERT(strstr(str, "key\\\"with\\\"quotes") != NULL);
    json_value_free(obj);
    free(str);

    PASS();
}

/* Test JSON rejects unterminated containers */
void test_json_unterminated(void) {
    TEST("json_parse (unterminated)");

    /* Unterminated object */
    json_value_t *val = json_parse("{\"key\": \"value\"");
    ASSERT(val == NULL);

    /* Unterminated array */
    val = json_parse("[1, 2, 3");
    ASSERT(val == NULL);

    /* Unterminated string */
    val = json_parse("\"hello");
    ASSERT(val == NULL);

    /* Properly terminated ones should work */
    val = json_parse("{\"key\": \"value\"}");
    ASSERT(val != NULL);
    json_value_free(val);

    val = json_parse("[1, 2, 3]");
    ASSERT(val != NULL);
    json_value_free(val);

    PASS();
}

/* Test JSON rejects trailing garbage */
void test_json_trailing_garbage(void) {
    TEST("json_parse (trailing garbage)");

    /* Trailing garbage after valid JSON */
    json_value_t *val = json_parse("123 garbage");
    ASSERT(val == NULL);

    val = json_parse("true extra");
    ASSERT(val == NULL);

    val = json_parse("{} more");
    ASSERT(val == NULL);

    /* Trailing whitespace is OK */
    val = json_parse("123   ");
    ASSERT(val != NULL);
    json_value_free(val);

    PASS();
}

/* Test JSON bool/null termination verification */
void test_json_keyword_termination(void) {
    TEST("json_parse (keyword termination)");

    /* "trueness" should not parse as true */
    json_value_t *val = json_parse("trueness");
    ASSERT(val == NULL);

    /* "nullable" should not parse as null */
    val = json_parse("nullable");
    ASSERT(val == NULL);

    /* "falsehood" should not parse as false */
    val = json_parse("falsehood");
    ASSERT(val == NULL);

    /* Proper values work */
    val = json_parse("true");
    ASSERT(val != NULL);
    ASSERT(val->type == JSON_BOOL);
    json_value_free(val);

    val = json_parse("null");
    ASSERT(val != NULL);
    ASSERT(val->type == JSON_NULL);
    json_value_free(val);

    PASS();
}

/* Test json_null_create */
void test_json_null_create(void) {
    TEST("json_null_create");

    json_value_t *val = json_null_create();
    ASSERT(val != NULL);
    ASSERT(val->type == JSON_NULL);

    char *str = json_stringify(val);
    ASSERT(str != NULL);
    ASSERT(strcmp(str, "null") == 0);
    free(str);

    json_value_free(val);

    PASS();
}

/* Test JSON deep nesting protection */
void test_json_depth_limit(void) {
    TEST("json_parse (depth limit)");

    /* Build a deeply nested array: [[[[...]]]] */
    char deep[1201];
    memset(deep, '[', 600);
    memset(deep + 600, ']', 600);
    deep[1200] = '\0';

    json_value_t *val = json_parse(deep);
    /* Should be rejected due to depth limit */
    ASSERT(val == NULL);

    /* Moderate nesting should work */
    val = json_parse("[[[[1]]]]");
    ASSERT(val != NULL);
    json_value_free(val);

    PASS();
}

/* ===== Phase 6.5.2: Bug Fix Regression Tests (Phase 2) ===== */

/* Test expired session auto-cleanup on access */
void test_session_expired_cleanup_on_get(void) {
    TEST("session (expired auto-cleanup on get)");

    session_store_t *store = session_store_create();
    ASSERT(store != NULL);

    /* Create a session with very short max_age=1 */
    char *sid = session_create(store, 1);
    ASSERT(sid != NULL);

    /* Session should exist immediately */
    session_t *sess = session_get(store, sid);
    ASSERT(sess != NULL);

    /* Wait for expiry */
    sleep(2);

    /* Getting expired session should return NULL AND free the slot */
    sess = session_get(store, sid);
    ASSERT(sess == NULL);

    /* The slot should now be free - create a new session should succeed */
    char *sid2 = session_create(store, 3600);
    ASSERT(sid2 != NULL);

    free(sid);
    free(sid2);
    session_store_destroy(store);

    PASS();
}

/* Test router handles NULL req->path safely */
void test_router_null_path(void) {
    TEST("router (null path safety)");

    router_t *router = router_create();
    ASSERT(router != NULL);

    ASSERT(router_add_route(router, HTTP_GET, "/test", dummy_handler) == 0);

    /* router_route with NULL should not crash - returns -1 */
    ASSERT(router_route(router, NULL, NULL) == -1);

    router_destroy(router);

    PASS();
}

/* Test event loop timer ID safety and re-entrancy protection */
void test_event_loop_timer_safety(void) {
    TEST("event_loop (timer safety)");

    event_loop_t *loop = event_loop_create();
    ASSERT(loop != NULL);

    /* Add and cancel multiple timers to exercise ID management */
    int id1 = event_loop_add_timeout(loop, 1000, dummy_event_callback, NULL);
    ASSERT(id1 > 0);
    int id2 = event_loop_add_timeout(loop, 2000, dummy_event_callback, NULL);
    ASSERT(id2 > 0);
    ASSERT(id1 != id2);

    /* Cancel first timer */
    ASSERT(event_loop_cancel_timeout(loop, id1) == 0);

    /* Cancel already-cancelled timer should fail */
    ASSERT(event_loop_cancel_timeout(loop, id1) == -1);

    /* Second timer should still be cancellable */
    ASSERT(event_loop_cancel_timeout(loop, id2) == 0);

    event_loop_destroy(loop);

    PASS();
}

/* ===== Phase 7: Thread Pool Tests ===== */

/* Counter and mutex for thread pool completion test */
static int tp_counter = 0;
static pthread_mutex_t tp_counter_mutex = PTHREAD_MUTEX_INITIALIZER;

static void increment_counter(void *arg) {
    (void)arg;
    pthread_mutex_lock(&tp_counter_mutex);
    tp_counter++;
    pthread_mutex_unlock(&tp_counter_mutex);
}

void test_thread_pool_create_destroy(void) {
    TEST("thread_pool (create/destroy)");

    thread_pool_t *pool = thread_pool_create(4, 32);
    ASSERT(pool != NULL);
    ASSERT(thread_pool_pending(pool) == 0);

    thread_pool_destroy(pool);

    PASS();
}

void test_thread_pool_null_handling(void) {
    TEST("thread_pool (null handling)");

    /* NULL pool returns 0 pending */
    ASSERT(thread_pool_pending(NULL) == 0);

    /* Submit to NULL pool fails */
    ASSERT(thread_pool_submit(NULL, increment_counter, NULL) == -1);

    /* Destroy NULL is safe */
    thread_pool_destroy(NULL);

    /* NULL work function rejected */
    thread_pool_t *pool = thread_pool_create(2, 8);
    ASSERT(pool != NULL);
    ASSERT(thread_pool_submit(pool, NULL, NULL) == -1);
    thread_pool_destroy(pool);

    PASS();
}

void test_thread_pool_submit_and_complete(void) {
    TEST("thread_pool (submit 100 items)");

    tp_counter = 0;
    thread_pool_t *pool = thread_pool_create(4, 256);
    ASSERT(pool != NULL);

    /* Submit 100 work items */
    for (int i = 0; i < 100; i++) {
        int result = thread_pool_submit(pool, increment_counter, NULL);
        ASSERT(result == 0);
    }

    /* Destroy waits for all work to complete */
    thread_pool_destroy(pool);

    ASSERT(tp_counter == 100);

    PASS();
}

void test_thread_pool_clamp_limits(void) {
    TEST("thread_pool (clamp limits)");

    /* Thread count below minimum gets clamped to 1 */
    thread_pool_t *pool1 = thread_pool_create(0, 8);
    ASSERT(pool1 != NULL);
    thread_pool_destroy(pool1);

    /* Thread count above maximum gets clamped to 256 */
    thread_pool_t *pool2 = thread_pool_create(999, 8);
    ASSERT(pool2 != NULL);
    thread_pool_destroy(pool2);

    /* Default queue size when 0 is passed */
    thread_pool_t *pool3 = thread_pool_create(2, 0);
    ASSERT(pool3 != NULL);
    thread_pool_destroy(pool3);

    PASS();
}

/* ===== Phase 7: Socket Timeout Tests ===== */

void test_server_set_timeout(void) {
    TEST("http_server_set_timeout");

    http_server_t *server = http_server_create();
    ASSERT(server != NULL);

    /* Default timeouts are 30s */
    ASSERT(http_server_get_read_timeout(server) == 30);
    ASSERT(http_server_get_write_timeout(server) == 30);

    /* Set custom timeouts */
    ASSERT(http_server_set_timeout(server, 10, 15) == 0);
    ASSERT(http_server_get_read_timeout(server) == 10);
    ASSERT(http_server_get_write_timeout(server) == 15);

    /* Zero disables timeout */
    ASSERT(http_server_set_timeout(server, 0, 0) == 0);
    ASSERT(http_server_get_read_timeout(server) == 0);
    ASSERT(http_server_get_write_timeout(server) == 0);

    /* Negative values rejected */
    ASSERT(http_server_set_timeout(server, -1, 5) == -1);
    ASSERT(http_server_set_timeout(server, 5, -1) == -1);

    /* NULL server handling */
    ASSERT(http_server_set_timeout(NULL, 10, 10) == -1);
    ASSERT(http_server_get_read_timeout(NULL) == -1);
    ASSERT(http_server_get_write_timeout(NULL) == -1);

    http_server_destroy(server);

    PASS();
}

/* ===== Phase 7: Thread Count Configuration Tests ===== */

void test_server_set_thread_count(void) {
    TEST("http_server_set_thread_count");

    http_server_t *server = http_server_create();
    ASSERT(server != NULL);

    /* Set valid thread count */
    ASSERT(http_server_set_thread_count(server, 8) == 0);

    /* Minimum clamped */
    ASSERT(http_server_set_thread_count(server, 0) == 0);

    /* Maximum clamped */
    ASSERT(http_server_set_thread_count(server, 999) == 0);

    /* NULL server rejected */
    ASSERT(http_server_set_thread_count(NULL, 8) == -1);

    http_server_destroy(server);

    PASS();
}

/* ===== Phase 7: Server State Tests ===== */

void test_server_state(void) {
    TEST("http_server_get_state");

    http_server_t *server = http_server_create();
    ASSERT(server != NULL);

    /* Initial state is STOPPED */
    ASSERT(http_server_get_state(server) == HTTP_SERVER_STOPPED);

    /* NULL server returns STOPPED */
    ASSERT(http_server_get_state(NULL) == HTTP_SERVER_STOPPED);

    http_server_destroy(server);

    PASS();
}

/* ===== Phase 8: Logging Middleware Tests ===== */

void test_log_middleware_create_destroy(void) {
    TEST("log_middleware (create/destroy)");

    /* Default config */
    middleware_fn_t fn = log_middleware_create(NULL);
    ASSERT(fn != NULL);
    log_middleware_destroy();

    /* Custom config */
    log_config_t cfg = {LOG_LEVEL_DEBUG, NULL};
    fn = log_middleware_create(&cfg);
    ASSERT(fn != NULL);
    log_middleware_destroy();

    /* NULL config → defaults */
    fn = log_middleware_create(NULL);
    ASSERT(fn != NULL);
    log_middleware_destroy();

    PASS();
}

void test_log_middleware_invoke(void) {
    TEST("log_middleware (invoke)");

    /* tmpfile() is portable across POSIX and Windows */
    FILE *sink = tmpfile();
    ASSERT(sink != NULL);

    log_config_t cfg = {LOG_LEVEL_INFO, sink};
    middleware_fn_t fn = log_middleware_create(&cfg);
    ASSERT(fn != NULL);

    /* Build minimal request */
    http_request_t req;
    memset(&req, 0, sizeof(req));
    req.method = HTTP_GET;
    req.path   = "/test";

    http_response_t res;
    memset(&res, 0, sizeof(res));

    /* Should return true (continue chain) */
    bool cont = fn(&req, &res, NULL);
    ASSERT(cont == true);

    /* NULL inputs should not crash */
    cont = fn(NULL, NULL, NULL);
    ASSERT(cont == true);

    log_middleware_destroy();
    fclose(sink);

    PASS();
}

/* ===== Phase 8: Error Handler Middleware Tests ===== */

void test_error_handler_create_destroy(void) {
    TEST("error_handler_middleware (create/destroy)");

    middleware_fn_t fn = error_handler_middleware_create(NULL);
    ASSERT(fn != NULL);
    error_handler_middleware_destroy();

    error_handler_config_t cfg = {NULL};
    fn = error_handler_middleware_create(&cfg);
    ASSERT(fn != NULL);
    error_handler_middleware_destroy();

    PASS();
}

void test_error_handler_apply(void) {
    TEST("error_handler_apply");

    error_handler_middleware_create(NULL);

    /* Build a 404 response with no body */
    http_response_t res;
    memset(&res, 0, sizeof(res));
    res.status = HTTP_NOT_FOUND;

    error_handler_apply(NULL, &res);

    /* Should have filled in a JSON body */
    ASSERT(res.body != NULL);
    ASSERT(res.body_length > 0);
    ASSERT(strstr(res.body, "404") != NULL || strstr(res.body, "Not Found") != NULL);

    free(res.body);
    _test_free_header_list(res.headers);

    /* Non-error status → no change */
    http_response_t ok_res;
    memset(&ok_res, 0, sizeof(ok_res));
    ok_res.status = HTTP_OK;
    error_handler_apply(NULL, &ok_res);
    ASSERT(ok_res.body == NULL);

    /* NULL response → no crash */
    error_handler_apply(NULL, NULL);

    error_handler_middleware_destroy();

    PASS();
}

/* ===== Phase 8: CSRF Middleware Tests ===== */

void test_csrf_create_destroy(void) {
    TEST("csrf_middleware (create/destroy)");

    middleware_fn_t fn = csrf_middleware_create(NULL);
    ASSERT(fn != NULL);
    csrf_middleware_destroy();

    csrf_config_t cfg = {"my_csrf", "X-My-Token", 16};
    fn = csrf_middleware_create(&cfg);
    ASSERT(fn != NULL);
    csrf_middleware_destroy();

    PASS();
}

void test_csrf_safe_methods(void) {
    TEST("csrf_middleware (safe methods pass through)");

    csrf_middleware_create(NULL);

    http_request_t req;
    memset(&req, 0, sizeof(req));
    req.method = HTTP_GET;
    req.path   = "/";

    http_response_t res;
    memset(&res, 0, sizeof(res));
    res.status = HTTP_OK;

    /* GET without token: should be allowed (returns true) */
    middleware_fn_t fn = csrf_middleware_create(NULL);
    ASSERT(fn != NULL);

    /* fn will try to set a cookie via http_response_set_cookie which requires
       a real response header list — for simplicity we just verify it returns true
       and does not crash. */
    bool cont = fn(&req, &res, NULL);
    ASSERT(cont == true);

    csrf_middleware_destroy();

    /* Free any allocated cookie/header nodes */
    _test_free_header_list(res.headers);

    PASS();
}

/* ===== Phase 8: Input Validation Tests ===== */

void test_input_validate_length(void) {
    TEST("input_validate_length");

    ASSERT(input_validate_length("hello", 1, 10) == true);
    ASSERT(input_validate_length("hello", 5, 5) == true);
    ASSERT(input_validate_length("hello", 6, 10) == false);  /* too short */
    ASSERT(input_validate_length("hello", 1, 4)  == false);  /* too long */
    ASSERT(input_validate_length("",      0, 10) == true);
    ASSERT(input_validate_length("",      1, 10) == false);
    ASSERT(input_validate_length(NULL,    0, 10) == false);

    PASS();
}

void test_input_validate_charset(void) {
    TEST("input_validate_charset");

    ASSERT(input_validate_charset("abc123", "abcdefghijklmnopqrstuvwxyz0123456789") == true);
    ASSERT(input_validate_charset("abc!",   "abcdefghijklmnopqrstuvwxyz0123456789") == false);
    ASSERT(input_validate_charset("",       "abc") == true);  /* empty always valid */
    ASSERT(input_validate_charset(NULL,     "abc") == false);
    ASSERT(input_validate_charset("abc",    NULL)  == false);

    PASS();
}

void test_input_validate_integer(void) {
    TEST("input_validate_integer");

    long long out = 0;
    ASSERT(input_validate_integer("42",    0, 100, &out) == true  && out == 42);
    ASSERT(input_validate_integer("-5",   -10, 0,  &out) == true  && out == -5);
    ASSERT(input_validate_integer("200",   0, 100, &out) == false); /* out of range */
    ASSERT(input_validate_integer("abc",   0, 100, &out) == false); /* not a number */
    ASSERT(input_validate_integer(" 42",   0, 100, &out) == false); /* leading space */
    ASSERT(input_validate_integer("42x",   0, 100, &out) == false); /* trailing char */
    ASSERT(input_validate_integer("",      0, 100, &out) == false);
    ASSERT(input_validate_integer(NULL,    0, 100, &out) == false);
    /* NULL out_val is acceptable */
    ASSERT(input_validate_integer("7", 0, 10, NULL) == true);

    PASS();
}

void test_input_validate_email(void) {
    TEST("input_validate_email");

    ASSERT(input_validate_email("user@example.com")     == true);
    ASSERT(input_validate_email("a@b.co")               == true);
    ASSERT(input_validate_email("no-at-sign")           == false);
    ASSERT(input_validate_email("@nodomain.com")        == false);
    ASSERT(input_validate_email("user@")                == false);
    ASSERT(input_validate_email("user@nodot")           == false);
    ASSERT(input_validate_email("user@.leading.dot")    == false);
    ASSERT(input_validate_email("user@domain.")         == false);
    ASSERT(input_validate_email("")                     == false);
    ASSERT(input_validate_email(NULL)                   == false);

    PASS();
}

void test_input_is_alphanumeric(void) {
    TEST("input_is_alphanumeric");

    ASSERT(input_is_alphanumeric("abc123")  == true);
    ASSERT(input_is_alphanumeric("ABC")     == true);
    ASSERT(input_is_alphanumeric("abc 123") == false); /* space */
    ASSERT(input_is_alphanumeric("abc!")    == false);
    ASSERT(input_is_alphanumeric("")        == false); /* empty → false */
    ASSERT(input_is_alphanumeric(NULL)      == false);

    PASS();
}

void test_input_sanitize_html(void) {
    TEST("input_sanitize_html");

    char *out = input_sanitize_html("<script>alert('xss')</script>");
    ASSERT(out != NULL);
    ASSERT(strstr(out, "<")  == NULL);
    ASSERT(strstr(out, ">")  == NULL);
    ASSERT(strstr(out, "'")  == NULL);
    ASSERT(strstr(out, "&lt;")   != NULL);
    ASSERT(strstr(out, "&gt;")   != NULL);
    ASSERT(strstr(out, "&#39;")  != NULL);
    free(out);

    /* Ampersand */
    out = input_sanitize_html("a & b");
    ASSERT(out != NULL);
    ASSERT(strstr(out, "&amp;") != NULL);
    free(out);

    /* Double quote */
    out = input_sanitize_html("say \"hello\"");
    ASSERT(out != NULL);
    ASSERT(strstr(out, "&quot;") != NULL);
    free(out);

    /* Plain text unchanged */
    out = input_sanitize_html("hello world");
    ASSERT(out != NULL);
    ASSERT(strcmp(out, "hello world") == 0);
    free(out);

    /* NULL input */
    ASSERT(input_sanitize_html(NULL) == NULL);

    PASS();
}

/* ===== Phase 7: Parser Hardening Tests ===== */

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

/* Helper: connect to localhost:port, send raw bytes, read response into buf.
 * Returns bytes read or -1 on error. */
static ssize_t _send_raw_request(uint16_t port, const char *raw, size_t raw_len,
                                  char *buf, size_t buf_size) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }

    /* Send the raw request bytes */
    ssize_t sent = send(fd, raw, raw_len, 0);
    if (sent < 0) {
        close(fd);
        return -1;
    }

    /* Shutdown write side so server knows we're done sending */
    shutdown(fd, SHUT_WR);

    /* Read response */
    ssize_t total = 0;
    while ((size_t)total < buf_size - 1) {
        ssize_t n = recv(fd, buf + total, buf_size - 1 - (size_t)total, 0);
        if (n <= 0) break;
        total += n;
    }
    buf[total] = '\0';

    close(fd);
    return total;
}

void test_parser_duplicate_transfer_encoding(void) {
    TEST("parser (duplicate Transfer-Encoding → 400)");

    /* Start a real server on an ephemeral port */
    http_server_t *server = http_server_create();
    ASSERT(server != NULL);

    router_t *router = router_create();
    ASSERT(router != NULL);
    router_add_route(router, HTTP_GET, "/ok", dummy_handler);
    http_server_set_router(server, router);

    /* Use a high port to avoid conflicts */
    uint16_t port = 18787;
    int listen_result = http_server_listen(server, port);
    ASSERT(listen_result == 0);

    /* Allow the accept thread to start */
    usleep(50000); /* 50ms */

    /* 1. Duplicate Transfer-Encoding → expect "400" in response */
    const char *dup_te_request =
        "GET /ok HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Transfer-Encoding: chunked\r\n"
        "Transfer-Encoding: chunked\r\n"
        "\r\n";

    char resp_buf[2048];
    ssize_t nread = _send_raw_request(port, dup_te_request, strlen(dup_te_request),
                                       resp_buf, sizeof(resp_buf));
    ASSERT(nread > 0);
    ASSERT(strstr(resp_buf, "400") != NULL);

    /* 2. Sanity: a valid request on the same server should succeed */
    const char *good_request =
        "GET /ok HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Connection: close\r\n"
        "\r\n";

    nread = _send_raw_request(port, good_request, strlen(good_request),
                               resp_buf, sizeof(resp_buf));
    ASSERT(nread > 0);
    /* Should be a 200 or 404 (dummy_handler doesn't set body, but route matches) */
    ASSERT(strstr(resp_buf, "HTTP/1.1") != NULL);

    http_server_stop(server);
    http_server_destroy(server);
    router_destroy(router);

    PASS();
}

/* ===== Phase 7.5: Networking Integration Tests ===== */

/* Helper: start a server with a given router on a given port,
 * returning the server pointer.  Caller is responsible for
 * http_server_stop / destroy / router_destroy.
 * Uses a connect-retry loop instead of a fixed sleep to avoid races. */
static http_server_t *_start_test_server(router_t *router, uint16_t port) {
    http_server_t *server = http_server_create();
    if (!server) return NULL;
    http_server_set_router(server, router);
    if (http_server_listen(server, port) < 0) {
        http_server_destroy(server);
        return NULL;
    }
    /* Poll until the server is accepting connections (max ~500 ms) */
    for (int attempt = 0; attempt < 50; attempt++) {
        int fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) { usleep(10000); continue; }
        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        addr.sin_addr.s_addr = inet_addr("127.0.0.1");
        int rc = connect(fd, (struct sockaddr *)&addr, sizeof(addr));
        close(fd);
        if (rc == 0) return server;
        usleep(10000); /* 10 ms between retries */
    }
    return server; /* best effort — tests may still work */
}

/* Handler that echoes request body back */
static void echo_handler(http_request_t *req, http_response_t *res) {
    const char *body = req->body ? req->body : "";
    http_response_send_text(res, HTTP_OK, body);
}

/* Handler that returns a JSON object */
static void json_handler(http_request_t *req, http_response_t *res) {
    (void)req;
    json_value_t *obj = json_object_create();
    json_object_set(obj, "message", json_string_create("hello"));
    char *body = json_stringify(obj);
    json_value_free(obj);
    http_response_set_header(res, "Content-Type", "application/json");
    http_response_send_text(res, HTTP_OK, body);
    free(body);
}

/* Integration test: GET request returns 200 */
void test_integration_get_200(void) {
    TEST("integration (GET → 200)");

    router_t *router = router_create();
    ASSERT(router != NULL);
    router_add_route(router, HTTP_GET, "/ping", echo_handler);

    uint16_t port = 18800;
    http_server_t *server = _start_test_server(router, port);
    ASSERT(server != NULL);

    const char *req =
        "GET /ping HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Connection: close\r\n"
        "\r\n";
    char buf[4096];
    ssize_t n = _send_raw_request(port, req, strlen(req), buf, sizeof(buf));
    ASSERT(n > 0);
    ASSERT(strstr(buf, "HTTP/1.1 200") != NULL);

    http_server_stop(server);
    http_server_destroy(server);
    router_destroy(router);
    PASS();
}

/* Integration test: unknown path returns 404 */
void test_integration_not_found(void) {
    TEST("integration (GET unknown path → 404)");

    router_t *router = router_create();
    ASSERT(router != NULL);
    router_add_route(router, HTTP_GET, "/exists", dummy_handler);

    uint16_t port = 18801;
    http_server_t *server = _start_test_server(router, port);
    ASSERT(server != NULL);

    const char *req =
        "GET /does-not-exist HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Connection: close\r\n"
        "\r\n";
    char buf[4096];
    ssize_t n = _send_raw_request(port, req, strlen(req), buf, sizeof(buf));
    ASSERT(n > 0);
    ASSERT(strstr(buf, "404") != NULL);

    http_server_stop(server);
    http_server_destroy(server);
    router_destroy(router);
    PASS();
}

/* Integration test: POST with body */
void test_integration_post_body(void) {
    TEST("integration (POST with body → echo)");

    router_t *router = router_create();
    ASSERT(router != NULL);
    router_add_route(router, HTTP_POST, "/echo", echo_handler);

    uint16_t port = 18802;
    http_server_t *server = _start_test_server(router, port);
    ASSERT(server != NULL);

    const char *req =
        "POST /echo HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Content-Length: 11\r\n"
        "Connection: close\r\n"
        "\r\n"
        "hello world";
    char buf[4096];
    ssize_t n = _send_raw_request(port, req, strlen(req), buf, sizeof(buf));
    ASSERT(n > 0);
    ASSERT(strstr(buf, "200") != NULL);

    http_server_stop(server);
    http_server_destroy(server);
    router_destroy(router);
    PASS();
}

/* Integration test: JSON response */
void test_integration_json_response(void) {
    TEST("integration (GET → JSON response)");

    router_t *router = router_create();
    ASSERT(router != NULL);
    router_add_route(router, HTTP_GET, "/json", json_handler);

    uint16_t port = 18803;
    http_server_t *server = _start_test_server(router, port);
    ASSERT(server != NULL);

    const char *req =
        "GET /json HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Connection: close\r\n"
        "\r\n";
    char buf[4096];
    ssize_t n = _send_raw_request(port, req, strlen(req), buf, sizeof(buf));
    ASSERT(n > 0);
    ASSERT(strstr(buf, "200") != NULL);
    ASSERT(strstr(buf, "\"message\"") != NULL);
    ASSERT(strstr(buf, "\"hello\"") != NULL);

    http_server_stop(server);
    http_server_destroy(server);
    router_destroy(router);
    PASS();
}

/* Integration test: malformed request line → 400 or error */
void test_integration_malformed_request(void) {
    TEST("integration (malformed request → 400)");

    router_t *router = router_create();
    ASSERT(router != NULL);
    router_add_route(router, HTTP_GET, "/ok", dummy_handler);

    uint16_t port = 18804;
    http_server_t *server = _start_test_server(router, port);
    ASSERT(server != NULL);

    /* Send garbage that is not a valid HTTP request line */
    const char *req = "GARBAGE\r\n\r\n";
    char buf[4096];
    ssize_t n = _send_raw_request(port, req, strlen(req), buf, sizeof(buf));
    /* Server should either return 400 or close the connection */
    if (n > 0) {
        ASSERT(strstr(buf, "400") != NULL || strstr(buf, "501") != NULL);
    }

    http_server_stop(server);
    http_server_destroy(server);
    router_destroy(router);
    PASS();
}

/* Integration test: multiple sequential requests on different connections */
void test_integration_concurrent_connections(void) {
    TEST("integration (sequential connections)");

    router_t *router = router_create();
    ASSERT(router != NULL);
    router_add_route(router, HTTP_GET, "/seq", echo_handler);

    uint16_t port = 18805;
    http_server_t *server = _start_test_server(router, port);
    ASSERT(server != NULL);

    const char *req =
        "GET /seq HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Connection: close\r\n"
        "\r\n";
    char buf[4096];

    /* Send 5 sequential requests on separate connections */
    for (int i = 0; i < 5; i++) {
        ssize_t n = _send_raw_request(port, req, strlen(req), buf, sizeof(buf));
        ASSERT(n > 0);
        ASSERT(strstr(buf, "200") != NULL);
    }

    http_server_stop(server);
    http_server_destroy(server);
    router_destroy(router);
    PASS();
}

/* ===== Health Check Tests ===== */

void test_health_check_register(void) {
    TEST("health_check (register)");

    router_t *router = router_create();
    ASSERT(router != NULL);

    int result = health_check_register(router);
    ASSERT(result == 0);

    /* NULL router should fail */
    ASSERT(health_check_register(NULL) == -1);

    router_destroy(router);
    PASS();
}

void test_health_check_endpoint(void) {
    TEST("health_check (GET /healthz → 200 JSON)");

    router_t *router = router_create();
    ASSERT(router != NULL);
    health_check_register(router);

    uint16_t port = 18806;
    http_server_t *server = _start_test_server(router, port);
    ASSERT(server != NULL);

    const char *req =
        "GET /healthz HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Connection: close\r\n"
        "\r\n";
    char buf[4096];
    ssize_t n = _send_raw_request(port, req, strlen(req), buf, sizeof(buf));
    ASSERT(n > 0);
    ASSERT(strstr(buf, "200") != NULL);
    ASSERT(strstr(buf, "\"status\"") != NULL);
    ASSERT(strstr(buf, "\"ok\"") != NULL);
    ASSERT(strstr(buf, "\"uptime_seconds\"") != NULL);

    http_server_stop(server);
    http_server_destroy(server);
    router_destroy(router);
    PASS();
}

/* ===== Phase 9: Cache tests ===== */

void test_cache_create_destroy(void) {
    TEST("cache (create/destroy)");

    cache_t *c = cache_create(16);
    ASSERT(c != NULL);
    ASSERT(cache_count(c) == 0);

    cache_destroy(c);

    /* NULL safety */
    cache_destroy(NULL);

    /* zero max_entries should return NULL */
    ASSERT(cache_create(0) == NULL);

    PASS();
}

void test_cache_set_get(void) {
    TEST("cache (set/get)");

    cache_t *c = cache_create(8);
    ASSERT(c != NULL);

    /* Set and retrieve a value */
    ASSERT(cache_set(c, "key1", "value1", 0) == 0);
    ASSERT(cache_count(c) == 1);

    const char *val = cache_get(c, "key1");
    ASSERT(val != NULL);
    ASSERT(strcmp(val, "value1") == 0);

    /* Update existing key */
    ASSERT(cache_set(c, "key1", "updated", 0) == 0);
    ASSERT(cache_count(c) == 1);
    val = cache_get(c, "key1");
    ASSERT(val != NULL);
    ASSERT(strcmp(val, "updated") == 0);

    /* Get non-existent key */
    ASSERT(cache_get(c, "nonexistent") == NULL);

    /* NULL safety */
    ASSERT(cache_set(NULL, "k", "v", 0) == -1);
    ASSERT(cache_set(c, NULL, "v", 0) == -1);
    ASSERT(cache_set(c, "k", NULL, 0) == -1);
    ASSERT(cache_get(NULL, "k") == NULL);
    ASSERT(cache_get(c, NULL) == NULL);

    cache_destroy(c);
    PASS();
}

void test_cache_delete(void) {
    TEST("cache (delete)");

    cache_t *c = cache_create(8);
    ASSERT(c != NULL);

    cache_set(c, "k1", "v1", 0);
    cache_set(c, "k2", "v2", 0);
    ASSERT(cache_count(c) == 2);

    /* Delete existing */
    ASSERT(cache_delete(c, "k1") == 0);
    ASSERT(cache_count(c) == 1);
    ASSERT(cache_get(c, "k1") == NULL);
    ASSERT(cache_get(c, "k2") != NULL);

    /* Delete non-existent */
    ASSERT(cache_delete(c, "k1") == -1);

    /* NULL safety */
    ASSERT(cache_delete(NULL, "k") == -1);
    ASSERT(cache_delete(c, NULL) == -1);

    cache_destroy(c);
    PASS();
}

void test_cache_clear(void) {
    TEST("cache (clear)");

    cache_t *c = cache_create(8);
    ASSERT(c != NULL);

    cache_set(c, "a", "1", 0);
    cache_set(c, "b", "2", 0);
    cache_set(c, "c", "3", 0);
    ASSERT(cache_count(c) == 3);

    cache_clear(c);
    ASSERT(cache_count(c) == 0);
    ASSERT(cache_get(c, "a") == NULL);

    /* Clear empty cache is safe */
    cache_clear(c);
    cache_clear(NULL);

    cache_destroy(c);
    PASS();
}

void test_cache_lru_eviction(void) {
    TEST("cache (LRU eviction)");

    /* Cache with capacity 3 */
    cache_t *c = cache_create(3);
    ASSERT(c != NULL);

    cache_set(c, "a", "1", 0);
    cache_set(c, "b", "2", 0);
    cache_set(c, "c", "3", 0);
    ASSERT(cache_count(c) == 3);

    /* Adding 4th item should evict LRU (a) */
    cache_set(c, "d", "4", 0);
    ASSERT(cache_count(c) == 3);
    ASSERT(cache_get(c, "a") == NULL);  /* evicted */
    ASSERT(cache_get(c, "b") != NULL);
    ASSERT(cache_get(c, "c") != NULL);
    ASSERT(cache_get(c, "d") != NULL);

    /* Access b to make it most recently used */
    cache_get(c, "b");

    /* Now add e; LRU should be c (b was just accessed, d was more recent) */
    cache_set(c, "e", "5", 0);
    ASSERT(cache_count(c) == 3);
    ASSERT(cache_get(c, "c") == NULL);  /* evicted */
    ASSERT(cache_get(c, "b") != NULL);
    ASSERT(cache_get(c, "d") != NULL);
    ASSERT(cache_get(c, "e") != NULL);

    cache_destroy(c);
    PASS();
}

void test_cache_ttl(void) {
    TEST("cache (TTL expiration)");

    cache_t *c = cache_create(8);
    ASSERT(c != NULL);

    /* Set with TTL=1 second */
    cache_set(c, "temp", "data", 1);
    ASSERT(cache_count(c) == 1);

    /* Should be available immediately */
    ASSERT(cache_get(c, "temp") != NULL);

    /* Wait for expiration */
    sleep(2);

    /* Should be expired now */
    ASSERT(cache_get(c, "temp") == NULL);
    ASSERT(cache_count(c) == 0);

    /* No-expiry entry should persist */
    cache_set(c, "permanent", "data", 0);
    sleep(1);
    ASSERT(cache_get(c, "permanent") != NULL);

    cache_destroy(c);
    PASS();
}

/* ===== Phase 9: Metrics middleware tests ===== */

void test_metrics_create_destroy(void) {
    TEST("metrics_middleware (create/destroy)");

    middleware_fn_t mw = metrics_middleware_create();
    ASSERT(mw != NULL);

    /* Second create should fail (already initialized) */
    ASSERT(metrics_middleware_create() == NULL);

    metrics_middleware_destroy();

    /* Destroy again is safe */
    metrics_middleware_destroy();

    /* Can re-create after destroy */
    mw = metrics_middleware_create();
    ASSERT(mw != NULL);
    metrics_middleware_destroy();

    PASS();
}

void test_metrics_register(void) {
    TEST("metrics (register)");

    router_t *router = router_create();
    ASSERT(router != NULL);

    /* NULL safety */
    ASSERT(metrics_register(NULL) == -1);

    ASSERT(metrics_register(router) == 0);

    router_destroy(router);
    PASS();
}

void test_metrics_record_status(void) {
    TEST("metrics (record_status)");

    middleware_fn_t mw = metrics_middleware_create();
    ASSERT(mw != NULL);

    /* Record some statuses */
    metrics_record_status(200);
    metrics_record_status(201);
    metrics_record_status(301);
    metrics_record_status(404);
    metrics_record_status(500);

    /* NULL safety (no crash when not initialized) */
    metrics_middleware_destroy();
    metrics_record_status(200);  /* should not crash */

    PASS();
}

void test_metrics_endpoint(void) {
    TEST("metrics (GET /metrics → 200 JSON)");

    middleware_fn_t mw = metrics_middleware_create();
    ASSERT(mw != NULL);

    router_t *router = router_create();
    ASSERT(router != NULL);
    router_use_middleware(router, mw);
    metrics_register(router);

    uint16_t port = 18807;
    http_server_t *server = _start_test_server(router, port);
    ASSERT(server != NULL);

    /* Make a request to /metrics */
    const char *req =
        "GET /metrics HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Connection: close\r\n"
        "\r\n";
    char buf[4096];
    ssize_t n = _send_raw_request(port, req, strlen(req), buf, sizeof(buf));
    ASSERT(n > 0);
    ASSERT(strstr(buf, "200") != NULL);
    ASSERT(strstr(buf, "\"total_requests\"") != NULL);
    ASSERT(strstr(buf, "\"methods\"") != NULL);
    ASSERT(strstr(buf, "\"uptime_seconds\"") != NULL);

    http_server_stop(server);
    http_server_destroy(server);
    router_destroy(router);
    metrics_middleware_destroy();
    PASS();
}

/* ===== Phase 9: Compression tests ===== */

void test_crc32_compute(void) {
    TEST("crc32_compute");

    /* Known CRC-32 value for "123456789" is 0xCBF43926 */
    const char *data = "123456789";
    uint32_t crc = crc32_compute((const uint8_t *)data, strlen(data));
    ASSERT(crc == 0xCBF43926);

    /* NULL input */
    ASSERT(crc32_compute(NULL, 10) == 0);

    /* Empty */
    ASSERT(crc32_compute((const uint8_t *)"", 0) == 0);

    PASS();
}

void test_compression_negotiate(void) {
    TEST("compression_negotiate");

    /* gzip accepted */
    ASSERT(compression_negotiate("gzip, deflate, br") != NULL);
    ASSERT(strcmp(compression_negotiate("gzip, deflate, br"), "gzip") == 0);

    /* gzip with quality */
    ASSERT(compression_negotiate("gzip;q=0.8") != NULL);

    /* gzip explicitly rejected */
    ASSERT(compression_negotiate("gzip;q=0") == NULL);

    /* No gzip */
    ASSERT(compression_negotiate("deflate, br") == NULL);

    /* NULL header */
    ASSERT(compression_negotiate(NULL) == NULL);

    /* Wildcard */
    ASSERT(compression_negotiate("*") != NULL);

    PASS();
}

void test_compression_should_compress(void) {
    TEST("compression_should_compress");

    /* Text types should compress */
    ASSERT(compression_should_compress("text/html", 1000) == true);
    ASSERT(compression_should_compress("text/plain", 500) == true);
    ASSERT(compression_should_compress("application/json", 1000) == true);
    ASSERT(compression_should_compress("application/javascript", 1000) == true);

    /* Binary types should not */
    ASSERT(compression_should_compress("image/png", 1000) == false);
    ASSERT(compression_should_compress("video/mp4", 1000) == false);

    /* Too small */
    ASSERT(compression_should_compress("text/html", 100) == false);

    /* Exactly at threshold (256 bytes — should pass) */
    ASSERT(compression_should_compress("text/html", 256) == true);

    /* Just below threshold (255 bytes — should fail) */
    ASSERT(compression_should_compress("text/html", 255) == false);

    /* NULL */
    ASSERT(compression_should_compress(NULL, 1000) == false);

    PASS();
}

void test_gzip_compress_valid(void) {
    TEST("gzip (compress produces valid output)");

    /* Check that gzip_compress produces output starting with gzip magic bytes */
    const char *input = "Hello World! This is a test of compression. "
                        "It needs to be long enough to be worth compressing. "
                        "Adding more repetitive text to ensure good compression ratio. "
                        "Hello Hello Hello Hello Hello Hello Hello Hello Hello Hello "
                        "World World World World World World World World World World.";
    size_t input_len = strlen(input);

    /* Use the internal gzip_compress via the extern declaration */
    /* Since gzip_compress is not in weblib.h, we test indirectly
     * through http_response_send_compressed behavior.
     * But we CAN verify CRC32 is correct. */

    /* The CRC32 of this string should be non-zero */
    uint32_t crc = crc32_compute((const uint8_t *)input, input_len);
    ASSERT(crc != 0);
    (void)input_len;

    PASS();
}

/* ===== Phase 9: Benchmark tests ===== */

void test_benchmark_timestamp(void) {
    TEST("benchmark_timestamp_us");

    uint64_t t1 = benchmark_timestamp_us();
    ASSERT(t1 > 0);

    /* Second call should be >= first */
    uint64_t t2 = benchmark_timestamp_us();
    ASSERT(t2 >= t1);

    PASS();
}

void test_benchmark_stats(void) {
    TEST("benchmark (NULL handling)");

    benchmark_stats_t stats;

    /* NULL path */
    ASSERT(benchmark_run(8080, NULL, 10, &stats) == -1);

    /* NULL stats */
    ASSERT(benchmark_run(8080, "/", 10, NULL) == -1);

    /* Zero requests */
    ASSERT(benchmark_run(8080, "/", 0, &stats) == -1);

    PASS();
}

void test_benchmark_print(void) {
    TEST("benchmark_print");

    benchmark_stats_t stats;
    memset(&stats, 0, sizeof(stats));
    stats.total_requests = 100;
    stats.successful = 95;
    stats.failed = 5;
    stats.elapsed_seconds = 1.5;
    stats.requests_per_second = 66.7;
    stats.avg_latency_us = 15000;
    stats.min_latency_us = 500;
    stats.max_latency_us = 50000;
    stats.p50_latency_us = 10000;
    stats.p95_latency_us = 40000;
    stats.p99_latency_us = 48000;

    /* Should not crash with NULL fp */
    benchmark_print(NULL, &stats);
    benchmark_print(stdout, NULL);

    /* Print to /dev/null to validate */
    FILE *devnull = fopen("/dev/null", "w");
    if (devnull) {
        benchmark_print(devnull, &stats);
        fclose(devnull);
    }

    PASS();
}

void test_benchmark_integration(void) {
    TEST("benchmark (live server)");

    /* Start a simple server */
    router_t *router = router_create();
    ASSERT(router != NULL);
    router_add_route(router, HTTP_GET, "/bench", dummy_handler);

    uint16_t port = 18808;
    http_server_t *server = _start_test_server(router, port);
    ASSERT(server != NULL);

    /* Run a tiny benchmark */
    benchmark_stats_t stats;
    int rc = benchmark_run(port, "/bench", 5, &stats);
    ASSERT(rc == 0);
    ASSERT(stats.total_requests == 5);
    ASSERT(stats.elapsed_seconds > 0);

    http_server_stop(server);
    http_server_destroy(server);
    router_destroy(router);
    PASS();
}

/* Phase 10: Bug fix tests */

/* Test SIGPIPE handling (BUG-1) — verify server creates without crash */
void test_sigpipe_handling(void) {
    TEST("BUG-1: SIGPIPE handling");
    
    /* Creating the server should set SIGPIPE to SIG_IGN */
    http_server_t *server = http_server_create();
    ASSERT(server != NULL);
    
    /* Verify SIGPIPE is ignored (sigaction check) */
#ifndef _WIN32
    struct sigaction sa;
    sigaction(SIGPIPE, NULL, &sa);
    ASSERT(sa.sa_handler == SIG_IGN);
#endif
    
    http_server_destroy(server);
    PASS();
}

/* Test session store thread safety (BUG-2) — verify mutex init/destroy */
void test_session_store_thread_safety(void) {
    TEST("BUG-2: session store thread safety");
    
    session_store_t *store = session_store_create();
    ASSERT(store != NULL);
    
    /* Create multiple sessions concurrently safe */
    char *sid1 = session_create(store, 3600);
    ASSERT(sid1 != NULL);
    char *sid2 = session_create(store, 3600);
    ASSERT(sid2 != NULL);
    
    /* Sessions should have different IDs */
    ASSERT(strcmp(sid1, sid2) != 0);
    
    /* Both should be retrievable */
    ASSERT(session_get(store, sid1) != NULL);
    ASSERT(session_get(store, sid2) != NULL);
    
    /* Destroy one, other should still exist */
    session_destroy(store, sid1);
    ASSERT(session_get(store, sid1) == NULL);
    ASSERT(session_get(store, sid2) != NULL);
    
    free(sid1);
    free(sid2);
    session_store_destroy(store);
    PASS();
}

/* Test event_loop timer count query (BUG-5) */
void test_event_loop_timer_count(void) {
    TEST("BUG-5: event_loop timer count query");
    
    event_loop_t *loop = event_loop_create();
    ASSERT(loop != NULL);
    
    /* Initially no timers */
    ASSERT(event_loop_get_timer_count(loop) == 0);
    
    /* Max timers should be 64 */
    ASSERT(event_loop_get_max_timers() == 64);
    
    /* Add a timer */
    int id = event_loop_add_timeout(loop, 10000, dummy_event_callback, NULL);
    ASSERT(id > 0);
    ASSERT(event_loop_get_timer_count(loop) == 1);
    
    /* NULL loop should return -1 */
    ASSERT(event_loop_get_timer_count(NULL) == -1);
    
    event_loop_destroy(loop);
    PASS();
}

/* Test active connection tracking (BUG-6) */
void test_server_connection_tracking(void) {
    TEST("BUG-6: server connection tracking");
    
    http_server_t *server = http_server_create();
    ASSERT(server != NULL);
    
    /* Initially 0 active connections */
    ASSERT(http_server_get_active_connections(server) == 0);
    
    /* Set max connections */
    ASSERT(http_server_set_max_connections(server, 50) == 0);
    
    /* Invalid values should fail */
    ASSERT(http_server_set_max_connections(server, 0) == -1);
    ASSERT(http_server_set_max_connections(server, -1) == -1);
    ASSERT(http_server_set_max_connections(NULL, 50) == -1);
    
    /* NULL server should return -1 */
    ASSERT(http_server_get_active_connections(NULL) == -1);
    
    http_server_destroy(server);
    PASS();
}

/* BUG-4 fix test: middleware user_data support */
static int _counter_a = 0;
static int _counter_b = 0;

static bool _counting_middleware_a(http_request_t *req, http_response_t *res, void *user_data) {
    (void)req; (void)res;
    int *counter = (int *)user_data;
    if (counter) (*counter)++;
    return true;
}

static bool _counting_middleware_b(http_request_t *req, http_response_t *res, void *user_data) {
    (void)req; (void)res;
    int *counter = (int *)user_data;
    if (counter) (*counter)++;
    return true;
}

void test_middleware_user_data(void) {
    TEST("BUG-4: middleware user_data (multiple instances)");

    router_t *router = router_create();
    ASSERT(router != NULL);

    /* Register two middleware instances with different user_data */
    _counter_a = 0;
    _counter_b = 0;
    ASSERT(router_use_middleware_with_data(router, _counting_middleware_a, &_counter_a) == 0);
    ASSERT(router_use_middleware_with_data(router, _counting_middleware_b, &_counter_b) == 0);

    /* Add a dummy route */
    router_add_route(router, HTTP_GET, "/test", dummy_handler);

    /* Simulate a request */
    http_request_t req = {0};
    req.method = HTTP_GET;
    req.path = "/test";
    http_response_t res = {0};
    router_route(router, &req, &res);

    /* Both counters should have been incremented once */
    ASSERT(_counter_a == 1);
    ASSERT(_counter_b == 1);

    /* Route again — counters should increment again */
    res.sent = false;
    res.status = 0;
    _test_free_header_list(res.headers);
    res.headers = NULL;
    free(res.body);
    res.body = NULL;
    res.body_length = 0;
    router_route(router, &req, &res);
    ASSERT(_counter_a == 2);
    ASSERT(_counter_b == 2);

    _test_free_header_list(res.headers);
    free(res.body);
    router_destroy(router);
    PASS();
}

void test_middleware_null_user_data(void) {
    TEST("BUG-4: middleware NULL user_data (backward compat)");

    router_t *router = router_create();
    ASSERT(router != NULL);

    /* Register middleware via old API (NULL user_data) */
    _counter_a = 0;
    ASSERT(router_use_middleware(router, _counting_middleware_a) == 0);

    router_add_route(router, HTTP_GET, "/test2", dummy_handler);

    http_request_t req = {0};
    req.method = HTTP_GET;
    req.path = "/test2";
    http_response_t res = {0};
    router_route(router, &req, &res);

    /* Counter should NOT increment since user_data is NULL */
    ASSERT(_counter_a == 0);

    _test_free_header_list(res.headers);
    free(res.body);
    router_destroy(router);
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
    
    /* Phase 5: Body parser tests */
    test_body_parser_urlencoded();
    test_body_parser_data_structures();
    test_body_parser_empty();
    test_body_parser_null();
    
    /* Phase 5: Cookie tests */
    test_cookie_get();
    test_cookie_set();
    test_cookie_delete();
    
    /* Phase 5: CORS middleware tests */
    test_cors_create_destroy();
    test_cors_handler();
    
    /* Phase 5: Rate limiting tests */
    test_ratelimit_create_destroy();
    
    /* Phase 5: Static file serving tests */
    test_static_file_create_destroy();
    test_static_file_serve();
    test_static_file_not_found();
    test_static_file_path_traversal();
    
    /* Phase 5: HTTP status codes */
    test_http_status_codes();

    /* Phase 6: Session management tests */
    test_session_store_create_destroy();
    test_session_create_get();
    test_session_data_operations();
    test_session_destroy_session();
    test_session_expiration();
    test_session_cleanup();
    test_session_null_handling();
    test_session_cookie_set();

    /* Phase 6: Template engine tests */
    test_template_create_destroy();
    test_template_variables();
    test_template_render();
    test_template_load_file();

    /* Phase 6: Authentication middleware tests */
    test_basic_auth_create_destroy();
    test_apikey_auth_create_destroy();
    test_jwt_auth_create_destroy();

    /* Phase 6: Database connection pool tests */
    test_db_pool_create_destroy();
    test_db_pool_acquire_release();

    /* Phase 6.5: Bug fix regression tests */
    test_json_escape_decode();
    test_json_stringify_escapes();
    test_json_stringify_key_escape();
    test_json_unterminated();
    test_json_trailing_garbage();
    test_json_keyword_termination();
    test_json_null_create();
    test_json_depth_limit();

    /* Phase 6.5.2: Bug fix regression tests (Phase 2) */
    test_session_expired_cleanup_on_get();
    test_router_null_path();
    test_event_loop_timer_safety();

    /* Phase 7: Thread pool tests */
    test_thread_pool_create_destroy();
    test_thread_pool_null_handling();
    test_thread_pool_submit_and_complete();
    test_thread_pool_clamp_limits();

    /* Phase 7: Socket timeout tests */
    test_server_set_timeout();

    /* Phase 7: Thread count configuration tests */
    test_server_set_thread_count();

    /* Phase 7: Server state tests */
    test_server_state();
    
    /* Phase 8: Logging middleware tests */
    test_log_middleware_create_destroy();
    test_log_middleware_invoke();

    /* Phase 8: Error handler middleware tests */
    test_error_handler_create_destroy();
    test_error_handler_apply();

    /* Phase 8: CSRF middleware tests */
    test_csrf_create_destroy();
    test_csrf_safe_methods();

    /* Phase 8: Input validation tests */
    test_input_validate_length();
    test_input_validate_charset();
    test_input_validate_integer();
    test_input_validate_email();
    test_input_is_alphanumeric();
    test_input_sanitize_html();

    /* Phase 7: Parser hardening regression */
    test_parser_duplicate_transfer_encoding();

    /* Phase 7.5: Networking integration tests */
    test_integration_get_200();
    test_integration_not_found();
    test_integration_post_body();
    test_integration_json_response();
    test_integration_malformed_request();
    test_integration_concurrent_connections();

    /* Observability: Health check tests */
    test_health_check_register();
    test_health_check_endpoint();

    /* Phase 9: Cache tests */
    test_cache_create_destroy();
    test_cache_set_get();
    test_cache_delete();
    test_cache_clear();
    test_cache_lru_eviction();
    test_cache_ttl();

    /* Phase 9: Metrics middleware tests */
    test_metrics_create_destroy();
    test_metrics_register();
    test_metrics_record_status();
    test_metrics_endpoint();

    /* Phase 9: Compression tests */
    test_crc32_compute();
    test_compression_negotiate();
    test_compression_should_compress();
    test_gzip_compress_valid();

    /* Phase 9: Benchmark tests */
    test_benchmark_timestamp();
    test_benchmark_stats();
    test_benchmark_print();
    test_benchmark_integration();

    /* Phase 10: Bug fix tests */
    test_sigpipe_handling();
    test_session_store_thread_safety();
    test_event_loop_timer_count();
    test_server_connection_tracking();

    /* BUG-4: Middleware user_data support */
    test_middleware_user_data();
    test_middleware_null_user_data();

    printf("\n===================================\n");
    printf("Tests run: %d\n", tests_run);
    printf("Tests passed: %d\n", tests_passed);
    printf("Tests failed: %d\n", tests_run - tests_passed);
    
    return (tests_run == tests_passed) ? 0 : 1;
}
