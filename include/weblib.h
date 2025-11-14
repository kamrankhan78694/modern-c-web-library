#ifndef WEBLIB_H
#define WEBLIB_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/* HTTP Methods */
typedef enum {
    HTTP_GET,
    HTTP_POST,
    HTTP_PUT,
    HTTP_DELETE,
    HTTP_PATCH,
    HTTP_HEAD,
    HTTP_OPTIONS
} http_method_t;

/* HTTP Status Codes */
typedef enum {
    HTTP_OK = 200,
    HTTP_CREATED = 201,
    HTTP_ACCEPTED = 202,
    HTTP_NO_CONTENT = 204,
    HTTP_BAD_REQUEST = 400,
    HTTP_UNAUTHORIZED = 401,
    HTTP_FORBIDDEN = 403,
    HTTP_NOT_FOUND = 404,
    HTTP_METHOD_NOT_ALLOWED = 405,
    HTTP_INTERNAL_ERROR = 500,
    HTTP_NOT_IMPLEMENTED = 501,
    HTTP_BAD_GATEWAY = 502,
    HTTP_SERVICE_UNAVAILABLE = 503
} http_status_t;

/* Forward declarations */
typedef struct http_request http_request_t;
typedef struct http_response http_response_t;
typedef struct http_server http_server_t;
typedef struct router router_t;
typedef struct route route_t;
typedef struct middleware middleware_t;
typedef struct json_value json_value_t;
typedef struct event_loop event_loop_t;
typedef struct event_handler event_handler_t;

/* HTTP Request structure */
struct http_request {
    http_method_t method;
    char *path;
    char *query_string;
    char *body;
    size_t body_length;
    void *headers;  /* Hash map of headers */
    void *params;   /* Route parameters */
    void *user_data; /* For middleware context */
};

/* HTTP Response structure */
struct http_response {
    http_status_t status;
    char *body;
    size_t body_length;
    void *headers;  /* Hash map of headers */
    bool sent;
};

/* Route handler callback */
typedef void (*route_handler_t)(http_request_t *req, http_response_t *res);

/* Middleware callback - return true to continue, false to stop */
typedef bool (*middleware_fn_t)(http_request_t *req, http_response_t *res);

/* JSON Value Types */
typedef enum {
    JSON_NULL,
    JSON_BOOL,
    JSON_NUMBER,
    JSON_STRING,
    JSON_ARRAY,
    JSON_OBJECT
} json_type_t;

/* JSON Value */
struct json_value {
    json_type_t type;
    union {
        bool bool_val;
        double number_val;
        char *string_val;
        void *array_val;
        void *object_val;
    } data;
};

/* ===== HTTP Server API ===== */

/**
 * Create a new HTTP server
 * @return Pointer to server instance or NULL on failure
 */
http_server_t *http_server_create(void);

/**
 * Start the HTTP server on specified port
 * @param server Server instance
 * @param port Port number to listen on
 * @return 0 on success, -1 on failure
 */
int http_server_listen(http_server_t *server, uint16_t port);

/**
 * Stop the HTTP server
 * @param server Server instance
 */
void http_server_stop(http_server_t *server);

/**
 * Destroy the HTTP server and free resources
 * @param server Server instance
 */
void http_server_destroy(http_server_t *server);

/**
 * Set the router for the server
 * @param server Server instance
 * @param router Router instance
 */
void http_server_set_router(http_server_t *server, router_t *router);

/* ===== Router API ===== */

/**
 * Create a new router
 * @return Pointer to router instance or NULL on failure
 */
router_t *router_create(void);

/**
 * Add a route to the router
 * @param router Router instance
 * @param method HTTP method
 * @param path Route path (supports parameters like /users/:id)
 * @param handler Route handler function
 * @return 0 on success, -1 on failure
 */
int router_add_route(router_t *router, http_method_t method, const char *path, route_handler_t handler);

/**
 * Add middleware to the router (executes before all routes)
 * @param router Router instance
 * @param middleware Middleware function
 * @return 0 on success, -1 on failure
 */
int router_use_middleware(router_t *router, middleware_fn_t middleware);

/**
 * Route an incoming request
 * @param router Router instance
 * @param req Request object
 * @param res Response object
 * @return 0 if route found and handled, -1 if not found
 */
int router_route(router_t *router, http_request_t *req, http_response_t *res);

/**
 * Destroy router and free resources
 * @param router Router instance
 */
void router_destroy(router_t *router);

/* ===== Request/Response Helpers ===== */

/**
 * Get header value from request
 * @param req Request object
 * @param key Header key
 * @return Header value or NULL if not found
 */
const char *http_request_get_header(http_request_t *req, const char *key);

/**
 * Get route parameter value
 * @param req Request object
 * @param key Parameter key
 * @return Parameter value or NULL if not found
 */
const char *http_request_get_param(http_request_t *req, const char *key);

/**
 * Set route parameter value (used by router during matching)
 * @param req Request object
 * @param key Parameter key
 * @param value Parameter value
 * @return 0 on success, -1 on failure
 */
int http_request_set_param(http_request_t *req, const char *key, const char *value);

/**
 * Set response header
 * @param res Response object
 * @param key Header key
 * @param value Header value
 */
void http_response_set_header(http_response_t *res, const char *key, const char *value);

