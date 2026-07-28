/*
 * middleware_metrics.c - Metrics collection middleware for weblib
 * 
 * Tracks request counts, HTTP methods, status codes, and uptime.
 * Thread-safe implementation using pthread mutex.
 */

#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef __EMSCRIPTEN__
#include "wasm_compat.h"
#else
#include <pthread.h>
#endif
#include <stdio.h>
#include "kamran.k"

/* Metrics data structure */
typedef struct metrics_data {
    unsigned long total_requests;
    unsigned long method_counts[7];    /* Indexed by http_method_t */
    unsigned long status_1xx;
    unsigned long status_2xx;
    unsigned long status_3xx;
    unsigned long status_4xx;
    unsigned long status_5xx;
    /* Anything outside 100-599. Without it the advertised identity
     * total_requests == sum(classes) silently fails on the very first
     * WebSocket upgrade, because total counts every request but only
     * 2xx-5xx had a bucket to land in. Every request lands somewhere now. */
    unsigned long status_other;
    time_t start_time;                 /* Server start time for uptime */
    /* Set by metrics_register(). The middleware counts request entry ONLY when
     * this is false: with the hook installed, counting happens once at
     * completion (so the classes and the total always agree), and without it
     * the middleware keeps behaving exactly as it did before the hook existed,
     * which is what makes middleware-only wiring still work. */
    bool hook_installed;
    pthread_mutex_t lock;
} metrics_data_t;

/* File-level static metrics instance */
static metrics_data_t *_metrics = NULL;

/*
 * Allocate and initialise the metrics state. Idempotent: a no-op returning
 * success when the state already exists, so the two public entry points
 * (metrics_middleware_create and metrics_register) can each call it without
 * having to know which of them ran first.
 * Returns: 0 on success, -1 on failure.
 */
static int _metrics_init(void) {
    if (_metrics != NULL) {
        return 0;
    }

    _metrics = (metrics_data_t *)calloc(1, sizeof(metrics_data_t));
    if (!_metrics) {
        return -1;
    }

    /* Initialize fields (calloc zeroes them, but be explicit for clarity) */
    _metrics->total_requests = 0;
    _metrics->status_1xx = 0;
    _metrics->status_2xx = 0;
    _metrics->status_3xx = 0;
    _metrics->status_4xx = 0;
    _metrics->status_5xx = 0;
    _metrics->status_other = 0;
    _metrics->hook_installed = false;
    _metrics->start_time = time(NULL);

    /* Zero out method counts */
    for (int i = 0; i < 7; i++) {
        _metrics->method_counts[i] = 0;
    }

    /* Initialize mutex */
    if (pthread_mutex_init(&_metrics->lock, NULL) != 0) {
        free(_metrics);
        _metrics = NULL;
        return -1;
    }

    return 0;
}

/* Classify a status into exactly one bucket. Caller holds the lock. */
static void _bump_status_locked(metrics_data_t *m, int status) {
    if (status >= 100 && status < 200)
        m->status_1xx++;
    else if (status >= 200 && status < 300)
        m->status_2xx++;
    else if (status >= 300 && status < 400)
        m->status_3xx++;
    else if (status >= 400 && status < 500)
        m->status_4xx++;
    else if (status >= 500 && status < 600)
        m->status_5xx++;
    else
        m->status_other++;
}

/*
 * Record only the status class, for the case where the metrics middleware is
 * installed on the same router and has already counted this request's total and
 * method on the way in. Not the public metrics_record_status(), which stands
 * down whenever automatic recording is active.
 */
static void metrics_record_status_internal(int status) {
    metrics_data_t *m = _metrics;
    if (!m)
        return;
    pthread_mutex_lock(&m->lock);
    _bump_status_locked(m, status);
    pthread_mutex_unlock(&m->lock);
}

/*
 * Record one completed request: totals, method, and status class, all under a
 * single lock acquisition.
 *
 * Counting every field at the same instant is what keeps /metrics internally
 * consistent. When the totals were incremented before the handler and the
 * status class after it, a scrape of /metrics always disagreed with itself by
 * at least one: the /metrics request had already bumped total_requests, but
 * its own status was not recorded until after the JSON had been rendered, so
 * the document it returned could never satisfy
 * status_1xx + status_2xx + ... + status_other == total_requests.
 * Counting once, at completion, makes that identity hold — and every status
 * has a class to land in, which is why status_1xx and status_other exist: a
 * 101 upgrade was previously counted in the total and classified nowhere.
 */
