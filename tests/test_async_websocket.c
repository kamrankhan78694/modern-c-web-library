/* Test program for async WebSocket manager */
#include "kamran.k"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/*
 * Assertions that survive NDEBUG.
 *
 * This suite previously used the standard assert(), which the C library compiles
 * to nothing when NDEBUG is defined — and CMake defines NDEBUG for the Release /
 * RelWithDebInfo configurations, the latter being exactly what CI builds. Every
 * assertion here was therefore removed by the preprocessor in CI, so the suite
 * passed unconditionally: it would have reported success even if every invariant
 * below were false.
 *
 * CHECK()   — always evaluated; records a failure and keeps going, so one broken
 *             invariant does not hide the others.
 * REQUIRE() — for preconditions that later statements depend on (chiefly non-NULL
 *             pointers). Unlike CHECK() it returns immediately, because assert()
 *             used to abort here: continuing past a failed pointer check would
 *             dereference NULL, which is undefined behaviour rather than a
 *             reported test failure.
 */
static int g_failures = 0;
#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            printf("FAIL: %s (%s:%d)\n", #cond, __FILE__, __LINE__);      \
            g_failures++;                                                 \
        }                                                                 \
    } while (0)
#define REQUIRE(cond)                                                     \
    do {                                                                  \
        if (!(cond)) {                                                    \
            printf("FAIL (fatal): %s (%s:%d)\n", #cond, __FILE__, __LINE__); \
            return 1;                                                     \
        }                                                                 \
    } while (0)

static int test_create_destroy(void) {
    printf("Testing async_ws_manager (create/destroy)... ");
    
    event_loop_t *loop = event_loop_create();
    REQUIRE(loop != NULL);
    
    async_ws_manager_t *mgr = async_ws_manager_create(loop);
    REQUIRE(mgr != NULL);
    CHECK(async_ws_manager_count(mgr) == 0);
    
    async_ws_manager_destroy(mgr);
    event_loop_destroy(loop);
    
    printf("PASSED\n");
    return 0;
}

static int test_null_handling(void) {
    printf("Testing async_ws_manager (null handling)... ");
    
    /* NULL event loop */
    async_ws_manager_t *mgr = async_ws_manager_create(NULL);
    (void)mgr;
    CHECK(mgr == NULL);
    
    /* NULL manager operations */
    async_ws_manager_destroy(NULL);
    async_ws_manager_set_callbacks(NULL, NULL, NULL, NULL);
    CHECK(async_ws_manager_add(NULL, NULL) == -1);
    CHECK(async_ws_manager_remove(NULL, NULL) == -1);
    CHECK(async_ws_send(NULL, NULL, WS_MESSAGE_TEXT, "test", 4) == -1);
    CHECK(async_ws_manager_count(NULL) == 0);
    
    printf("PASSED\n");
    return 0;
}

static void dummy_message_cb(websocket_connection_t *conn, ws_message_type_t type, const void *data, size_t len) {
    (void)conn; (void)type; (void)data; (void)len;
}

static void dummy_close_cb(websocket_connection_t *conn, uint16_t code) {
    (void)conn; (void)code;
}

static void dummy_error_cb(websocket_connection_t *conn, const char *error) {
    (void)conn; (void)error;
}

static int test_set_callbacks(void) {
    printf("Testing async_ws_manager (set callbacks)... ");
    
    event_loop_t *loop = event_loop_create();
    REQUIRE(loop != NULL);
    
    async_ws_manager_t *mgr = async_ws_manager_create(loop);
    REQUIRE(mgr != NULL);
    
    /* Should not crash */
    async_ws_manager_set_callbacks(mgr, dummy_message_cb, dummy_close_cb, dummy_error_cb);
    async_ws_manager_set_callbacks(mgr, NULL, NULL, NULL);
    
    async_ws_manager_destroy(mgr);
    event_loop_destroy(loop);
    
    printf("PASSED\n");
    return 0;
}

static int test_add_remove_invalid(void) {
    printf("Testing async_ws_manager (add/remove invalid)... ");
    
    event_loop_t *loop = event_loop_create();
    REQUIRE(loop != NULL);
    
    async_ws_manager_t *mgr = async_ws_manager_create(loop);
    REQUIRE(mgr != NULL);
    
    /* Add NULL connection should fail */
    CHECK(async_ws_manager_add(mgr, NULL) == -1);
    
    /* Remove non-existent connection should fail */
    CHECK(async_ws_manager_remove(mgr, (websocket_connection_t *)0xDEADBEEF) == -1);
    
    CHECK(async_ws_manager_count(mgr) == 0);
    
    async_ws_manager_destroy(mgr);
    event_loop_destroy(loop);
    
    printf("PASSED\n");
    return 0;
}

static int test_send_invalid(void) {
    printf("Testing async_ws_manager (send invalid)... ");
    
    event_loop_t *loop = event_loop_create();
    REQUIRE(loop != NULL);
    
    async_ws_manager_t *mgr = async_ws_manager_create(loop);
    REQUIRE(mgr != NULL);
    
    /* Send with NULL data should fail */
    CHECK(async_ws_send(mgr, (websocket_connection_t *)0xDEADBEEF, WS_MESSAGE_TEXT, NULL, 10) == -1);
    
    /* Send with NULL connection should fail */
    const char *msg = "test";
    (void)msg;
    CHECK(async_ws_send(mgr, NULL, WS_MESSAGE_TEXT, msg, 4) == -1);
    
    async_ws_manager_destroy(mgr);
    event_loop_destroy(loop);
    
    printf("PASSED\n");
    return 0;
}

int main(void) {
    printf("\n=== Testing Async WebSocket Manager ===\n\n");
    
    int failed = 0;
    failed += test_create_destroy();
    failed += test_null_handling();
    failed += test_set_callbacks();
    failed += test_add_remove_invalid();
    failed += test_send_invalid();
    
    printf("\n===================================\n");
    printf("Async WebSocket Tests: %d failed\n", failed + g_failures);
    printf("===================================\n\n");
    
    return failed + g_failures;
}
