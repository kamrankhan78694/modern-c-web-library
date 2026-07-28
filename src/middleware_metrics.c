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
    unsigned long status_2xx;
    unsigned long status_3xx;
    unsigned long status_4xx;
    unsigned long status_5xx;
    time_t start_time;                 /* Server start time for uptime */
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
    _metrics->status_2xx = 0;
    _metrics->status_3xx = 0;
    _metrics->status_4xx = 0;
    _metrics->status_5xx = 0;
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
 * status_2xx + status_3xx + status_4xx + status_5xx == total_requests.
 * Counting once, at completion, makes that identity hold.
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

    if (status >= 200 && status < 300)
        m->status_2xx++;
    else if (status >= 300 && status < 400)
        m->status_3xx++;
    else if (status >= 400 && status < 500)
        m->status_4xx++;
    else if (status >= 500 && status < 600)
        m->status_5xx++;

    pthread_mutex_unlock(&m->lock);
}

/*
 * Internal middleware function implementation.
 *
 * This deliberately counts nothing. Middleware runs BEFORE the handler, so the
 * only fields it could record are the ones knowable at entry — and recording
 * those here while the status class is recorded at exit is precisely what made
 * the two halves of /metrics disagree. All counting now happens once, in the
 * response hook installed by metrics_register(). The middleware is retained so
 * existing wiring keeps compiling and running.
 */
static bool _metrics_middleware(http_request_t *req, http_response_t *res, void *user_data) {
    (void)req;
    (void)res;
    (void)user_data;
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
 * Records response status code in metrics
 */
void metrics_record_status(int status_code) {
    if (!_metrics)
        return;
    
    pthread_mutex_lock(&_metrics->lock);
    
    if (status_code >= 200 && status_code < 300)
        _metrics->status_2xx++;
    else if (status_code >= 300 && status_code < 400)
        _metrics->status_3xx++;
    else if (status_code >= 400 && status_code < 500)
        _metrics->status_4xx++;
    else if (status_code >= 500 && status_code < 600)
        _metrics->status_5xx++;
    
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
        json_value_t *s2xx = json_number_create((double)_metrics->status_2xx);
        json_value_t *s3xx = json_number_create((double)_metrics->status_3xx);
        json_value_t *s4xx = json_number_create((double)_metrics->status_4xx);
        json_value_t *s5xx = json_number_create((double)_metrics->status_5xx);
        
        if (s2xx) json_object_set(status_obj, "2xx", s2xx);
        if (s3xx) json_object_set(status_obj, "3xx", s3xx);
        if (s4xx) json_object_set(status_obj, "4xx", s4xx);
        if (s5xx) json_object_set(status_obj, "5xx", s5xx);
        
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
    (void)user_data;
    if (res) {
        _metrics_record(req, (int)res->status);
    }
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
    if (router_add_response_hook(router, _metrics_response_hook, NULL) != 0) {
        return -1;
    }
    
    return 0;
}
