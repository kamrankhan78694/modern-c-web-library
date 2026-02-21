/**
 * @file middleware_log.c
 * @brief Request logging middleware — Phase 8
 *
 * Emits one log line per incoming request:
 *   [YYYY-MM-DD HH:MM:SS] LEVEL  METHOD /path
 *
 * Configurable minimum log level and output stream.
 * Zero external dependencies; pure C.
 */

#include "weblib.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Global configuration (one instance per process, same pattern as other middlewares) */
static log_config_t g_log_config = {LOG_LEVEL_INFO, NULL};

/* Level label strings — indexed by log_level_t */
static const char *const LOG_LEVEL_LABELS[] = {
    "DEBUG",
    "INFO ",
    "WARN ",
    "ERROR"
};

/* Map HTTP method enum to string */
static const char *_method_str(http_method_t m) {
    switch (m) {
        case HTTP_GET:     return "GET";
        case HTTP_POST:    return "POST";
        case HTTP_PUT:     return "PUT";
        case HTTP_DELETE:  return "DELETE";
        case HTTP_PATCH:   return "PATCH";
        case HTTP_HEAD:    return "HEAD";
        case HTTP_OPTIONS: return "OPTIONS";
        default:           return "UNKNOWN";
    }
}

/**
 * @brief The actual middleware handler function
 */
static bool _log_middleware_handler(http_request_t *req, http_response_t *res, void *user_data) {
    if (req == NULL) {
        return true;
    }

    log_config_t *config = user_data ? (log_config_t *)user_data : &g_log_config;

    /* Only emit if request-level >= configured minimum */
    if (LOG_LEVEL_INFO < config->level) {
        return true;
    }

    FILE *out = config->output ? config->output : stderr;

    /* Format timestamp */
    time_t now = time(NULL);
    struct tm tm_buf;
    struct tm *tm_info = NULL;
#ifndef _WIN32
    tm_info = gmtime_r(&now, &tm_buf);
#else
    /* Use thread-safe variant on Windows */
    if (gmtime_s(&tm_buf, &now) == 0) {
        tm_info = &tm_buf;
    }
#endif

    char ts[32] = "0000-00-00 00:00:00";
    if (tm_info) {
        strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", tm_info);
    }

    const char *path = req->path ? req->path : "/";
    const char *method = _method_str(req->method);

    fprintf(out, "[%s] %s  %s %s\n", ts, LOG_LEVEL_LABELS[LOG_LEVEL_INFO], method, path);
    fflush(out);

    (void)res;
    return true; /* Always continue chain */
}

/**
 * @brief Create a logging middleware with the given configuration
 */
middleware_fn_t log_middleware_create(const log_config_t *config) {
    if (config != NULL) {
        g_log_config.level  = config->level;
        g_log_config.output = config->output;
    } else {
        g_log_config.level  = LOG_LEVEL_INFO;
        g_log_config.output = NULL; /* defaults to stderr in handler */
    }
    return _log_middleware_handler;
}

/**
 * @brief Destroy logging middleware and reset configuration
 */
void log_middleware_destroy(void) {
    g_log_config.level  = LOG_LEVEL_INFO;
    g_log_config.output = NULL;
}
