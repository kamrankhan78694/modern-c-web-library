#include "db_pool.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>

/* Example custom database callbacks */

/* Custom connect function (simulated) */
void *custom_db_connect(const char *connection_string) {
    printf("Connecting to database: %s\n", connection_string);
    /* In a real implementation, this would connect to an actual database */
    /* For demo purposes, we just return a dummy handle */
    return (void *)0x12345;
}

/* Custom disconnect function (simulated) */
int custom_db_disconnect(void *db_handle) {
    printf("Disconnecting database handle: %p\n", db_handle);
    return 0;
}

/* Custom ping function (simulated) */
int custom_db_ping(void *db_handle) {
    (void)db_handle;
    /* In a real implementation, this would check if connection is alive */
    return 0; /* Success */
}

/* Custom execute function (simulated) */
int custom_db_execute(void *db_handle, const char *query) {
    printf("Executing query on handle %p: %s\n", db_handle, query);
    /* In a real implementation, this would execute the query */
    return 0; /* Success */
}

/* Worker thread function */
void *worker_thread(void *arg) {
    db_pool_t *pool = (db_pool_t *)arg;
    int thread_id = (int)(long)pthread_self() % 1000;
    
    printf("Thread %d: Starting work\n", thread_id);
    
    for (int i = 0; i < 5; i++) {
        /* Acquire a connection from the pool */
        printf("Thread %d: Acquiring connection (iteration %d)\n", thread_id, i + 1);
        db_connection_t *conn = db_pool_acquire(pool);
        
        if (!conn) {
            printf("Thread %d: Failed to acquire connection!\n", thread_id);
            continue;
        }
        
        printf("Thread %d: Got connection %p\n", thread_id, (void *)conn);
        
        /* Get the underlying database handle */
        void *db_handle = db_connection_get_handle(conn);
        printf("Thread %d: Using database handle %p\n", thread_id, db_handle);
        
        /* Simulate some database work */
        printf("Thread %d: Performing database operations...\n", thread_id);
        usleep(100000); /* Sleep for 100ms */
        
        /* Release the connection back to the pool */
        printf("Thread %d: Releasing connection\n", thread_id);
        db_pool_release(pool, conn);
        
        /* Small delay between iterations */
        usleep(50000);
    }
    
    printf("Thread %d: Work completed\n", thread_id);
    return NULL;
}

int main(void) {
    printf("===========================================\n");
    printf("Database Connection Pool Example\n");
    printf("===========================================\n\n");
    
    /* Example 1: Using default generic database type */
    printf("Example 1: Generic Database Pool\n");
    printf("-----------------------------------------\n");
    
    db_pool_config_t config = db_pool_config_default(DB_TYPE_GENERIC, "generic://localhost:5432/mydb");
    config.min_connections = 2;
    config.max_connections = 5;
    config.connection_timeout = 10;
    
    db_pool_t *pool = db_pool_create(&config);
    if (!pool) {
        fprintf(stderr, "Failed to create connection pool!\n");
        free(config.connection_string);
        return 1;
    }
    
    printf("Pool created successfully\n");
    
    /* Get initial stats */
    db_pool_stats_t stats;
    db_pool_get_stats(pool, &stats);
    printf("Initial pool stats:\n");
    printf("  Total connections: %zu\n", stats.total_connections);
    printf("  Idle connections: %zu\n", stats.idle_connections);
    printf("  Active connections: %zu\n", stats.active_connections);
    printf("\n");
    
    /* Acquire and release a single connection */
    printf("Acquiring a single connection...\n");
    db_connection_t *conn = db_pool_acquire(pool);
    if (conn) {
        printf("Connection acquired: %p\n", conn);
        printf("Connection is valid: %s\n", db_connection_is_valid(conn) ? "yes" : "no");
        
        /* Get stats while connection is in use */
        db_pool_get_stats(pool, &stats);
        printf("Stats with active connection:\n");
        printf("  Active connections: %zu\n", stats.active_connections);
        printf("  Idle connections: %zu\n", stats.idle_connections);
        
        db_pool_release(pool, conn);
        printf("Connection released\n");
    }
    printf("\n");
    
    /* Example 2: Multi-threaded usage */
    printf("Example 2: Multi-threaded Connection Pool Usage\n");
    printf("-----------------------------------------\n");
    
    const int num_threads = 3;
    pthread_t threads[num_threads];
    
    printf("Starting %d worker threads...\n", num_threads);
    for (int i = 0; i < num_threads; i++) {
        if (pthread_create(&threads[i], NULL, worker_thread, pool) != 0) {
            fprintf(stderr, "Failed to create thread %d\n", i);
        }
    }
    
    /* Wait for all threads to complete */
    for (int i = 0; i < num_threads; i++) {
        pthread_join(threads[i], NULL);
    }
    
    printf("\nAll threads completed\n\n");
    
    /* Get final stats */
    db_pool_get_stats(pool, &stats);
    printf("Final pool statistics:\n");
    printf("  Total connections: %zu\n", stats.total_connections);
    printf("  Total acquired: %zu\n", stats.total_acquired);
    printf("  Total released: %zu\n", stats.total_released);
    printf("  Total created: %zu\n", stats.total_created);
    printf("  Total closed: %zu\n", stats.total_closed);
    printf("  Wait count: %zu\n", stats.wait_count);
    printf("\n");
    
    /* Close idle connections */
    printf("Closing idle connections...\n");
    int closed = db_pool_close_idle(pool);
    printf("Closed %d idle connections\n", closed);
    
    db_pool_get_stats(pool, &stats);
    printf("After closing idle:\n");
    printf("  Total connections: %zu\n", stats.total_connections);
    printf("\n");
    
    /* Cleanup */
    db_pool_destroy(pool);
    free(config.connection_string);
    printf("Pool destroyed\n\n");
    
    /* Example 3: Custom database callbacks */
    printf("Example 3: Custom Database Callbacks\n");
    printf("-----------------------------------------\n");
    
    db_pool_config_t custom_config = db_pool_config_default(DB_TYPE_CUSTOM, "custom://localhost/myapp");
    custom_config.min_connections = 1;
    custom_config.max_connections = 3;
    custom_config.connect_fn = custom_db_connect;
    custom_config.disconnect_fn = custom_db_disconnect;
    custom_config.ping_fn = custom_db_ping;
    custom_config.execute_fn = custom_db_execute;
    
    db_pool_t *custom_pool = db_pool_create(&custom_config);
    if (custom_pool) {
        printf("\nCustom pool created\n");
        
        db_connection_t *custom_conn = db_pool_acquire(custom_pool);
        if (custom_conn) {
            printf("Custom connection acquired\n");
            
            /* Use the connection */
            void *handle = db_connection_get_handle(custom_conn);
            custom_db_execute(handle, "SELECT * FROM users");
            
            db_pool_release(custom_pool, custom_conn);
            printf("Custom connection released\n");
        }
        
        db_pool_destroy(custom_pool);
        printf("Custom pool destroyed\n");
    }
    free(custom_config.connection_string);
    
    printf("\n===========================================\n");
    printf("Examples completed successfully!\n");
    printf("===========================================\n");
    
    return 0;
}
