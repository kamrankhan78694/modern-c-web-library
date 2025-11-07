#include "weblib.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdint.h>

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
};

/* Generate random session ID */
static void generate_session_id(char *buffer, size_t length) {
    static const char charset[] = 
        "0123456789"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz";
    
    /* Initialize random seed on first call with better entropy */
    static bool seeded = false;
    if (!seeded) {
        /* Combine multiple sources for better randomness */
        unsigned int seed = (unsigned int)time(NULL);
        seed ^= (unsigned int)clock();
        seed ^= (unsigned int)(uintptr_t)buffer; /* Use stack address for additional entropy */
        srand(seed);
        seeded = true;
    }
    
    for (size_t i = 0; i < length; i++) {
        /* Use multiple rand() calls to increase randomness */
        int index = (rand() ^ (rand() << 15)) % (sizeof(charset) - 1);
        buffer[i] = charset[index];
    }
    buffer[length] = '\0';
}

/* Create session store */
session_store_t *session_store_create(void) {
    session_store_t *store = (session_store_t *)calloc(1, sizeof(session_store_t));
    if (!store) {
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
    
    /* Free all session data */
    for (size_t i = 0; i < MAX_SESSIONS; i++) {
        if (store->sessions[i].in_use) {
            free_session_data(store->sessions[i].data);
        }
    }
    
    free(store);
}

/* Create new session */
char *session_create(session_store_t *store, int max_age) {
    if (!store) {
        return NULL;
    }
    
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
        return NULL;
    }
    
    /* Generate unique session ID */
    generate_session_id(session->session_id, SESSION_ID_LENGTH);
    
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
    
    return strdup(session->session_id);
}

/* Get session by ID */
session_t *session_get(session_store_t *store, const char *session_id) {
    if (!store || !session_id) {
        return NULL;
    }
    
    /* Find session with matching ID */
    for (size_t i = 0; i < MAX_SESSIONS; i++) {
        if (store->sessions[i].in_use && 
            strcmp(store->sessions[i].session_id, session_id) == 0) {
            
            /* Check if expired */
            if (session_is_expired(&store->sessions[i])) {
                return NULL;
            }
            
            return &store->sessions[i];
        }
    }
    
    return NULL;
}

/* Destroy session */
void session_destroy(session_store_t *store, const char *session_id) {
    if (!store || !session_id) {
        return;
    }
    
    /* Find and destroy session */
    for (size_t i = 0; i < MAX_SESSIONS; i++) {
        if (store->sessions[i].in_use && 
            strcmp(store->sessions[i].session_id, session_id) == 0) {
            
            /* Free session data */
            free_session_data(store->sessions[i].data);
            store->sessions[i].data = NULL;
            store->sessions[i].in_use = false;
            store->session_count--;
            break;
        }
    }
}

/* Set session data */
void session_set(session_t *session, const char *key, const char *value) {
    if (!session || !key || !value) {
        return;
    }
    
    /* Check if key already exists - update value */
    session_data_entry_t *current = session->data;
    while (current) {
        if (strcmp(current->key, key) == 0) {
            /* Update existing value */
            char *new_value = strdup(value);
            if (!new_value) {
                return; /* Keep old value if allocation fails */
            }
            free(current->value);
            current->value = new_value;
            return;
        }
        current = current->next;
    }
    
    /* Create new entry */
    session_data_entry_t *entry = (session_data_entry_t *)malloc(sizeof(session_data_entry_t));
    if (!entry) {
        return;
    }
    
    entry->key = strdup(key);
    if (!entry->key) {
        free(entry);
        return;
    }
    
    entry->value = strdup(value);
    if (!entry->value) {
        free(entry->key);
        free(entry);
        return;
    }
    
    entry->next = session->data;
    session->data = entry;
}

/* Get session data */
const char *session_get_data(session_t *session, const char *key) {
    if (!session || !key) {
        return NULL;
    }
    
    session_data_entry_t *current = session->data;
    while (current) {
        if (strcmp(current->key, key) == 0) {
            return current->value;
        }
        current = current->next;
    }
    
    return NULL;
}

/* Remove session data */
void session_remove_data(session_t *session, const char *key) {
    if (!session || !key) {
        return;
    }
    
    session_data_entry_t *current = session->data;
    session_data_entry_t *prev = NULL;
    
    while (current) {
        if (strcmp(current->key, key) == 0) {
            /* Remove entry */
            if (prev) {
                prev->next = current->next;
            } else {
                session->data = current->next;
            }
            
            free(current->key);
            free(current->value);
            free(current);
            return;
        }
        
        prev = current;
        current = current->next;
    }
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
    
    int cleaned = 0;
    
    for (size_t i = 0; i < MAX_SESSIONS; i++) {
        if (store->sessions[i].in_use && session_is_expired(&store->sessions[i])) {
            free_session_data(store->sessions[i].data);
            store->sessions[i].data = NULL;
            store->sessions[i].in_use = false;
            store->session_count--;
            cleaned++;
        }
    }
    
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
        if (found == cookie_header || (found > cookie_header + 1 && 
            found[-1] == ' ' && found[-2] == ';')) {
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
