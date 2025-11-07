#include "weblib.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

/* Test counter */
static int tests_run = 0;
static int tests_passed = 0;

/* Get temporary directory path (cross-platform) */
static const char* get_temp_dir(void) {
#ifdef _WIN32
    static char temp_path[260];
    DWORD result = GetTempPath(260, temp_path);
    if (result > 0 && result < 260) {
        return temp_path;
    }
    return ".";
#else
    const char *temp = getenv("TMPDIR");
    if (temp) return temp;
    temp = getenv("TMP");
    if (temp) return temp;
    temp = getenv("TEMP");
    if (temp) return temp;
    return "/tmp";
#endif
}

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

/* Test static file response - file exists */
void test_static_file_response(void) {
    TEST("http_response_send_file");
    
    /* Create a temporary test file */
    char test_path[512];
    snprintf(test_path, sizeof(test_path), "%s/test_static.txt", get_temp_dir());
    
    FILE *test_file = fopen(test_path, "w");
    ASSERT(test_file != NULL);
    fprintf(test_file, "Test content");
    fclose(test_file);
    
    /* Create response and send file */
    http_response_t res = {0};
    int result = http_response_send_file(&res, test_path);
    
    ASSERT(result == 0);
    ASSERT(res.status == HTTP_OK);
    ASSERT(res.body != NULL);
    ASSERT(res.body_length == 12); /* "Test content" */
    ASSERT(strncmp(res.body, "Test content", 12) == 0);
    
    /* Cleanup */
    free(res.body);
    free(res.content_type);
    remove(test_path);
    
    PASS();
}

/* Test static file response - file not found */
void test_static_file_not_found(void) {
    TEST("http_response_send_file (not found)");
    
    char test_path[512];
    snprintf(test_path, sizeof(test_path), "%s/nonexistent_file.txt", get_temp_dir());
    
    http_response_t res = {0};
    int result = http_response_send_file(&res, test_path);
    
    ASSERT(result == -1);
    ASSERT(res.status == HTTP_NOT_FOUND);
    ASSERT(res.body != NULL); /* Error message is set */
    
    /* Cleanup */
    free(res.body);
    free(res.content_type);
    
    PASS();
}

/* Test static file handler middleware */
void test_static_file_handler(void) {
    TEST("static_file_handler");
    
    /* Create test directory and file paths */
    char test_dir[512];
    char test_file_path[512];
    snprintf(test_dir, sizeof(test_dir), "%s/test_public", get_temp_dir());
    snprintf(test_file_path, sizeof(test_file_path), "%s/test.html", test_dir);
    
    /* Create a temporary test directory and file using C standard library */
    #ifdef _WIN32
        _mkdir(test_dir);
    #else
        mkdir(test_dir, 0755);
    #endif
    
    FILE *test_file = fopen(test_file_path, "w");
    ASSERT(test_file != NULL);
    fprintf(test_file, "<html>Test</html>");
    fclose(test_file);
    
    /* Create request and response */
    http_request_t req = {0};
    req.method = HTTP_GET;
    req.path = "/test.html";
    
    http_response_t res = {0};
    
    /* Call static file handler */
    bool continue_processing = static_file_handler(&req, &res, test_dir);
    
    ASSERT(continue_processing == false); /* File was served, stop processing */
    ASSERT(res.status == HTTP_OK);
    ASSERT(res.body != NULL);
    
    /* Cleanup */
    free(res.body);
    free(res.content_type);
    remove(test_file_path);
    rmdir(test_dir);
    
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
    
    /* Static file serving tests */
    test_static_file_response();
    test_static_file_not_found();
    test_static_file_handler();
    
    printf("\n===================================\n");
    printf("Tests run: %d\n", tests_run);
    printf("Tests passed: %d\n", tests_passed);
    printf("Tests failed: %d\n", tests_run - tests_passed);
    
    return (tests_run == tests_passed) ? 0 : 1;
}
