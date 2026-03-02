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
