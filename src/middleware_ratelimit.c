/*
 * Rate Limiting Middleware - Phase 5.4
 * Modern C Web Library
 * 
 * Implements IP-based rate limiting using token bucket algorithm.
 * Pure C with zero external dependencies.
 */

#include "weblib.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <pthread.h>
#endif

/* Configuration */
#define HASH_TABLE_SIZE 1024
#define MAX_IP_STRING 46  /* IPv6 max length */
#define CLEANUP_INTERVAL 100  /* Cleanup every N requests */
#define ENTRY_EXPIRE_SECONDS 300  /* Remove entries inactive for 5 minutes */

/* Token bucket entry for a single IP */
typedef struct bucket_entry {
    char ip[MAX_IP_STRING];
    double tokens;
    time_t last_refill_time;
    time_t last_access_time;
    struct bucket_entry *next;  /* For hash table chaining */
} bucket_entry_t;

/* Hash table bucket */
typedef struct {
    bucket_entry_t *head;
} hash_bucket_t;

/* Rate limiter state */
typedef struct {
    ratelimit_config_t config;
    hash_bucket_t table[HASH_TABLE_SIZE];
    unsigned long request_count;
    pthread_mutex_t mutex;
} ratelimiter_t;

/* Global rate limiter instance */
static ratelimiter_t *g_ratelimiter = NULL;

/*
 * Simple hash function for IP strings
 * Using djb2 algorithm
 */
static unsigned int _hash_ip(const char *ip) {
    unsigned long hash = 5381;
    int c;
    
    while ((c = *ip++)) {
        hash = ((hash << 5) + hash) + c; /* hash * 33 + c */
    }
    
    return hash % HASH_TABLE_SIZE;
}

/*
 * Extract client IP address from socket
 * Returns 0 on success, -1 on failure
 */
static int _get_client_ip(int socket_fd, char *ip_buffer, size_t buffer_size) {
    struct sockaddr_storage addr;
    socklen_t addr_len = sizeof(addr);
    
    if (getpeername(socket_fd, (struct sockaddr*)&addr, &addr_len) != 0) {
        return -1;
    }
    
    if (addr.ss_family == AF_INET) {
        /* IPv4 */
        struct sockaddr_in *s = (struct sockaddr_in*)&addr;
        if (inet_ntop(AF_INET, &s->sin_addr, ip_buffer, buffer_size) == NULL) {
            return -1;
        }
    } else if (addr.ss_family == AF_INET6) {
        /* IPv6 */
        struct sockaddr_in6 *s = (struct sockaddr_in6*)&addr;
        if (inet_ntop(AF_INET6, &s->sin6_addr, ip_buffer, buffer_size) == NULL) {
            return -1;
        }
    } else {
        return -1;
    }
    
    return 0;
}

/*
 * Find or create a bucket entry for an IP
 */
static bucket_entry_t* _find_or_create_entry(ratelimiter_t *limiter, const char *ip) {
    unsigned int hash = _hash_ip(ip);
    hash_bucket_t *bucket = &limiter->table[hash];
    bucket_entry_t *entry = bucket->head;
    time_t now = time(NULL);
    
    /* Search for existing entry */
    while (entry != NULL) {
        if (strcmp(entry->ip, ip) == 0) {
            entry->last_access_time = now;
            return entry;
        }
        entry = entry->next;
    }
    
    /* Create new entry */
    entry = (bucket_entry_t*)malloc(sizeof(bucket_entry_t));
    if (entry == NULL) {
        return NULL;
    }
    
    strncpy(entry->ip, ip, MAX_IP_STRING - 1);
    entry->ip[MAX_IP_STRING - 1] = '\0';
    entry->tokens = (double)limiter->config.burst_size;
    entry->last_refill_time = now;
    entry->last_access_time = now;
    entry->next = bucket->head;
    bucket->head = entry;
    
    return entry;
}

/*
 * Refill tokens based on elapsed time
 */
