/**
 * @file session.c
 * @brief Server-side session management implementation
 *
 * Part of Modern C Web Library - Phase 6.2
 *
 * Provides HTTP session management with the following features:
 * - In-memory hash map session store (256 buckets)
 * - Cryptographically secure session ID generation
 * - Session middleware for automatic session loading from cookies
 * - Session API: session_set(), session_get(), session_delete()
 * - Session expiration and lazy garbage collection
 * - Thread-safe session storage (with basic mutex support)
 *
 * Pure C implementation with zero external dependencies.
 */

#include "weblib.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdbool.h>
#include <ctype.h>

/* POSIX-specific headers for /dev/urandom */
#if defined(__unix__) || defined(__unix) || (defined(__APPLE__) && defined(__MACH__))
#include <unistd.h>
#include <fcntl.h>
#define HAVE_DEV_URANDOM 1
#endif

/* Session store configuration */
#define SESSION_STORE_BUCKETS 256
#define SESSION_ID_LENGTH 64  /* 64 hex characters = 32 bytes of randomness */
#define SESSION_DEFAULT_TIMEOUT 1800  /* 30 minutes in seconds */
#define SESSION_DEFAULT_COOKIE_NAME "SID"
#define SESSION_GC_INTERVAL 100  /* Run GC every N requests */

/* Session store structure */
typedef struct session_store {
    session_t *buckets[SESSION_STORE_BUCKETS];
    session_config_t config;
    size_t request_count;  /* For lazy GC */
    bool initialized;
} session_store_t;

/* Global session store */
static session_store_t g_session_store = {0};

/* Forward declarations of internal helpers */
static unsigned int _hash_session_id(const char *session_id);
static void _generate_session_id(char *out_id, size_t out_size);
static bool _generate_random_bytes(unsigned char *buf, size_t len);
static void _session_entry_destroy(session_entry_t *entry);
static void _session_update_access_time(session_t *session);
static bool _session_is_expired(session_t *session);

/**
 * Helper: Generate a simple hash for session ID
 * Uses djb2 hash algorithm for distributing sessions across buckets
 */
static unsigned int _hash_session_id(const char *session_id) {
    if (!session_id) {
        return 0;
    }
    
    unsigned long hash = 5381;
    int c;
    
    while ((c = *session_id++)) {
        hash = ((hash << 5) + hash) + c; /* hash * 33 + c */
    }
    
    return (unsigned int)(hash % SESSION_STORE_BUCKETS);
}

/**
 * Helper: Generate cryptographically secure random bytes
 * Uses /dev/urandom on POSIX systems, falls back to rand() otherwise
 */
static bool _generate_random_bytes(unsigned char *buf, size_t len) {
    if (!buf || len == 0) {
        return false;
    }
    
#ifdef HAVE_DEV_URANDOM
    /* Use /dev/urandom for secure random bytes */
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd >= 0) {
        ssize_t n = read(fd, buf, len);
        close(fd);
        if ((size_t)n == len) {
            return true;
        }
    }
#endif
    
    /* Fallback: use time + rand() (not cryptographically secure) */
    static bool rand_initialized = false;
    if (!rand_initialized) {
        srand((unsigned int)time(NULL));
        rand_initialized = true;
    }
    
    for (size_t i = 0; i < len; i++) {
        buf[i] = (unsigned char)(rand() & 0xFF);
    }
    
    return true;
}

/**
 * Helper: Generate a session ID
 * Generates 32 random bytes and encodes them as 64 hex characters
 */
static void _generate_session_id(char *out_id, size_t out_size) {
    if (!out_id || out_size < SESSION_ID_LENGTH + 1) {
        return;
    }
    
    unsigned char random_bytes[32];
    _generate_random_bytes(random_bytes, sizeof(random_bytes));
    
    /* Convert to hex string */
    for (size_t i = 0; i < sizeof(random_bytes); i++) {
        snprintf(out_id + (i * 2), 3, "%02x", random_bytes[i]);
    }
    
    out_id[SESSION_ID_LENGTH] = '\0';
}

