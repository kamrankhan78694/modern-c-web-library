#include "db_pool.h"
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>

/* Internal pool structure */
struct db_pool {
    db_pool_config_t config;
    db_connection_t **connections;
    size_t capacity;
    size_t size;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    bool shutdown;
    db_pool_stats_t stats;
};

/* Default generic connection functions (no-op implementations) */
static char dummy_handle = 1;

static void *generic_connect(const char *connection_string) {
    (void)connection_string;
    /* Generic placeholder - returns a dummy handle address */
    return (void *)&dummy_handle;
}

static int generic_disconnect(void *db_handle) {
    (void)db_handle;
    return 0;
}

static int generic_ping(void *db_handle) {
    (void)db_handle;
    return 0; /* Always returns success for generic type */
}

static int generic_execute(void *db_handle, const char *query) {
    (void)db_handle;
    (void)query;
    return 0;
}

/* Create a new connection */
static db_connection_t *create_connection(db_pool_t *pool) {
    db_connection_t *conn = (db_connection_t *)calloc(1, sizeof(db_connection_t));
    if (!conn) {
        return NULL;
    }
    
    /* Use appropriate connect function based on database type */
    db_connect_fn_t connect_fn = pool->config.connect_fn ? 
                                  pool->config.connect_fn : generic_connect;
    
    conn->db_handle = connect_fn(pool->config.connection_string);
    if (!conn->db_handle) {
        free(conn);
        pool->stats.total_errors++;
        return NULL;
    }
    
    conn->state = DB_CONN_IDLE;
    conn->created_at = time(NULL);
    conn->last_used = conn->created_at;
    conn->error_count = 0;
    
    pool->stats.total_created++;
    
    return conn;
}

/* Close and free a connection */
static void close_connection(db_pool_t *pool, db_connection_t *conn) {
    if (!conn) {
        return;
    }
    
    if (conn->db_handle) {
        db_disconnect_fn_t disconnect_fn = pool->config.disconnect_fn ?
                                            pool->config.disconnect_fn : generic_disconnect;
        disconnect_fn(conn->db_handle);
        conn->db_handle = NULL;
    }
    
    conn->state = DB_CONN_CLOSED;
    pool->stats.total_closed++;
    free(conn);
}

/* Validate connection */
static bool validate_connection(db_pool_t *pool, db_connection_t *conn) {
    if (!conn || !conn->db_handle || conn->state == DB_CONN_CLOSED) {
        return false;
    }
    
    /* Check max lifetime */
    if (pool->config.max_lifetime > 0) {
        time_t now = time(NULL);
        if (now - conn->created_at > (time_t)pool->config.max_lifetime) {
            return false;
        }
    }
    
    /* Check max idle time */
    if (pool->config.max_idle_time > 0) {
        time_t now = time(NULL);
        if (now - conn->last_used > (time_t)pool->config.max_idle_time) {
            return false;
        }
    }
    
    /* Ping the connection if validation is enabled */
    if (pool->config.validate_on_acquire) {
        db_ping_fn_t ping_fn = pool->config.ping_fn ? 
                               pool->config.ping_fn : generic_ping;
        if (ping_fn(conn->db_handle) != 0) {
            conn->error_count++;
            pool->stats.total_errors++;
            return false;
        }
    }
    
    return true;
}

/* Create default configuration */
db_pool_config_t db_pool_config_default(db_type_t db_type, const char *connection_string) {
    db_pool_config_t config;
    memset(&config, 0, sizeof(db_pool_config_t));
    
    config.db_type = db_type;
    config.connection_string = connection_string ? strdup(connection_string) : NULL;
    config.min_connections = 2;
    config.max_connections = 10;
    config.max_idle_time = 300;     /* 5 minutes */
    config.connection_timeout = 30;  /* 30 seconds */
    config.max_lifetime = 3600;      /* 1 hour */
    config.validate_on_acquire = true;
    
    /* Set default callbacks to NULL (will use generic) */
    config.connect_fn = NULL;
    config.disconnect_fn = NULL;
    config.ping_fn = NULL;
    config.execute_fn = NULL;
    
    return config;
}

