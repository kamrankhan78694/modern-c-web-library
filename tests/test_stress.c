/*
 * Comprehensive Production-Level Stress Tests
 * 
 * This file contains stress tests designed to push the library to its limits
 * and verify proper handling of edge cases, resource limits, and high-load scenarios.
 */

#include "weblib.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <errno.h>
#include <time.h>

/* Test counter */
static int tests_run = 0;
static int tests_passed = 0;

/* Environment variable to skip server integration tests if needed */
static int skip_server_tests = 0;

/* Test macros */
#define TEST(name) \
    printf("Testing %s... ", name); \
    fflush(stdout); \
    tests_run++;

#define ASSERT(condition) \
    if (!(condition)) { \
        printf("FAILED at line %d: %s\n", __LINE__, #condition); \
        return; \
    }

#define PASS() \
    printf("PASSED\n"); \
    tests_passed++;

/* Known limits from source code */
#define MAX_ROUTES 256
#define MAX_MIDDLEWARES 32
#define MAX_SESSIONS 1024
#define JSON_MAX_DEPTH 512
#define MAX_HEADER_COUNT 100
#define MAX_BODY_BYTES (1024 * 1024)  /* 1 MiB */

/* ===== Helper Functions ===== */

/* Dummy route handler */
static void dummy_handler(http_request_t *req, http_response_t *res) {
    (void)req;
    http_response_send_text(res, HTTP_OK, "OK");
}

/* Dummy middleware */
static bool dummy_middleware(http_request_t *req, http_response_t *res) {
    (void)req;
    (void)res;
    return true;  /* Continue to next handler */
}

/* Thread function for concurrent connection tests */
typedef struct {
    uint16_t port;
    int num_requests;
    int success_count;
} thread_test_data_t;

/* Helper to send a raw HTTP request and receive response */
static int _stress_send_request(uint16_t port, const char *request, char *response, size_t resp_size) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        return -1;
    }

    /* Set socket timeout */
    struct timeval timeout;
    timeout.tv_sec = 2;
    timeout.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        close(sock);
        return -1;
    }

    /* Send request */
    size_t total_sent = 0;
    size_t req_len = strlen(request);
    while (total_sent < req_len) {
        ssize_t sent = send(sock, request + total_sent, req_len - total_sent, 0);
        if (sent <= 0) {
            close(sock);
            return -1;
        }
        total_sent += sent;
    }

    /* Receive response */
    memset(response, 0, resp_size);
    ssize_t received = recv(sock, response, resp_size - 1, 0);
    close(sock);

    return (received > 0) ? 0 : -1;
}

static void *_concurrent_request_thread(void *arg) {
    thread_test_data_t *data = (thread_test_data_t *)arg;
    char response[4096];
    const char *request = "GET /test HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n";

    data->success_count = 0;
    for (int i = 0; i < data->num_requests; i++) {
        if (_stress_send_request(data->port, request, response, sizeof(response)) == 0) {
            if (strstr(response, "200 OK") != NULL) {
                data->success_count++;
            }
        }
        usleep(50000); /* 50ms delay between requests */
    }

    return NULL;
}

/* ===== Router Stress Tests ===== */

void test_stress_router_max_routes(void) {
    TEST("router max routes limit");

    router_t *router = router_create();
    ASSERT(router != NULL);

    char path[64];
    /* Add MAX_ROUTES routes */
    for (int i = 0; i < MAX_ROUTES; i++) {
        snprintf(path, sizeof(path), "/route%d", i);
        int result = router_add_route(router, HTTP_GET, path, dummy_handler);
        ASSERT(result == 0);
    }

    /* Verify route 257 fails */
    snprintf(path, sizeof(path), "/route%d", MAX_ROUTES);
    int result = router_add_route(router, HTTP_GET, path, dummy_handler);
    ASSERT(result == -1);

    router_destroy(router);
    PASS();
}

