#include "weblib.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#endif

#define MAX_CLIENTS 1024
#define CLEANUP_THRESHOLD 100  /* Cleanup after this many checks */

/* Client entry for rate limiting */
typedef struct {
    char ip[46];  /* IPv6 max length */
    time_t *timestamps;
    size_t timestamp_count;
    size_t timestamp_capacity;
    bool in_use;
} client_entry_t;

/* Rate limiter structure */
struct rate_limiter {
    size_t max_requests;
    size_t window_seconds;
    client_entry_t clients[MAX_CLIENTS];
    size_t check_counter;
#ifdef _WIN32
    CRITICAL_SECTION mutex;
#else
    pthread_mutex_t mutex;
#endif
};

/* Internal helper functions */
static size_t hash_ip(const char *ip);
static client_entry_t *find_or_create_client(rate_limiter_t *limiter, const char *ip);
static void cleanup_old_timestamps(client_entry_t *client, time_t current_time, size_t window_seconds);
static void free_client_entry(client_entry_t *client);
static void cleanup_unlocked(rate_limiter_t *limiter);

/* Create rate limiter */
rate_limiter_t *rate_limiter_create(size_t max_requests, size_t window_seconds) {
    if (max_requests == 0 || window_seconds == 0) {
        return NULL;
    }

    rate_limiter_t *limiter = (rate_limiter_t *)calloc(1, sizeof(rate_limiter_t));
    if (!limiter) {
        return NULL;
    }

    limiter->max_requests = max_requests;
    limiter->window_seconds = window_seconds;
    limiter->check_counter = 0;

    /* Initialize all client entries as unused */
    for (size_t i = 0; i < MAX_CLIENTS; i++) {
        limiter->clients[i].in_use = false;
        limiter->clients[i].timestamps = NULL;
        limiter->clients[i].timestamp_count = 0;
        limiter->clients[i].timestamp_capacity = 0;
    }

#ifdef _WIN32
    InitializeCriticalSection(&limiter->mutex);
#else
    pthread_mutex_init(&limiter->mutex, NULL);
#endif

    return limiter;
}

/* Check if request is allowed */
bool rate_limiter_check(rate_limiter_t *limiter, const char *client_ip) {
    if (!limiter || !client_ip) {
        return true;  /* Allow by default if invalid params */
    }

#ifdef _WIN32
    EnterCriticalSection(&limiter->mutex);
#else
    pthread_mutex_lock(&limiter->mutex);
#endif

    /* Periodic cleanup */
    limiter->check_counter++;
    if (limiter->check_counter >= CLEANUP_THRESHOLD) {
        limiter->check_counter = 0;
        cleanup_unlocked(limiter);
    }

    time_t current_time = time(NULL);
    client_entry_t *client = find_or_create_client(limiter, client_ip);
    
    if (!client) {
#ifdef _WIN32
        LeaveCriticalSection(&limiter->mutex);
#else
        pthread_mutex_unlock(&limiter->mutex);
#endif
        return true;  /* Allow if we can't track */
    }

    /* Clean up old timestamps */
    cleanup_old_timestamps(client, current_time, limiter->window_seconds);

    /* Check if limit exceeded */
    bool allowed = (client->timestamp_count < limiter->max_requests);

    if (allowed) {
        /* Add current timestamp */
        if (client->timestamp_count >= client->timestamp_capacity) {
            size_t new_capacity = client->timestamp_capacity == 0 ? 8 : client->timestamp_capacity * 2;
            time_t *new_timestamps = (time_t *)realloc(client->timestamps, new_capacity * sizeof(time_t));
            if (new_timestamps) {
                client->timestamps = new_timestamps;
                client->timestamp_capacity = new_capacity;
            } else {
                /* Allocation failed, but allow the request */
#ifdef _WIN32
                LeaveCriticalSection(&limiter->mutex);
#else
                pthread_mutex_unlock(&limiter->mutex);
#endif
                return true;
            }
        }
        client->timestamps[client->timestamp_count++] = current_time;
    }

#ifdef _WIN32
    LeaveCriticalSection(&limiter->mutex);
#else
    pthread_mutex_unlock(&limiter->mutex);
#endif

    return allowed;
}

