#include "kamran.k"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdint.h>
#ifdef __EMSCRIPTEN__
#include "wasm_compat.h"
#else
#include <pthread.h>
#endif

#define MAX_SESSIONS 1024
#define SESSION_ID_LENGTH 32
#define SESSION_COOKIE_NAME "MCWL_SESSION"

/* Session data entry */
typedef struct session_data_entry {
    char *key;
    char *value;
    struct session_data_entry *next;
} session_data_entry_t;

/* Session structure */
struct session {
    char session_id[SESSION_ID_LENGTH + 1];
    time_t created_at;
    time_t expires_at;
    int max_age;
    session_data_entry_t *data;
    bool in_use;
};

/* Session store structure */
struct session_store {
    session_t sessions[MAX_SESSIONS];
    size_t session_count;
    pthread_mutex_t lock;
};

/* Generate a random session ID.  Returns 0 on success, -1 on CSPRNG failure.
 * Fails closed: it never falls back to a predictable PRNG (rand/rand_r) for a
 * security token -- a caller that cannot obtain entropy must refuse the
 * session rather than issue a guessable ID. */
static int generate_session_id(char *buffer, size_t length) {
    static const char charset[] =
        "0123456789"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz";

    size_t charset_len = sizeof(charset) - 1;
    unsigned char random_bytes[64]; /* Max SESSION_ID_LENGTH */

    if (length > sizeof(random_bytes)) {
        length = sizeof(random_bytes);
    }

    if (secure_random_bytes(random_bytes, length) != 0) {
        /* May hold partial CSPRNG output on failure: wipe it, leave no ID. */
        secure_zero(random_bytes, sizeof(random_bytes));
        buffer[0] = '\0';
        return -1;
    }

    /* Rejection sampling to avoid modular bias.
     * 256 / 62 = 4 remainder 8; threshold = 256 - 8 = 248 */
    unsigned char threshold = (unsigned char)(256 - (256 % charset_len));
    for (size_t i = 0; i < length; ) {
        if (random_bytes[i] < threshold) {
            buffer[i] = charset[random_bytes[i] % charset_len];
            i++;
        } else {
            /* Re-sample this byte. */
            unsigned char replacement;
            if (secure_random_bytes(&replacement, 1) != 0) {
                secure_zero(random_bytes, sizeof(random_bytes));
                return -1;
            }
            random_bytes[i] = replacement;
        }
    }
    buffer[length] = '\0';
    secure_zero(random_bytes, sizeof(random_bytes));   /* wipe raw entropy */
    return 0;
}

/* Create session store */
session_store_t *session_store_create(void) {
    session_store_t *store = (session_store_t *)calloc(1, sizeof(session_store_t));
    if (!store) {
        return NULL;
    }
    
    if (pthread_mutex_init(&store->lock, NULL) != 0) {
        free(store);
        return NULL;
    }
    
    store->session_count = 0;
    
    /* Initialize all sessions as unused */
    for (size_t i = 0; i < MAX_SESSIONS; i++) {
        store->sessions[i].in_use = false;
        store->sessions[i].data = NULL;
    }
    
    return store;
}

/* Free session data entries */
static void free_session_data(session_data_entry_t *data) {
    session_data_entry_t *current = data;
    while (current) {
        session_data_entry_t *next = current->next;
        free(current->key);
        free(current->value);
        free(current);
        current = next;
    }
}

/* Destroy session store */
void session_store_destroy(session_store_t *store) {
    if (!store) {
        return;
    }
    
    pthread_mutex_lock(&store->lock);
    
    /* Free all session data */
    for (size_t i = 0; i < MAX_SESSIONS; i++) {
        if (store->sessions[i].in_use) {
            free_session_data(store->sessions[i].data);
        }
    }
    
    pthread_mutex_unlock(&store->lock);
    pthread_mutex_destroy(&store->lock);
    
    free(store);
}