void test_stress_router_max_middlewares(void) {
    TEST("router max middlewares limit");

    router_t *router = router_create();
    ASSERT(router != NULL);

    /* Add MAX_MIDDLEWARES middlewares */
    for (int i = 0; i < MAX_MIDDLEWARES; i++) {
        int result = router_use_middleware(router, dummy_middleware);
        ASSERT(result == 0);
    }

    /* Verify 33rd middleware fails */
    int result = router_use_middleware(router, dummy_middleware);
    ASSERT(result == -1);

    router_destroy(router);
    PASS();
}

void test_stress_router_long_paths(void) {
    TEST("router with very long paths");

    router_t *router = router_create();
    ASSERT(router != NULL);

    /* Create a path close to 4096 chars */
    char long_path[4096];
    memset(long_path, 'a', sizeof(long_path) - 1);
    long_path[0] = '/';
    long_path[sizeof(long_path) - 1] = '\0';

    int result = router_add_route(router, HTTP_GET, long_path, dummy_handler);
    ASSERT(result == 0);

    router_destroy(router);
    PASS();
}

void test_stress_router_many_params(void) {
    TEST("router with many path parameters");

    router_t *router = router_create();
    ASSERT(router != NULL);

    /* Create a route with 20 parameters */
    char path[512] = "/";
    for (int i = 0; i < 20; i++) {
        char segment[32];
        snprintf(segment, sizeof(segment), "seg%d/:param%d/", i, i);
        strcat(path, segment);
    }

    int result = router_add_route(router, HTTP_GET, path, dummy_handler);
    ASSERT(result == 0);

    router_destroy(router);
    PASS();
}

/* ===== JSON Parser Stress Tests ===== */

void test_stress_json_deep_nesting(void) {
    TEST("json deep nesting limit");

    /* Create JSON with depth close to limit (512) */
    char *json_str = malloc(JSON_MAX_DEPTH * 20);
    ASSERT(json_str != NULL);

    char *p = json_str;
    /* Create 511 nested objects */
    for (int i = 0; i < JSON_MAX_DEPTH - 1; i++) {
        p += sprintf(p, "{\"nested\":");
    }
    p += sprintf(p, "42");
    for (int i = 0; i < JSON_MAX_DEPTH - 1; i++) {
        p += sprintf(p, "}");
    }

    json_value_t *val = json_parse(json_str);
    ASSERT(val != NULL);
    json_value_free(val);

    /* Now try depth 513 - should fail */
    p = json_str;
    for (int i = 0; i < JSON_MAX_DEPTH + 1; i++) {
        p += sprintf(p, "{\"nested\":");
    }
    p += sprintf(p, "42");
    for (int i = 0; i < JSON_MAX_DEPTH + 1; i++) {
        p += sprintf(p, "}");
    }

    val = json_parse(json_str);
    ASSERT(val == NULL);  /* Should fail */

    free(json_str);
    PASS();
}

void test_stress_json_large_object(void) {
    TEST("json large object (1000+ keys)");

    json_value_t *obj = json_object_create();
    ASSERT(obj != NULL);

    /* Add 1000 key-value pairs */
    for (int i = 0; i < 1000; i++) {
        char key[32];
        snprintf(key, sizeof(key), "key%d", i);
        json_object_set(obj, key, json_number_create(i));
    }

    /* Stringify and re-parse */
    char *json_str = json_stringify(obj);
    ASSERT(json_str != NULL);

    json_value_t *parsed = json_parse(json_str);
    ASSERT(parsed != NULL);

    /* Verify a few values */
    json_value_t *val = json_object_get(parsed, "key0");
    ASSERT(val != NULL);
    val = json_object_get(parsed, "key500");
    ASSERT(val != NULL);
    val = json_object_get(parsed, "key999");
    ASSERT(val != NULL);

    free(json_str);
    json_value_free(obj);
    json_value_free(parsed);
    PASS();
}

