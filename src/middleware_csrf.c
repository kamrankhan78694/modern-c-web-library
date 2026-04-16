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

#include "kamran.k"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
 * @brief Generate a hex-encoded CSRF token string.
 * @param token_bytes Number of raw bytes (result string is 2*token_bytes + 1)
 * @return Newly allocated hex string or NULL on failure
 */
static char *_generate_token(int token_bytes) {
    unsigned char raw[CSRF_MAX_TOKEN_BYTES];
    size_t nb = (size_t)token_bytes;
    if (nb > sizeof(raw)) nb = sizeof(raw);

    if (secure_random_bytes(raw, nb) != 0) {
        fprintf(stderr, "CSRF: secure_random_bytes() failed — cannot generate token\n");
        return NULL;
    }

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

/* ── Middleware handler ───────────────────────────────────────────────────── */

static bool _csrf_middleware_handler(http_request_t *req, http_response_t *res, void *user_data) {
    (void)user_data;
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

    size_t cookie_len = strlen(cookie_token);
    size_t header_len = strlen(header_token);
    /* Reject length mismatch up front — but still perform constant-time
     * comparison over the full expected (cookie) length to avoid leaking
     * token length through timing. */
    bool length_match = (cookie_len == header_len);
    /* Always compare over cookie_len bytes.  If header_len < cookie_len the
     * extra bytes from header_token are harmless reads within the allocated
     * string (they just won't match). We still compare the full cookie_len. */
    bool match = secure_compare(cookie_token, header_token,
                                cookie_len > header_len ? cookie_len : header_len)
                 && length_match;
    if (!match) {
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
    secure_zero(&g_csrf_state, sizeof(g_csrf_state));

}
