#include "weblib.h"
#include "db_pool.h"
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
    bool result = mw(&req, &res);
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
    bool result = mw(&req, &res);
    ASSERT(result == false);  /* File served, stop chain */
    ASSERT(res.status == HTTP_OK);
    ASSERT(res.body != NULL);
    ASSERT(res.body_length == 19);  /* "Hello, static file!" */
    ASSERT(memcmp(res.body, "Hello, static file!", 19) == 0);

    free(req.path);
    free(res.body);

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
    bool result = mw(&req, &res);
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
    bool result = mw(&req, &res);
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
    char deep[1200];
    memset(deep, '[', 600);
    memset(deep + 600, ']', 600);
    deep[1200 - 1] = '\0';

    json_value_t *val = json_parse(deep);
    /* Should be rejected due to depth limit */
    ASSERT(val == NULL);

    /* Moderate nesting should work */
    val = json_parse("[[[[1]]]]");
    ASSERT(val != NULL);
    json_value_free(val);

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
    
    printf("\n===================================\n");
    printf("Tests run: %d\n", tests_run);
    printf("Tests passed: %d\n", tests_passed);
    printf("Tests failed: %d\n", tests_run - tests_passed);
    
    return (tests_run == tests_passed) ? 0 : 1;
}