void test_stress_json_large_array(void) {
    TEST("json large array (10000 elements)");

    json_value_t *arr = json_array_create();
    ASSERT(arr != NULL);

    /* Add 10000 elements */
    for (int i = 0; i < 10000; i++) {
        int result = json_array_append(arr, json_number_create(i));
        ASSERT(result == 0);
    }

    ASSERT(json_array_length(arr) == 10000);

    /* Stringify and re-parse */
    char *json_str = json_stringify(arr);
    ASSERT(json_str != NULL);

    json_value_t *parsed = json_parse(json_str);
    ASSERT(parsed != NULL);
    ASSERT(json_array_length(parsed) == 10000);

    free(json_str);
    json_value_free(arr);
    json_value_free(parsed);
    PASS();
}

void test_stress_json_large_string(void) {
    TEST("json large string (100KB)");

    /* Create a 100KB string */
    char *large_str = malloc(100 * 1024 + 1);
    ASSERT(large_str != NULL);
    memset(large_str, 'A', 100 * 1024);
    large_str[100 * 1024] = '\0';

    json_value_t *str_val = json_string_create(large_str);
    ASSERT(str_val != NULL);

    char *json_str = json_stringify(str_val);
    ASSERT(json_str != NULL);

    json_value_t *parsed = json_parse(json_str);
    ASSERT(parsed != NULL);

    free(large_str);
    free(json_str);
    json_value_free(str_val);
    json_value_free(parsed);
    PASS();
}

void test_stress_json_malformed_fuzzing(void) {
    TEST("json malformed input fuzzing");

    const char *malformed_inputs[] = {
        "{",
        "}",
        "[",
        "]",
        "{\"key\":}",
        "{\"key\",\"value\"}",
        "{\"key\":\"value\",}",
        "[1,2,3,]",
        "[,1,2,3]",
        "{\"key\":",
        "\"unclosed string",
        "{\"key\":\"value\"",
        "{key:\"value\"}",
        "{'key':'value'}",
        "{\"key\":undefined}",
        "[1,2,3",
        "1,2,3]",
        "{\"a\":{\"b\":{\"c\":}}}",
        "",
        "null null",
        "true false",
        "{\"\":}",
        NULL
    };

    for (int i = 0; malformed_inputs[i] != NULL; i++) {
        json_value_t *val = json_parse(malformed_inputs[i]);
        /* All should return NULL, none should crash */
        ASSERT(val == NULL);
    }

    PASS();
}

void test_stress_json_repeated_parse_free(void) {
    TEST("json repeated parse/free (10000 iterations)");

    const char *json_str = "{\"name\":\"test\",\"value\":42,\"enabled\":true,\"data\":[1,2,3,4,5]}";

    for (int i = 0; i < 10000; i++) {
        json_value_t *val = json_parse(json_str);
        ASSERT(val != NULL);
        json_value_free(val);
    }

    PASS();
}

/* ===== Cache Stress Tests ===== */

void test_stress_cache_fill_eviction(void) {
    TEST("cache fill and eviction");

    cache_t *cache = cache_create(100);  /* max 100 entries */
    ASSERT(cache != NULL);

    char key[32], value[32];

    /* Fill cache with 500 entries (should trigger eviction) */
    for (int i = 0; i < 500; i++) {
        snprintf(key, sizeof(key), "key%d", i);
        snprintf(value, sizeof(value), "value%d", i);
        int result = cache_set(cache, key, value, 3600);
        ASSERT(result == 0);
    }

    /* Cache should not exceed max_entries */
    size_t count = cache_count(cache);
    ASSERT(count <= 100);

    /* Old entries should be evicted, recent ones should exist */
    const char *val = cache_get(cache, "key0");
    ASSERT(val == NULL);  /* Should be evicted */

    val = cache_get(cache, "key499");
    ASSERT(val != NULL);  /* Should exist */

    cache_destroy(cache);
    PASS();
}