/**
 * Send text response
 * @param res Response object
 * @param status HTTP status code
 * @param text Response text
 */
void http_response_send_text(http_response_t *res, http_status_t status, const char *text);

/**
 * Send JSON response
 * @param res Response object
 * @param status HTTP status code
 * @param json JSON value to send
 */
void http_response_send_json(http_response_t *res, http_status_t status, json_value_t *json);

/* ===== JSON API ===== */

/**
 * Parse JSON string into JSON value
 * @param json_str JSON string
 * @return JSON value or NULL on parse error
 */
json_value_t *json_parse(const char *json_str);

/**
 * Create JSON object
 * @return JSON object value
 */
json_value_t *json_object_create(void);

/**
 * Set property in JSON object
 * @param obj JSON object
 * @param key Property key
 * @param value JSON value
 */
void json_object_set(json_value_t *obj, const char *key, json_value_t *value);

/**
 * Get property from JSON object
 * @param obj JSON object
 * @param key Property key
 * @return JSON value or NULL if not found
 */
json_value_t *json_object_get(json_value_t *obj, const char *key);

/**
 * Create JSON string value
 * @param str String value
 * @return JSON string value
 */
json_value_t *json_string_create(const char *str);

/**
 * Create JSON number value
 * @param num Number value
 * @return JSON number value
 */
json_value_t *json_number_create(double num);

/**
 * Create JSON boolean value
 * @param val Boolean value
 * @return JSON boolean value
 */
json_value_t *json_bool_create(bool val);

/**
 * Stringify JSON value
 * @param value JSON value
 * @return JSON string (must be freed by caller) or NULL on error
 */
char *json_stringify(json_value_t *value);

/**
 * Free JSON value and associated resources
 * @param value JSON value
 */
void json_value_free(json_value_t *value);

/* ===== Event Loop API (Async I/O) ===== */

/**
 * Event types for I/O operations
 */
typedef enum {
    EVENT_READ = 1 << 0,   /* File descriptor is readable */
    EVENT_WRITE = 1 << 1,  /* File descriptor is writable */
    EVENT_ERROR = 1 << 2,  /* Error condition on file descriptor */
    EVENT_TIMEOUT = 1 << 3 /* Timeout event */
} event_type_t;

/**
 * Event callback function
 * @param fd File descriptor that triggered the event
 * @param events Event types that occurred
 * @param user_data User-provided data
 */
typedef void (*event_callback_t)(int fd, int events, void *user_data);

/**
 * Create a new event loop
 * @return Pointer to event loop instance or NULL on failure
 */
event_loop_t *event_loop_create(void);

/**
 * Add a file descriptor to the event loop
 * @param loop Event loop instance
 * @param fd File descriptor to monitor
 * @param events Event types to monitor (EVENT_READ, EVENT_WRITE, etc.)
 * @param callback Callback function to invoke when event occurs
 * @param user_data User data to pass to callback
 * @return 0 on success, -1 on failure
 */
int event_loop_add_fd(event_loop_t *loop, int fd, int events, event_callback_t callback, void *user_data);

/**
 * Modify events for a file descriptor
 * @param loop Event loop instance
 * @param fd File descriptor to modify
 * @param events New event types to monitor
 * @return 0 on success, -1 on failure
 */
int event_loop_modify_fd(event_loop_t *loop, int fd, int events);

/**
 * Remove a file descriptor from the event loop
 * @param loop Event loop instance
 * @param fd File descriptor to remove
 * @return 0 on success, -1 on failure
 */
int event_loop_remove_fd(event_loop_t *loop, int fd);

/**
 * Run the event loop (blocking)
 * @param loop Event loop instance
 * @return 0 on normal exit, -1 on error
 */
int event_loop_run(event_loop_t *loop);

/**
 * Stop the event loop
 * @param loop Event loop instance
 */
void event_loop_stop(event_loop_t *loop);

/**
 * Destroy the event loop and free resources
 * @param loop Event loop instance
 */
void event_loop_destroy(event_loop_t *loop);

/**
 * Set a timeout callback
 * @param loop Event loop instance
 * @param timeout_ms Timeout in milliseconds
 * @param callback Callback function to invoke on timeout
 * @param user_data User data to pass to callback
 * @return Timer ID on success, -1 on failure
 */
int event_loop_add_timeout(event_loop_t *loop, int timeout_ms, event_callback_t callback, void *user_data);

/**
 * Cancel a timeout
 * @param loop Event loop instance
 * @param timer_id Timer ID returned from event_loop_add_timeout
 * @return 0 on success, -1 on failure
 */
int event_loop_cancel_timeout(event_loop_t *loop, int timer_id);

/* ===== Async HTTP Server API ===== */

/**
 * Enable async I/O mode for the HTTP server
 * @param server Server instance
 * @param enable true to enable async mode, false to disable
 * @return 0 on success, -1 on failure
 */
int http_server_set_async(http_server_t *server, bool enable);

/**
 * Get the event loop associated with the server (async mode only)
 * @param server Server instance
 * @return Event loop instance or NULL if not in async mode
 */
event_loop_t *http_server_get_event_loop(http_server_t *server);

#ifdef __cplusplus
}
#endif

#endif /* WEBLIB_H */
