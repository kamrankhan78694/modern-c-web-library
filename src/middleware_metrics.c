/*
 * middleware_metrics.c - Metrics collection middleware for weblib
 * 
 * Tracks request counts, HTTP methods, status codes, and uptime.
 * Thread-safe implementation using pthread mutex.
 */

#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <stdio.h>
#include "weblib.h"

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

/* Internal middleware function implementation */
static bool _metrics_middleware(http_request_t *req, http_response_t *res) {
    (void)res;
    if (!_metrics)
        return true;
    
    pthread_mutex_lock(&_metrics->lock);
    _metrics->total_requests++;
    
    /* Increment method count (method is an enum 0-6) */
    int method_idx = (int)req->method;
    if (method_idx >= 0 && method_idx < 7)
        _metrics->method_counts[method_idx]++;
    
    pthread_mutex_unlock(&_metrics->lock);
    
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
    
    /* Allocate metrics structure */
    _metrics = (metrics_data_t *)calloc(1, sizeof(metrics_data_t));
    if (!_metrics) {
        return NULL;
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
int metrics_register(router_t *router) {
    if (!router) {
        return -1;
    }
    
    /* Register GET /metrics route */
    if (router_add_route(router, HTTP_GET, "/metrics", metrics_handler) != 0) {
        return -1;
    }
    
    return 0;
}