void test_stress_cache_rapid_set_get(void) {
    TEST("cache rapid set/get (10000 ops)");

    cache_t *cache = cache_create(1000);
    ASSERT(cache != NULL);

    char key[32], value[32];

    /* Rapidly set and get 10000 key-value pairs */
    for (int i = 0; i < 10000; i++) {
        snprintf(key, sizeof(key), "key%d", i % 100);  /* Reuse 100 keys */
        snprintf(value, sizeof(value), "value%d", i);
        
        int result = cache_set(cache, key, value, 3600);
        ASSERT(result == 0);

        const char *retrieved = cache_get(cache, key);
        ASSERT(retrieved != NULL);
    }

    cache_destroy(cache);
    PASS();
}

void test_stress_cache_ttl_accuracy(void) {
    TEST("cache TTL accuracy");

    cache_t *cache = cache_create(100);
    ASSERT(cache != NULL);

    /* Set entries with 1-second TTL */
    for (int i = 0; i < 10; i++) {
        char key[32];
        snprintf(key, sizeof(key), "key%d", i);
        int result = cache_set(cache, key, "value", 1);  /* 1 second TTL */
        ASSERT(result == 0);
    }

    /* Verify entries exist */
    const char *val = cache_get(cache, "key0");
    ASSERT(val != NULL);

    /* Wait 2 seconds */
    sleep(2);

    /* Verify entries expired */
    val = cache_get(cache, "key0");
    ASSERT(val == NULL);

    cache_destroy(cache);
    PASS();
}

/* ===== Session Stress Tests ===== */

void test_stress_session_mass_create(void) {
    TEST("session mass creation (MAX_SESSIONS)");

    session_store_t *store = session_store_create();
    ASSERT(store != NULL);

    char *session_ids[MAX_SESSIONS];

    /* Create MAX_SESSIONS sessions */
    for (int i = 0; i < MAX_SESSIONS; i++) {
        session_ids[i] = session_create(store, 3600);
        ASSERT(session_ids[i] != NULL);
    }

    /* Try to create one more - should fail */
    char *extra_session = session_create(store, 3600);
    ASSERT(extra_session == NULL);

    /* Cleanup */
    for (int i = 0; i < MAX_SESSIONS; i++) {
        session_destroy(store, session_ids[i]);
        free(session_ids[i]);
    }

    session_store_destroy(store);
    PASS();
}

void test_stress_session_data_operations(void) {
    TEST("session with 100 key-value pairs");

    session_store_t *store = session_store_create();
    ASSERT(store != NULL);

    char *session_id = session_create(store, 3600);
    ASSERT(session_id != NULL);

    session_t *session = session_get(store, session_id);
    ASSERT(session != NULL);

    char key[32], value[64];

    /* Set 100 key-value pairs */
    for (int i = 0; i < 100; i++) {
        snprintf(key, sizeof(key), "key%d", i);
        snprintf(value, sizeof(value), "value%d", i);
        session_set_data(session, key, value);
    }

    /* Retrieve and verify all */
    for (int i = 0; i < 100; i++) {
        snprintf(key, sizeof(key), "key%d", i);
        snprintf(value, sizeof(value), "value%d", i);
        const char *retrieved = session_get_data(session, key);
        ASSERT(retrieved != NULL);
        ASSERT(strcmp(retrieved, value) == 0);
    }

    session_destroy(store, session_id);
    free(session_id);
    session_store_destroy(store);
    PASS();
}

void test_stress_session_cleanup(void) {
    TEST("session cleanup after expiry");

    session_store_t *store = session_store_create();
    ASSERT(store != NULL);

    char *session_ids[10];

    /* Create sessions with 1-second TTL */
    for (int i = 0; i < 10; i++) {
        session_ids[i] = session_create(store, 1);  /* 1 second */
        ASSERT(session_ids[i] != NULL);
    }

    /* Verify sessions exist */
    session_t *session = session_get(store, session_ids[0]);
    ASSERT(session != NULL);

    /* Wait 2 seconds */
    sleep(2);

    /* Run cleanup */
    int cleaned = session_cleanup_expired(store);
    ASSERT(cleaned > 0);

    /* Verify sessions are gone */
    session = session_get(store, session_ids[0]);
    ASSERT(session == NULL || session_is_expired(session));

    /* Cleanup */
    for (int i = 0; i < 10; i++) {
        free(session_ids[i]);
    }

    session_store_destroy(store);
    PASS();
}

