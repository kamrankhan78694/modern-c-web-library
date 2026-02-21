/**
 * @file middleware_error.c
 * @brief Centralized error-handler middleware — Phase 8
 *
 * When a response status of 4xx or 5xx has been set but no body has been
 * written, this middleware fills in a standard JSON error body:
 *
 *   {"error": "<reason phrase>", "status": <code>}
 *
 * A custom error_handler_fn_t callback can be provided to override the
 * default behaviour.
 *
 * Note: because middleware_fn_t runs *before* the route handler this
 * middleware registers itself, but cannot wrap the handler call.  Instead
 * it pre-seeds the response with the error body when the response has
 * already been populated with an error status (e.g., by a prior middleware
 * such as auth or rate-limiting that stopped the chain and set a 4xx status).
 * For post-handler error normalisation, call
 * error_handler_apply(req, res) directly from a route handler or from a
 * custom wrapper.
 */

#include "weblib.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Global state */
static error_handler_config_t g_error_config = {NULL};

/**
 * @brief Return a human-readable reason phrase for an HTTP status code.
 */
static const char *_error_reason(http_status_t status) {
    switch (status) {
        case HTTP_BAD_REQUEST:              return "Bad Request";
        case HTTP_UNAUTHORIZED:            return "Unauthorized";
        case HTTP_FORBIDDEN:               return "Forbidden";
        case HTTP_NOT_FOUND:               return "Not Found";
        case HTTP_METHOD_NOT_ALLOWED:      return "Method Not Allowed";
        case HTTP_PAYLOAD_TOO_LARGE:       return "Payload Too Large";
        case HTTP_URI_TOO_LONG:            return "URI Too Long";
        case HTTP_TOO_MANY_REQUESTS:       return "Too Many Requests";
        case HTTP_HEADER_FIELDS_TOO_LARGE: return "Request Header Fields Too Large";
        case HTTP_INTERNAL_ERROR:          return "Internal Server Error";
        case HTTP_NOT_IMPLEMENTED:         return "Not Implemented";
        case HTTP_BAD_GATEWAY:             return "Bad Gateway";
        case HTTP_SERVICE_UNAVAILABLE:     return "Service Unavailable";
        default:
            if (status >= 400 && status < 500) return "Client Error";
            if (status >= 500 && status < 600) return "Server Error";
            return "Error";
    }
}

/**
 * @brief Apply the error handler to req/res if the status indicates an error.
 *
 * This is a public helper so route handlers can call it explicitly.
 */
void error_handler_apply(http_request_t *req, http_response_t *res) {
    if (res == NULL) {
        return;
    }

    int status = (int)res->status;
    if (status < 400) {
        return; /* Not an error status */
    }

    if (g_error_config.handler != NULL) {
        /* Delegate to custom handler */
        g_error_config.handler(req, res, (http_status_t)status);
        return;
    }

    /* Built-in: write JSON error body only if no body has been set yet */
    if (res->body != NULL && res->body_length > 0) {
        return;
    }

    const char *reason = _error_reason((http_status_t)status);

    /* Build: {"error":"<reason>","status":<code>} */
    char body[256];
    int n = snprintf(body, sizeof(body),
                     "{\"error\":\"%s\",\"status\":%d}", reason, status);
    if (n <= 0 || (size_t)n >= sizeof(body)) {
        return;
    }

    char *copy = (char *)malloc((size_t)n + 1);
    if (!copy) {
        return;
    }
    memcpy(copy, body, (size_t)n + 1);

    free(res->body);
    res->body = copy;
    res->body_length = (size_t)n;
    http_response_set_header(res, "Content-Type", "application/json");
}

/**
 * @brief Middleware handler function
 *
 * Runs before the route handler. When a prior middleware has already set an
 * error status (e.g., rate limiter → 429, auth → 401) but allowed the chain
 * to continue, this middleware will normalise that error via
 * error_handler_apply(). If an earlier middleware both sets the error and
 * marks the response as sent, the chain will have stopped before we run.
 *
 * Note: Errors produced by the route handler itself still require an explicit
 * call to error_handler_apply() from user code or from server-side send logic.
 */
static bool _error_handler_middleware(http_request_t *req, http_response_t *res, void *user_data) {
    (void)user_data;
    if (res != NULL) {
        error_handler_apply(req, res);
    }
    /* Always continue; this middleware never short-circuits the chain */
    return true;
}

/**
 * @brief Create the error handler middleware
 */
middleware_fn_t error_handler_middleware_create(const error_handler_config_t *config) {
    if (config != NULL) {
        g_error_config.handler = config->handler;
    } else {
        g_error_config.handler = NULL;
    }
    return _error_handler_middleware;
}

/**
 * @brief Destroy error handler middleware and reset state
 */
void error_handler_middleware_destroy(void) {
    g_error_config.handler = NULL;
}