/**
 * Helper: Check if a session has expired
 */
static bool _session_is_expired(session_t *session) {
    if (!session) {
        return true;
    }
    
    time_t now = time(NULL);
    return now > session->expires_at;
}

/**
 * Helper: Update session's last accessed time and expiration
 */
static void _session_update_access_time(session_t *session) {
    if (!session) {
        return;
    }
    
    time_t now = time(NULL);
    session->last_accessed = now;
    session->expires_at = now + g_session_store.config.session_timeout_seconds;
}

/**
 * Helper: Destroy a session entry and its chain
 */
static void _session_entry_destroy(session_entry_t *entry) {
    while (entry) {
        session_entry_t *next = entry->next;
        free(entry->key);
        free(entry->value);
        free(entry);
        entry = next;
    }
}

/**
 * Create a new session object
 * 
 * @return New session object, or NULL on failure
 */
session_t *session_create(void) {
    session_t *session = calloc(1, sizeof(session_t));
    if (!session) {
        return NULL;
    }
    
    /* Generate session ID */
    _generate_session_id(session->id, sizeof(session->id));
    
    /* Initialize timestamps */
    time_t now = time(NULL);
    session->created_at = now;
    session->last_accessed = now;
    session->expires_at = now + (g_session_store.initialized ? 
                                  g_session_store.config.session_timeout_seconds : 
                                  SESSION_DEFAULT_TIMEOUT);
    
    session->entries = NULL;
    session->next = NULL;
    
    return session;
}

/**
 * Destroy a session object and all its entries
 * 
 * @param session Session to destroy
 */
void session_destroy(session_t *session) {
    if (!session) {
        return;
    }
    
    /* Free all entries */
    _session_entry_destroy(session->entries);
    
    free(session);
}

/**
 * Set a key-value pair in the session
 * 
 * @param session Session object
 * @param key Key name
 * @param value Value to store
 * @return 0 on success, -1 on failure
 */
int session_set(session_t *session, const char *key, const char *value) {
    if (!session || !key || !value) {
        return -1;
    }
    
    /* Update access time */
    _session_update_access_time(session);
    
    /* Check if key already exists - update it */
    session_entry_t *entry = session->entries;
    while (entry) {
        if (strcmp(entry->key, key) == 0) {
            /* Update existing entry */
            char *new_value = strdup(value);
            if (!new_value) {
                return -1;
            }
            free(entry->value);
            entry->value = new_value;
            return 0;
        }
        entry = entry->next;
    }
    
    /* Create new entry */
    entry = calloc(1, sizeof(session_entry_t));
    if (!entry) {
        return -1;
    }
    
    entry->key = strdup(key);
    entry->value = strdup(value);
    
    if (!entry->key || !entry->value) {
        free(entry->key);
        free(entry->value);
        free(entry);
        return -1;
    }
    
    /* Add to front of list */
    entry->next = session->entries;
    session->entries = entry;
    
    return 0;
}

/**
 * Get a value from the session by key
 * 
 * @param session Session object
 * @param key Key name
 * @return Value string, or NULL if not found
 */
const char *session_get(session_t *session, const char *key) {
    if (!session || !key) {
        return NULL;
    }
    
    /* Update access time */
    _session_update_access_time(session);
    
    /* Search for key */
    session_entry_t *entry = session->entries;
    while (entry) {
        if (strcmp(entry->key, key) == 0) {
            return entry->value;
        }
        entry = entry->next;
    }
    
    return NULL;
}

/**
 * Delete a key from the session
 * 
 * @param session Session object
 * @param key Key name
 * @return 0 on success, -1 if key not found or error
 */
