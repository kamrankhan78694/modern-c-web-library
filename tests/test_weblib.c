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

/* Test cookie creation */
void test_cookie_create(void) {
    TEST("http_cookie_create");
    
    http_cookie_t *cookie = http_cookie_create("session_id", "abc123");
    ASSERT(cookie != NULL);
    ASSERT(strcmp(cookie->name, "session_id") == 0);
    ASSERT(strcmp(cookie->value, "abc123") == 0);
    ASSERT(cookie->max_age == -1);
    ASSERT(cookie->http_only == false);
    ASSERT(cookie->secure == false);
    
    http_cookie_free(cookie);
    
    PASS();
}

/* Test cookie attributes */
void test_cookie_attributes(void) {
    TEST("http_cookie_set_attributes");
    
    http_cookie_t *cookie = http_cookie_create("test", "value");
    ASSERT(cookie != NULL);
    
    http_cookie_set_domain(cookie, "example.com");
    ASSERT(cookie->domain != NULL);
    ASSERT(strcmp(cookie->domain, "example.com") == 0);
    
    http_cookie_set_path(cookie, "/api");
    ASSERT(cookie->path != NULL);
    ASSERT(strcmp(cookie->path, "/api") == 0);
    
    http_cookie_set_max_age(cookie, 3600);
    ASSERT(cookie->max_age == 3600);
    
    http_cookie_set_http_only(cookie, true);
    ASSERT(cookie->http_only == true);
    
    http_cookie_set_secure(cookie, true);
    ASSERT(cookie->secure == true);
    
    http_cookie_set_same_site(cookie, "Strict");
    ASSERT(cookie->same_site != NULL);
    ASSERT(strcmp(cookie->same_site, "Strict") == 0);
    
    http_cookie_free(cookie);
    
    PASS();
}

/* Test cookie parsing */
void test_cookie_parsing(void) {
    TEST("http_request_parse_cookies");
    
    http_request_t req = {0};
    http_request_parse_cookies(&req, "session_id=abc123; user=john");
    
    ASSERT(req.cookies != NULL);
    ASSERT(strcmp(req.cookies->name, "session_id") == 0);
    ASSERT(strcmp(req.cookies->value, "abc123") == 0);
    
    ASSERT(req.cookies->next != NULL);
    ASSERT(strcmp(req.cookies->next->name, "user") == 0);
    ASSERT(strcmp(req.cookies->next->value, "john") == 0);
    
    http_cookie_free(req.cookies);
    
    PASS();
}

/* Test getting cookie from request */
void test_request_get_cookie(void) {
    TEST("http_request_get_cookie");
    
    http_request_t req = {0};
    http_request_parse_cookies(&req, "session_id=abc123; user=john");
    
    const char *session = http_request_get_cookie(&req, "session_id");
    ASSERT(session != NULL);
    ASSERT(strcmp(session, "abc123") == 0);
    
    const char *user = http_request_get_cookie(&req, "user");
    ASSERT(user != NULL);
    ASSERT(strcmp(user, "john") == 0);
    
    const char *missing = http_request_get_cookie(&req, "missing");
    ASSERT(missing == NULL);
    
    http_cookie_free(req.cookies);
    
    PASS();
}

/* Test setting cookie in response */
void test_response_set_cookie(void) {
    TEST("http_response_set_cookie");
    
    http_response_t res = {0};
    
    http_cookie_t *cookie1 = http_cookie_create("session", "xyz789");
    http_response_set_cookie(&res, cookie1);
    ASSERT(res.cookies != NULL);
    ASSERT(strcmp(res.cookies->name, "session") == 0);
    
    http_cookie_t *cookie2 = http_cookie_create("user", "jane");
    http_response_set_cookie(&res, cookie2);
    ASSERT(res.cookies->next != NULL);
    ASSERT(strcmp(res.cookies->next->name, "user") == 0);
    
    http_cookie_free(res.cookies);
    
    PASS();
}

/* Test Set-Cookie header generation */
void test_cookie_to_header(void) {
    TEST("http_cookie_to_set_cookie_header");
    
    http_cookie_t *cookie = http_cookie_create("session", "abc123");
    http_cookie_set_path(cookie, "/");
    http_cookie_set_max_age(cookie, 3600);
    http_cookie_set_http_only(cookie, true);
    http_cookie_set_secure(cookie, true);
    
    char *header = http_cookie_to_set_cookie_header(cookie);
    ASSERT(header != NULL);
    ASSERT(strstr(header, "session=abc123") != NULL);
    ASSERT(strstr(header, "Path=/") != NULL);
    ASSERT(strstr(header, "Max-Age=3600") != NULL);
    ASSERT(strstr(header, "HttpOnly") != NULL);
    ASSERT(strstr(header, "Secure") != NULL);
    
    free(header);
    http_cookie_free(cookie);
    
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
    
    /* Cookie tests */
    test_cookie_create();
    test_cookie_attributes();
    test_cookie_parsing();
    test_request_get_cookie();
    test_response_set_cookie();
    test_cookie_to_header();
    
    printf("\n===================================\n");
    printf("Tests run: %d\n", tests_run);
    printf("Tests passed: %d\n", tests_passed);
    printf("Tests failed: %d\n", tests_run - tests_passed);
    
    return (tests_run == tests_passed) ? 0 : 1;
}