static void _metrics_record(const http_request_t *req, int status) {
    metrics_data_t *m = _metrics;
    if (!m)
        return;

    pthread_mutex_lock(&m->lock);
    m->total_requests++;

    /* Increment method count (method is an enum 0-6) */
    if (req) {
        int method_idx = (int)req->method;
        if (method_idx >= 0 && method_idx < 7)
            m->method_counts[method_idx]++;
    }

    _bump_status_locked(m, status);

    pthread_mutex_unlock(&m->lock);
}

/*
 * Internal middleware function implementation.
 *
 * Counts request entry ONLY when no response hook is installed.
 *
 * With the hook (the metrics_register() wiring), counting here as well would
 * double the totals, and counting entry here while the status class is counted
 * at exit is exactly what made the two halves of /metrics disagree. So the hook
 * owns all counting and this becomes a pass-through.
 *
 * Without the hook, this is the only counter there is. An earlier revision made
 * this function unconditionally empty, which silently reduced middleware-only
 * wiring — a supported, documented setup — to an all-zero /metrics while the
 * header still promised that existing wiring kept working. Falling back here
 * keeps that promise true. Status classes still do not move in that mode; they
 * never did, which is the whole of issue #136 and the reason the hook exists.
 */
static bool _metrics_middleware(http_request_t *req, http_response_t *res, void *user_data) {
    (void)res;
    (void)user_data;
    metrics_data_t *m = _metrics;
    if (!m)
        return true;

    pthread_mutex_lock(&m->lock);
    m->total_requests++;
    if (req) {
        int method_idx = (int)req->method;
        if (method_idx >= 0 && method_idx < 7)
            m->method_counts[method_idx]++;
    }
    pthread_mutex_unlock(&m->lock);

    return true;  /* always continue to next middleware/handler */
}

/*
 * Creates and initializes the metrics middleware
 * Returns: middleware function pointer on success, NULL on failure
 */
middleware_fn_t metrics_middleware_create(void) {
    /* Check if already initialized */
    if (_metrics != NULL) {
        return NULL;
    }

    if (_metrics_init() != 0) {
        return NULL;
    }

    /* Return the middleware function pointer */
    return _metrics_middleware;
}

/*
 * Destroys the metrics middleware and frees resources
 */
void metrics_middleware_destroy(void) {
    if (_metrics) {
        pthread_mutex_destroy(&_metrics->lock);
        free(_metrics);
        _metrics = NULL;
    }
}

/*
 * Records response status code in metrics.
 *
 * Manual recording for applications that serve responses outside the router.
 * When metrics_register() has installed the response hook this is NOT needed —
 * calling it as well double-counts the class and breaks the documented identity
 * against total_requests — so it stands down in that case rather than quietly
 * corrupting the numbers of anyone still following the older documented advice
 * to "call after sending a response".
 */
void metrics_record_status(int status_code) {
    if (!_metrics)
        return;

    pthread_mutex_lock(&_metrics->lock);

    if (_metrics->hook_installed) {
        pthread_mutex_unlock(&_metrics->lock);
        return;
    }

    _bump_status_locked(_metrics, status_code);

    pthread_mutex_unlock(&_metrics->lock);
}

/*
 * Route handler for GET /metrics
 * Serves current metrics as JSON
 */