/* Create new session */
char *session_create(session_store_t *store, int max_age) {
    if (!store) {
        return NULL;
    }
    
    pthread_mutex_lock(&store->lock);
    
    /* Find free session slot */
    session_t *session = NULL;
    for (size_t i = 0; i < MAX_SESSIONS; i++) {
        if (!store->sessions[i].in_use) {
            session = &store->sessions[i];
            break;
        }
    }
    
    if (!session) {
        fprintf(stderr, "No free session slots available\n");
        pthread_mutex_unlock(&store->lock);
        return NULL;
    }
    
    /* Generate unique session ID with collision check */
    int max_attempts = 10;
    bool unique = false;
    
    for (int attempt = 0; attempt < max_attempts && !unique; attempt++) {
        if (generate_session_id(session->session_id, SESSION_ID_LENGTH) != 0) {
            /* CSPRNG unavailable: fail closed rather than issue a guessable ID. */
            fprintf(stderr, "session_create: secure RNG unavailable — refusing to issue session ID\n");
            pthread_mutex_unlock(&store->lock);
            return NULL;
        }

        /* Check for uniqueness */
        unique = true;
        for (size_t i = 0; i < MAX_SESSIONS; i++) {
            if (store->sessions[i].in_use && &store->sessions[i] != session &&
                strcmp(store->sessions[i].session_id, session->session_id) == 0) {
                unique = false;
                break;
            }
        }
    }
    
    if (!unique) {
        fprintf(stderr, "Failed to generate unique session ID after %d attempts\n", max_attempts);
        pthread_mutex_unlock(&store->lock);
        return NULL;
    }
    
    /* Set session metadata */
    session->created_at = time(NULL);
    session->max_age = max_age;
    
    if (max_age > 0) {
        session->expires_at = session->created_at + max_age;
    } else {
        session->expires_at = 0; /* Never expires (session cookie) */
    }
    
    session->data = NULL;
    session->in_use = true;
    store->session_count++;
    
    char *result = strdup(session->session_id);
    
    pthread_mutex_unlock(&store->lock);
    
    return result;
}

/* Get session by ID */
session_t *session_get(session_store_t *store, const char *session_id) {
    if (!store || !session_id) {
        return NULL;
    }
    
    pthread_mutex_lock(&store->lock);
    
    /* Find session with matching ID */
    for (size_t i = 0; i < MAX_SESSIONS; i++) {
        if (store->sessions[i].in_use && 
            strcmp(store->sessions[i].session_id, session_id) == 0) {
            
            /* Check if expired - auto-cleanup to prevent slot exhaustion */
            if (session_is_expired(&store->sessions[i])) {
                free_session_data(store->sessions[i].data);
                store->sessions[i].data = NULL;
                store->sessions[i].in_use = false;
                if (store->session_count > 0) {
                    store->session_count--;
                }
                pthread_mutex_unlock(&store->lock);
                return NULL;
            }
            
            pthread_mutex_unlock(&store->lock);
            return &store->sessions[i];
        }
    }
    
    pthread_mutex_unlock(&store->lock);
    return NULL;
}

/* Destroy session */
void session_destroy(session_store_t *store, const char *session_id) {
    if (!store || !session_id) {
        return;
    }
    
    pthread_mutex_lock(&store->lock);
    
    /* Find and destroy session */
    for (size_t i = 0; i < MAX_SESSIONS; i++) {
        if (store->sessions[i].in_use && 
            strcmp(store->sessions[i].session_id, session_id) == 0) {
            
            /* Free session data */
            free_session_data(store->sessions[i].data);
            store->sessions[i].data = NULL;
            store->sessions[i].in_use = false;
            if (store->session_count > 0) {
                store->session_count--;
            }
            break;
        }
    }
    
    pthread_mutex_unlock(&store->lock);
}

/* Find an in-use, non-expired session by ID. MUST be called with store->lock
 * held. Returns the slot or NULL.
 *
 * Resolving the session by ID under the lock on every data operation is what
 * makes the data API immune to the stale-handle / slot-reuse hazards of a raw
 * session_t*: a session that was destroyed or has expired (in_use == false, or
 * past its deadline) is reported as not-found, and a fixed slot that was reused
 * for a different session cannot be mistaken for this one because the IDs
 * differ. No internal pointer is ever returned to the caller. */
