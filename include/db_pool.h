#ifndef DB_POOL_H
#define DB_POOL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdbool.h>
#include <time.h>

/**
 * Database Connection Pool
 * 
 * A thread-safe connection pool for managing database connections.
 * Supports multiple database backends through a generic interface.
 */

/* Forward declarations */
typedef struct db_pool db_pool_t;
typedef struct db_connection db_connection_t;

/* Connection state */
typedef enum {
    DB_CONN_IDLE,
    DB_CONN_IN_USE,
    DB_CONN_CLOSED,
    DB_CONN_ERROR
} db_connection_state_t;

/* Database types */
typedef enum {
    DB_TYPE_GENERIC,
    DB_TYPE_SQLITE,
    DB_TYPE_POSTGRESQL,
    DB_TYPE_MYSQL,
    DB_TYPE_CUSTOM
} db_type_t;

/* Connection callbacks - for custom database implementations */
typedef void* (*db_connect_fn_t)(const char *connection_string);
typedef int (*db_disconnect_fn_t)(void *db_handle);
typedef int (*db_ping_fn_t)(void *db_handle);
typedef int (*db_execute_fn_t)(void *db_handle, const char *query);

/* Database connection structure */
struct db_connection {
    void *db_handle;                /* Database-specific connection handle */
    db_connection_state_t state;    /* Current state of the connection */
    time_t last_used;               /* Last time connection was used */
    time_t created_at;              /* When connection was created */
    int error_count;                /* Number of consecutive errors */
};

/* Connection pool configuration */
typedef struct {
    db_type_t db_type;              /* Type of database */
    char *connection_string;        /* Database connection string */
    size_t min_connections;         /* Minimum number of connections to maintain */
    size_t max_connections;         /* Maximum number of connections allowed */
    size_t max_idle_time;           /* Max idle time in seconds before closing */
    size_t connection_timeout;      /* Timeout for acquiring a connection (seconds) */
    size_t max_lifetime;            /* Max lifetime of a connection in seconds */
    bool validate_on_acquire;       /* Validate connection before giving to client */
    
    /* Custom connection callbacks (optional, for custom db types) */
    db_connect_fn_t connect_fn;
    db_disconnect_fn_t disconnect_fn;
    db_ping_fn_t ping_fn;
    db_execute_fn_t execute_fn;
} db_pool_config_t;

/* Pool statistics */
typedef struct {
    size_t total_connections;       /* Total connections in pool */
    size_t active_connections;      /* Connections currently in use */
    size_t idle_connections;        /* Connections available for use */
    size_t total_acquired;          /* Total acquisitions since pool creation */
    size_t total_released;          /* Total releases since pool creation */
    size_t total_created;           /* Total connections created */
    size_t total_closed;            /* Total connections closed */
    size_t total_errors;            /* Total connection errors */
    size_t wait_count;              /* Number of times threads waited for connection */
} db_pool_stats_t;

/* ===== Database Connection Pool API ===== */

/**
 * Create a new database connection pool
 * @param config Pool configuration
 * @return Pointer to pool instance or NULL on failure
 */
db_pool_t *db_pool_create(const db_pool_config_t *config);

/**
 * Acquire a connection from the pool
 * Blocks if no connections available until timeout
 * @param pool Pool instance
 * @return Connection instance or NULL on timeout/error
 */
db_connection_t *db_pool_acquire(db_pool_t *pool);

/**
 * Release a connection back to the pool
 * @param pool Pool instance
 * @param conn Connection to release
 * @return 0 on success, -1 on failure
 */
int db_pool_release(db_pool_t *pool, db_connection_t *conn);

/**
 * Get pool statistics
 * @param pool Pool instance
 * @param stats Pointer to stats structure to fill
 * @return 0 on success, -1 on failure
 */
int db_pool_get_stats(db_pool_t *pool, db_pool_stats_t *stats);

/**
 * Close all idle connections
 * @param pool Pool instance
 * @return Number of connections closed
 */
int db_pool_close_idle(db_pool_t *pool);

/**
 * Destroy the connection pool and close all connections
 * @param pool Pool instance
 */
void db_pool_destroy(db_pool_t *pool);

/**
 * Execute a query on a connection
 * Convenience wrapper around the execute callback
 * @param conn Connection instance
 * @param query SQL query to execute
 * @return 0 on success, -1 on failure
 */
int db_connection_execute(db_connection_t *conn, const char *query);

/**
 * Get the underlying database handle from a connection
 * @param conn Connection instance
 * @return Database handle or NULL
 */
void *db_connection_get_handle(db_connection_t *conn);

/**
 * Check if connection is valid
 * @param conn Connection instance
 * @return true if valid, false otherwise
 */
bool db_connection_is_valid(db_connection_t *conn);

/**
 * Create default pool configuration
 * @param db_type Database type
 * @param connection_string Connection string for the database
 * @return Default configuration
 */
db_pool_config_t db_pool_config_default(db_type_t db_type, const char *connection_string);

#ifdef __cplusplus
}
#endif

#endif /* DB_POOL_H */