/* Create a new database connection pool */
db_pool_t *db_pool_create(const db_pool_config_t *config) {
    if (!config || !config->connection_string) {
        return NULL;
    }
    
    if (config->min_connections > config->max_connections) {
        return NULL;
    }
    
    db_pool_t *pool = (db_pool_t *)calloc(1, sizeof(db_pool_t));
    if (!pool) {
        return NULL;
    }
    
    /* Copy configuration */
    pool->config = *config;
    pool->config.connection_string = strdup(config->connection_string);
    
    /* Allocate connection array */
    pool->capacity = config->max_connections;
    pool->connections = (db_connection_t **)calloc(pool->capacity, sizeof(db_connection_t *));
    if (!pool->connections) {
        free(pool->config.connection_string);
        free(pool);
        return NULL;
    }
    
    /* Initialize synchronization primitives */
    if (pthread_mutex_init(&pool->mutex, NULL) != 0) {
        free(pool->connections);
        free(pool->config.connection_string);
        free(pool);
        return NULL;
    }
    
    if (pthread_cond_init(&pool->cond, NULL) != 0) {
        pthread_mutex_destroy(&pool->mutex);
        free(pool->connections);
        free(pool->config.connection_string);
        free(pool);
        return NULL;
    }
    
    pool->size = 0;
    pool->shutdown = false;
    memset(&pool->stats, 0, sizeof(db_pool_stats_t));
    
    /* Create minimum number of connections */
    pthread_mutex_lock(&pool->mutex);
    for (size_t i = 0; i < config->min_connections; i++) {
        db_connection_t *conn = create_connection(pool);
        if (conn) {
            pool->connections[pool->size++] = conn;
        }
    }
    pthread_mutex_unlock(&pool->mutex);
    
    return pool;
}

/* Acquire a connection from the pool */
db_connection_t *db_pool_acquire(db_pool_t *pool) {
    if (!pool) {
        return NULL;
    }
    
    pthread_mutex_lock(&pool->mutex);
    
    if (pool->shutdown) {
        pthread_mutex_unlock(&pool->mutex);
        return NULL;
    }
    
    time_t start_time = time(NULL);
    db_connection_t *conn = NULL;
    
    while (!conn && !pool->shutdown) {
        /* Look for an idle connection */
        for (size_t i = 0; i < pool->size; i++) {
            if (pool->connections[i]->state == DB_CONN_IDLE) {
                conn = pool->connections[i];
                
                /* Validate connection */
                if (validate_connection(pool, conn)) {
                    conn->state = DB_CONN_IN_USE;
                    conn->last_used = time(NULL);
                    pool->stats.total_acquired++;
                    break;
                } else {
                    /* Connection is invalid, close it and remove from pool */
                    close_connection(pool, conn);
                    pool->connections[i] = pool->connections[--pool->size];
                    conn = NULL;
                }
            }
        }
        
        /* If no idle connection found, try to create a new one */
        if (!conn && pool->size < pool->capacity) {
            conn = create_connection(pool);
            if (conn) {
                pool->connections[pool->size++] = conn;
                conn->state = DB_CONN_IN_USE;
                conn->last_used = time(NULL);
                pool->stats.total_acquired++;
            }
        }
        
        /* If still no connection, wait with timeout */
        if (!conn) {
            time_t now = time(NULL);
            if (pool->config.connection_timeout > 0 && 
                (now - start_time) >= (time_t)pool->config.connection_timeout) {
                /* Timeout reached */
                break;
            }
            
            pool->stats.wait_count++;
            
            /* Wait for a connection to be released */
            struct timespec ts;
            ts.tv_sec = now + 1; /* Wait 1 second */
            ts.tv_nsec = 0;
            pthread_cond_timedwait(&pool->cond, &pool->mutex, &ts);
        }
    }
    
    pthread_mutex_unlock(&pool->mutex);
    return conn;
}