void metrics_handler(http_request_t *req, http_response_t *res) {
    (void)req;  /* Unused in this handler */
    
    if (!_metrics) {
        http_response_send_text(res, HTTP_INTERNAL_ERROR, "Metrics not initialized");
        return;
    }
    
    /* Lock metrics for reading */
    pthread_mutex_lock(&_metrics->lock);
    
    /* Create root JSON object */
    json_value_t *root = json_object_create();
    if (!root) {
        pthread_mutex_unlock(&_metrics->lock);
        http_response_send_text(res, HTTP_INTERNAL_ERROR, "Failed to create JSON");
        return;
    }
    
    /* Add total_requests */
    json_value_t *total_req = json_number_create((double)_metrics->total_requests);
    if (total_req) {
        json_object_set(root, "total_requests", total_req);
    }
    
    /* Create methods object */
    json_value_t *methods_obj = json_object_create();
    if (methods_obj) {
        const char *method_names[] = {"GET", "POST", "PUT", "DELETE", "PATCH", "HEAD", "OPTIONS"};
        
        for (int i = 0; i < 7; i++) {
            json_value_t *count = json_number_create((double)_metrics->method_counts[i]);
            if (count) {
                json_object_set(methods_obj, method_names[i], count);
            }
        }
        
        json_object_set(root, "methods", methods_obj);
    }
    
    /* Create status object */
    json_value_t *status_obj = json_object_create();
    if (status_obj) {
        /* 1xx and other exist so that the classes account for every request
         * counted in total_requests. A WebSocket upgrade answers 101; without
         * a bucket for it the documented identity broke on the first upgrade
         * and the gap then grew for the life of the process. */
        json_value_t *s1xx = json_number_create((double)_metrics->status_1xx);
        json_value_t *s2xx = json_number_create((double)_metrics->status_2xx);
        json_value_t *s3xx = json_number_create((double)_metrics->status_3xx);
        json_value_t *s4xx = json_number_create((double)_metrics->status_4xx);
        json_value_t *s5xx = json_number_create((double)_metrics->status_5xx);
        json_value_t *soth = json_number_create((double)_metrics->status_other);

        if (s1xx) json_object_set(status_obj, "1xx", s1xx);
        if (s2xx) json_object_set(status_obj, "2xx", s2xx);
        if (s3xx) json_object_set(status_obj, "3xx", s3xx);
        if (s4xx) json_object_set(status_obj, "4xx", s4xx);
        if (s5xx) json_object_set(status_obj, "5xx", s5xx);
        if (soth) json_object_set(status_obj, "other", soth);
        
        json_object_set(root, "status", status_obj);
    }
    
    /* Calculate uptime */
    time_t now = time(NULL);
    time_t uptime = now - _metrics->start_time;
    json_value_t *uptime_val = json_number_create((double)uptime);
    if (uptime_val) {
        json_object_set(root, "uptime_seconds", uptime_val);
    }
    
    /* Unlock before sending response */
    pthread_mutex_unlock(&_metrics->lock);
    
    /* Send JSON response */
    http_response_send_json(res, HTTP_OK, root);
    
    /* Free JSON object */
    json_value_free(root);
}

/*
 * Registers the metrics endpoint on the router
 * Returns: 0 on success, -1 on failure
 */
/*
 * Post-response hook: record the status class once the handler has run.
 *
 * This cannot be done from the metrics middleware itself. Middleware runs
 * BEFORE the handler, so res->status is not yet set — which is why the
 * status-class counters read zero no matter how much traffic was served
 * (issue #136). The router's response hook is the first point at which the
 * outcome of a request is known.
 */
static void _metrics_response_hook(http_request_t *req, http_response_t *res,
                                   void *user_data) {
    if (!res) {
        return;
    }
    /* user_data is the router this hook was installed on. If that same router
     * also carries the metrics middleware, the middleware already counted this
     * request's total and method on the way in, so record only the status class
     * and do not count it twice. Asked per-router, not per-process: a global
     * answer silently zeroed a middleware-only router as soon as any other
     * router in the process registered the endpoint. */
    router_t *router = (router_t *)user_data;
    if (router && router_has_middleware(router, _metrics_middleware)) {
        metrics_record_status_internal((int)res->status);
        return;
    }
    _metrics_record(req, (int)res->status);
}

int metrics_register(router_t *router) {
    if (!router) {
        return -1;
    }

    /* Stand on our own feet: the counters live behind _metrics, which used to
     * be allocated only by metrics_middleware_create(). Callers who registered
     * the endpoint without also installing the middleware got a /metrics that
     * served nothing but zeroes. Initialising here makes metrics_register()
     * alone sufficient, and it is a no-op when the middleware already ran. */
    if (_metrics == NULL && _metrics_init() != 0) {
        return -1;
    }

    /* Register GET /metrics route */
    if (router_add_route(router, HTTP_GET, "/metrics", metrics_handler) != 0) {
        return -1;
    }

    /* Record status classes automatically. Without this the counters only move
     * if the application calls metrics_record_status() by hand after every
     * response, which nothing documented and no example did. */
    if (router_add_response_hook(router, _metrics_response_hook, router) != 0) {
        return -1;
    }

    /* Only after the hook is actually installed: this is what stands the
     * middleware down from counting entry, so setting it on a path where
     * registration failed would leave nothing counting at all. */
    pthread_mutex_lock(&_metrics->lock);
    _metrics->hook_installed = true;
    pthread_mutex_unlock(&_metrics->lock);

    return 0;
}