static void _refill_tokens(ratelimiter_t *limiter, bucket_entry_t *entry) {
    time_t now = time(NULL);
    time_t elapsed = now - entry->last_refill_time;
    
    if (elapsed <= 0) {
        return;
    }
    
    /* Calculate refill rate: tokens per second */
    double refill_rate = (double)limiter->config.requests_per_window / 
                         (double)limiter->config.window_seconds;
    
    /* Add tokens based on elapsed time */
    double tokens_to_add = refill_rate * elapsed;
    entry->tokens += tokens_to_add;
    
    /* Cap at burst size */
    if (entry->tokens > limiter->config.burst_size) {
        entry->tokens = (double)limiter->config.burst_size;
    }
    
    entry->last_refill_time = now;
}

/*
 * Try to consume a token
 * Returns 1 if successful, 0 if rate limited
 */
static int _consume_token(bucket_entry_t *entry) {
    if (entry->tokens >= 1.0) {
        entry->tokens -= 1.0;
        return 1;
    }
    return 0;
}

/*
 * Clean up old, expired entries
 */
static void _cleanup_expired_entries(ratelimiter_t *limiter) {
    time_t now = time(NULL);
    
    for (int i = 0; i < HASH_TABLE_SIZE; i++) {
        hash_bucket_t *bucket = &limiter->table[i];
        bucket_entry_t *entry = bucket->head;
        bucket_entry_t *prev = NULL;
        
        while (entry != NULL) {
            bucket_entry_t *next = entry->next;
            
            /* Check if entry has expired */
            if ((now - entry->last_access_time) > ENTRY_EXPIRE_SECONDS) {
                /* Remove entry */
                if (prev == NULL) {
                    bucket->head = next;
                } else {
                    prev->next = next;
                }
                free(entry);
            } else {
                prev = entry;
            }
            
            entry = next;
        }
    }
}

/*
 * Set rate limit headers
 */
static void _set_ratelimit_headers(http_response_t *res, 
                                   const ratelimiter_t *limiter,
                                   const bucket_entry_t *entry) {
    char buffer[64];
    
    /* X-RateLimit-Limit: maximum requests per window */
    snprintf(buffer, sizeof(buffer), "%d", limiter->config.requests_per_window);
    http_response_set_header(res, "X-RateLimit-Limit", buffer);
    
    /* X-RateLimit-Remaining: tokens remaining (floor value) */
    int remaining = (int)entry->tokens;
    if (remaining < 0) remaining = 0;
    snprintf(buffer, sizeof(buffer), "%d", remaining);
    http_response_set_header(res, "X-RateLimit-Remaining", buffer);
    
    /* X-RateLimit-Reset: timestamp when tokens will be fully replenished */
    time_t now = time(NULL);
    double tokens_needed = limiter->config.burst_size - entry->tokens;
    double refill_rate = (double)limiter->config.requests_per_window / 
                         (double)limiter->config.window_seconds;
    int seconds_until_reset = (int)(tokens_needed / refill_rate) + 1;
    time_t reset_time = now + seconds_until_reset;
    snprintf(buffer, sizeof(buffer), "%ld", (long)reset_time);
    http_response_set_header(res, "X-RateLimit-Reset", buffer);
}

/*
 * Rate limiting middleware function
 */
