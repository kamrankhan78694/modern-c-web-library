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

/* Test session store creation */
void test_session_store_create(void) {
    TEST("session_store_create");
    
    session_store_t *store = session_store_create();
    ASSERT(store != NULL);
    
    session_store_destroy(store);
    
    PASS();
}

/* Test session creation */
void test_session_create(void) {
    TEST("session_create");
    
    session_store_t *store = session_store_create();
    ASSERT(store != NULL);
    
    char *session_id = session_create(store, 3600);
    ASSERT(session_id != NULL);
    ASSERT(strlen(session_id) > 0);
    
    free(session_id);
    session_store_destroy(store);
    
    PASS();
}

/* Test session get */
void test_session_get(void) {
    TEST("session_get");
    
    session_store_t *store = session_store_create();
    ASSERT(store != NULL);
    
    char *session_id = session_create(store, 3600);
    ASSERT(session_id != NULL);
    
    session_t *session = session_get(store, session_id);
    ASSERT(session != NULL);
    ASSERT(strcmp(session_get_id(session), session_id) == 0);
    
    free(session_id);
    session_store_destroy(store);
    
    PASS();
}

/* Test session data set/get */
void test_session_data(void) {
    TEST("session_set/get_data");
    
    session_store_t *store = session_store_create();
    ASSERT(store != NULL);
    
    char *session_id = session_create(store, 3600);
    ASSERT(session_id != NULL);
    
    session_t *session = session_get(store, session_id);
    ASSERT(session != NULL);
    
    /* Set session data */
    session_set(session, "user_id", "12345");
    session_set(session, "username", "testuser");
    
    /* Get session data */
    const char *user_id = session_get_data(session, "user_id");
    ASSERT(user_id != NULL);
    ASSERT(strcmp(user_id, "12345") == 0);
    
    const char *username = session_get_data(session, "username");
    ASSERT(username != NULL);
    ASSERT(strcmp(username, "testuser") == 0);
    
    /* Get non-existent data */
    const char *missing = session_get_data(session, "missing_key");
    ASSERT(missing == NULL);
    
    free(session_id);
    session_store_destroy(store);
    
    PASS();
}

/* Test session data update */
void test_session_data_update(void) {
    TEST("session_set (update)");
    
    session_store_t *store = session_store_create();
    ASSERT(store != NULL);
    
    char *session_id = session_create(store, 3600);
    session_t *session = session_get(store, session_id);
    ASSERT(session != NULL);
    
    /* Set initial value */
    session_set(session, "counter", "1");
    const char *val1 = session_get_data(session, "counter");
    ASSERT(strcmp(val1, "1") == 0);
    
    /* Update value */
    session_set(session, "counter", "2");
    const char *val2 = session_get_data(session, "counter");
    ASSERT(strcmp(val2, "2") == 0);
    
    free(session_id);
    session_store_destroy(store);
    
    PASS();
}

/* Test session data removal */
void test_session_data_remove(void) {
    TEST("session_remove_data");
    
    session_store_t *store = session_store_create();
    ASSERT(store != NULL);
    
    char *session_id = session_create(store, 3600);
    session_t *session = session_get(store, session_id);
    ASSERT(session != NULL);
    
    /* Set and then remove data */
    session_set(session, "temp_data", "temporary");
    const char *val = session_get_data(session, "temp_data");
    ASSERT(val != NULL);
    
    session_remove_data(session, "temp_data");
    const char *removed = session_get_data(session, "temp_data");
    ASSERT(removed == NULL);
    
    free(session_id);
    session_store_destroy(store);
    
    PASS();
}

/* Test session destruction */
void test_session_destroy(void) {
    TEST("session_destroy");
    
    session_store_t *store = session_store_create();
    ASSERT(store != NULL);
    
    char *session_id = session_create(store, 3600);
    ASSERT(session_id != NULL);
    
    /* Verify session exists */
    session_t *session = session_get(store, session_id);
    ASSERT(session != NULL);
    
    /* Destroy session */
    session_destroy(store, session_id);
    
    /* Verify session is gone */
    session_t *destroyed = session_get(store, session_id);
    ASSERT(destroyed == NULL);
    
    free(session_id);
    session_store_destroy(store);
    
    PASS();
}

/* Test session expiration */
void test_session_expiration(void) {
    TEST("session_is_expired");
    
    session_store_t *store = session_store_create();
    ASSERT(store != NULL);
    
    /* Create session with 0 max_age (session cookie - never expires by time) */
    char *session_id1 = session_create(store, 0);
    session_t *session1 = session_get(store, session_id1);
    ASSERT(session1 != NULL);
    ASSERT(session_is_expired(session1) == false);
    
    /* Create session with positive max_age */
    char *session_id2 = session_create(store, 3600);
    session_t *session2 = session_get(store, session_id2);
    ASSERT(session2 != NULL);
    ASSERT(session_is_expired(session2) == false);
    
    free(session_id1);
    free(session_id2);
    session_store_destroy(store);
    
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
    
    /* Session tests */
    test_session_store_create();
    test_session_create();
    test_session_get();
    test_session_data();
    test_session_data_update();
    test_session_data_remove();
    test_session_destroy();
    test_session_expiration();
    
    printf("\n===================================\n");
    printf("Tests run: %d\n", tests_run);
    printf("Tests passed: %d\n", tests_passed);
    printf("Tests failed: %d\n", tests_run - tests_passed);
    
    return (tests_run == tests_passed) ? 0 : 1;
}