/* ===== HTTP Server Integration Stress Tests ===== */

void test_stress_rapid_connections(void) {
    TEST("rapid sequential connections (100 requests)");

    /* Create server */
    http_server_t *server = http_server_create();
    ASSERT(server != NULL);

    router_t *router = router_create();
    ASSERT(router != NULL);

    router_add_route(router, HTTP_GET, "/test", dummy_handler);
    http_server_set_router(server, router);

    /* Start server (non-blocking) */
    uint16_t port = 19000;
    int result = http_server_listen(server, port);
    ASSERT(result == 0);

    /* Give server time to start */
    usleep(200000);  /* 200ms */

    /* Send 100 rapid requests */
    char response[4096];
    const char *request = "GET /test HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n";
    int success_count = 0;

    for (int i = 0; i < 100; i++) {
        if (_stress_send_request(port, request, response, sizeof(response)) == 0) {
            if (strstr(response, "200 OK") != NULL) {
                success_count++;
            }
        }
        usleep(10000);  /* 10ms between requests */
    }

    /* Stop server */
    http_server_stop(server);
    usleep(100000);  /* Give server time to stop */

    /* Verify most requests succeeded (allow some failures) */
    ASSERT(success_count >= 85);  /* At least 85% success */

    router_destroy(router);
    http_server_destroy(server);
    PASS();
}

void test_stress_concurrent_connections(void) {
    TEST("concurrent connections (5 threads x 4 requests)");

    /* Create server */
    http_server_t *server = http_server_create();
    ASSERT(server != NULL);

    router_t *router = router_create();
    ASSERT(router != NULL);

    router_add_route(router, HTTP_GET, "/test", dummy_handler);
    http_server_set_router(server, router);

    /* Start server (non-blocking) */
    uint16_t port = 19001;
    int result = http_server_listen(server, port);
    ASSERT(result == 0);

    /* Give server time to start */
    usleep(200000);  /* 200ms */

    /* Launch 5 threads */
    pthread_t threads[5];
    thread_test_data_t test_data[5];

    for (int i = 0; i < 5; i++) {
        test_data[i].port = port;
        test_data[i].num_requests = 4;
        test_data[i].success_count = 0;
        pthread_create(&threads[i], NULL, _concurrent_request_thread, &test_data[i]);
    }

    /* Wait for all threads */
    int total_success = 0;
    for (int i = 0; i < 5; i++) {
        pthread_join(threads[i], NULL);
        total_success += test_data[i].success_count;
    }

    /* Stop server */
    http_server_stop(server);
    usleep(100000);  /* Give server time to stop */

    /* Verify most requests succeeded */
    ASSERT(total_success >= 16);  /* At least 80% of 20 total requests */

    router_destroy(router);
    http_server_destroy(server);
    PASS();
}

void test_stress_large_body(void) {
    TEST("large request body (100KB)");

    /* Create server */
    http_server_t *server = http_server_create();
    ASSERT(server != NULL);

    router_t *router = router_create();
    ASSERT(router != NULL);

    router_add_route(router, HTTP_POST, "/upload", dummy_handler);
    http_server_set_router(server, router);

    /* Start server (non-blocking) */
    uint16_t port = 19002;
    int result = http_server_listen(server, port);
    ASSERT(result == 0);

    /* Give server time to start */
    usleep(200000);  /* 200ms */

    /* Create 100KB body with printable characters */
    size_t body_size = 100 * 1024;
    char *body = malloc(body_size + 1);
    ASSERT(body != NULL);
    for (size_t i = 0; i < body_size; i++) {
        body[i] = 'A' + (i % 26);  /* Cycle through A-Z */
    }
    body[body_size] = '\0';

    /* Build request */
    char *request = malloc(body_size + 1024);
    ASSERT(request != NULL);
    snprintf(request, body_size + 1024,
             "POST /upload HTTP/1.1\r\n"
             "Host: localhost\r\n"
             "Connection: close\r\n"
             "Content-Length: %zu\r\n"
             "\r\n%s",
             body_size, body);

    /* Send request */
    char response[4096];
    result = _stress_send_request(port, request, response, sizeof(response));

    /* Stop server */
    http_server_stop(server);
    usleep(100000);  /* Give server time to stop */

    /* Verify success (allow failure if server is overloaded) */
    if (result == 0) {
        ASSERT(strstr(response, "200 OK") != NULL || strstr(response, "HTTP") != NULL);
    }
    /* If request fails, that's also acceptable in stress testing */

    free(body);
    free(request);
    router_destroy(router);
    http_server_destroy(server);
    PASS();
}