static session_t *find_active_session_locked(session_store_t *store,
                                             const char *session_id) {
    for (size_t i = 0; i < MAX_SESSIONS; i++) {
        session_t *s = &store->sessions[i];
        if (s->in_use && strcmp(s->session_id, session_id) == 0) {
            return session_is_expired(s) ? NULL : s;
        }
    }
    return NULL;
}

/* Set session data (keyed on store + session_id; see header). */
int session_set_data(session_store_t *store, const char *session_id,
                     const char *key, const char *value) {
    if (!store || !session_id || !key || !value) {
        return -1;
    }

    pthread_mutex_lock(&store->lock);

    session_t *session = find_active_session_locked(store, session_id);
    if (!session) {
        pthread_mutex_unlock(&store->lock);
        return -1;
    }

    /* Update in place if the key already exists. */
    for (session_data_entry_t *current = session->data; current; current = current->next) {
        if (strcmp(current->key, key) == 0) {
            char *new_value = strdup(value);
            if (!new_value) {
                pthread_mutex_unlock(&store->lock);
                return -1; /* Keep old value if allocation fails */
            }
            free(current->value);
            current->value = new_value;
            pthread_mutex_unlock(&store->lock);
            return 0;
        }
    }

    /* Otherwise prepend a new entry. */
    session_data_entry_t *entry = (session_data_entry_t *)malloc(sizeof(session_data_entry_t));
    if (!entry) {
        pthread_mutex_unlock(&store->lock);
        return -1;
    }
    entry->key = strdup(key);
    entry->value = strdup(value);
    if (!entry->key || !entry->value) {
        free(entry->key);
        free(entry->value);
        free(entry);
        pthread_mutex_unlock(&store->lock);
        return -1;
    }
    entry->next = session->data;
    session->data = entry;

    pthread_mutex_unlock(&store->lock);
    return 0;
}

/* Get session data.
 * Returns a freshly allocated copy the caller must free() (NULL if not found).
 * Copying under the lock removes the use-after-free window that a borrowed
 * pointer into the store's data would leave open once the lock is released. */
char *session_get_data(session_store_t *store, const char *session_id,
                       const char *key) {
    if (!store || !session_id || !key) {
        return NULL;
    }

    pthread_mutex_lock(&store->lock);

    char *result = NULL;
    session_t *session = find_active_session_locked(store, session_id);
    if (session) {
        for (session_data_entry_t *current = session->data; current; current = current->next) {
            if (strcmp(current->key, key) == 0) {
                result = strdup(current->value);
                break;
            }
        }
    }

    pthread_mutex_unlock(&store->lock);
    return result;
}

/* Remove session data. Returns 0 if removed, -1 if the session/key was absent. */
int session_remove_data(session_store_t *store, const char *session_id,
                        const char *key) {
    if (!store || !session_id || !key) {
        return -1;
    }

    pthread_mutex_lock(&store->lock);

    session_t *session = find_active_session_locked(store, session_id);
    if (!session) {
        pthread_mutex_unlock(&store->lock);
        return -1;
    }

    session_data_entry_t *current = session->data;
    session_data_entry_t *prev = NULL;
    int removed = -1;
    while (current) {
        if (strcmp(current->key, key) == 0) {
            if (prev) {
                prev->next = current->next;
            } else {
                session->data = current->next;
            }
            free(current->key);
            free(current->value);
            free(current);
            removed = 0;
            break;
        }
        prev = current;
        current = current->next;
    }

    pthread_mutex_unlock(&store->lock);
    return removed;
}

/* Get session ID */
const char *session_get_id(session_t *session) {
    if (!session) {
        return NULL;
    }
    return session->session_id;
}

/* Check if session is expired */
bool session_is_expired(session_t *session) {
    if (!session) {
        return true;
    }
    
    /* Session cookies (max_age = 0) never expire based on time */
    if (session->max_age == 0) {
        return false;
    }
    
    time_t now = time(NULL);
    return (session->expires_at > 0 && now >= session->expires_at);
}

