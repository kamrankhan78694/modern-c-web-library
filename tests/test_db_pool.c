#include "db_pool.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <pthread.h>

/* Test counter */
static int tests_run = 0;
static int tests_passed = 0;

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

/* Test pool creation */
void test_pool_create(void) {
    TEST("db_pool_create");
    
    db_pool_config_t config = db_pool_config_default(DB_TYPE_GENERIC, "test://localhost");
    ASSERT(config.connection_string != NULL);
    
    db_pool_t *pool = db_pool_create(&config);
    ASSERT(pool != NULL);
    
    db_pool_destroy(pool);
    free(config.connection_string);
    
    PASS();
}

/* Test connection acquisition */
void test_pool_acquire(void) {
    TEST("db_pool_acquire");
    
    db_pool_config_t config = db_pool_config_default(DB_TYPE_GENERIC, "test://localhost");
    db_pool_t *pool = db_pool_create(&config);
    ASSERT(pool != NULL);
    
    db_connection_t *conn = db_pool_acquire(pool);
    ASSERT(conn != NULL);
    ASSERT(db_connection_is_valid(conn));
    
    int result = db_pool_release(pool, conn);
    ASSERT(result == 0);
    
    db_pool_destroy(pool);
    free(config.connection_string);
    
    PASS();
}

/* Test multiple connections */
void test_pool_multiple_acquire(void) {
    TEST("db_pool_multiple_acquire");
    
    db_pool_config_t config = db_pool_config_default(DB_TYPE_GENERIC, "test://localhost");
    config.max_connections = 5;
    
    db_pool_t *pool = db_pool_create(&config);
    ASSERT(pool != NULL);
    
    db_connection_t *conn1 = db_pool_acquire(pool);
    db_connection_t *conn2 = db_pool_acquire(pool);
    db_connection_t *conn3 = db_pool_acquire(pool);
    
    ASSERT(conn1 != NULL);
    ASSERT(conn2 != NULL);
    ASSERT(conn3 != NULL);
    ASSERT(conn1 != conn2);
    ASSERT(conn2 != conn3);
    
    db_pool_release(pool, conn1);
    db_pool_release(pool, conn2);
    db_pool_release(pool, conn3);
    
    db_pool_destroy(pool);
    free(config.connection_string);
    
    PASS();
}

/* Test pool statistics */
void test_pool_stats(void) {
    TEST("db_pool_get_stats");
    
    db_pool_config_t config = db_pool_config_default(DB_TYPE_GENERIC, "test://localhost");
    config.min_connections = 2;
    config.max_connections = 5;
    
    db_pool_t *pool = db_pool_create(&config);
    ASSERT(pool != NULL);
    
    db_pool_stats_t stats;
    int result = db_pool_get_stats(pool, &stats);
    ASSERT(result == 0);
    ASSERT(stats.total_connections >= 2); /* At least min connections */
    ASSERT(stats.idle_connections >= 2);
    ASSERT(stats.active_connections == 0);
    
    /* Acquire a connection */
    db_connection_t *conn = db_pool_acquire(pool);
    ASSERT(conn != NULL);
    
    result = db_pool_get_stats(pool, &stats);
    ASSERT(result == 0);
    ASSERT(stats.active_connections == 1);
    ASSERT(stats.total_acquired >= 1);
    
    db_pool_release(pool, conn);
    
    result = db_pool_get_stats(pool, &stats);
    ASSERT(result == 0);
    ASSERT(stats.active_connections == 0);
    ASSERT(stats.total_released >= 1);
    
    db_pool_destroy(pool);
    free(config.connection_string);
    
    PASS();
}