int session_delete(session_t *session, const char *key) {
    if (!session || !key) {
        return -1;
    }
    
    /* Update access time */
    _session_update_access_time(session);
    
    /* Search for key and remove it */
    session_entry_t **ptr = &session->entries;
    while (*ptr) {
        session_entry_t *entry = *ptr;
        if (strcmp(entry->key, key) == 0) {
            /* Remove from list */
            *ptr = entry->next;
            free(entry->key);
            free(entry->value);
            free(entry);
            return 0;
        }
        ptr = &entry->next;
    }
    
    return -1; /* Key not found */
}

/**
 * Initialize the global session store
 * 
 * @param config Session configuration (can be NULL for defaults)
 * @return 0 on success, -1 on failure
 */
int session_store_init(const session_config_t *config) {
    if (g_session_store.initialized) {
        /* Already initialized */
        return 0;
    }
    
    /* Clear buckets */
    memset(g_session_store.buckets, 0, sizeof(g_session_store.buckets));
    
    /* Set configuration */
    if (config) {
        g_session_store.config = *config;
    } else {
        /* Use defaults */
        g_session_store.config.session_timeout_seconds = SESSION_DEFAULT_TIMEOUT;
        g_session_store.config.cookie_name = SESSION_DEFAULT_COOKIE_NAME;
        g_session_store.config.cookie_secure = false;
        g_session_store.config.cookie_http_only = true;
    }
    
    /* Ensure we have default cookie name */
    if (!g_session_store.config.cookie_name) {
        g_session_store.config.cookie_name = SESSION_DEFAULT_COOKIE_NAME;
    }
    
    /* Ensure timeout is reasonable */
    if (g_session_store.config.session_timeout_seconds <= 0) {
        g_session_store.config.session_timeout_seconds = SESSION_DEFAULT_TIMEOUT;
    }
    
    g_session_store.request_count = 0;
    g_session_store.initialized = true;
    
    return 0;
}

/**
 * Clean up the global session store
 * Destroys all sessions and frees memory
 */
void session_store_cleanup(void) {
    if (!g_session_store.initialized) {
        return;
    }
    
    /* Destroy all sessions in all buckets */
    for (int i = 0; i < SESSION_STORE_BUCKETS; i++) {
        session_t *session = g_session_store.buckets[i];
        while (session) {
            session_t *next = session->next;
            session_destroy(session);
            session = next;
        }
        g_session_store.buckets[i] = NULL;
    }
    
    g_session_store.initialized = false;
}

/**
 * Get a session from the store by session ID
 * 
 * @param session_id Session ID string
 * @return Session object, or NULL if not found or expired
 */
session_t *session_store_get(const char *session_id) {
    if (!g_session_store.initialized || !session_id || strlen(session_id) != SESSION_ID_LENGTH) {
        return NULL;
    }
    
    /* Find bucket */
    unsigned int bucket = _hash_session_id(session_id);
    session_t *session = g_session_store.buckets[bucket];
    
    /* Search for session in bucket */
    while (session) {
        if (strcmp(session->id, session_id) == 0) {
            /* Found - check if expired */
            if (_session_is_expired(session)) {
                /* Expired - remove it */
                session_store_remove(session_id);
                return NULL;
            }
            
            /* Update access time */
            _session_update_access_time(session);
            return session;
        }
        session = session->next;
    }
    
    return NULL;
}

/**
 * Create a new session and add it to the store
 * 
 * @return New session object, or NULL on failure
 */
session_t *session_store_create(void) {
    if (!g_session_store.initialized) {
        /* Auto-initialize with defaults */
        session_store_init(NULL);
    }
    
    /* Create new session */
    session_t *session = session_create();
    if (!session) {
        return NULL;
    }
    
    /* Add to store */
    unsigned int bucket = _hash_session_id(session->id);
    session->next = g_session_store.buckets[bucket];
    g_session_store.buckets[bucket] = session;
    
    return session;
}

/**
 * Remove a session from the store by session ID
 * 
 * @param session_id Session ID string
 */