/* Get remaining requests */
size_t rate_limiter_get_remaining(rate_limiter_t *limiter, const char *client_ip) {
    if (!limiter || !client_ip) {
        return 0;
    }

#ifdef _WIN32
    EnterCriticalSection(&limiter->mutex);
#else
    pthread_mutex_lock(&limiter->mutex);
#endif

    time_t current_time = time(NULL);
    client_entry_t *client = find_or_create_client(limiter, client_ip);
    
    if (!client) {
#ifdef _WIN32
        LeaveCriticalSection(&limiter->mutex);
#else
        pthread_mutex_unlock(&limiter->mutex);
#endif
        return limiter->max_requests;
    }

    /* Clean up old timestamps */
    cleanup_old_timestamps(client, current_time, limiter->window_seconds);

    size_t remaining = 0;
    if (client->timestamp_count < limiter->max_requests) {
        remaining = limiter->max_requests - client->timestamp_count;
    }

#ifdef _WIN32
    LeaveCriticalSection(&limiter->mutex);
#else
    pthread_mutex_unlock(&limiter->mutex);
#endif

    return remaining;
}

/* Reset client */
void rate_limiter_reset_client(rate_limiter_t *limiter, const char *client_ip) {
    if (!limiter || !client_ip) {
        return;
    }

#ifdef _WIN32
    EnterCriticalSection(&limiter->mutex);
#else
    pthread_mutex_lock(&limiter->mutex);
#endif

    size_t index = hash_ip(client_ip);
    for (size_t i = 0; i < MAX_CLIENTS; i++) {
        size_t pos = (index + i) % MAX_CLIENTS;
        client_entry_t *client = &limiter->clients[pos];
        
        if (client->in_use && strcmp(client->ip, client_ip) == 0) {
            client->timestamp_count = 0;
            break;
        }
    }

#ifdef _WIN32
    LeaveCriticalSection(&limiter->mutex);
#else
    pthread_mutex_unlock(&limiter->mutex);
#endif
}

/* Cleanup expired entries (internal, assumes lock is held) */
static void cleanup_unlocked(rate_limiter_t *limiter) {
    if (!limiter) {
        return;
    }

    time_t current_time = time(NULL);

    for (size_t i = 0; i < MAX_CLIENTS; i++) {
        client_entry_t *client = &limiter->clients[i];
        
        if (!client->in_use) {
            continue;
        }

        /* Clean up old timestamps */
        cleanup_old_timestamps(client, current_time, limiter->window_seconds);

        /* If no active timestamps, free the entry */
        if (client->timestamp_count == 0) {
            free_client_entry(client);
            client->in_use = false;
        }
    }
}

/* Cleanup expired entries (public API) */
void rate_limiter_cleanup(rate_limiter_t *limiter) {
    if (!limiter) {
        return;
    }

#ifdef _WIN32
    EnterCriticalSection(&limiter->mutex);
#else
    pthread_mutex_lock(&limiter->mutex);
#endif

    cleanup_unlocked(limiter);

#ifdef _WIN32
    LeaveCriticalSection(&limiter->mutex);
#else
    pthread_mutex_unlock(&limiter->mutex);
#endif
}

/* Destroy rate limiter */
void rate_limiter_destroy(rate_limiter_t *limiter) {
    if (!limiter) {
        return;
    }

    /* Free all client entries */
    for (size_t i = 0; i < MAX_CLIENTS; i++) {
        if (limiter->clients[i].in_use) {
            free_client_entry(&limiter->clients[i]);
        }
    }

#ifdef _WIN32
    DeleteCriticalSection(&limiter->mutex);
#else
    pthread_mutex_destroy(&limiter->mutex);
#endif

    free(limiter);
}