/* Clean up expired sessions */
int session_cleanup_expired(session_store_t *store) {
    if (!store) {
        return 0;
    }
    
    pthread_mutex_lock(&store->lock);
    
    int cleaned = 0;
    
    for (size_t i = 0; i < MAX_SESSIONS; i++) {
        if (store->sessions[i].in_use && session_is_expired(&store->sessions[i])) {
            free_session_data(store->sessions[i].data);
            store->sessions[i].data = NULL;
            store->sessions[i].in_use = false;
            if (store->session_count > 0) {
                store->session_count--;
            }
            cleaned++;
        }
    }
    
    pthread_mutex_unlock(&store->lock);
    
    return cleaned;
}

/* Parse cookie header to extract session ID */
static char *extract_session_id_from_cookies(const char *cookie_header) {
    if (!cookie_header) {
        return NULL;
    }
    
    /* Look for SESSION_COOKIE_NAME=value in cookie header */
    char search_pattern[64];
    snprintf(search_pattern, sizeof(search_pattern), "%s=", SESSION_COOKIE_NAME);
    
    const char *start = cookie_header;
    const char *found = NULL;
    
    /* Search for the cookie name, ensuring it's at the start or after "; " */
    while ((found = strstr(start, search_pattern)) != NULL) {
        /* Check if this is at the beginning or preceded by "; " */
        if (found == cookie_header || 
            (found > cookie_header && found[-1] == ';') ||
            (found > cookie_header + 1 && found[-1] == ' ' && found[-2] == ';')) {
            /* Valid match found */
            break;
        }
        /* Continue searching after this false match */
        start = found + 1;
        found = NULL;
    }
    
    if (!found) {
        return NULL;
    }
    
    start = found + strlen(search_pattern);
    
    /* Find end of cookie value (semicolon or end of string) */
    const char *end = strchr(start, ';');
    size_t length;
    
    if (end) {
        length = end - start;
    } else {
        length = strlen(start);
    }
    
    /* Allocate and copy session ID */
    char *session_id = (char *)malloc(length + 1);
    if (session_id) {
        strncpy(session_id, start, length);
        session_id[length] = '\0';
    }
    
    return session_id;
}

/* Get session from request */
session_t *session_from_request(session_store_t *store, http_request_t *req) {
    if (!store || !req) {
        return NULL;
    }
    
    /* Get cookie header */
    const char *cookie_header = http_request_get_header(req, "Cookie");
    if (!cookie_header) {
        return NULL;
    }
    
    /* Extract session ID from cookie */
    char *session_id = extract_session_id_from_cookies(cookie_header);
    if (!session_id) {
        return NULL;
    }
    
    /* Get session from store */
    session_t *session = session_get(store, session_id);
    free(session_id);
    
    return session;
}

/* Set session cookie in response */
void session_set_cookie(http_response_t *res, const char *session_id, int max_age, const char *path) {
    if (!res || !session_id) {
        return;
    }
    
    /* Build cookie value */
    char cookie[512];
    int len;
    
    if (max_age < 0) {
        /* Delete cookie */
        len = snprintf(cookie, sizeof(cookie), 
            "%s=; Path=%s; Max-Age=0; HttpOnly; SameSite=Lax",
            SESSION_COOKIE_NAME, path ? path : "/");
    } else if (max_age == 0) {
        /* Session cookie (no Max-Age) */
        len = snprintf(cookie, sizeof(cookie), 
            "%s=%s; Path=%s; HttpOnly; SameSite=Lax",
            SESSION_COOKIE_NAME, session_id, path ? path : "/");
    } else {
        /* Persistent cookie with Max-Age */
        len = snprintf(cookie, sizeof(cookie), 
            "%s=%s; Path=%s; Max-Age=%d; HttpOnly; SameSite=Lax",
            SESSION_COOKIE_NAME, session_id, path ? path : "/", max_age);
    }
    
    if (len > 0 && len < (int)sizeof(cookie)) {
        http_response_set_header(res, "Set-Cookie", cookie);
    }
}