static bool _ratelimit_middleware(http_request_t *req, http_response_t *res, void *user_data) {
    ratelimiter_t *limiter = user_data ? (ratelimiter_t *)user_data : g_ratelimiter;
    
    if (limiter == NULL) {
        /* No limiter configured, allow request */
        return true;
    }
    
    /* Get client IP from socket */
    char client_ip[MAX_IP_STRING];
    if (_get_client_ip(req->socket_fd, client_ip, sizeof(client_ip)) != 0) {
        /* Failed to get IP, allow request (fail open) */
        return true;
    }
    
    pthread_mutex_lock(&limiter->mutex);
    
    /* Find or create bucket entry for this IP */
    bucket_entry_t *entry = _find_or_create_entry(limiter, client_ip);
    if (entry == NULL) {
        /* Memory allocation failed, allow request (fail open) */
        pthread_mutex_unlock(&limiter->mutex);
        return true;
    }
    
    /* Refill tokens based on elapsed time */
    _refill_tokens(limiter, entry);
    
    /* Try to consume a token */
    int allowed = _consume_token(entry);
    
    /* Set rate limit headers */
    _set_ratelimit_headers(res, limiter, entry);
    
    if (!allowed) {
        /* Rate limit exceeded - send 429 response */
        /* Calculate retry time: time until at least 1 token is available */
        double refill_rate = (double)limiter->config.requests_per_window / 
                             (double)limiter->config.window_seconds;
        int retry_seconds = (refill_rate > 0) ? (int)(1.0 / refill_rate) + 1 : 60;
        if (retry_seconds < 1) retry_seconds = 1;
        char retry_after[32];
        snprintf(retry_after, sizeof(retry_after), "%d", retry_seconds);
        http_response_set_header(res, "Retry-After", retry_after);
        http_response_send_text(res, HTTP_TOO_MANY_REQUESTS, 
                               "Rate limit exceeded. Please try again later.");
        pthread_mutex_unlock(&limiter->mutex);
        return false;
    }
    
    /* Periodic cleanup of expired entries */
    limiter->request_count++;
    if (limiter->request_count % CLEANUP_INTERVAL == 0) {
        _cleanup_expired_entries(limiter);
    }
    
    pthread_mutex_unlock(&limiter->mutex);
    return true;
}

/*
 * Create a rate limiting middleware with the given configuration
 */
middleware_fn_t ratelimit_middleware_create(const ratelimit_config_t *config) {
    if (config == NULL) {
        fprintf(stderr, "ratelimit_middleware_create: config is NULL\n");
        return NULL;
    }
    
    /* Validate configuration */
    if (config->requests_per_window <= 0) {
        fprintf(stderr, "ratelimit_middleware_create: invalid requests_per_window\n");
        return NULL;
    }
    
    if (config->window_seconds <= 0) {
        fprintf(stderr, "ratelimit_middleware_create: invalid window_seconds\n");
        return NULL;
    }
    
    if (config->burst_size <= 0) {
        fprintf(stderr, "ratelimit_middleware_create: invalid burst_size\n");
        return NULL;
    }
    
    /* Destroy existing limiter if any */
    if (g_ratelimiter != NULL) {
        ratelimit_middleware_destroy();
    }
    
    /* Allocate rate limiter state */
    g_ratelimiter = (ratelimiter_t*)calloc(1, sizeof(ratelimiter_t));
    if (g_ratelimiter == NULL) {
        fprintf(stderr, "ratelimit_middleware_create: memory allocation failed\n");
        return NULL;
    }
    
    if (pthread_mutex_init(&g_ratelimiter->mutex, NULL) != 0) {
        free(g_ratelimiter);
        g_ratelimiter = NULL;
        return NULL;
    }
    
    /* Copy configuration */
    g_ratelimiter->config = *config;
    g_ratelimiter->request_count = 0;
    
    /* Initialize hash table (calloc already zeroed it) */
    
    return _ratelimit_middleware;
}

/*
 * Destroy rate limiting middleware resources
 */
void ratelimit_middleware_destroy(void) {
    if (g_ratelimiter == NULL) {
        return;
    }
    
    pthread_mutex_destroy(&g_ratelimiter->mutex);
    
    /* Free all bucket entries */
    for (int i = 0; i < HASH_TABLE_SIZE; i++) {
        hash_bucket_t *bucket = &g_ratelimiter->table[i];
        bucket_entry_t *entry = bucket->head;
        
        while (entry != NULL) {
            bucket_entry_t *next = entry->next;
            free(entry);
            entry = next;
        }
    }
    
    /* Free the rate limiter */
    free(g_ratelimiter);
    g_ratelimiter = NULL;
}