/* Internal: Hash IP address */
static size_t hash_ip(const char *ip) {
    size_t hash = 5381;
    int c;

    while ((c = *ip++)) {
        hash = ((hash << 5) + hash) + c; /* hash * 33 + c */
    }

    return hash % MAX_CLIENTS;
}

/* Internal: Find or create client entry */
static client_entry_t *find_or_create_client(rate_limiter_t *limiter, const char *ip) {
    if (!limiter || !ip) {
        return NULL;
    }

    size_t index = hash_ip(ip);

    /* Linear probing to find existing or free slot */
    for (size_t i = 0; i < MAX_CLIENTS; i++) {
        size_t pos = (index + i) % MAX_CLIENTS;
        client_entry_t *client = &limiter->clients[pos];

        /* Found existing entry */
        if (client->in_use && strcmp(client->ip, ip) == 0) {
            return client;
        }

        /* Found free slot */
        if (!client->in_use) {
            client->in_use = true;
            strncpy(client->ip, ip, sizeof(client->ip) - 1);
            client->ip[sizeof(client->ip) - 1] = '\0';
            client->timestamp_count = 0;
            client->timestamp_capacity = 0;
            client->timestamps = NULL;
            return client;
        }
    }

    /* Table is full */
    return NULL;
}

/* Internal: Cleanup old timestamps */
static void cleanup_old_timestamps(client_entry_t *client, time_t current_time, size_t window_seconds) {
    if (!client || !client->timestamps) {
        return;
    }

    time_t cutoff_time = current_time - (time_t)window_seconds;
    size_t valid_count = 0;

    /* Count valid timestamps and compact array */
    for (size_t i = 0; i < client->timestamp_count; i++) {
        if (client->timestamps[i] > cutoff_time) {
            if (valid_count != i) {
                client->timestamps[valid_count] = client->timestamps[i];
            }
            valid_count++;
        }
    }

    client->timestamp_count = valid_count;
}

/* Internal: Free client entry */
static void free_client_entry(client_entry_t *client) {
    if (!client) {
        return;
    }

    free(client->timestamps);
    client->timestamps = NULL;
    client->timestamp_count = 0;
    client->timestamp_capacity = 0;
}

/* Middleware function holder */
typedef struct {
    rate_limiter_t *limiter;
} rate_limiter_middleware_ctx_t;

/* Static middleware context - simplified approach 
 * NOTE: This uses a global variable which limits the application to using
 * one rate limiter at a time. This is acceptable for most use cases where
 * there's one rate limiter per server. A more robust solution would require
 * changing the middleware API to support context passing.
 */
static rate_limiter_t *volatile global_middleware_limiter = NULL;

/* Middleware implementation */
static bool rate_limiter_middleware_impl(http_request_t *req, http_response_t *res) {
    if (!global_middleware_limiter || !req || !res) {
        return true;  /* Continue if not configured */
    }

    const char *client_ip = req->client_ip ? req->client_ip : "unknown";
    
    if (!rate_limiter_check(global_middleware_limiter, client_ip)) {
        /* Rate limit exceeded */
        http_response_send_text(res, HTTP_TOO_MANY_REQUESTS, "Too Many Requests");
        
        /* Add rate limit headers */
        char header_value[64];
        snprintf(header_value, sizeof(header_value), "%zu", global_middleware_limiter->max_requests);
        http_response_set_header(res, "X-RateLimit-Limit", header_value);
        
        size_t remaining = rate_limiter_get_remaining(global_middleware_limiter, client_ip);
        snprintf(header_value, sizeof(header_value), "%zu", remaining);
        http_response_set_header(res, "X-RateLimit-Remaining", header_value);
        
        snprintf(header_value, sizeof(header_value), "%zu", global_middleware_limiter->window_seconds);
        http_response_set_header(res, "X-RateLimit-Window", header_value);
        
        return false;  /* Stop processing */
    }

    return true;  /* Continue to next middleware/handler */
}

/* Create middleware */
middleware_fn_t rate_limiter_middleware(rate_limiter_t *limiter) {
    global_middleware_limiter = limiter;
    return rate_limiter_middleware_impl;
}