/* Test close idle connections */
void test_pool_close_idle(void) {
    TEST("db_pool_close_idle");
    
    db_pool_config_t config = db_pool_config_default(DB_TYPE_GENERIC, "test://localhost");
    config.min_connections = 2;
    config.max_connections = 10;
    
    db_pool_t *pool = db_pool_create(&config);
    ASSERT(pool != NULL);
    
    /* Acquire and release some connections to create more */
    for (int i = 0; i < 5; i++) {
        db_connection_t *conn = db_pool_acquire(pool);
        ASSERT(conn != NULL);
        db_pool_release(pool, conn);
    }
    
    db_pool_stats_t stats;
    db_pool_get_stats(pool, &stats);
    size_t before_close = stats.total_connections;
    
    /* Close idle connections (should keep min_connections) */
    int closed = db_pool_close_idle(pool);
    ASSERT(closed >= 0);
    
    db_pool_get_stats(pool, &stats);
    ASSERT(stats.total_connections >= config.min_connections);
    ASSERT(stats.total_connections <= before_close);
    
    db_pool_destroy(pool);
    free(config.connection_string);
    
    PASS();
}

/* Test connection handle retrieval */
void test_connection_get_handle(void) {
    TEST("db_connection_get_handle");
    
    db_pool_config_t config = db_pool_config_default(DB_TYPE_GENERIC, "test://localhost");
    db_pool_t *pool = db_pool_create(&config);
    ASSERT(pool != NULL);
    
    db_connection_t *conn = db_pool_acquire(pool);
    ASSERT(conn != NULL);
    
    void *handle = db_connection_get_handle(conn);
    ASSERT(handle != NULL); /* Generic type returns non-NULL */
    
    db_pool_release(pool, conn);
    db_pool_destroy(pool);
    free(config.connection_string);
    
    PASS();
}

/* Thread test data */
typedef struct {
    db_pool_t *pool;
    int iterations;
    int thread_id;
} thread_test_data_t;

/* Thread worker function */
void *thread_worker(void *arg) {
    thread_test_data_t *data = (thread_test_data_t *)arg;
    
    for (int i = 0; i < data->iterations; i++) {
        db_connection_t *conn = db_pool_acquire(data->pool);
        if (conn) {
            /* Simulate some work */
            usleep(100);
            db_pool_release(data->pool, conn);
        }
    }
    
    return NULL;
}

/* Test thread safety */
void test_pool_thread_safety(void) {
    TEST("db_pool_thread_safety");
    
    db_pool_config_t config = db_pool_config_default(DB_TYPE_GENERIC, "test://localhost");
    config.max_connections = 5;
    
    db_pool_t *pool = db_pool_create(&config);
    ASSERT(pool != NULL);
    
    const int num_threads = 10;
    const int iterations = 10;
    pthread_t threads[num_threads];
    thread_test_data_t thread_data[num_threads];
    
    /* Create threads */
    for (int i = 0; i < num_threads; i++) {
        thread_data[i].pool = pool;
        thread_data[i].iterations = iterations;
        thread_data[i].thread_id = i;
        
        int result = pthread_create(&threads[i], NULL, thread_worker, &thread_data[i]);
        ASSERT(result == 0);
    }
    
    /* Wait for all threads */
    for (int i = 0; i < num_threads; i++) {
        pthread_join(threads[i], NULL);
    }
    
    /* Verify stats */
    db_pool_stats_t stats;
    db_pool_get_stats(pool, &stats);
    ASSERT(stats.total_acquired == stats.total_released);
    ASSERT(stats.active_connections == 0);
    
    db_pool_destroy(pool);
    free(config.connection_string);
    
    PASS();
}

/* Main test runner */
int main(void) {
    printf("Running database connection pool tests...\n\n");
    
    test_pool_create();
    test_pool_acquire();
    test_pool_multiple_acquire();
    test_pool_stats();
    test_pool_close_idle();
    test_connection_get_handle();
    test_pool_thread_safety();
    
    printf("\n========================================\n");
    printf("Tests run: %d\n", tests_run);
    printf("Tests passed: %d\n", tests_passed);
    printf("Tests failed: %d\n", tests_run - tests_passed);
    printf("========================================\n");
    
    return (tests_run == tests_passed) ? 0 : 1;
}