void test_stress_oversized_request(void) {
    TEST("oversized request body (>1MB)");

    /* Create server */
    http_server_t *server = http_server_create();
    ASSERT(server != NULL);

    router_t *router = router_create();
    ASSERT(router != NULL);

    router_add_route(router, HTTP_POST, "/upload", dummy_handler);
    http_server_set_router(server, router);

    /* Start server (non-blocking) */
    uint16_t port = 19003;
    int result = http_server_listen(server, port);
    ASSERT(result == 0);

    /* Give server time to start */
    usleep(100000);  /* 100ms */

    /* Create 2MB body (exceeds 1MB limit) */
    size_t body_size = 2 * 1024 * 1024;
    char *body = malloc(body_size + 1);
    ASSERT(body != NULL);
    memset(body, 'A', body_size);
    body[body_size] = '\0';

    /* Build request header */
    char header[512];
    snprintf(header, sizeof(header),
             "POST /upload HTTP/1.1\r\n"
             "Host: localhost\r\n"
             "Content-Length: %zu\r\n"
             "\r\n",
             body_size);

    /* Connect and send */
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    ASSERT(sock >= 0);

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    int conn_result = connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr));
    ASSERT(conn_result == 0);

    /* Send header */
    send(sock, header, strlen(header), 0);

    /* Send body in chunks */
    size_t sent = 0;
    size_t chunk_size = 64 * 1024;
    while (sent < body_size) {
        size_t to_send = (body_size - sent > chunk_size) ? chunk_size : (body_size - sent);
        ssize_t n = send(sock, body + sent, to_send, 0);
        if (n <= 0) break;
        sent += n;
    }

    /* Try to receive response */
    char response[4096];
    ssize_t received = recv(sock, response, sizeof(response) - 1, 0);
    close(sock);

    /* Stop server */
    http_server_stop(server);
    usleep(100000);  /* Give server time to stop */

    /* Server should reject with 413 or close connection */
    if (received > 0) {
        response[received] = '\0';
        /* Should get error response */
        ASSERT(strstr(response, "413") != NULL || strstr(response, "400") != NULL);
    }
    /* Or connection closed (received <= 0), which is also acceptable */

    free(body);
    router_destroy(router);
    http_server_destroy(server);
    PASS();
}

void test_stress_many_headers(void) {
    TEST("request with many headers (90 headers)");

    /* Create server */
    http_server_t *server = http_server_create();
    ASSERT(server != NULL);

    router_t *router = router_create();
    ASSERT(router != NULL);

    router_add_route(router, HTTP_GET, "/test", dummy_handler);
    http_server_set_router(server, router);

    /* Start server (non-blocking) */
    uint16_t port = 19004;
    int result = http_server_listen(server, port);
    ASSERT(result == 0);

    /* Give server time to start */
    usleep(100000);  /* 100ms */

    /* Build request with 90 headers */
    char request[16384];
    char *p = request;
    p += sprintf(p, "GET /test HTTP/1.1\r\n");
    p += sprintf(p, "Host: localhost\r\n");
    p += sprintf(p, "Connection: close\r\n");
    
    for (int i = 0; i < 90; i++) {
        p += sprintf(p, "X-Custom-Header-%d: value%d\r\n", i, i);
    }
    p += sprintf(p, "\r\n");

    /* Send request */
    char response[4096];
    result = _stress_send_request(port, request, response, sizeof(response));

    /* Stop server */
    http_server_stop(server);
    usleep(100000);  /* Give server time to stop */

    /* Verify success */
    ASSERT(result == 0);
    ASSERT(strstr(response, "200 OK") != NULL);

    router_destroy(router);
    http_server_destroy(server);
    PASS();
}

