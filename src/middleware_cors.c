/**
 * @file middleware_cors.c
 * @brief CORS (Cross-Origin Resource Sharing) middleware implementation
 *
 * Implements CORS support for the Modern C Web Library, handling:
 * - Preflight OPTIONS requests
 * - CORS response headers
 * - Origin validation
 * - Configurable allowed origins, methods, and headers
 */

#include "weblib.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <ctype.h>

/* Default CORS configuration values */
#define DEFAULT_ALLOWED_METHODS "GET, POST, PUT, DELETE, PATCH, OPTIONS"
#define DEFAULT_ALLOWED_HEADERS "Content-Type, Authorization"
#define DEFAULT_MAX_AGE 86400

/* Global CORS configuration storage (static since middleware_fn_t doesn't support user_data) */
static cors_options_t *g_cors_config = NULL;

/**
 * @brief Check if a string is empty or NULL
 */
static bool _is_empty(const char *str) {
    return str == NULL || str[0] == '\0';
}

/**
 * @brief Check if the request origin matches any allowed origin
 * @param origin The origin from the request
 * @param allowed_origins NULL-terminated array of allowed origins, or NULL for wildcard
 * @return true if origin is allowed, false otherwise
 */
static bool _is_origin_allowed(const char *origin, const char **allowed_origins) {
    if (_is_empty(origin)) {
        return false;
    }

    /* NULL allowed_origins means allow all (wildcard) */
    if (allowed_origins == NULL) {
        return true;
    }

    /* Check against each allowed origin */
    for (int i = 0; allowed_origins[i] != NULL; i++) {
        if (strcmp(origin, allowed_origins[i]) == 0) {
            return true;
        }
    }

    return false;
}

/**
 * @brief Set CORS headers on the response
 * @param res The HTTP response
 * @param origin The request origin (if allowed)
 * @param is_preflight Whether this is a preflight request
 */
static void _set_cors_headers(http_response_t *res, const char *origin, bool is_preflight) {
    if (res == NULL || g_cors_config == NULL) {
        return;
    }

    /* Set Access-Control-Allow-Origin */
    if (g_cors_config->allowed_origins == NULL) {
        /* Wildcard - allow all origins */
        http_response_set_header(res, "Access-Control-Allow-Origin", "*");
    } else if (origin != NULL) {
        /* Specific origin is allowed */
        http_response_set_header(res, "Access-Control-Allow-Origin", origin);
        /* Vary header to indicate origin-based response */
        http_response_set_header(res, "Vary", "Origin");
    }

    /* Set Access-Control-Allow-Credentials if enabled */
    if (g_cors_config->allow_credentials) {
        http_response_set_header(res, "Access-Control-Allow-Credentials", "true");
    }

    /* For preflight requests, set additional headers */
    if (is_preflight) {
        /* Set Access-Control-Allow-Methods */
        const char *methods = g_cors_config->allowed_methods;
        if (_is_empty(methods)) {
            methods = DEFAULT_ALLOWED_METHODS;
        }
        http_response_set_header(res, "Access-Control-Allow-Methods", methods);

        /* Set Access-Control-Allow-Headers */
        const char *headers = g_cors_config->allowed_headers;
        if (_is_empty(headers)) {
            headers = DEFAULT_ALLOWED_HEADERS;
        }
        http_response_set_header(res, "Access-Control-Allow-Headers", headers);

        /* Set Access-Control-Max-Age */
        int max_age = g_cors_config->max_age;
        if (max_age <= 0) {
            max_age = DEFAULT_MAX_AGE;
        }
        char max_age_str[32];
        snprintf(max_age_str, sizeof(max_age_str), "%d", max_age);
        http_response_set_header(res, "Access-Control-Max-Age", max_age_str);
    } else {
        /* For regular requests, set Access-Control-Expose-Headers if configured */
        if (!_is_empty(g_cors_config->expose_headers)) {
            http_response_set_header(res, "Access-Control-Expose-Headers", 
                                   g_cors_config->expose_headers);
        }
    }
}

/**
 * @brief CORS middleware handler function
 * @param req The HTTP request
 * @param res The HTTP response
 * @return true to continue the middleware chain, false to stop
 */
