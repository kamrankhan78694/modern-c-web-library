/*
 * cache.c - In-memory LRU cache with TTL support
 * 
 * Thread-safe cache implementation using:
 * - Hash table with separate chaining for O(1) lookups
 * - Doubly-linked list for LRU eviction policy
 * - Per-entry TTL (time-to-live) with expiration
 * - String key-value pairs (internally copied)
 */

#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef __EMSCRIPTEN__
#include "wasm_compat.h"
#else
#include <pthread.h>
#endif
#include "kamran.k"

/* Internal cache entry structure */
typedef struct cache_entry {
    char *key;
    char *value;
    time_t expires_at;              /* 0 = no expiry */
    struct cache_entry *hash_next;  /* next in hash chain */
    struct cache_entry *lru_prev;   /* previous in LRU list */
    struct cache_entry *lru_next;   /* next in LRU list */
} cache_entry_t;

/* Cache structure */
struct cache {
    cache_entry_t **buckets;    /* hash table array */
    size_t bucket_count;        /* number of hash buckets */
    size_t count;               /* current number of entries */
    size_t max_entries;         /* maximum entries before LRU eviction */
    cache_entry_t *lru_head;    /* most recently used */
    cache_entry_t *lru_tail;    /* least recently used */
    pthread_mutex_t lock;
};

/* ============================================================================
 * Internal Helper Functions
 * ============================================================================ */

/**
 * djb2 hash function - simple and effective for strings
 */
static unsigned long _hash_djb2(const char *str)
{
    unsigned long hash = 5381;
    int c;
    while ((c = (unsigned char)*str++))
        hash = ((hash << 5) + hash) + c;
    return hash;
}

/**
 * Remove entry from LRU list (does not free or modify hash chains)
 */
static void _lru_remove(cache_t *cache, cache_entry_t *entry)
{
    if (!entry)
        return;

    if (entry->lru_prev)
        entry->lru_prev->lru_next = entry->lru_next;
    else
        cache->lru_head = entry->lru_next;

    if (entry->lru_next)
        entry->lru_next->lru_prev = entry->lru_prev;
    else
        cache->lru_tail = entry->lru_prev;

    entry->lru_prev = NULL;
    entry->lru_next = NULL;
}

/**
 * Move entry to head of LRU list (makes it most recently used)
 */
static void _lru_move_to_head(cache_t *cache, cache_entry_t *entry)
{
    if (!entry || cache->lru_head == entry)
        return;

    /* Remove from current position */
    _lru_remove(cache, entry);

    /* Insert at head */
    entry->lru_next = cache->lru_head;
    entry->lru_prev = NULL;

    if (cache->lru_head)
        cache->lru_head->lru_prev = entry;
    else
        cache->lru_tail = entry;

    cache->lru_head = entry;
}

/**
 * Add new entry to head of LRU list
 */
static void _lru_add_to_head(cache_t *cache, cache_entry_t *entry)
{
    entry->lru_prev = NULL;
    entry->lru_next = cache->lru_head;

    if (cache->lru_head)
        cache->lru_head->lru_prev = entry;
    else
        cache->lru_tail = entry;

    cache->lru_head = entry;
}

/**
 * Remove entry from hash table and LRU list, then free it
 */
static void _cache_remove_entry(cache_t *cache, cache_entry_t *entry)
{
    if (!cache || !entry)
        return;

    /* Remove from hash chain */
    unsigned long hash = _hash_djb2(entry->key);
    size_t bucket_idx = hash % cache->bucket_count;
    cache_entry_t **ptr = &cache->buckets[bucket_idx];

    while (*ptr) {
        if (*ptr == entry) {
            *ptr = entry->hash_next;
            break;
        }
        ptr = &(*ptr)->hash_next;
    }

    /* Remove from LRU list */
    _lru_remove(cache, entry);

    /* Free memory */
    free(entry->key);
    free(entry->value);
    free(entry);

    cache->count--;
}

/**
 * Evict least recently used entry (from tail)
 */
static void _cache_evict_lru(cache_t *cache)
{
    if (!cache || !cache->lru_tail)
        return;

    cache_entry_t *victim = cache->lru_tail;
    _cache_remove_entry(cache, victim);
}

/**
 * Find entry in hash table by key (returns NULL if not found)
 */
static cache_entry_t *_cache_find_entry(cache_t *cache, const char *key)
{
    if (!cache || !key)
        return NULL;

    unsigned long hash = _hash_djb2(key);
    size_t bucket_idx = hash % cache->bucket_count;
    cache_entry_t *entry = cache->buckets[bucket_idx];

    while (entry) {
        if (strcmp(entry->key, key) == 0)
            return entry;
        entry = entry->hash_next;
    }

    return NULL;
}

/**
 * Check if entry has expired
 */
static int _is_expired(cache_entry_t *entry)
{
    if (!entry || entry->expires_at == 0)
        return 0;

    time_t now = time(NULL);
    return now >= entry->expires_at;
}

/* ============================================================================
 * Public API Functions
 * ============================================================================ */

/**
 * Create a new cache with given maximum capacity
 * Uses max_entries * 2 buckets for good hash distribution
 * Returns NULL on failure
 */
cache_t *cache_create(size_t max_entries)
{
    if (max_entries == 0 || max_entries > SIZE_MAX / 2)
        return NULL;

    cache_t *cache = calloc(1, sizeof(cache_t));
    if (!cache)
        return NULL;

    cache->bucket_count = max_entries * 2;
    cache->buckets = calloc(cache->bucket_count, sizeof(cache_entry_t *));
    if (!cache->buckets) {
        free(cache);
        return NULL;
    }

    cache->max_entries = max_entries;
    cache->count = 0;
    cache->lru_head = NULL;
    cache->lru_tail = NULL;

    if (pthread_mutex_init(&cache->lock, NULL) != 0) {
        free(cache->buckets);
        free(cache);
        return NULL;
    }

    return cache;
}

