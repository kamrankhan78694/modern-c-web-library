/**
 * @file middleware_csrf.c
 * @brief CSRF protection middleware — Phase 8 (double-submit cookie pattern)
 *
 * On every request:
 *  - If the cookie is absent a fresh random token is generated and set as a
 *    cookie on the response (the client should then echo it in the header on
 *    subsequent state-changing requests).
 *  - Safe methods (GET, HEAD, OPTIONS) are always allowed regardless of token.
 *  - State-changing methods (POST, PUT, PATCH, DELETE) require the
 *    X-CSRF-Token request header value to match the cookie value.
 *    A constant-time comparison is used to resist timing attacks.
 *
 * Token format: hex-encoded random bytes read from /dev/urandom (or fallback).
 *
 * Copyright (c) 2024 Modern C Web Library — Licensed under MIT
 */

#include "weblib.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef _WIN32
#  include <fcntl.h>
#  include <unistd.h>
#endif

/* ── Defaults ────────────────────────────────────────────────────────────── */
#define CSRF_DEFAULT_COOKIE_NAME "csrf_token"
#define CSRF_DEFAULT_HEADER_NAME "X-CSRF-Token"
#define CSRF_DEFAULT_TOKEN_BYTES 16
#define CSRF_MIN_TOKEN_BYTES      8
#define CSRF_MAX_TOKEN_BYTES     32

/* ── Global state ─────────────────────────────────────────────────────────── */
typedef struct {
    char  cookie_name[64];
    char  header_name[64];
    int   token_bytes; /* raw byte count before hex encoding */
} csrf_state_t;

static csrf_state_t g_csrf_state;

/* ── Random token generation ─────────────────────────────────────────────── */

/**
 * @brief Fill buf with len random bytes.
 *        Tries /dev/urandom first; falls back to rand() seeded from time.
 */
static void _fill_random(unsigned char *buf, size_t len) {
#ifndef _WIN32
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd >= 0) {
        size_t got = 0;
        while (got < len) {
            ssize_t n = read(fd, buf + got, len - got);
            if (n <= 0) break;
            got += (size_t)n;
        }
        close(fd);
        if (got == len) return;
    }
#endif
    /* Fallback: weak PRNG — acceptable only when /dev/urandom is unavailable */
    static unsigned long seed_state = 0;
    if (seed_state == 0) {
        seed_state = (unsigned long)time(NULL);
        srand((unsigned int)seed_state);
    }
    for (size_t i = 0; i < len; i++) {
        buf[i] = (unsigned char)(rand() & 0xFF);
    }
}

/**
 * @brief Generate a hex-encoded CSRF token string.
 * @param token_bytes Number of raw bytes (result string is 2*token_bytes + 1)
 * @return Newly allocated hex string or NULL on failure
 */
static char *_generate_token(int token_bytes) {
    unsigned char raw[CSRF_MAX_TOKEN_BYTES];
    size_t nb = (size_t)token_bytes;
    if (nb > sizeof(raw)) nb = sizeof(raw);

    _fill_random(raw, nb);

    char *hex = (char *)malloc(nb * 2 + 1);
    if (!hex) return NULL;

    static const char HEX[] = "0123456789abcdef";
    for (size_t i = 0; i < nb; i++) {
        hex[i * 2]     = HEX[raw[i] >> 4];
        hex[i * 2 + 1] = HEX[raw[i] & 0x0F];
    }
    hex[nb * 2] = '\0';
    return hex;
}

/* ── Constant-time comparison ────────────────────────────────────────────── */

/**
 * @brief Compare two strings in constant time (length-independent timing).
 * @return true if equal
 */
static bool _ct_strcmp(const char *a, const char *b) {
    size_t la = strlen(a);
    size_t lb = strlen(b);
    /* Use the longer length to avoid short-circuit on first mismatch */
    size_t max_len = la > lb ? la : lb;
    unsigned char diff = 0;
    for (size_t i = 0; i < max_len; i++) {
        unsigned char ca = (i < la) ? (unsigned char)a[i] : 0;
        unsigned char cb = (i < lb) ? (unsigned char)b[i] : 0;
        diff |= ca ^ cb;
    }
    return (diff == 0) && (la == lb);
}

