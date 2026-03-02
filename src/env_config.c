/*
 * env_config.c – Environment variable configuration for weblib
 *
 * Pure C implementation – no external dependencies.
 * Provides typed accessors for environment variables and a convenience
 * function to apply well-known WEBLIB_* variables to an http_server_t.
 *
 * Author: kamran
 */

#include "weblib.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>

/* ---- helpers --------------------------------------------------------- */

/**
 * Case-insensitive comparison of two NUL-terminated strings.
 * Returns 0 when they are equal (ignoring case).
 */
static int _ci_strcmp(const char *a, const char *b) {
    while (*a && *b) {
        int diff = tolower((unsigned char)*a) - tolower((unsigned char)*b);
        if (diff != 0) return diff;
        a++;
        b++;
    }
    return tolower((unsigned char)*a) - tolower((unsigned char)*b);
}

/* ---- public API ------------------------------------------------------ */

const char *env_config_get(const char *key, const char *default_value) {
    if (!key) return default_value;
    const char *val = getenv(key);
    if (!val || val[0] == '\0') return default_value;
    return val;
}

int env_config_get_int(const char *key, int default_value) {
    if (!key) return default_value;
    const char *val = getenv(key);
    if (!val || val[0] == '\0') return default_value;

    char *end = NULL;
    errno = 0;
    long lv = strtol(val, &end, 10);

    /* Reject trailing garbage, overflow, and empty conversions */
    if (end == val || *end != '\0' || errno == ERANGE) return default_value;
    if (lv < INT_MIN || lv > INT_MAX) return default_value;

    return (int)lv;
}

bool env_config_get_bool(const char *key, bool default_value) {
    if (!key) return default_value;
    const char *val = getenv(key);
    if (!val || val[0] == '\0') return default_value;

    /* Truthy: "1", "true", "yes", "on" */
    if (_ci_strcmp(val, "1") == 0 || _ci_strcmp(val, "true") == 0 ||
        _ci_strcmp(val, "yes") == 0 || _ci_strcmp(val, "on") == 0)
        return true;

    /* Falsy: "0", "false", "no", "off" */
    if (_ci_strcmp(val, "0") == 0 || _ci_strcmp(val, "false") == 0 ||
        _ci_strcmp(val, "no") == 0 || _ci_strcmp(val, "off") == 0)
        return false;

    return default_value;
}

uint16_t env_config_get_port(const char *key, uint16_t default_value) {
    if (!key) return default_value;
    int v = env_config_get_int(key, -1);
    if (v < 0 || v > 65535) return default_value;
    return (uint16_t)v;
}

const char *env_config_require(const char *key) {
    if (!key) return NULL;
    const char *val = getenv(key);
    if (!val || val[0] == '\0') return NULL;
    return val;
}

bool env_config_is_set(const char *key) {
    if (!key) return false;
    const char *val = getenv(key);
    return val != NULL && val[0] != '\0';
}

/* ---- secure value API ------------------------------------------------ */

/**
 * Overwrite memory with zeros in a way the compiler cannot optimise away.
 * Uses C11 memset_s when available, otherwise a volatile-pointer trick.
 */
static void _secure_wipe(void *ptr, size_t len) {
    if (!ptr || len == 0) return;
#if defined(__STDC_LIB_EXT1__) || defined(_WIN32)
    memset_s(ptr, len, 0, len);
#else
    volatile unsigned char *p = (volatile unsigned char *)ptr;
    while (len--) *p++ = 0;
#endif
}

struct env_secure_value {
    char  *data;   /* heap-allocated copy of the secret */
    size_t len;    /* strlen of data (excludes NUL)     */
};

env_secure_value_t *env_config_get_secure(const char *key) {
    if (!key) return NULL;
    const char *raw = getenv(key);
    if (!raw || raw[0] == '\0') return NULL;

    size_t len = strlen(raw);
    env_secure_value_t *sv = malloc(sizeof(*sv));
    if (!sv) return NULL;

    sv->data = malloc(len + 1);
    if (!sv->data) {
        free(sv);
        return NULL;
    }
    memcpy(sv->data, raw, len + 1);
    sv->len = len;
    return sv;
}

const char *env_secure_value_get(const env_secure_value_t *sv) {
    if (!sv) return NULL;
    return sv->data;
}

size_t env_secure_value_len(const env_secure_value_t *sv) {
    if (!sv) return 0;
    return sv->len;
}

void env_secure_value_free(env_secure_value_t *sv) {
    if (!sv) return;
    if (sv->data) {
        _secure_wipe(sv->data, sv->len);
        free(sv->data);
    }
    _secure_wipe(sv, sizeof(*sv));
    free(sv);
}

char *env_config_redact(const char *value) {
    if (!value) return NULL;
    size_t len = strlen(value);
    if (len == 0) return NULL;

    /* Short values: fully masked */
    if (len <= 4) {
        char *out = malloc(4);
        if (!out) return NULL;
        memcpy(out, "****", 4);
        out[3] = '\0';
        return out;
    }

    /* Longer values: show first char + mask + last char */
    /* e.g. "sk-abc123xyz" → "s**********z" */
    char *out = malloc(len + 1);
    if (!out) return NULL;
    out[0] = value[0];
    memset(out + 1, '*', len - 2);
    out[len - 1] = value[len - 1];
    out[len] = '\0';
    return out;
}

/* ---- server integration ---------------------------------------------- */

int http_server_apply_env(http_server_t *server) {
    if (!server) return -1;

    /* WEBLIB_READ_TIMEOUT / WEBLIB_WRITE_TIMEOUT */
    int rt = env_config_get_int("WEBLIB_READ_TIMEOUT", -1);
    int wt = env_config_get_int("WEBLIB_WRITE_TIMEOUT", -1);
    if (rt >= 0 || wt >= 0) {
        int cur_rt = http_server_get_read_timeout(server);
        int cur_wt = http_server_get_write_timeout(server);
        http_server_set_timeout(server,
                                rt >= 0 ? rt : cur_rt,
                                wt >= 0 ? wt : cur_wt);
    }

    /* WEBLIB_THREAD_COUNT */
    int tc = env_config_get_int("WEBLIB_THREAD_COUNT", -1);
    if (tc > 0) {
        http_server_set_thread_count(server, tc);
    }

    /* WEBLIB_MAX_CONNECTIONS */
    int mc = env_config_get_int("WEBLIB_MAX_CONNECTIONS", -1);
    if (mc > 0) {
        http_server_set_max_connections(server, mc);
    }

    /* WEBLIB_ASYNC_MODE */
    const char *am = getenv("WEBLIB_ASYNC_MODE");
    if (am && am[0] != '\0') {
        bool async = env_config_get_bool("WEBLIB_ASYNC_MODE", false);
        http_server_set_async(server, async);
    }

    return 0;
}