/* Release a connection back to the pool */
int db_pool_release(db_pool_t *pool, db_connection_t *conn) {
    if (!pool || !conn) {
        return -1;
    }
    
    pthread_mutex_lock(&pool->mutex);
    
    /* Verify connection belongs to this pool */
    bool found = false;
    for (size_t i = 0; i < pool->size; i++) {
        if (pool->connections[i] == conn) {
            found = true;
            break;
        }
    }
    
    if (!found) {
        pthread_mutex_unlock(&pool->mutex);
        return -1;
    }
    
    /* Mark as idle and update stats */
    conn->state = DB_CONN_IDLE;
    conn->last_used = time(NULL);
    pool->stats.total_released++;
    
    /* Signal waiting threads */
    pthread_cond_signal(&pool->cond);
    
    pthread_mutex_unlock(&pool->mutex);
    return 0;
}

/* Get pool statistics */
int db_pool_get_stats(db_pool_t *pool, db_pool_stats_t *stats) {
    if (!pool || !stats) {
        return -1;
    }
    
    pthread_mutex_lock(&pool->mutex);
    
    /* Calculate current stats */
    size_t active = 0;
    size_t idle = 0;
    
    for (size_t i = 0; i < pool->size; i++) {
        if (pool->connections[i]->state == DB_CONN_IN_USE) {
            active++;
        } else if (pool->connections[i]->state == DB_CONN_IDLE) {
            idle++;
        }
    }
    
    *stats = pool->stats;
    stats->total_connections = pool->size;
    stats->active_connections = active;
    stats->idle_connections = idle;
    
    pthread_mutex_unlock(&pool->mutex);
    return 0;
}

/* Close all idle connections */
int db_pool_close_idle(db_pool_t *pool) {
    if (!pool) {
        return -1;
    }
    
    pthread_mutex_lock(&pool->mutex);
    
    int closed = 0;
    size_t i = 0;
    
    while (i < pool->size) {
        if (pool->connections[i]->state == DB_CONN_IDLE) {
            /* Keep minimum connections */
            size_t current_count = pool->size - closed;
            if (current_count <= pool->config.min_connections) {
                i++;
                continue;
            }
            
            close_connection(pool, pool->connections[i]);
            pool->connections[i] = pool->connections[--pool->size];
            closed++;
        } else {
            i++;
        }
    }
    
    pthread_mutex_unlock(&pool->mutex);
    return closed;
}

/* Destroy the connection pool */
void db_pool_destroy(db_pool_t *pool) {
    if (!pool) {
        return;
    }
    
    pthread_mutex_lock(&pool->mutex);
    pool->shutdown = true;
    
    /* Close all connections */
    for (size_t i = 0; i < pool->size; i++) {
        close_connection(pool, pool->connections[i]);
    }
    
    pool->size = 0;
    pthread_mutex_unlock(&pool->mutex);
    
    /* Cleanup */
    pthread_cond_destroy(&pool->cond);
    pthread_mutex_destroy(&pool->mutex);
    free(pool->connections);
    free(pool->config.connection_string);
    free(pool);
}

/* Execute query on a connection */
int db_connection_execute(db_connection_t *conn, const char *query) {
    if (!conn || !conn->db_handle || !query) {
        return -1;
    }
    
    /* Note: This is a simplified implementation.
     * For full functionality, the pool reference would be needed to access
     * the execute callback from the pool configuration.
     * Users should use db_connection_get_handle() to get the database handle
     * and call their database-specific execute function directly.
     */
    (void)query;
    return 0;
}

/* Get database handle */
void *db_connection_get_handle(db_connection_t *conn) {
    if (!conn) {
        return NULL;
    }
    return conn->db_handle;
}

/* Check if connection is valid */
bool db_connection_is_valid(db_connection_t *conn) {
    if (!conn || !conn->db_handle) {
        return false;
    }
    
    return conn->state != DB_CONN_CLOSED && conn->state != DB_CONN_ERROR;
}