/* ── Middleware handler ───────────────────────────────────────────────────── */

static bool _csrf_middleware_handler(http_request_t *req, http_response_t *res) {
    if (!req || !res) return true;

    /* Safe methods bypass CSRF check */
    bool is_safe = (req->method == HTTP_GET     ||
                    req->method == HTTP_HEAD    ||
                    req->method == HTTP_OPTIONS);

    /* Read token from cookie */
    const char *cookie_token = http_request_get_cookie(req, g_csrf_state.cookie_name);

    if (!cookie_token || cookie_token[0] == '\0') {
        /* No token yet — generate one and set the cookie */
        char *new_token = _generate_token(g_csrf_state.token_bytes);
        if (new_token) {
            cookie_options_t opts;
            memset(&opts, 0, sizeof(opts));
            opts.path      = "/";
            opts.max_age   = -1;   /* session cookie */
            opts.http_only = false; /* Must be readable by JS for double-submit */
            opts.secure    = false; /* caller can override via custom opts if needed */
            opts.same_site = "Strict";
            http_response_set_cookie(res, g_csrf_state.cookie_name, new_token, &opts);
            free(new_token);
        }
        /* Allow safe methods; block unsafe methods since no token to compare */
        if (!is_safe) {
            http_response_send_text(res, HTTP_FORBIDDEN, "CSRF token missing");
            return false;
        }
        return true;
    }

    /* Cookie token is present */
    if (is_safe) {
        return true; /* No check needed */
    }

    /* Unsafe method: validate the header matches the cookie */
    const char *header_token = http_request_get_header(req, g_csrf_state.header_name);
    if (!header_token || header_token[0] == '\0') {
        http_response_send_text(res, HTTP_FORBIDDEN, "CSRF token missing in header");
        return false;
    }

    if (!_ct_strcmp(cookie_token, header_token)) {
        http_response_send_text(res, HTTP_FORBIDDEN, "CSRF token mismatch");
        return false;
    }

    return true; /* Validated — continue chain */
}

/* ── Public API ──────────────────────────────────────────────────────────── */

middleware_fn_t csrf_middleware_create(const csrf_config_t *config) {
    /* Destroy any previous instance */
    csrf_middleware_destroy();

    /* Cookie name */
    const char *cname = CSRF_DEFAULT_COOKIE_NAME;
    if (config && config->cookie_name && config->cookie_name[0] != '\0') {
        cname = config->cookie_name;
    }
    strncpy(g_csrf_state.cookie_name, cname, sizeof(g_csrf_state.cookie_name) - 1);
    g_csrf_state.cookie_name[sizeof(g_csrf_state.cookie_name) - 1] = '\0';

    /* Header name */
    const char *hname = CSRF_DEFAULT_HEADER_NAME;
    if (config && config->header_name && config->header_name[0] != '\0') {
        hname = config->header_name;
    }
    strncpy(g_csrf_state.header_name, hname, sizeof(g_csrf_state.header_name) - 1);
    g_csrf_state.header_name[sizeof(g_csrf_state.header_name) - 1] = '\0';

    /* Token length */
    int tlen = CSRF_DEFAULT_TOKEN_BYTES;
    if (config && config->token_length >= CSRF_MIN_TOKEN_BYTES) {
        tlen = config->token_length;
    }
    if (tlen > CSRF_MAX_TOKEN_BYTES) tlen = CSRF_MAX_TOKEN_BYTES;
    g_csrf_state.token_bytes = tlen;


    return _csrf_middleware_handler;
}

void csrf_middleware_destroy(void) {
    memset(&g_csrf_state, 0, sizeof(g_csrf_state));

}