static bool _cors_middleware_handler(http_request_t *req, http_response_t *res) {
    if (req == NULL || res == NULL || g_cors_config == NULL) {
        return true; /* Continue chain on invalid input */
    }

    /* Get the Origin header from the request */
    const char *origin = http_request_get_header(req, "Origin");
    
    /* If no Origin header, this is not a CORS request - continue normally */
    if (_is_empty(origin)) {
        return true;
    }

    /* Check if the origin is allowed */
    bool origin_allowed = _is_origin_allowed(origin, g_cors_config->allowed_origins);
    
    /* If origin is not allowed, continue without setting CORS headers */
    if (!origin_allowed) {
        return true;
    }

    /* Check if this is a preflight OPTIONS request */
    /* Note: We need to check the request method. Since we don't have direct access,
     * we'll check for the Access-Control-Request-Method header which indicates preflight */
    const char *request_method = http_request_get_header(req, "Access-Control-Request-Method");
    bool is_preflight = !_is_empty(request_method);

    if (is_preflight) {
        /* This is a preflight request */
        _set_cors_headers(res, origin, true);
        
        /* Send 204 No Content response and stop the middleware chain */
        http_response_send_text(res, HTTP_NO_CONTENT, "");
        return false; /* Stop middleware chain */
    } else {
        /* Regular CORS request */
        _set_cors_headers(res, origin, false);
        return true; /* Continue middleware chain */
    }
}

/**
 * @brief Deep copy a NULL-terminated array of strings
 * @param src Source array (NULL-terminated)
 * @return Newly allocated copy, or NULL on failure
 */
static const char **_copy_string_array(const char **src) {
    if (src == NULL) {
        return NULL;
    }

    /* Count elements */
    size_t count = 0;
    while (src[count] != NULL) {
        count++;
    }

    /* Allocate array (count + 1 for NULL terminator) */
    const char **dst = (const char **)calloc(count + 1, sizeof(char *));
    if (dst == NULL) {
        return NULL;
    }

    /* Copy each string */
    for (size_t i = 0; i < count; i++) {
        dst[i] = strdup(src[i]);
        if (dst[i] == NULL) {
            /* Cleanup on failure */
            for (size_t j = 0; j < i; j++) {
                free((void *)dst[j]);
            }
            free(dst);
            return NULL;
        }
    }

    dst[count] = NULL; /* NULL terminator */
    return dst;
}

/**
 * @brief Duplicate a string (helper for optional strings)
 * @param src Source string (can be NULL)
 * @return Newly allocated copy, or NULL if src is NULL
 */
static char *_strdup_optional(const char *src) {
    if (src == NULL) {
        return NULL;
    }
    return strdup(src);
}

/**
 * @brief Create a CORS middleware function with the given configuration
 * @param options CORS configuration options
 * @return Middleware function pointer, or NULL on failure
 */
middleware_fn_t cors_middleware_create(const cors_options_t *options) {
    if (options == NULL) {
        return NULL;
    }

    /* Free any existing configuration */
    cors_middleware_destroy();

    /* Allocate new configuration */
    g_cors_config = (cors_options_t *)calloc(1, sizeof(cors_options_t));
    if (g_cors_config == NULL) {
        return NULL;
    }

    /* Deep copy the configuration */
    g_cors_config->allowed_origins = _copy_string_array(options->allowed_origins);
    g_cors_config->allowed_methods = _strdup_optional(options->allowed_methods);
    g_cors_config->allowed_headers = _strdup_optional(options->allowed_headers);
    g_cors_config->expose_headers = _strdup_optional(options->expose_headers);
    g_cors_config->allow_credentials = options->allow_credentials;
    g_cors_config->max_age = options->max_age;

    /* Return the middleware handler function */
    return _cors_middleware_handler;
}

/**
 * @brief Destroy CORS middleware resources
 */
void cors_middleware_destroy(void) {
    if (g_cors_config == NULL) {
        return;
    }

    /* Free allowed_origins array */
    if (g_cors_config->allowed_origins != NULL) {
        for (int i = 0; g_cors_config->allowed_origins[i] != NULL; i++) {
            free((void *)g_cors_config->allowed_origins[i]);
        }
        free((void *)g_cors_config->allowed_origins);
    }

    /* Free strings */
    free((void *)g_cors_config->allowed_methods);
    free((void *)g_cors_config->allowed_headers);
    free((void *)g_cors_config->expose_headers);

    /* Free the configuration structure */
    free(g_cors_config);
    g_cors_config = NULL;
}
