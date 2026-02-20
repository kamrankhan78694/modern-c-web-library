/*
 * health_check.c — Health check endpoint for observability.
 *
 * Provides a lightweight /healthz handler that returns JSON with server
 * status and uptime.  Designed for load balancers, Kubernetes probes, and
 * monitoring dashboards.
 */
#include "weblib.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Module-level start time, set on first handler invocation */
static time_t _health_start_time = 0;

/* ------------------------------------------------------------------ */
/* Route handler: GET /healthz                                        */
/* ------------------------------------------------------------------ */
void health_check_handler(http_request_t *req, http_response_t *res) {
    (void)req;

    if (_health_start_time == 0) {
        _health_start_time = time(NULL);
    }

    time_t now = time(NULL);
    long uptime_sec = (long)(now - _health_start_time);

    json_value_t *obj = json_object_create();
    if (!obj) {
        http_response_send_text(res, HTTP_INTERNAL_ERROR,
                                "{\"error\":\"out of memory\"}");
        return;
    }

    json_object_set(obj, "status", json_string_create("ok"));
    json_object_set(obj, "uptime_seconds", json_number_create((double)uptime_sec));

    char *body = json_stringify(obj);
    json_value_free(obj);

    if (!body) {
        http_response_send_text(res, HTTP_INTERNAL_ERROR,
                                "{\"error\":\"serialization failed\"}");
        return;
    }

    http_response_set_header(res, "Content-Type", "application/json");
    http_response_send_text(res, HTTP_OK, body);
    free(body);
}

/* ------------------------------------------------------------------ */
/* Convenience: register /healthz on an existing router               */
/* ------------------------------------------------------------------ */
int health_check_register(router_t *router) {
    if (!router) return -1;
    return router_add_route(router, HTTP_GET, "/healthz", health_check_handler);
}
