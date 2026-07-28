#include "kamran.k"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>

#define MAX_ROUTES 256
#define MAX_MIDDLEWARES 32
#define MAX_RESPONSE_HOOKS 8

/* Route structure */
struct route {
    http_method_t method;
    char *path;
    route_handler_t handler;
    bool has_params;
};

/* Router structure */
struct router {
    route_t routes[MAX_ROUTES];
    size_t route_count;
    middleware_fn_t middlewares[MAX_MIDDLEWARES];
    void *middleware_data[MAX_MIDDLEWARES];
    size_t middleware_count;
    response_hook_fn_t response_hooks[MAX_RESPONSE_HOOKS];
    void *response_hook_data[MAX_RESPONSE_HOOKS];
    size_t response_hook_count;
};

/* Internal functions */
static bool match_route(const char *pattern, const char *path);
static void run_response_hooks(router_t *router, http_request_t *req, http_response_t *res);
/*
 * Percent-decode a path segment in place.
 *
 * Path parameters used to be handed to handlers raw, so /api/greet/hello%20world
 * yielded "hello%20world". Every handler had to decode by hand, and most would
 * not — which is also a validation hazard, since length and charset checks then
 * run against the encoded form.
 *
 * Decoding happens AFTER route matching, on the extracted value only. Matching
 * still runs on the raw path, so this cannot re-open path traversal: a %2F in a
 * segment never becomes a separator the router acted on.
 *
 * Output is never longer than input, so in-place is safe. A malformed escape
 * ("%zz", a truncated "%4") is left verbatim rather than guessed at. "+" is left
 * alone: it means space in a query string, not in a path (RFC 3986).
 *
 * An encoded NUL (%00) is REFUSED — decoding it would truncate the C string and
 * silently shorten a value that validation had already accepted at full length.
 * Returns false in that case and the caller stores nothing.
 */
static bool percent_decode_inplace(char *s) {
    char *out = s;
    for (const char *in = s; *in; ) {
        if (in[0] == '%' && isxdigit((unsigned char)in[1]) && isxdigit((unsigned char)in[2])) {
            char hex[3] = { in[1], in[2], '\0' };
            long v = strtol(hex, NULL, 16);
            if (v == 0) {
                return false;                  /* embedded NUL — refuse */
            }
            *out++ = (char)v;
            in += 3;
        } else {
            *out++ = *in++;
        }
    }
    *out = '\0';
    return true;
}

static void extract_params(http_request_t *req, const char *pattern, const char *path);

/* Create router */
router_t *router_create(void) {
    router_t *router = (router_t *)calloc(1, sizeof(router_t));
    if (!router) {
        return NULL;
    }
    
    router->route_count = 0;
    router->middleware_count = 0;
    
    return router;
}

/* Add route */
int router_add_route(router_t *router, http_method_t method, const char *path, route_handler_t handler) {
    if (!router || !path || !handler) {
        return -1;
    }
    
    if (router->route_count >= MAX_ROUTES) {
        fprintf(stderr, "Max routes exceeded\n");
        return -1;
    }
    
    route_t *route = &router->routes[router->route_count];
    route->method = method;
    route->path = strdup(path);
    if (!route->path) {
        fprintf(stderr, "Failed to allocate memory for route path\n");
        return -1;
    }
    route->handler = handler;
    route->has_params = (strchr(path, ':') != NULL);
    
    router->route_count++;
    
    return 0;
}

/* Add middleware */
int router_use_middleware(router_t *router, middleware_fn_t middleware) {
    return router_use_middleware_with_data(router, middleware, NULL);
}

/* Add middleware with per-instance data */
int router_use_middleware_with_data(router_t *router, middleware_fn_t middleware, void *user_data) {
    if (!router || !middleware) {
        return -1;
    }
    
    if (router->middleware_count >= MAX_MIDDLEWARES) {
        fprintf(stderr, "Max middlewares exceeded\n");
        return -1;
    }
    
    router->middlewares[router->middleware_count] = middleware;
    router->middleware_data[router->middleware_count] = user_data;
    router->middleware_count++;
    
    return 0;
}

/* Route request.
 * Returns: 0 = routed, 1 = no route found (404 already sent), -1 = invalid input */
int router_add_response_hook(router_t *router, response_hook_fn_t hook,
                             void *user_data) {
    if (!router || !hook) {
        return -1;
    }
    if (router->response_hook_count >= MAX_RESPONSE_HOOKS) {
        return -1;
    }
    router->response_hooks[router->response_hook_count] = hook;
    router->response_hook_data[router->response_hook_count] = user_data;
    router->response_hook_count++;
    return 0;
}

/*
 * Run the post-response hooks. Called at every router_route() exit that
 * produced a response, and nowhere else.
 *
 * The status guard matters: a middleware may stop the chain without sending
 * anything, leaving status at its zero-initialised value. Reporting that as a
 * real status would put garbage in whatever the hook feeds.
 */
static void run_response_hooks(router_t *router, http_request_t *req,
                               http_response_t *res) {
    if (res->status == 0) {
        return;
    }
    for (size_t i = 0; i < router->response_hook_count; i++) {
        router->response_hooks[i](req, res, router->response_hook_data[i]);
    }
}

