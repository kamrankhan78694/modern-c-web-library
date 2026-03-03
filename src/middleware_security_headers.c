/*
 * middleware_security_headers.c – Security headers middleware for weblib
 *
 * Pure C implementation – no external dependencies.
 * Sets industry-standard security headers on every response to protect
 * against XSS, clickjacking, MIME sniffing, and other common attacks.
 *
 * Author: kamran
 */

#include "weblib.h"
#include <stdlib.h>
#include <string.h>

/* ---- defaults -------------------------------------------------------- */

#define DEFAULT_CSP             "default-src 'self'"
#define DEFAULT_FRAME_OPTIONS   "DENY"
#define DEFAULT_REFERRER_POLICY "strict-origin-when-cross-origin"
#define DEFAULT_PERMISSIONS     "geolocation=(), camera=(), microphone=()"
#define HSTS_HEADER_MAX_LEN     128

/* ---- global config --------------------------------------------------- */

static security_headers_config_t *g_sec_cfg = NULL;

/* ---- middleware handler ---------------------------------------------- */

static bool _security_headers_handler(http_request_t *req,
                                      http_response_t *res,
                                      void *user_data) {
    (void)req;
    (void)user_data;

    const security_headers_config_t *cfg = g_sec_cfg;

    /* Content-Security-Policy */
    if (cfg && cfg->content_security_policy) {
        http_response_set_header(res, "Content-Security-Policy",
                                 cfg->content_security_policy);
    } else {
        http_response_set_header(res, "Content-Security-Policy",
                                 DEFAULT_CSP);
    }

    /* X-Content-Type-Options — prevent MIME type sniffing */
    http_response_set_header(res, "X-Content-Type-Options", "nosniff");

    /* X-Frame-Options — clickjacking protection */
    if (cfg && cfg->frame_options) {
        http_response_set_header(res, "X-Frame-Options",
                                 cfg->frame_options);
    } else {
        http_response_set_header(res, "X-Frame-Options",
                                 DEFAULT_FRAME_OPTIONS);
    }

    /* Referrer-Policy — control referrer leakage */
    if (cfg && cfg->referrer_policy) {
        http_response_set_header(res, "Referrer-Policy",
                                 cfg->referrer_policy);
    } else {
        http_response_set_header(res, "Referrer-Policy",
                                 DEFAULT_REFERRER_POLICY);
    }

    /* Permissions-Policy — restrict browser features */
    if (cfg && cfg->permissions_policy) {
        http_response_set_header(res, "Permissions-Policy",
                                 cfg->permissions_policy);
    } else {
        http_response_set_header(res, "Permissions-Policy",
                                 DEFAULT_PERMISSIONS);
    }

    /* Strict-Transport-Security — force HTTPS (opt-in) */
    if (cfg && cfg->enable_hsts) {
        int max_age = cfg->hsts_max_age > 0 ? cfg->hsts_max_age : 31536000;
        char hsts_val[HSTS_HEADER_MAX_LEN];
        if (cfg->hsts_include_subdomains) {
            snprintf(hsts_val, sizeof(hsts_val),
                     "max-age=%d; includeSubDomains", max_age);
        } else {
            snprintf(hsts_val, sizeof(hsts_val), "max-age=%d", max_age);
        }
        http_response_set_header(res, "Strict-Transport-Security", hsts_val);
    }

    /* X-XSS-Protection — set to 0 (modern best practice: rely on CSP) */
    http_response_set_header(res, "X-XSS-Protection", "0");

    return true; /* continue middleware chain */
}

/* ---- public API ------------------------------------------------------ */

middleware_fn_t security_headers_middleware_create(
        const security_headers_config_t *config) {
    /* Free any existing configuration */
    security_headers_middleware_destroy();

    if (config) {
        g_sec_cfg = (security_headers_config_t *)calloc(1, sizeof(*g_sec_cfg));
        if (!g_sec_cfg) return NULL;

        /* Deep copy strings (matching CORS middleware pattern) */
        if (config->content_security_policy) {
            g_sec_cfg->content_security_policy = strdup(config->content_security_policy);
            if (!g_sec_cfg->content_security_policy) {
                security_headers_middleware_destroy();
                return NULL;
            }
        }
        if (config->frame_options) {
            g_sec_cfg->frame_options = strdup(config->frame_options);
            if (!g_sec_cfg->frame_options) {
                security_headers_middleware_destroy();
                return NULL;
            }
        }
        if (config->referrer_policy) {
            g_sec_cfg->referrer_policy = strdup(config->referrer_policy);
            if (!g_sec_cfg->referrer_policy) {
                security_headers_middleware_destroy();
                return NULL;
            }
        }
        if (config->permissions_policy) {
            g_sec_cfg->permissions_policy = strdup(config->permissions_policy);
            if (!g_sec_cfg->permissions_policy) {
                security_headers_middleware_destroy();
                return NULL;
            }
        }
        g_sec_cfg->enable_hsts             = config->enable_hsts;
        g_sec_cfg->hsts_max_age            = config->hsts_max_age;
        g_sec_cfg->hsts_include_subdomains = config->hsts_include_subdomains;
    }

    return _security_headers_handler;
}

void security_headers_middleware_destroy(void) {
    if (g_sec_cfg) {
        free((void *)g_sec_cfg->content_security_policy);
        free((void *)g_sec_cfg->frame_options);
        free((void *)g_sec_cfg->referrer_policy);
        free((void *)g_sec_cfg->permissions_policy);
        free(g_sec_cfg);
        g_sec_cfg = NULL;
    }
}