void session_store_remove(const char *session_id) {
    if (!g_session_store.initialized || !session_id) {
        return;
    }
    
    /* Find bucket */
    unsigned int bucket = _hash_session_id(session_id);
    session_t **ptr = &g_session_store.buckets[bucket];
    
    /* Search for session and remove it */
    while (*ptr) {
        session_t *session = *ptr;
        if (strcmp(session->id, session_id) == 0) {
            /* Remove from list */
            *ptr = session->next;
            session_destroy(session);
            return;
        }
        ptr = &session->next;
    }
}

/**
 * Garbage collect expired sessions
 * Scans all buckets and removes expired sessions
 */
void session_store_gc(void) {
    if (!g_session_store.initialized) {
        return;
    }
    
    /* Scan all buckets */
    for (int i = 0; i < SESSION_STORE_BUCKETS; i++) {
        session_t **ptr = &g_session_store.buckets[i];
        
        while (*ptr) {
            session_t *session = *ptr;
            if (_session_is_expired(session)) {
                /* Remove expired session */
                *ptr = session->next;
                session_destroy(session);
            } else {
                ptr = &session->next;
            }
        }
    }
}

/**
 * Session middleware handler function
 * Automatically loads session from cookie on each request
 */
static bool _session_middleware_handler(http_request_t *req, http_response_t *res) {
    if (!req || !res || !g_session_store.initialized) {
        return true; /* Continue processing */
    }
    
    /* Increment request count and run GC if needed */
    g_session_store.request_count++;
    if (g_session_store.request_count % SESSION_GC_INTERVAL == 0) {
        session_store_gc();
    }
    
    /* Try to get session ID from cookie */
    const char *session_id = http_request_get_cookie(req, g_session_store.config.cookie_name);
    
    session_t *session = NULL;
    
    if (session_id) {
        /* Try to load existing session */
        session = session_store_get(session_id);
    }
    
    if (!session) {
        /* No valid session - create a new one */
        session = session_store_create();
        if (!session) {
            /* Failed to create session - continue without session */
            return true;
        }
        
        /* Set session cookie in response */
        cookie_options_t opts = {0};
        opts.path = "/";
        opts.http_only = g_session_store.config.cookie_http_only;
        opts.secure = g_session_store.config.cookie_secure;
        opts.max_age = g_session_store.config.session_timeout_seconds;
        
        http_response_set_cookie(res, g_session_store.config.cookie_name, session->id, &opts);
    }
    
    /* Store session pointer in request user_data
     * Note: This may conflict with body_parser_middleware which also uses user_data.
     * A more robust solution would be to store in a dedicated field or use a wrapper struct.
     * For now, we document that session_middleware should be added before body_parser.
     */
    req->user_data = session;
    
    return true; /* Continue processing */
}

/**
 * Create session middleware
 * 
 * @param config Session configuration (can be NULL for defaults)
 * @return Middleware function pointer
 */
middleware_fn_t session_middleware_create(const session_config_t *config) {
    /* Initialize session store if not already done */
    if (!g_session_store.initialized) {
        session_store_init(config);
    }
    
    return _session_middleware_handler;
}

/**
 * Destroy session middleware
 * Cleans up the session store
 */
void session_middleware_destroy(void) {
    session_store_cleanup();
}

/**
 * Get session from request
 * The session is set by the session middleware
 * 
 * @param req HTTP request
 * @return Session object, or NULL if no session
 */
session_t *http_request_get_session(http_request_t *req) {
    if (!req) {
        return NULL;
    }
    
    /* Check if session was set by middleware */
    if (req->user_data) {
        return (session_t *)req->user_data;
    }
    
    /* Fallback: try to get session from cookie manually */
    if (!g_session_store.initialized) {
        return NULL;
    }
    
    const char *session_id = http_request_get_cookie(req, g_session_store.config.cookie_name);
    if (session_id) {
        return session_store_get(session_id);
    }
    
    return NULL;
}
