#include "weblib.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_ROUTES 256
#define MAX_MIDDLEWARES 32

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
    size_t middleware_count;
};

/* Internal functions */
static bool match_route(const char *pattern, const char *path);
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
    if (!router || !middleware) {
        return -1;
    }
    
    if (router->middleware_count >= MAX_MIDDLEWARES) {
        fprintf(stderr, "Max middlewares exceeded\n");
        return -1;
    }
    
    router->middlewares[router->middleware_count] = middleware;
    router->middleware_count++;
    
    return 0;
}

/* Route request */
int router_route(router_t *router, http_request_t *req, http_response_t *res) {
    if (!router || !req || !res) {
        return -1;
    }
    
    /* Execute middlewares */
    for (size_t i = 0; i < router->middleware_count; i++) {
        if (!router->middlewares[i](req, res)) {
            /* Middleware stopped the chain */
            return 0;
        }
    }
    
    /* Find matching route */
    for (size_t i = 0; i < router->route_count; i++) {
        route_t *route = &router->routes[i];
        
        /* Check method */
        if (route->method != req->method) {
            continue;
        }
        
        /* Check path */
        if (route->has_params) {
            if (match_route(route->path, req->path)) {
                extract_params(req, route->path, req->path);
                route->handler(req, res);
                return 0;
            }
        } else {
            if (strcmp(route->path, req->path) == 0) {
                route->handler(req, res);
                return 0;
            }
        }
    }
    
    /* No route found */
    http_response_send_text(res, HTTP_NOT_FOUND, "Not Found");
    return -1;
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
    
    char *pattern_token = strtok(pattern_copy, "/");
    char *path_token = strtok(path_copy, "/");
    
    bool match = true;
    
    while (pattern_token && path_token) {
        /* If pattern segment starts with ':', it's a parameter - matches anything */
        if (pattern_token[0] != ':') {
            if (strcmp(pattern_token, path_token) != 0) {
                match = false;
                break;
            }
        }
        
        pattern_token = strtok(NULL, "/");
        path_token = strtok(NULL, "/");
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
    
    /* TODO: Implement parameter extraction and storage */
    /* This would extract values like "123" from /users/123 when pattern is /users/:id */
    (void)req;
    (void)pattern;
    (void)path;
}
