/*
 * health_check.c — Health check endpoint for observability.
 *
 * Provides a lightweight /healthz handler that returns JSON with server
 * status and uptime.  Designed for load balancers, Kubernetes probes, and
 * monitoring dashboards.
 */
#include "kamran.k"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>

/*
 * Module-level start time.
 *
 * This used to be initialised by pthread_once on the FIRST handler call, which
 * meant /healthz reported time-since-first-probe rather than uptime: a server
 * up for an hour but never probed answered 0, and thereafter counted from the
 * first probe. For an endpoint whose stated purpose is load-balancer and
 * Kubernetes probes that is worse than useless — it looks plausible, because
 * once probes arrive on a schedule the number grows normally.
 *
 * health_check_register() now stamps it at wiring time, which is server start
 * for any realistic program. The pthread_once remains as a fallback so calling
 * health_check_handler() directly (as the unit tests do) still yields a sane
 * value rather than an epoch-sized one.
 */
static time_t _health_start_time = 0;
static pthread_once_t _health_once = PTHREAD_ONCE_INIT;

static void _health_init_start_time(void) {
    if (_health_start_time == 0) {
        _health_start_time = time(NULL);
    }
}

/* ------------------------------------------------------------------ */
/* Route handler: GET /healthz                                        */
/* ------------------------------------------------------------------ */
void health_check_handler(http_request_t *req, http_response_t *res) {
    (void)req;

    pthread_once(&_health_once, _health_init_start_time);

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

    /* set_header AFTER send_text: send_text sets text/plain (replacing, since
     * BUG-11), and this replaces it again with the JSON type. Before BUG-11 was
     * fixed this ordering was load-bearing — send_text APPENDED, so the reverse
     * order emitted two conflicting Content-Type headers (audit #34). */
    http_response_send_text(res, HTTP_OK, body);
    http_response_set_header(res, "Content-Type", "application/json");
    free(body);
}

/* ------------------------------------------------------------------ */
/* Convenience: register /healthz on an existing router               */
/* ------------------------------------------------------------------ */
int health_check_register(router_t *router) {
    if (!router) return -1;
    /* Stamp the start time at wiring time, not on the first probe. */
    pthread_once(&_health_once, _health_init_start_time);
    return router_add_route(router, HTTP_GET, "/healthz", health_check_handler);
}