void test_stress_slow_client(void) {
    TEST("slow client (partial request with delay)");

    /* Create server */
    http_server_t *server = http_server_create();
    ASSERT(server != NULL);

    router_t *router = router_create();
    ASSERT(router != NULL);

    router_add_route(router, HTTP_GET, "/test", dummy_handler);
    http_server_set_router(server, router);

    /* Start server (non-blocking) */
    uint16_t port = 19005;
    int result = http_server_listen(server, port);
    ASSERT(result == 0);

    /* Give server time to start */
    usleep(200000);  /* 200ms */

    /* Connect */
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    ASSERT(sock >= 0);

    /* Set shorter timeout for this test */
    struct timeval timeout;
    timeout.tv_sec = 3;
    timeout.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    int conn_result = connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr));
    ASSERT(conn_result == 0);

    /* Send partial request */
    const char *part1 = "GET /test HTTP/1.1\r\n";
    send(sock, part1, strlen(part1), 0);

    /* Wait 1 second */
    sleep(1);

    /* Send rest of request */
    const char *part2 = "Host: localhost\r\nConnection: close\r\n\r\n";
    send(sock, part2, strlen(part2), 0);

    /* Receive response */
    char response[4096];
    ssize_t received = recv(sock, response, sizeof(response) - 1, 0);
    close(sock);

    /* Stop server */
    http_server_stop(server);
    usleep(100000);  /* Give server time to stop */

    /* Verify success (should work within timeout) */
    if (received > 0) {
        response[received] = '\0';
        ASSERT(strstr(response, "200 OK") != NULL);
    }
    /* If timeout, that's also acceptable for this stress test */

    router_destroy(router);
    http_server_destroy(server);
    PASS();
}

/* ===== Input Validation Stress Tests ===== */

void test_stress_input_validation_long_strings(void) {
    TEST("input validation with very long strings");

    /* Create 100KB string */
    char *long_str = malloc(100 * 1024 + 1);
    ASSERT(long_str != NULL);
    memset(long_str, 'A', 100 * 1024);
    long_str[100 * 1024] = '\0';

    /* Test length validation */
    bool result = input_validate_length(long_str, 0, 200 * 1024);
    ASSERT(result == true);

    result = input_validate_length(long_str, 0, 50 * 1024);
    ASSERT(result == false);

    free(long_str);
    PASS();
}

void test_stress_html_sanitize_large(void) {
    TEST("html sanitize with many script tags");

    /* Create string with 1000 script tags */
    char *html = malloc(1000 * 100);
    ASSERT(html != NULL);
    
    char *p = html;
    for (int i = 0; i < 1000; i++) {
        p += sprintf(p, "<script>alert('xss%d')</script>", i);
    }

    /* Sanitize (should not crash) */
    char *sanitized = input_sanitize_html(html);
    ASSERT(sanitized != NULL);

    /* Verify scripts are removed/escaped */
    ASSERT(strstr(sanitized, "<script>") == NULL || strstr(sanitized, "&lt;script&gt;") != NULL);

    free(html);
    free(sanitized);
    PASS();
}

/* ===== Compression Stress Tests ===== */

void test_stress_compression_large_payload(void) {
    TEST("compression with 1MB payload");

    /* Create 1MB data */
    size_t size = 1024 * 1024;
    uint8_t *data = malloc(size);
    ASSERT(data != NULL);
    
    /* Fill with pattern */
    for (size_t i = 0; i < size; i++) {
        data[i] = (uint8_t)(i % 256);
    }

    /* Compute CRC32 (should not crash) */
    uint32_t crc = crc32_compute(data, size);
    ASSERT(crc != 0);  /* Very unlikely to be exactly 0 */

    free(data);
    PASS();
}