int router_route(router_t *router, http_request_t *req, http_response_t *res) {
    if (!router || !req || !res || !req->path) {
        return -1;
    }
    
    /* Execute middlewares */
    for (size_t i = 0; i < router->middleware_count; i++) {
        if (!router->middlewares[i](req, res, router->middleware_data[i])) {
            /* Middleware stopped the chain — it may have sent a response
             * (a 429 from the rate limiter, a 403 from auth), so the hooks
             * still need to see the outcome. */
            run_response_hooks(router, req, res);
            return 0;
        }
        /* If middleware already sent a response, stop */
        if (res->sent) {
            run_response_hooks(router, req, res);
            return 0;
        }
    }
    
    /*
     * Find matching route.
     *
     * RFC 9110 §9.3.2: HEAD is identical to GET except that the server must not
     * send a body. A HEAD request therefore matches a GET route when no explicit
     * HEAD route exists — previously HEAD / returned 404 on a path where GET /
     * returned 200. The handler runs normally and produces a body; the server
     * drops it at send time while keeping Content-Length, so the response
     * describes the resource exactly as GET would.
     */
    http_method_t match_method = req->method;
    for (int pass = 0; pass < 2; pass++) {
    for (size_t i = 0; i < router->route_count; i++) {
        route_t *route = &router->routes[i];
        
        /* Check method */
        if (route->method != match_method) {
            continue;
        }
        
        /* Check path */
        if (route->has_params) {
            if (match_route(route->path, req->path)) {
                extract_params(req, route->path, req->path);
                route->handler(req, res);
                run_response_hooks(router, req, res);
                return 0;
            }
        } else {
            if (strcmp(route->path, req->path) == 0) {
                route->handler(req, res);
                run_response_hooks(router, req, res);
                return 0;
            }
        }
    }
        /* Nothing matched as HEAD — retry the search as GET, once. */
        if (pass == 0 && req->method == HTTP_HEAD) {
            match_method = HTTP_GET;
        } else {
            break;
        }
    }
    
    /* No route found */
    http_response_send_text(res, HTTP_NOT_FOUND, "Not Found");
    run_response_hooks(router, req, res);
    return 1; /* Distinct from -1 (invalid input) */
}

/* Destroy router */
void router_destroy(router_t *router) {
    if (!router) {
        return;
    }
    
    /* Free route paths */
    for (size_t i = 0; i < router->route_count; i++) {
        free(router->routes[i].path);
    }
    
    free(router);
}

/* Match route pattern with path (supports :param syntax) */
static bool match_route(const char *pattern, const char *path) {
    if (!pattern || !path) {
        return false;
    }
    
    /* Simple implementation - split by '/' and match segments */
    char *pattern_copy = strdup(pattern);
    char *path_copy = strdup(path);
    
    if (!pattern_copy || !path_copy) {
        free(pattern_copy);
        free(path_copy);
        return false;
    }
    
    /* Use strtok_r for thread-safe tokenization */
    char *pattern_saveptr = NULL;
    char *path_saveptr = NULL;
    char *pattern_token = strtok_r(pattern_copy, "/", &pattern_saveptr);
    char *path_token = strtok_r(path_copy, "/", &path_saveptr);
    
    bool match = true;
    
    while (pattern_token && path_token) {
        /* If pattern segment starts with ':' and has a name, it's a parameter - matches anything */
        if (pattern_token[0] != ':' || pattern_token[1] == '\0') {
            if (strcmp(pattern_token, path_token) != 0) {
                match = false;
                break;
            }
        }
        
        pattern_token = strtok_r(NULL, "/", &pattern_saveptr);
        path_token = strtok_r(NULL, "/", &path_saveptr);
    }
    
    /* Both must be exhausted for a match */
    if (pattern_token != NULL || path_token != NULL) {
        match = false;
    }
    
    free(pattern_copy);
    free(path_copy);
    
    return match;
}

/* Extract route parameters */
static void extract_params(http_request_t *req, const char *pattern, const char *path) {
    if (!req || !pattern || !path) {
        return;
    }

    char *pattern_copy = strdup(pattern);
    char *path_copy = strdup(path);
    if (!pattern_copy || !path_copy) {
        free(pattern_copy);
        free(path_copy);
        return;
    }

    char *pattern_save = NULL;
    char *path_save = NULL;
    char *p_tok = strtok_r(pattern_copy, "/", &pattern_save);
    char *s_tok = strtok_r(path_copy, "/", &path_save);

    while (p_tok && s_tok) {
        if (p_tok[0] == ':' && p_tok[1] != '\0') {
            const char *key = p_tok + 1;
            /* Decode the segment before handing it to the handler. s_tok points
             * into path_copy, which we own, so decoding in place is safe. */
            if (percent_decode_inplace(s_tok)) {
                http_request_set_param(req, key, s_tok);
            }
        }
        p_tok = strtok_r(NULL, "/", &pattern_save);
        s_tok = strtok_r(NULL, "/", &path_save);
    }

    free(pattern_copy);
    free(path_copy);
}