/**
 * Destroy cache and free all resources
 */
void cache_destroy(cache_t *cache)
{
    if (!cache)
        return;

    pthread_mutex_lock(&cache->lock);

    /* Free all entries */
    for (size_t i = 0; i < cache->bucket_count; i++) {
        cache_entry_t *entry = cache->buckets[i];
        while (entry) {
            cache_entry_t *next = entry->hash_next;
            free(entry->key);
            free(entry->value);
            free(entry);
            entry = next;
        }
    }

    free(cache->buckets);

    pthread_mutex_unlock(&cache->lock);
    pthread_mutex_destroy(&cache->lock);
    free(cache);
}

/**
 * Set or update a key-value pair in the cache
 * ttl_seconds: 0 = no expiry, >0 = expires after ttl_seconds
 * Returns 0 on success, -1 on failure
 */
int cache_set(cache_t *cache, const char *key, const char *value, int ttl_seconds)
{
    if (!cache || !key || !value)
        return -1;

    pthread_mutex_lock(&cache->lock);

    /* Check if key already exists */
    cache_entry_t *existing = _cache_find_entry(cache, key);
    if (existing) {
        /* Update existing entry */
        char *new_value = strdup(value);
        if (!new_value) {
            pthread_mutex_unlock(&cache->lock);
            return -1;
        }

        free(existing->value);
        existing->value = new_value;

        /* Update TTL */
        if (ttl_seconds > 0)
            existing->expires_at = time(NULL) + ttl_seconds;
        else
            existing->expires_at = 0;

        /* Move to head (most recently used) */
        _lru_move_to_head(cache, existing);

        pthread_mutex_unlock(&cache->lock);
        return 0;
    }

    /* Create new entry */
    cache_entry_t *entry = calloc(1, sizeof(cache_entry_t));
    if (!entry) {
        pthread_mutex_unlock(&cache->lock);
        return -1;
    }

    entry->key = strdup(key);
    entry->value = strdup(value);
    if (!entry->key || !entry->value) {
        free(entry->key);
        free(entry->value);
        free(entry);
        pthread_mutex_unlock(&cache->lock);
        return -1;
    }

    /* Set TTL */
    if (ttl_seconds > 0)
        entry->expires_at = time(NULL) + ttl_seconds;
    else
        entry->expires_at = 0;

    /* Evict LRU if at capacity */
    if (cache->count >= cache->max_entries)
        _cache_evict_lru(cache);

    /* Insert into hash table */
    unsigned long hash = _hash_djb2(key);
    size_t bucket_idx = hash % cache->bucket_count;
    entry->hash_next = cache->buckets[bucket_idx];
    cache->buckets[bucket_idx] = entry;

    /* Add to head of LRU list */
    _lru_add_to_head(cache, entry);

    cache->count++;

    pthread_mutex_unlock(&cache->lock);
    return 0;
}

/**
 * Get value by key
 * Returns a freshly allocated copy of the cached value, or NULL if not found/expired.
 * If expired, automatically removes the entry.
 * Caller must free() the returned string when done.
 */
const char *cache_get(cache_t *cache, const char *key)
{
    if (!cache || !key)
        return NULL;

    pthread_mutex_lock(&cache->lock);

    cache_entry_t *entry = _cache_find_entry(cache, key);
    if (!entry) {
        pthread_mutex_unlock(&cache->lock);
        return NULL;
    }

    /* Check if expired */
    if (_is_expired(entry)) {
        _cache_remove_entry(cache, entry);
        pthread_mutex_unlock(&cache->lock);
        return NULL;
    }

    /* Move to head (most recently used) */
    _lru_move_to_head(cache, entry);

    char *result = strdup(entry->value);
    pthread_mutex_unlock(&cache->lock);

    return result;
}

/**
 * Delete entry by key
 * Returns 0 if found and deleted, -1 if not found
 */
int cache_delete(cache_t *cache, const char *key)
{
    if (!cache || !key)
        return -1;

    pthread_mutex_lock(&cache->lock);

    cache_entry_t *entry = _cache_find_entry(cache, key);
    if (!entry) {
        pthread_mutex_unlock(&cache->lock);
        return -1;
    }

    _cache_remove_entry(cache, entry);

    pthread_mutex_unlock(&cache->lock);
    return 0;
}

/**
 * Clear all entries from the cache
 */
void cache_clear(cache_t *cache)
{
    if (!cache)
        return;

    pthread_mutex_lock(&cache->lock);

    /* Remove all entries */
    for (size_t i = 0; i < cache->bucket_count; i++) {
        cache_entry_t *entry = cache->buckets[i];
        while (entry) {
            cache_entry_t *next = entry->hash_next;
            free(entry->key);
            free(entry->value);
            free(entry);
            entry = next;
        }
        cache->buckets[i] = NULL;
    }

    cache->count = 0;
    cache->lru_head = NULL;
    cache->lru_tail = NULL;

    pthread_mutex_unlock(&cache->lock);
}

/**
 * Get current number of entries in the cache
 */
size_t cache_count(cache_t *cache)
{
    if (!cache)
        return 0;

    pthread_mutex_lock(&cache->lock);
    size_t count = cache->count;
    pthread_mutex_unlock(&cache->lock);

    return count;
}