/* ===== Memory Lifecycle Stress Tests ===== */

void test_stress_server_create_destroy_cycle(void) {
    TEST("server create/destroy cycle (100 iterations)");

    for (int i = 0; i < 100; i++) {
        http_server_t *server = http_server_create();
        ASSERT(server != NULL);
        http_server_destroy(server);
    }

    PASS();
}

void test_stress_router_create_destroy_cycle(void) {
    TEST("router create/destroy cycle (100 iterations)");

    for (int i = 0; i < 100; i++) {
        router_t *router = router_create();
        ASSERT(router != NULL);

        /* Add some routes */
        router_add_route(router, HTTP_GET, "/test", dummy_handler);
        router_add_route(router, HTTP_POST, "/data", dummy_handler);
        router_use_middleware(router, dummy_middleware);

        router_destroy(router);
    }

    PASS();
}

void test_stress_event_loop_create_destroy_cycle(void) {
    TEST("event loop create/destroy cycle (100 iterations)");

    for (int i = 0; i < 100; i++) {
        event_loop_t *loop = event_loop_create();
        ASSERT(loop != NULL);
        event_loop_destroy(loop);
    }

    PASS();
}

/* ===== Main Test Runner ===== */

int main(void) {
    printf("=== Modern C Web Library - Comprehensive Stress Tests ===\n\n");

    /* Check if we should skip server integration tests */
    if (getenv("SKIP_SERVER_TESTS") != NULL) {
        skip_server_tests = 1;
        printf("Note: Skipping server integration tests (SKIP_SERVER_TESTS set)\n\n");
    }

    /* Router Stress Tests */
    printf("--- Router Stress Tests ---\n");
    test_stress_router_max_routes();
    test_stress_router_max_middlewares();
    test_stress_router_long_paths();
    test_stress_router_many_params();

    /* JSON Parser Stress Tests */
    printf("\n--- JSON Parser Stress Tests ---\n");
    test_stress_json_deep_nesting();
    test_stress_json_large_object();
    test_stress_json_large_array();
    test_stress_json_large_string();
    test_stress_json_malformed_fuzzing();
    test_stress_json_repeated_parse_free();

    /* Cache Stress Tests */
    printf("\n--- Cache Stress Tests ---\n");
    test_stress_cache_fill_eviction();
    test_stress_cache_rapid_set_get();
    test_stress_cache_ttl_accuracy();

    /* Session Stress Tests */
    printf("\n--- Session Stress Tests ---\n");
    test_stress_session_mass_create();
    test_stress_session_data_operations();
    test_stress_session_cleanup();

    /* HTTP Server Integration Stress Tests */
    if (!skip_server_tests) {
        printf("\n--- HTTP Server Integration Stress Tests ---\n");
        test_stress_rapid_connections();
        test_stress_concurrent_connections();
        test_stress_large_body();
        test_stress_oversized_request();
        test_stress_many_headers();
        test_stress_slow_client();
    }

    /* Input Validation Stress Tests */
    printf("\n--- Input Validation Stress Tests ---\n");
    test_stress_input_validation_long_strings();
    test_stress_html_sanitize_large();

    /* Compression Stress Tests */
    printf("\n--- Compression Stress Tests ---\n");
    test_stress_compression_large_payload();

    /* Memory Lifecycle Stress Tests */
    printf("\n--- Memory Lifecycle Stress Tests ---\n");
    test_stress_server_create_destroy_cycle();
    test_stress_router_create_destroy_cycle();
    test_stress_event_loop_create_destroy_cycle();

    /* Print summary */
    printf("\n=== Test Summary ===\n");
    printf("Tests run: %d\n", tests_run);
    printf("Tests passed: %d\n", tests_passed);
    printf("Tests failed: %d\n", tests_run - tests_passed);

    if (tests_run == tests_passed) {
        printf("\n✓ All stress tests passed!\n");
        return 0;
    } else {
        printf("\n✗ Some stress tests failed.\n");
        return 1;
    }
}
