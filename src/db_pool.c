#include "db_pool.h"
#include <stdlib.h>
#include <string.h>
#ifdef __EMSCRIPTEN__
#include "wasm_compat.h"
#else
#include <pthread.h>
#include <unistd.h>
#endif

/* Internal pool structure */
struct db_pool {
    db_pool_config_t config;
    char *owned_connection_string;  /* Pool-owned copy of config.connection_string */
    db_connection_t **connections;
    size_t capacity;
    size_t size;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    bool shutdown;
    size_t active_acquirers;        /* Threads currently executing inside db_pool_acquire */
    db_pool_stats_t stats;
};

/* Default generic connection functions (no-op implementations) */
static char dummy_handle = 1;

static void *generic_connect(const char *connection_string) {
    (void)connection_string;
    return (void *)&dummy_handle;
}

static int generic_disconnect(void *db_handle) {
    (void)db_handle;
    return 0;
}

static int generic_ping(void *db_handle) {
    (void)db_handle;
    return 0;
}

/* Create a new connection */
static db_connection_t *create_connection(db_pool_t *pool) {
    db_connection_t *conn = (db_connection_t *)calloc(1, sizeof(db_connection_t));
    if (!conn) {
        return NULL;
    }
    
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
    
    if (pool->config.max_lifetime > 0) {
        time_t now = time(NULL);
        if (now - conn->created_at > (time_t)pool->config.max_lifetime) {
            return false;
        }
    }
    
    if (pool->config.max_idle_time > 0) {
        time_t now = time(NULL);
        if (now - conn->last_used > (time_t)pool->config.max_idle_time) {
            return false;
        }
    }
    
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

/* Create default configuration.
 * Note: connection_string is stored directly (not duplicated). The caller
 * must ensure the string remains valid for the lifetime of this config. */
db_pool_config_t db_pool_config_default(db_type_t db_type, const char *connection_string) {
    db_pool_config_t config;
    memset(&config, 0, sizeof(db_pool_config_t));
    
    config.db_type = db_type;
    config.connection_string = connection_string;
    config.min_connections = 2;
    config.max_connections = 10;
    config.max_idle_time = 300;
    config.connection_timeout = 30;
    config.max_lifetime = 3600;
    config.validate_on_acquire = true;
    
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
    
    pool->config = *config;
    /* The pool owns its own copy so the caller's buffer need not outlive the pool. */
    pool->owned_connection_string = strdup(config->connection_string);
    if (!pool->owned_connection_string) {
        free(pool);
        return NULL;
    }
    pool->config.connection_string = pool->owned_connection_string;

    pool->capacity = config->max_connections;
    pool->connections = (db_connection_t **)calloc(pool->capacity, sizeof(db_connection_t *));
    if (!pool->connections) {
        free(pool->owned_connection_string);
        free(pool);
        return NULL;
    }

    if (pthread_mutex_init(&pool->mutex, NULL) != 0) {
        free(pool->connections);
        free(pool->owned_connection_string);
        free(pool);
        return NULL;
    }

    if (pthread_cond_init(&pool->cond, NULL) != 0) {
        pthread_mutex_destroy(&pool->mutex);
        free(pool->connections);
        free(pool->owned_connection_string);
        free(pool);
        return NULL;
    }
    
    pool->size = 0;
    pool->shutdown = false;
    memset(&pool->stats, 0, sizeof(db_pool_stats_t));
    
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
    /* Register as an in-flight acquirer so db_pool_destroy waits for us to leave
     * before it destroys the mutex/cond and frees the pool. */
    pool->active_acquirers++;

    if (pool->shutdown) {
        pool->active_acquirers--;
        pthread_cond_broadcast(&pool->cond);
        pthread_mutex_unlock(&pool->mutex);
        return NULL;
    }

    time_t start_time = time(NULL);
    db_connection_t *conn = NULL;
    
    while (!conn && !pool->shutdown) {
        for (size_t i = 0; i < pool->size; i++) {
            if (pool->connections[i]->state == DB_CONN_IDLE) {
                conn = pool->connections[i];
                
                if (validate_connection(pool, conn)) {
                    conn->state = DB_CONN_IN_USE;
                    conn->last_used = time(NULL);
                    pool->stats.total_acquired++;
                    break;
                } else {
                    close_connection(pool, conn);
                    pool->connections[i] = pool->connections[--pool->size];
                    i--;  /* Re-check this index after compaction */
                    conn = NULL;
                }
            }
        }
        
        if (!conn && pool->size < pool->capacity) {
            conn = create_connection(pool);
            if (conn) {
                pool->connections[pool->size++] = conn;
                conn->state = DB_CONN_IN_USE;
                conn->last_used = time(NULL);
                pool->stats.total_acquired++;
            }
        }
        
        if (!conn) {
            time_t now = time(NULL);
            if (pool->config.connection_timeout > 0 && 
                (now - start_time) >= (time_t)pool->config.connection_timeout) {
                break;
            }
            
            pool->stats.wait_count++;
            
            struct timespec ts;
            ts.tv_sec = now + 1;
            ts.tv_nsec = 0;
            pthread_cond_timedwait(&pool->cond, &pool->mutex, &ts);
        }
    }
    
    pool->active_acquirers--;
    if (pool->shutdown) {
        pthread_cond_broadcast(&pool->cond);
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
    
    conn->state = DB_CONN_IDLE;
    conn->last_used = time(NULL);
    pool->stats.total_released++;
    
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
            if (pool->size <= pool->config.min_connections) {
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
    pthread_cond_broadcast(&pool->cond);

    /* Drain in-flight acquirers before teardown.  A thread blocked in
     * pthread_cond_timedwait() inside db_pool_acquire() must re-acquire the
     * mutex (which we hold) to return; once it observes shutdown it decrements
     * active_acquirers and broadcasts.  Destroying the mutex/cond or freeing the
     * pool while such a waiter is still parked would be a use-after-free.
     * Contract: callers must not begin NEW db_pool_acquire() calls after
     * db_pool_destroy() has started; this only drains already-in-flight ones. */
    while (pool->active_acquirers > 0) {
        pthread_cond_wait(&pool->cond, &pool->mutex);
    }

    /* Wait for in-use connections to be released before destroying */
    {
        bool has_in_use = true;
        int wait_rounds = 0;
        while (has_in_use && wait_rounds < 50) {
            has_in_use = false;
            for (size_t i = 0; i < pool->size; i++) {
                if (pool->connections[i]->state == DB_CONN_IN_USE) {
                    has_in_use = true;
                    break;
                }
            }
            if (has_in_use) {
                struct timespec ts;
                clock_gettime(CLOCK_REALTIME, &ts);
                ts.tv_nsec += 100000000; /* 100ms */
                if (ts.tv_nsec >= 1000000000) {
                    ts.tv_sec++;
                    ts.tv_nsec -= 1000000000;
                }
                pthread_cond_timedwait(&pool->cond, &pool->mutex, &ts);
                wait_rounds++;
            }
        }
    }
    
    for (size_t i = 0; i < pool->size; i++) {
        close_connection(pool, pool->connections[i]);
    }
    
    pool->size = 0;
    pthread_mutex_unlock(&pool->mutex);
    
    pthread_cond_destroy(&pool->cond);
    pthread_mutex_destroy(&pool->mutex);
    free(pool->connections);
    free(pool->owned_connection_string);
    free(pool);
}

/* Execute query on a connection */
int db_connection_execute(db_connection_t *conn, const char *query) {
    if (!conn || !conn->db_handle || !query) {
        return -1;
    }
    
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
