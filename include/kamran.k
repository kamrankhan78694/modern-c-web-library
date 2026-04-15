#ifndef KAMRAN_K_H
#define KAMRAN_K_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

/* ===== WebAssembly Support ===== */
#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#define WASM_EXPORT EMSCRIPTEN_KEEPALIVE
#define WEBLIB_WASM 1
#else
#define WASM_EXPORT
#define WEBLIB_WASM 0
#endif

/* ===== Library Identity ===== */
#define WEBLIB_AUTHOR_KAMRAN "kamran"

/* ===== Semantic Versioning (https://semver.org/) ===== */
/**
 * Version components follow Semantic Versioning 2.0.0:
 *   MAJOR — incremented for incompatible API changes
 *   MINOR — incremented for backwards-compatible new functionality
 *   PATCH — incremented for backwards-compatible bug fixes
 *
 * Update these macros (and CMakeLists.txt project VERSION) when releasing.
 */
#define WEBLIB_VERSION_MAJOR 1
#define WEBLIB_VERSION_MINOR 0
#define WEBLIB_VERSION_PATCH 0

/** Full version string in "MAJOR.MINOR.PATCH" form. */
#define WEBLIB_VERSION "1.0.0"

/** Server identity string embedded in HTTP Server header. */
#define WEBLIB_VERSION_STRING "weblib/" WEBLIB_VERSION " (author:" WEBLIB_AUTHOR_KAMRAN ")"

/**
 * Encode a version triplet as a single integer for compile-time comparison.
 * Example: WEBLIB_VERSION_ENCODE(1, 2, 3) → 10203
 */
#define WEBLIB_VERSION_ENCODE(major, minor, patch) \
    (((major) * 10000) + ((minor) * 100) + (patch))

/** Current library version as an encoded integer. */
#define WEBLIB_VERSION_NUMBER \
    WEBLIB_VERSION_ENCODE(WEBLIB_VERSION_MAJOR, WEBLIB_VERSION_MINOR, WEBLIB_VERSION_PATCH)

/**
 * Return the library signature string (contains author watermark).
 * This is embedded in every HTTP response as the Server header.
 */
const char *weblib_kamran_signature(void);

/**
 * Return the library version string (e.g. "1.0.0").
 */
const char *weblib_version(void);

/**
 * Return the library version as individual numeric components.
 * Any pointer may be NULL if the caller does not need that component.
 */
void weblib_version_components(int *major, int *minor, int *patch);

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
    HTTP_SWITCHING_PROTOCOLS = 101,
    HTTP_OK = 200,
    HTTP_CREATED = 201,
    HTTP_ACCEPTED = 202,
    HTTP_NO_CONTENT = 204,
    HTTP_NOT_MODIFIED = 304,
    HTTP_BAD_REQUEST = 400,
    HTTP_UNAUTHORIZED = 401,
    HTTP_FORBIDDEN = 403,
    HTTP_NOT_FOUND = 404,
    HTTP_METHOD_NOT_ALLOWED = 405,
    HTTP_PAYLOAD_TOO_LARGE = 413,
    HTTP_URI_TOO_LONG = 414,
    HTTP_TOO_MANY_REQUESTS = 429,
    HTTP_HEADER_FIELDS_TOO_LARGE = 431,
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
typedef struct websocket_connection websocket_connection_t;
typedef struct websocket_server websocket_server_t;
typedef struct http_uploaded_file http_uploaded_file_t;
typedef struct http_form_field http_form_field_t;
typedef struct body_parser_data body_parser_data_t;
typedef struct session session_t;
typedef struct session_store session_store_t;
typedef struct template_context template_context_t;

/* ===== Body Parser Types ===== */

/* Uploaded file structure */
struct http_uploaded_file {
    char *field_name;       /* Form field name */
    char *filename;         /* Original filename (sanitized) */
    char *content_type;     /* MIME type */
    uint8_t *data;          /* File data */
    size_t size;            /* File data size */
    struct http_uploaded_file *next;
};

/* Form field structure */
struct http_form_field {
    char *name;
    char *value;
    struct http_form_field *next;
};

/* Body parser context stored in request */
struct body_parser_data {
    http_form_field_t *fields;
    http_uploaded_file_t *files;
    bool parsed;
};

/* ===== Cookie Types ===== */

/* Cookie options for Set-Cookie header */
typedef struct cookie_options {
    const char *domain;     /* Domain attribute */
    const char *path;       /* Path attribute (default: "/") */
    int max_age;            /* Max-Age in seconds (-1 = session cookie) */
    bool secure;            /* Secure flag */
    bool http_only;         /* HttpOnly flag */
    const char *same_site;  /* SameSite attribute: "Strict", "Lax", "None" */
} cookie_options_t;

/* ===== CORS Types ===== */

/* CORS configuration */
typedef struct cors_options {
    const char **allowed_origins;  /* NULL-terminated array of origins, or NULL for "*" */
    const char *allowed_methods;   /* Comma-separated methods */
    const char *allowed_headers;   /* Comma-separated headers */
    const char *expose_headers;    /* Comma-separated headers to expose */
    bool allow_credentials;        /* Allow credentials */
    int max_age;                   /* Preflight cache duration in seconds */
} cors_options_t;

/* ===== Rate Limiting Types ===== */

/* Rate limit configuration */
typedef struct ratelimit_config {
    int requests_per_window;  /* Max requests per window */
    int window_seconds;       /* Time window in seconds */
    int burst_size;           /* Max burst size (token bucket capacity) */
} ratelimit_config_t;

/* ===== Static File Types ===== */

/* Static file middleware configuration */
typedef struct static_file_config {
    const char *root_dir;       /* Root directory for static files */
    const char *index_file;     /* Default index file (default: "index.html") */
    int cache_max_age;          /* Cache-Control max-age in seconds (default: 3600) */
    bool enable_etag;           /* Enable ETag support (default: true) */
} static_file_config_t;

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
    int socket_fd;   /* Client socket file descriptor (for WebSocket upgrade) */
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

/* Middleware callback - return true to continue, false to stop.
 * The user_data parameter enables per-instance middleware state,
 * allowing multiple instances of the same middleware type. */
typedef bool (*middleware_fn_t)(http_request_t *req, http_response_t *res, void *user_data);

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
 * Add middleware with per-instance user data to the router.
 * This enables multiple instances of the same middleware type with
 * different configurations (e.g., different rate limits per route group).
 * @param router Router instance
 * @param middleware Middleware function
 * @param user_data Opaque pointer passed to middleware on each invocation
 * @return 0 on success, -1 on failure
 */
int router_use_middleware_with_data(router_t *router, middleware_fn_t middleware, void *user_data);

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
 * Create JSON null value
 * @return JSON null value
 */
json_value_t *json_null_create(void);

/**
 * Create JSON array value
 * @return JSON array value
 */
json_value_t *json_array_create(void);

/**
 * Append value to JSON array
 * @param arr JSON array
 * @param value JSON value to append
 * @return 0 on success, -1 on failure
 */
int json_array_append(json_value_t *arr, json_value_t *value);

/**
 * Get element from JSON array by index
 * @param arr JSON array
 * @param index Array index (0-based)
 * @return JSON value at index or NULL if out of bounds
 */
json_value_t *json_array_get(json_value_t *arr, size_t index);

/**
 * Get the length of a JSON array
 * @param arr JSON array
 * @return Number of elements in array, or 0 if not an array
 */
size_t json_array_length(json_value_t *arr);

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

/**
 * Get the current number of active timers
 * @param loop Event loop instance
 * @return Number of active timers, or -1 on failure
 */
int event_loop_get_timer_count(event_loop_t *loop);

/**
 * Get the maximum number of timers supported
 * @return Maximum timer count (compile-time constant)
 */
int event_loop_get_max_timers(void);

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

/* ===== Server Hardening API ===== */

/**
 * Server lifecycle states
 */
typedef enum {
    HTTP_SERVER_STOPPED = 0,
    HTTP_SERVER_RUNNING = 1,
    HTTP_SERVER_DRAINING = 2
} http_server_state_t;

/**
 * Set socket timeouts for accepted client connections
 * Must be called before http_server_listen(). A value of 0 disables the timeout.
 * @param server Server instance
 * @param read_sec Read timeout in seconds (default: 30, 0 = no timeout)
 * @param write_sec Write timeout in seconds (default: 30, 0 = no timeout)
 * @return 0 on success, -1 on failure (NULL server or negative values)
 */
int http_server_set_timeout(http_server_t *server, int read_sec, int write_sec);

/**
 * Get current read timeout setting
 * @param server Server instance
 * @return Read timeout in seconds, or -1 if server is NULL
 */
int http_server_get_read_timeout(http_server_t *server);

/**
 * Get current write timeout setting
 * @param server Server instance
 * @return Write timeout in seconds, or -1 if server is NULL
 */
int http_server_get_write_timeout(http_server_t *server);

/**
 * Set thread pool size for threaded mode
 * Must be called before http_server_listen(). Clamped to [1, 256].
 * @param server Server instance
 * @param count Number of worker threads (default: 16)
 * @return 0 on success, -1 on failure
 */
int http_server_set_thread_count(http_server_t *server, int count);

/**
 * Graceful shutdown: stop accepting new connections and drain in-flight requests
 * @param server Server instance
 * @param timeout_sec Maximum seconds to wait for drain (0 = immediate)
 * @return 0 on success, -1 on failure
 */
int http_server_shutdown(http_server_t *server, int timeout_sec);

/**
 * Get current server lifecycle state
 * @param server Server instance
 * @return HTTP_SERVER_STOPPED, HTTP_SERVER_RUNNING, or HTTP_SERVER_DRAINING
 */
int http_server_get_state(http_server_t *server);

/**
 * Set maximum active connections for the server
 * @param server Server instance
 * @param max_conn Maximum number of simultaneous connections (must be >= 1)
 * @return 0 on success, -1 on failure
 */
int http_server_set_max_connections(http_server_t *server, int max_conn);

/**
 * Get the current number of active connections
 * @param server Server instance
 * @return Active connection count, or -1 on failure
 */
int http_server_get_active_connections(http_server_t *server);

/* ===== WebSocket API ===== */

/**
 * WebSocket message types
 */
typedef enum {
    WS_MESSAGE_TEXT,   /* UTF-8 text message */
    WS_MESSAGE_BINARY  /* Binary message */
} ws_message_type_t;

/**
 * WebSocket close codes (RFC 6455)
 */
typedef enum {
    WS_CLOSE_NORMAL = 1000,           /* Normal closure */
    WS_CLOSE_GOING_AWAY = 1001,       /* Endpoint is going away */
    WS_CLOSE_PROTOCOL_ERROR = 1002,   /* Protocol error */
    WS_CLOSE_UNSUPPORTED = 1003,      /* Unsupported data type */
    WS_CLOSE_NO_STATUS = 1005,        /* No status code received */
    WS_CLOSE_ABNORMAL = 1006,         /* Abnormal closure */
    WS_CLOSE_INVALID_DATA = 1007,     /* Invalid frame payload data */
    WS_CLOSE_POLICY = 1008,           /* Policy violation */
    WS_CLOSE_TOO_LARGE = 1009,        /* Message too large */
    WS_CLOSE_EXTENSION = 1010,        /* Extension negotiation failure */
    WS_CLOSE_UNEXPECTED = 1011,       /* Unexpected condition */
    WS_CLOSE_TLS_FAILED = 1015        /* TLS handshake failure */
} ws_close_code_t;

/**
 * WebSocket message callback
 * @param conn WebSocket connection
 * @param type Message type (text or binary)
 * @param data Message data
 * @param len Message length
 */
typedef void (*websocket_message_cb_t)(websocket_connection_t *conn, ws_message_type_t type, const void *data, size_t len);

/**
 * WebSocket close callback
 * @param conn WebSocket connection
 * @param code Close code
 */
typedef void (*websocket_close_cb_t)(websocket_connection_t *conn, uint16_t code);

/**
 * WebSocket error callback
 * @param conn WebSocket connection
 * @param error Error message
 */
typedef void (*websocket_error_cb_t)(websocket_connection_t *conn, const char *error);

/**
 * WebSocket connection callback (for server)
 * @param conn WebSocket connection
 * @param user_data User data provided when creating server
 */
typedef void (*websocket_connect_cb_t)(websocket_connection_t *conn, void *user_data);

/**
 * Handle WebSocket upgrade request
 * This function performs the WebSocket handshake and upgrades an HTTP connection
 * to a WebSocket connection. Should be called from an HTTP route handler.
 * 
 * @param req HTTP request object
 * @param res HTTP response object
 * @return true if upgrade was successful, false otherwise
 */
bool websocket_handle_upgrade(http_request_t *req, http_response_t *res);

/**
 * Create a new WebSocket connection from an existing file descriptor
 * This is typically called after websocket_handle_upgrade() to create a
 * WebSocket connection object for the upgraded connection.
 * 
 * @param fd File descriptor of the upgraded connection
 * @return WebSocket connection or NULL on failure
 */
websocket_connection_t *websocket_connection_create(int fd);

/**
 * Destroy a WebSocket connection and free resources
 * @param conn WebSocket connection
 */
void websocket_connection_destroy(websocket_connection_t *conn);

/**
 * Send a WebSocket message
 * @param conn WebSocket connection
 * @param type Message type (text or binary)
 * @param data Message data
 * @param len Message length
 * @return 0 on success, -1 on failure
 */
int websocket_send(websocket_connection_t *conn, ws_message_type_t type, const void *data, size_t len);

/**
 * Send a WebSocket text message
 * @param conn WebSocket connection
 * @param text Text message (null-terminated string)
 * @return 0 on success, -1 on failure
 */
int websocket_send_text(websocket_connection_t *conn, const char *text);

/**
 * Send a WebSocket binary message
 * @param conn WebSocket connection
 * @param data Binary data
 * @param len Data length
 * @return 0 on success, -1 on failure
 */
int websocket_send_binary(websocket_connection_t *conn, const void *data, size_t len);

/**
 * Send a WebSocket ping frame
 * @param conn WebSocket connection
 * @param data Optional ping data
 * @param len Ping data length
 * @return 0 on success, -1 on failure
 */
int websocket_send_ping(websocket_connection_t *conn, const void *data, size_t len);

/**
 * Send a WebSocket pong frame (usually in response to ping)
 * @param conn WebSocket connection
 * @param data Optional pong data
 * @param len Pong data length
 * @return 0 on success, -1 on failure
 */
int websocket_send_pong(websocket_connection_t *conn, const void *data, size_t len);

/**
 * Close a WebSocket connection gracefully
 * @param conn WebSocket connection
 * @param code Close code (see ws_close_code_t)
 * @param reason Optional close reason (null-terminated string)
 * @return 0 on success, -1 on failure
 */
int websocket_close(websocket_connection_t *conn, uint16_t code, const char *reason);

/**
 * Process incoming WebSocket data
 * This function parses WebSocket frames and invokes the appropriate callbacks.
 * Should be called when data is received on the WebSocket connection.
 * 
 * @param conn WebSocket connection
 * @param data Received data
 * @param len Data length
 * @return 0 on success, -1 on failure
 */
int websocket_process_data(websocket_connection_t *conn, const uint8_t *data, size_t len);

/**
 * Set message callback for WebSocket connection
 * @param conn WebSocket connection
 * @param callback Message callback function
 */
void websocket_set_message_callback(websocket_connection_t *conn, websocket_message_cb_t callback);

/**
 * Set close callback for WebSocket connection
 * @param conn WebSocket connection
 * @param callback Close callback function
 */
void websocket_set_close_callback(websocket_connection_t *conn, websocket_close_cb_t callback);

/**
 * Set error callback for WebSocket connection
 * @param conn WebSocket connection
 * @param callback Error callback function
 */
void websocket_set_error_callback(websocket_connection_t *conn, websocket_error_cb_t callback);

/**
 * Set user data for WebSocket connection
 * @param conn WebSocket connection
 * @param user_data User data pointer
 */
void websocket_set_user_data(websocket_connection_t *conn, void *user_data);

/**
 * Get user data from WebSocket connection
 * @param conn WebSocket connection
 * @return User data pointer
 */
void *websocket_get_user_data(websocket_connection_t *conn);

/**
 * Check if WebSocket connection is open
 * @param conn WebSocket connection
 * @return true if connection is open, false otherwise
 */
bool websocket_is_open(websocket_connection_t *conn);

/**
 * Get file descriptor of WebSocket connection
 * @param conn WebSocket connection
 * @return File descriptor or -1 if connection is NULL
 */
int websocket_get_fd(websocket_connection_t *conn);

/**
 * Get message callback from WebSocket connection
 * @param conn WebSocket connection
 * @return Message callback or NULL
 */
websocket_message_cb_t websocket_get_message_callback(websocket_connection_t *conn);

/**
 * Get close callback from WebSocket connection
 * @param conn WebSocket connection
 * @return Close callback or NULL
 */
websocket_close_cb_t websocket_get_close_callback(websocket_connection_t *conn);

/**
 * Get error callback from WebSocket connection
 * @param conn WebSocket connection
 * @return Error callback or NULL
 */
websocket_error_cb_t websocket_get_error_callback(websocket_connection_t *conn);

/* ===== Async WebSocket API ===== */

/**
 * Async WebSocket manager (forward declaration)
 * Manages multiple WebSocket connections with non-blocking I/O via event loop
 */
typedef struct async_ws_manager async_ws_manager_t;

/**
 * Create an async WebSocket manager bound to an event loop
 * @param loop Event loop instance
 * @return Async WebSocket manager or NULL on failure
 */
async_ws_manager_t *async_ws_manager_create(event_loop_t *loop);

/**
 * Destroy async WebSocket manager and close all connections
 * Note: Does not destroy the underlying websocket_connection_t objects
 * @param mgr Async WebSocket manager
 */
void async_ws_manager_destroy(async_ws_manager_t *mgr);

/**
 * Set default callbacks for all WebSocket connections in the manager
 * @param mgr Async WebSocket manager
 * @param on_msg Message callback (or NULL to keep existing)
 * @param on_close Close callback (or NULL to keep existing)
 * @param on_error Error callback (or NULL to keep existing)
 */
void async_ws_manager_set_callbacks(async_ws_manager_t *mgr,
                                     websocket_message_cb_t on_msg,
                                     websocket_close_cb_t on_close,
                                     websocket_error_cb_t on_error);

/**
 * Add a WebSocket connection to the async manager for non-blocking I/O
 * The socket will be made non-blocking and registered with the event loop
 * @param mgr Async WebSocket manager
 * @param ws WebSocket connection
 * @return 0 on success, -1 on failure
 */
int async_ws_manager_add(async_ws_manager_t *mgr, websocket_connection_t *ws);

/**
 * Remove a WebSocket connection from the async manager
 * Unregisters from event loop and flushes write queue
 * @param mgr Async WebSocket manager
 * @param ws WebSocket connection
 * @return 0 on success, -1 on failure
 */
int async_ws_manager_remove(async_ws_manager_t *mgr, websocket_connection_t *ws);

/**
 * Send a WebSocket message asynchronously (non-blocking)
 * The message is queued and sent when the socket is writable
 * @param mgr Async WebSocket manager
 * @param ws WebSocket connection
 * @param type Message type (text or binary)
 * @param data Message data
 * @param len Message length
 * @return 0 on success, -1 on failure
 */
int async_ws_send(async_ws_manager_t *mgr, websocket_connection_t *ws,
                  ws_message_type_t type, const void *data, size_t len);

/**
 * Get the number of active WebSocket connections in the manager
 * @param mgr Async WebSocket manager
 * @return Number of active connections
 */
size_t async_ws_manager_count(async_ws_manager_t *mgr);

/* ===== Body Parser API ===== */

/**
 * Parse the request body based on Content-Type header
 * Supports application/x-www-form-urlencoded and multipart/form-data
 * @param req Request object
 * @return 0 on success, -1 on failure
 */
int http_request_parse_body(http_request_t *req);

/**
 * Get a form field value from the parsed request body
 * Automatically parses body if not already parsed
 * @param req Request object
 * @param name Field name
 * @return Field value or NULL if not found
 */
const char *http_request_get_form_field(http_request_t *req, const char *name);

/**
 * Get an uploaded file from the parsed request body
 * Automatically parses body if not already parsed
 * @param req Request object
 * @param field_name Form field name for the file input
 * @return Uploaded file structure or NULL if not found
 */
http_uploaded_file_t *http_request_get_file(http_request_t *req, const char *field_name);

/**
 * Free body parser resources
 * @param data Body parser data to free
 */
void body_parser_data_free(body_parser_data_t *data);

/* ===== Cookie API ===== */

/**
 * Get a cookie value from the request
 * @param req Request object
 * @param name Cookie name
 * @return Cookie value or NULL if not found
 */
const char *http_request_get_cookie(http_request_t *req, const char *name);

/**
 * Set a cookie on the response with options
 * @param res Response object
 * @param name Cookie name
 * @param value Cookie value
 * @param options Cookie options (NULL for defaults)
 */
void http_response_set_cookie(http_response_t *res, const char *name,
                              const char *value, const cookie_options_t *options);

/**
 * Delete a cookie by setting Max-Age=0
 * @param res Response object
 * @param name Cookie name to delete
 */
void http_response_delete_cookie(http_response_t *res, const char *name);

/* ===== CORS Middleware API ===== */

/**
 * Create a CORS middleware function with the given configuration
 * @param options CORS configuration (NULL for default permissive CORS)
 * @return Middleware function for use with router_use_middleware()
 */
middleware_fn_t cors_middleware_create(const cors_options_t *options);

/**
 * Destroy CORS middleware and free resources
 */
void cors_middleware_destroy(void);

/* ===== Rate Limiting Middleware API ===== */

/**
 * Create a rate limiting middleware with the given configuration
 * @param config Rate limit configuration
 * @return Middleware function for use with router_use_middleware()
 */
middleware_fn_t ratelimit_middleware_create(const ratelimit_config_t *config);

/**
 * Destroy rate limiting middleware and free resources
 */
void ratelimit_middleware_destroy(void);

/* ===== Static File Middleware API ===== */

/**
 * Create a static file serving middleware
 * @param config Static file configuration
 * @return Middleware function for use with router_use_middleware()
 */
middleware_fn_t static_file_middleware_create(const static_file_config_t *config);

/**
 * Destroy static file middleware and free resources
 */
void static_file_middleware_destroy(void);

/* ===== Session Management API ===== */

/**
 * Create a session store
 * @return Pointer to session store instance or NULL on failure
 */
session_store_t *session_store_create(void);

/**
 * Destroy session store and free all sessions
 * @param store Session store instance
 */
void session_store_destroy(session_store_t *store);

/**
 * Create a new session
 * @param store Session store instance
 * @param max_age Maximum age of session in seconds (0 for session cookie)
 * @return Session ID or NULL on failure (caller must free)
 */
char *session_create(session_store_t *store, int max_age);

/**
 * Get session by ID
 * @param store Session store instance
 * @param session_id Session ID
 * @return Session instance or NULL if not found/expired
 */
session_t *session_get(session_store_t *store, const char *session_id);

/**
 * Destroy a session
 * @param store Session store instance
 * @param session_id Session ID
 */
void session_destroy(session_store_t *store, const char *session_id);

/**
 * Set session data
 * @param session Session instance
 * @param key Data key
 * @param value Data value (will be copied)
 */
void session_set_data(session_t *session, const char *key, const char *value);

/**
 * Get session data
 * @param session Session instance
 * @param key Data key
 * @return Data value or NULL if not found
 */
const char *session_get_data(session_t *session, const char *key);

/**
 * Remove session data
 * @param session Session instance
 * @param key Data key
 */
void session_remove_data(session_t *session, const char *key);

/**
 * Get session ID
 * @param session Session instance
 * @return Session ID
 */
const char *session_get_id(session_t *session);

/**
 * Check if session is expired
 * @param session Session instance
 * @return true if expired, false otherwise
 */
bool session_is_expired(session_t *session);

/**
 * Clean up expired sessions from the store
 * @param store Session store instance
 * @return Number of sessions cleaned up
 */
int session_cleanup_expired(session_store_t *store);

/**
 * Get session from request (via cookie)
 * @param store Session store instance
 * @param req Request object
 * @return Session instance or NULL if not found
 */
session_t *session_from_request(session_store_t *store, http_request_t *req);

/**
 * Set session cookie in response
 * @param res Response object
 * @param session_id Session ID
 * @param max_age Maximum age in seconds (0 for session cookie, -1 to delete)
 * @param path Cookie path (NULL for default "/")
 */
void session_set_cookie(http_response_t *res, const char *session_id, int max_age, const char *path);

/* ===== Template Engine API ===== */

/**
 * Create a template context for variable substitution
 * @return Template context or NULL on failure
 */
template_context_t *template_context_create(void);

/**
 * Set a variable in the template context
 * @param ctx Template context
 * @param key Variable name
 * @param value Variable value
 */
void template_context_set(template_context_t *ctx, const char *key, const char *value);

/**
 * Get a variable from the template context
 * @param ctx Template context
 * @param key Variable name
 * @return Variable value or NULL if not found
 */
const char *template_context_get(template_context_t *ctx, const char *key);

/**
 * Destroy a template context and free resources
 * @param ctx Template context
 */
void template_context_destroy(template_context_t *ctx);

/**
 * Render a template string with variable substitution
 * Variables use {{ variable_name }} syntax
 * @param template_str Template string
 * @param ctx Template context with variables
 * @return Rendered string (caller must free) or NULL on error
 */
char *template_render(const char *template_str, template_context_t *ctx);

/**
 * Load a template from a file
 * @param filename Path to template file
 * @return Template string (caller must free) or NULL on error
 */
char *template_load_file(const char *filename);

/**
 * Send a rendered template as HTTP response
 * @param res Response object
 * @param status HTTP status code
 * @param template_str Template string
 * @param ctx Template context
 */
void http_response_send_template(http_response_t *res, http_status_t status,
                                  const char *template_str, template_context_t *ctx);

/* ===== Authentication Middleware API ===== */

/* Auth callback: return true if authenticated, false otherwise */
typedef bool (*auth_verify_cb_t)(const char *username, const char *password, void *user_data);

/* API key verify callback */
typedef bool (*apikey_verify_cb_t)(const char *api_key, void *user_data);

/* Basic auth configuration */
typedef struct basic_auth_config {
    const char *realm;           /* HTTP realm for WWW-Authenticate header */
    auth_verify_cb_t verify;     /* Callback to verify credentials */
    void *user_data;             /* User data passed to callback */
} basic_auth_config_t;

/* API key auth configuration */
typedef struct apikey_auth_config {
    const char *header_name;     /* Header name (default: "X-API-Key") */
    apikey_verify_cb_t verify;   /* Callback to verify API key */
    void *user_data;             /* User data passed to callback */
} apikey_auth_config_t;

/* JWT auth configuration */
typedef struct jwt_auth_config {
    const char *secret;          /* HMAC-SHA256 secret key */
    size_t secret_len;           /* Secret key length */
    const char *header_name;     /* Header name (default: "Authorization") */
} jwt_auth_config_t;

/**
 * Create a Basic Auth middleware
 * @param config Basic auth configuration
 * @return Middleware function or NULL on failure
 */
middleware_fn_t basic_auth_middleware_create(const basic_auth_config_t *config);

/**
 * Destroy Basic Auth middleware and free resources
 */
void basic_auth_middleware_destroy(void);

/**
 * Create an API Key auth middleware
 * @param config API key auth configuration
 * @return Middleware function or NULL on failure
 */
middleware_fn_t apikey_auth_middleware_create(const apikey_auth_config_t *config);

/**
 * Destroy API Key auth middleware and free resources
 */
void apikey_auth_middleware_destroy(void);

/**
 * Create a JWT auth middleware
 * @param config JWT auth configuration
 * @return Middleware function or NULL on failure
 */
middleware_fn_t jwt_auth_middleware_create(const jwt_auth_config_t *config);

/**
 * Destroy JWT auth middleware and free resources
 */
void jwt_auth_middleware_destroy(void);

/* ===== Logging Middleware API (Phase 8) ===== */

/**
 * Log levels in ascending severity order
 */
typedef enum {
    LOG_LEVEL_DEBUG = 0,
    LOG_LEVEL_INFO  = 1,
    LOG_LEVEL_WARN  = 2,
    LOG_LEVEL_ERROR = 3
} log_level_t;

/**
 * Logging middleware configuration
 */
typedef struct log_config {
    log_level_t level;   /* Minimum log level to emit (default: LOG_LEVEL_INFO) */
    FILE *output;        /* Output stream; NULL defaults to stderr */
} log_config_t;

/**
 * Create a logging middleware that emits one line per request.
 * Format: [YYYY-MM-DD HH:MM:SS] LEVEL  METHOD /path
 * @param config Logging configuration (NULL for defaults)
 * @return Middleware function or NULL on failure
 */
middleware_fn_t log_middleware_create(const log_config_t *config);

/**
 * Destroy logging middleware and free resources
 */
void log_middleware_destroy(void);

/* ===== Error Handler Middleware API (Phase 8) ===== */

/**
 * Custom error handler callback
 * @param req  Incoming request
 * @param res  Response to populate
 * @param status HTTP status code that triggered the error
 */
typedef void (*error_handler_fn_t)(http_request_t *req, http_response_t *res,
                                    http_status_t status);

/**
 * Error handler middleware configuration
 */
typedef struct error_handler_config {
    error_handler_fn_t handler; /* Custom handler; NULL → built-in JSON error body */
} error_handler_config_t;

/**
 * Create an error-handler middleware.
 * When the response status is 4xx or 5xx and the body has not been set,
 * the middleware fills in a standard JSON error body:
 *   {"error": "<reason phrase>", "status": <code>}
 * This is applied automatically when the middleware runs (e.g., when a prior
 * middleware has set an error status).  For errors produced by the route
 * handler itself, call error_handler_apply() explicitly from user code.
 * @param config Configuration (NULL for defaults)
 * @return Middleware function or NULL on failure
 */
middleware_fn_t error_handler_middleware_create(const error_handler_config_t *config);

/**
 * Destroy error handler middleware and free resources
 */
void error_handler_middleware_destroy(void);

/**
 * Apply the error handler to a response that has an error status.
 * No-op when status < 400.  Can be called directly from route handlers.
 * @param req Request object (may be NULL)
 * @param res Response object to inspect and potentially populate
 */
void error_handler_apply(http_request_t *req, http_response_t *res);

/* ===== CSRF Middleware API (Phase 8) ===== */

/**
 * CSRF middleware configuration
 */
typedef struct csrf_config {
    const char *cookie_name; /* Cookie that carries the token (default: "csrf_token") */
    const char *header_name; /* Request header the client must echo (default: "X-CSRF-Token") */
    int token_length;        /* Token bytes before hex-encoding (default: 16, min: 8, max: 32) */
} csrf_config_t;

/**
 * Create a CSRF protection middleware (double-submit cookie pattern).
 *
 * On the first request (no cookie present) a random token is generated and
 * written into a cookie on the response.  For state-changing methods
 * (POST / PUT / PATCH / DELETE) the token found in the cookie must also appear
 * in the X-CSRF-Token request header (or the configured header_name).
 * Constant-time comparison is used to resist timing attacks.
 *
 * Safe methods (GET, HEAD, OPTIONS) are passed through unconditionally.
 *
 * @param config Configuration (NULL for defaults)
 * @return Middleware function or NULL on failure
 */
middleware_fn_t csrf_middleware_create(const csrf_config_t *config);

/**
 * Destroy CSRF middleware and free resources
 */
void csrf_middleware_destroy(void);

/* ===== Health Check API (Observability) ===== */

/**
 * Health check route handler for GET /healthz
 * Returns JSON: {"status":"ok","uptime_seconds":<N>}
 * Suitable for load-balancer probes, Kubernetes liveness/readiness checks,
 * and monitoring dashboards.
 * @param req Request object
 * @param res Response object
 */
void health_check_handler(http_request_t *req, http_response_t *res);

/**
 * Register the health check endpoint on an existing router.
 * Adds GET /healthz → health_check_handler.
 * @param router Router instance
 * @return 0 on success, -1 on failure
 */
int health_check_register(router_t *router);

/* ===== Input Validation API (Phase 8) ===== */

/**
 * Validate string length
 * @param str      String to validate
 * @param min_len  Minimum length (inclusive)
 * @param max_len  Maximum length (inclusive)
 * @return true if str length is within [min_len, max_len]
 */
bool input_validate_length(const char *str, size_t min_len, size_t max_len);

/**
 * Validate that a string contains only characters from an allowed set
 * @param str           String to validate
 * @param allowed_chars Null-terminated string of allowed characters
 * @return true if every character of str appears in allowed_chars
 */
bool input_validate_charset(const char *str, const char *allowed_chars);

/**
 * Parse a decimal integer and validate it falls within a range.
 * Rejects leading/trailing whitespace and non-numeric characters.
 * @param str     String representation of an integer
 * @param min_val Minimum acceptable value (inclusive)
 * @param max_val Maximum acceptable value (inclusive)
 * @param out_val If non-NULL and validation passes, receives the parsed value
 * @return true if str is a valid integer within [min_val, max_val]
 */
bool input_validate_integer(const char *str, long long min_val, long long max_val,
                             long long *out_val);

/**
 * Validate basic e-mail format: non-empty local-part, '@', non-empty domain
 * with at least one '.'.  Does not perform DNS or RFC 5321 full validation.
 * @param str String to validate
 * @return true if str looks like a plausible e-mail address
 */
bool input_validate_email(const char *str);

/**
 * Check whether a string is composed entirely of ASCII alphanumeric chars
 * @param str String to check
 * @return true if every character is [A-Za-z0-9] (empty string → false)
 */
bool input_is_alphanumeric(const char *str);

/**
 * Escape HTML special characters to prevent XSS.
 * Replaces & " ' < > with their named HTML entities.
 * @param str Input string
 * @return Newly allocated escaped string (caller must free), or NULL on error
 */
char *input_sanitize_html(const char *str);

/* ===== In-Memory Cache API (Performance) ===== */

/**
 * Opaque cache type
 */
typedef struct cache cache_t;

/**
 * Create an in-memory LRU cache
 * @param max_entries Maximum number of entries before LRU eviction
 * @return Cache instance or NULL on failure
 */
cache_t *cache_create(size_t max_entries);

/**
 * Destroy cache and free all resources
 * @param cache Cache instance
 */
void cache_destroy(cache_t *cache);

/**
 * Set or update a key-value pair in the cache
 * Both key and value are copied internally.
 * @param cache Cache instance
 * @param key Entry key
 * @param value Entry value
 * @param ttl_seconds Time-to-live in seconds (0 = no expiry)
 * @return 0 on success, -1 on failure
 */
int cache_set(cache_t *cache, const char *key, const char *value, int ttl_seconds);

/**
 * Get value by key
 * Returns NULL if not found or expired. Expired entries are automatically removed.
 * The returned pointer is internal; caller should copy if needed beyond immediate use.
 * @param cache Cache instance
 * @param key Entry key
 * @return Entry value or NULL
 */
const char *cache_get(cache_t *cache, const char *key);

/**
 * Delete entry by key
 * @param cache Cache instance
 * @param key Entry key
 * @return 0 if found and deleted, -1 if not found
 */
int cache_delete(cache_t *cache, const char *key);

/**
 * Remove all entries from the cache
 * @param cache Cache instance
 */
void cache_clear(cache_t *cache);

/**
 * Get current number of entries in the cache
 * @param cache Cache instance
 * @return Number of entries
 */
size_t cache_count(cache_t *cache);

/* ===== Metrics Middleware API (Observability) ===== */

/**
 * Create a metrics collection middleware.
 * Tracks total requests, per-method counts, and status code ranges.
 * @return Middleware function or NULL on failure
 */
middleware_fn_t metrics_middleware_create(void);

/**
 * Destroy metrics middleware and free resources
 */
void metrics_middleware_destroy(void);

/**
 * Record a response status code in metrics.
 * Call after sending a response to track 2xx/3xx/4xx/5xx counts.
 * @param status_code HTTP status code
 */
void metrics_record_status(int status_code);

/**
 * Route handler for GET /metrics
 * Returns JSON with request counts, method breakdown, status codes, and uptime.
 * @param req Request object
 * @param res Response object
 */
void metrics_handler(http_request_t *req, http_response_t *res);

/**
 * Register the metrics endpoint on an existing router.
 * Adds GET /metrics → metrics_handler.
 * @param router Router instance
 * @return 0 on success, -1 on failure
 */
int metrics_register(router_t *router);

/* ===== Response Compression API (Phase 9) ===== */

/**
 * Compute CRC-32 checksum (RFC 1952)
 * @param data Input data
 * @param len Data length
 * @return CRC-32 checksum
 */
uint32_t crc32_compute(const uint8_t *data, size_t len);

/**
 * Negotiate compression encoding from Accept-Encoding header.
 * @param accept_encoding Value of the Accept-Encoding request header
 * @return "gzip" if gzip is accepted, NULL otherwise
 */
const char *compression_negotiate(const char *accept_encoding);

/**
 * Check if content type and size make compression worthwhile.
 * Compresses text types, application/json, application/javascript, etc.
 * Skips images, audio, video, and small payloads (<256 bytes).
 * @param content_type Content-Type value
 * @param content_length Response body length
 * @return true if content should be compressed
 */
bool compression_should_compress(const char *content_type, size_t content_length);

/**
 * Send a compressed text response.
 *
 * If the accept_encoding header indicates gzip support and the content_type
 * is compressible, the body is gzip-compressed before being set on the
 * response.  Otherwise, the body is sent uncompressed via
 * http_response_send_text().
 *
 * @param res             HTTP response object
 * @param status          HTTP status code
 * @param body            Response body text
 * @param body_len        Response body length (0 = use strlen)
 * @param content_type    Content-Type for compressibility check (e.g. "text/html")
 * @param accept_encoding Value of the Accept-Encoding request header (may be NULL)
 */
void http_response_send_compressed(http_response_t *res, http_status_t status,
                                   const char *body, size_t body_len,
                                   const char *content_type,
                                   const char *accept_encoding);

/* ===== Benchmarking API (Phase 9) ===== */

/**
 * High-resolution timestamp in microseconds.
 * Uses clock_gettime(CLOCK_MONOTONIC) for accurate measurement.
 * @return Microseconds since an arbitrary epoch
 */
uint64_t benchmark_timestamp_us(void);

/**
 * Statistics from a benchmark run
 */
typedef struct {
    uint64_t total_requests;     /* Total requests executed */
    uint64_t successful;         /* 2xx responses */
    uint64_t failed;             /* Non-2xx or connection error */
    double elapsed_seconds;      /* Total wall-clock time */
    double requests_per_second;  /* Throughput */
    double avg_latency_us;       /* Average latency in microseconds */
    double min_latency_us;       /* Minimum latency */
    double max_latency_us;       /* Maximum latency */
    double p50_latency_us;       /* 50th percentile (median) latency */
    double p95_latency_us;       /* 95th percentile latency */
    double p99_latency_us;       /* 99th percentile latency */
} benchmark_stats_t;

/**
 * Run a throughput benchmark against a local HTTP endpoint.
 *
 * Sends @p num_requests sequential GET requests to http://127.0.0.1:@p port@p path
 * and collects latency samples.
 *
 * @param port          Server port
 * @param path          Request path (e.g. "/")
 * @param num_requests  Number of requests to send
 * @param stats         Output statistics (must not be NULL)
 * @return 0 on success, -1 on failure
 */
int benchmark_run(uint16_t port, const char *path,
                  uint64_t num_requests, benchmark_stats_t *stats);

/**
 * Print benchmark results to a FILE stream.
 * @param fp    Output stream (e.g. stdout)
 * @param stats Benchmark statistics
 */
void benchmark_print(FILE *fp, const benchmark_stats_t *stats);

/* ===== Environment Configuration API ===== */

/*
 * Type-safe helpers for reading environment variables.
 *
 * Designed for library consumers who configure their applications via
 * environment variables – the standard mechanism used by GitHub Secrets,
 * GitHub Variables, Docker, Kubernetes, and all major CI/CD systems.
 *
 * You choose the variable names.  The library just provides typed accessors
 * so there is zero learning curve:
 *
 *   const char *db  = env_config_get("DB_URL", "postgres://localhost/mydb");
 *   int         port = env_config_get_int("PORT", 8080);
 *   bool        debug = env_config_get_bool("DEBUG", false);
 *   const char *key  = env_config_require("API_KEY");   // NULL if missing
 */

/**
 * Get a string environment variable with a fallback default.
 * Works with any env var name – use your own GitHub Secret / Variable names.
 * @param key           Environment variable name (e.g. "DATABASE_URL")
 * @param default_value Value returned when key is unset or empty
 * @return The env value or default_value (never NULL when default_value != NULL)
 */
const char *env_config_get(const char *key, const char *default_value);

/**
 * Get an integer environment variable with a fallback default.
 * Returns default_value when the key is unset, empty, or not a valid integer.
 * @param key           Environment variable name (e.g. "PORT", "TIMEOUT")
 * @param default_value Value returned on missing/invalid input
 * @return Parsed integer or default_value
 */
int env_config_get_int(const char *key, int default_value);

/**
 * Get a boolean environment variable with a fallback default.
 * Truthy values: "1", "true", "yes", "on" (case-insensitive).
 * Falsy  values: "0", "false", "no", "off" (case-insensitive).
 * @param key           Environment variable name (e.g. "DEBUG", "ENABLE_TLS")
 * @param default_value Value returned on missing/unrecognized input
 * @return Parsed boolean or default_value
 */
bool env_config_get_bool(const char *key, bool default_value);

/**
 * Get a port number (uint16_t) environment variable with a fallback default.
 * Returns default_value when the key is unset, empty, or outside 0-65535.
 * @param key           Environment variable name (e.g. "PORT", "LISTEN_PORT")
 * @param default_value Value returned on missing/invalid input
 * @return Parsed port or default_value
 */
uint16_t env_config_get_port(const char *key, uint16_t default_value);

/**
 * Require an environment variable – returns NULL if unset or empty.
 * Ideal for secrets that must be present (e.g. GitHub Secrets injected
 * as env vars).  Callers can check for NULL and abort with a clear error.
 * @param key Environment variable name (e.g. "API_KEY", "DB_PASSWORD")
 * @return The value, or NULL when the variable is missing/empty
 */
const char *env_config_require(const char *key);

/**
 * Check whether an environment variable is set and non-empty.
 * Useful for checking secret presence without touching the value itself.
 * @param key Environment variable name
 * @return true when the variable exists and is non-empty
 */
bool env_config_is_set(const char *key);

/* ---- Secure secret handling ------------------------------------------ */

/*
 * For sensitive values (API keys, passwords, tokens) loaded from GitHub
 * Secrets or any environment variable, use the secure value API below.
 *
 * Why?
 *  - The secret is copied into a dedicated heap buffer.
 *  - env_secure_value_free() scrubs that buffer with zeros before freeing,
 *    using a compiler-barrier technique so the wipe cannot be optimised away.
 *  - env_config_redact() produces a masked version safe for logging.
 *  - This limits the window during which the plaintext secret resides in
 *    process memory and prevents accidental exposure in logs or core dumps.
 *
 * Usage:
 *   env_secure_value_t *key = env_config_get_secure("API_KEY");
 *   if (!key) { fprintf(stderr, "API_KEY required\n"); exit(1); }
 *   use_secret(env_secure_value_get(key));
 *   env_secure_value_free(key);   // secret wiped from memory
 */

/** Opaque handle to a securely-held secret value. */
typedef struct env_secure_value env_secure_value_t;

/**
 * Load a secret from an environment variable into a secure buffer.
 * The caller must free the result with env_secure_value_free().
 * @param key Environment variable name (e.g. "API_KEY")
 * @return Opaque handle, or NULL if the variable is missing/empty
 */
env_secure_value_t *env_config_get_secure(const char *key);

/**
 * Access the raw secret string held by a secure value.
 * @param sv Secure value handle (NULL-safe → returns NULL)
 * @return NUL-terminated secret string
 */
const char *env_secure_value_get(const env_secure_value_t *sv);

/**
 * Return the length of the secret (excluding NUL terminator).
 * @param sv Secure value handle (NULL-safe → returns 0)
 */
size_t env_secure_value_len(const env_secure_value_t *sv);

/**
 * Securely destroy a secret value – wipes memory with zeros before freeing.
 * Safe to call with NULL.
 * @param sv Secure value handle
 */
void env_secure_value_free(env_secure_value_t *sv);

/**
 * Return a heap-allocated redacted version of a value, safe for logging.
 * Short values (≤4 chars) become "***"; longer values show the first and
 * last character with asterisks in between (e.g. "s**********z").
 * Caller must free() the returned string.
 * @param value Raw secret string (NULL → returns NULL)
 * @return Redacted string, or NULL on error
 */
char *env_config_redact(const char *value);

/**
 * Convenience: apply standard WEBLIB_* environment variables to a server.
 * Reads WEBLIB_READ_TIMEOUT, WEBLIB_WRITE_TIMEOUT, WEBLIB_THREAD_COUNT,
 * WEBLIB_MAX_CONNECTIONS, and WEBLIB_ASYNC_MODE.  Unset variables are
 * silently skipped, so existing programmatic configuration is preserved.
 * @param server Server instance
 * @return 0 on success, -1 if server is NULL
 */
int http_server_apply_env(http_server_t *server);

/* ===== Security Utilities ===== */

/*
 * Core security primitives for building secure applications.
 * All functions are safe to call from any thread.
 */

/**
 * Securely wipe memory with zeros using a compiler-barrier technique.
 * The compiler cannot optimise this away (volatile pointer or memset_s).
 * Use to scrub secrets (passwords, keys, tokens) before freeing buffers.
 * @param ptr   Pointer to memory to wipe (NULL-safe → no-op)
 * @param len   Number of bytes to wipe
 */
void secure_zero(void *ptr, size_t len);

/**
 * Constant-time memory comparison – prevents timing side-channel attacks.
 * Always compares all bytes regardless of where the first difference is,
 * so an attacker cannot deduce partial matches from response time.
 * @param a   First buffer  (NULL → returns false)
 * @param b   Second buffer (NULL → returns false)
 * @param len Number of bytes to compare
 * @return true when all bytes are equal
 */
bool secure_compare(const void *a, const void *b, size_t len);

/**
 * Fill a buffer with cryptographically secure random bytes.
 * Uses /dev/urandom on POSIX or BCryptGenRandom on Windows.
 * Suitable for generating session IDs, CSRF tokens, nonces, etc.
 * @param buf  Destination buffer (must not be NULL)
 * @param len  Number of random bytes to generate (must be > 0)
 * @return 0 on success, -1 on failure
 */
int secure_random_bytes(void *buf, size_t len);

/* ===== Security Headers Middleware ===== */

/*
 * Automatically sets industry-standard security headers on every response:
 *
 *   Content-Security-Policy    — Restricts resource loading (XSS prevention)
 *   X-Content-Type-Options     — Prevents MIME type sniffing
 *   X-Frame-Options            — Clickjacking protection
 *   Referrer-Policy            — Controls referrer header leakage
 *   Permissions-Policy         — Disables dangerous browser features
 *   Strict-Transport-Security  — Forces HTTPS (opt-in via enable_hsts)
 *   X-XSS-Protection: 0       — Disables legacy XSS filter (CSP is preferred)
 *
 * Usage:
 *   security_headers_config_t cfg = {0};
 *   cfg.enable_hsts = true;
 *   cfg.content_security_policy = "default-src 'self'; script-src 'self'";
 *   middleware_fn_t sec = security_headers_middleware_create(&cfg);
 *   router_use_middleware(router, sec);
 *   // ... at shutdown:
 *   security_headers_middleware_destroy();
 */

/** Configuration for the security headers middleware. */
typedef struct security_headers_config {
    const char *content_security_policy; /**< CSP directive (NULL → "default-src 'self'") */
    const char *frame_options;           /**< X-Frame-Options value (NULL → "DENY") */
    const char *referrer_policy;         /**< Referrer-Policy value (NULL → "strict-origin-when-cross-origin") */
    const char *permissions_policy;      /**< Permissions-Policy value (NULL → "geolocation=(), camera=(), microphone=()") */
    bool   enable_hsts;                  /**< Set Strict-Transport-Security header */
    int    hsts_max_age;                 /**< HSTS max-age in seconds (0 → 31536000 = 1 year) */
    bool   hsts_include_subdomains;      /**< Add includeSubDomains to HSTS */
} security_headers_config_t;

/**
 * Create the security headers middleware.
 * Pass NULL config to use all defaults (CSP: default-src 'self', etc.).
 * @param config Configuration (NULL for safe defaults)
 * @return Middleware function pointer, or NULL on allocation failure
 */
middleware_fn_t security_headers_middleware_create(
        const security_headers_config_t *config);

/**
 * Destroy the security headers middleware and free its configuration.
 * Safe to call when the middleware was never created (no-op).
 */
void security_headers_middleware_destroy(void);

/* ===== Cloudflare Worker Runtime API ===== */

/**
 * Cloudflare Worker environment binding types.
 * Each binding corresponds to a Cloudflare service accessible
 * from the Worker's env object (env.MY_KV, env.MY_BUCKET, etc.).
 */
typedef enum {
    WORKER_BINDING_KV,          /**< KV Namespace           */
    WORKER_BINDING_R2,          /**< R2 Object Storage      */
    WORKER_BINDING_D1,          /**< D1 SQL Database         */
    WORKER_BINDING_QUEUE,       /**< Queue (producer/consumer) */
    WORKER_BINDING_SECRET,      /**< Secret / env variable   */
    WORKER_BINDING_SERVICE      /**< Service binding (Worker-to-Worker) */
} worker_binding_type_t;

/* Forward declarations — Worker types */
typedef struct worker_request    worker_request_t;
typedef struct worker_response   worker_response_t;
typedef struct worker_env        worker_env_t;
typedef struct worker_kv         worker_kv_t;
typedef struct worker_r2_bucket  worker_r2_bucket_t;
typedef struct worker_d1         worker_d1_t;
typedef struct worker_d1_stmt    worker_d1_stmt_t;
typedef struct worker_queue      worker_queue_t;

/**
 * Fetch handler signature for a Cloudflare Worker.
 * Called by the JS glue layer when the Worker receives a request.
 */
typedef worker_response_t *(*worker_fetch_handler_t)(
    worker_request_t *req, worker_env_t *env);

/* ----- Worker Request ----- */

/** Create a new Worker request with the given HTTP method and URL. */
worker_request_t *worker_request_create(const char *method, const char *url);

/** Destroy a Worker request and free all associated memory. */
void worker_request_destroy(worker_request_t *req);

/** Set a header on the Worker request. */
int worker_request_set_header(worker_request_t *req, const char *name,
                              const char *value);

/** Get a header value by name (case-insensitive). Returns NULL if absent. */
const char *worker_request_get_header(const worker_request_t *req,
                                      const char *name);

/** Set the request body. Pass NULL/0 to clear. */
int worker_request_set_body(worker_request_t *req, const char *body,
                            size_t len);

/** Get the HTTP method string (e.g. "GET", "POST"). */
const char *worker_request_get_method(const worker_request_t *req);

/** Get the request URL string. */
const char *worker_request_get_url(const worker_request_t *req);

/** Get the request body. If out_len is non-NULL, writes the body length. */
const char *worker_request_get_body(const worker_request_t *req,
                                    size_t *out_len);

/* ----- Worker Response ----- */

/** Create a new Worker response with the given HTTP status code. */
worker_response_t *worker_response_create(int status);

/** Destroy a Worker response and free all associated memory. */
void worker_response_destroy(worker_response_t *res);

/** Set a header on the Worker response. */
int worker_response_set_header(worker_response_t *res, const char *name,
                               const char *value);

/** Get a response header value by name (case-insensitive). */
const char *worker_response_get_header(const worker_response_t *res,
                                       const char *name);

/** Set the response body (binary). */
int worker_response_set_body(worker_response_t *res, const char *body,
                             size_t len);

/** Set the response body from a NUL-terminated string. */
int worker_response_set_body_text(worker_response_t *res, const char *text);

/** Set the response body to a JSON-serialised value (sets Content-Type). */
int worker_response_set_json(worker_response_t *res, json_value_t *json);

/** Get the response HTTP status code. */
int worker_response_get_status(const worker_response_t *res);

/** Get the response body. If out_len is non-NULL, writes the body length. */
const char *worker_response_get_body(const worker_response_t *res,
                                     size_t *out_len);

/* ----- Worker Environment ----- */

/** Create a new Worker environment container for service bindings. */
worker_env_t *worker_env_create(void);

/** Destroy the Worker environment (does NOT destroy bound handles). */
void worker_env_destroy(worker_env_t *env);

/** Register a named binding of the given type in the environment. */
int worker_env_add_binding(worker_env_t *env, const char *name,
                           worker_binding_type_t type, void *handle);

/** Look up a binding by name and expected type. Returns NULL if absent. */
void *worker_env_get_binding(const worker_env_t *env, const char *name,
                             worker_binding_type_t type);

/** Return the number of registered bindings. */
int worker_env_binding_count(const worker_env_t *env);

/* ----- Fetch Handler ----- */

/** Register a custom fetch handler for the Worker. */
void worker_set_fetch_handler(worker_fetch_handler_t handler);

/** Set the library router for automatic route matching in Workers. */
void worker_set_router(router_t *router);

/**
 * Handle an incoming Worker fetch event.  WASM-exported entry point.
 * If a custom handler is set, delegates to it; otherwise uses the
 * library router for route matching.
 */
WASM_EXPORT
worker_response_t *worker_handle_fetch(worker_request_t *req,
                                       worker_env_t *env);

/* ===== Cloudflare KV Namespace API ===== */

/** Options for worker_kv_put(). All fields optional (zero-init for defaults). */
typedef struct {
    int64_t  expiration;       /**< Unix timestamp for absolute expiration. */
    int      expiration_ttl;   /**< Seconds from now until expiration. */
    const char *metadata;      /**< Optional JSON metadata string (max 1024 B). */
} worker_kv_put_options_t;

/** Options for worker_kv_list(). All fields optional. */
typedef struct {
    const char *prefix;   /**< Only return keys starting with this prefix. */
    int         limit;    /**< Max keys to return (default 1000, max 1000). */
    int         cursor;   /**< Pagination cursor (from previous result). */
} worker_kv_list_options_t;

/** Result of worker_kv_list(). */
typedef struct {
    char **keys;          /**< Array of key strings (caller frees). */
    int    count;         /**< Number of keys returned. */
    bool   list_complete; /**< true if all matching keys returned. */
    int    cursor;        /**< Cursor for next page (0 if complete). */
} worker_kv_list_result_t;

/** Create a KV namespace handle (simulated in native builds). */
worker_kv_t *worker_kv_create(const char *namespace_name);

/** Destroy a KV namespace and free all stored entries. */
void worker_kv_destroy(worker_kv_t *kv);

/** Get the namespace name. */
const char *worker_kv_get_namespace(const worker_kv_t *kv);

/**
 * Get a value by key.  Returns a newly-allocated string the caller must
 * free, or NULL if the key does not exist or is expired.
 */
char *worker_kv_get(worker_kv_t *kv, const char *key);

/**
 * Get a value with its metadata.  Writes a newly-allocated metadata
 * JSON string to *out_metadata (or NULL).  Caller frees both.
 */
char *worker_kv_get_with_metadata(worker_kv_t *kv, const char *key,
                                  char **out_metadata);

/** Store a key-value pair with optional put options. */
int worker_kv_put(worker_kv_t *kv, const char *key, const char *value,
                  const worker_kv_put_options_t *opts);

/** Delete a key. Returns 0 on success, -1 if not found. */
int worker_kv_delete(worker_kv_t *kv, const char *key);

/** List keys with optional prefix/limit/cursor filtering. */
worker_kv_list_result_t *worker_kv_list(worker_kv_t *kv,
                                        const worker_kv_list_options_t *opts);

/** Free a KV list result (including all key strings). */
void worker_kv_list_result_destroy(worker_kv_list_result_t *result);

/* ===== Cloudflare R2 Object Storage API ===== */

/** An R2 object (returned by get/head/list). */
typedef struct {
    char    *key;           /**< Object key. */
    uint8_t *body;          /**< Object body (NULL for head/list). */
    size_t   body_len;      /**< Body length. */
    size_t   size;          /**< Total object size (available even without body). */
    char    *etag;          /**< ETag string. */
    char    *content_type;  /**< Content-Type header. */
    int64_t  uploaded;      /**< Upload timestamp (Unix epoch). */
} worker_r2_object_t;

/** Options for worker_r2_put(). */
typedef struct {
    const char *content_type;   /**< MIME type for the object. */
} worker_r2_put_options_t;

/** Options for worker_r2_list(). */
typedef struct {
    const char *prefix;   /**< Prefix filter. */
    int         limit;    /**< Max results (default 1000). */
    int         cursor;   /**< Pagination cursor. */
} worker_r2_list_options_t;

/** Result of worker_r2_list(). */
typedef struct {
    worker_r2_object_t **objects;   /**< Array of object pointers (metadata only). */
    int    count;                   /**< Number of objects returned. */
    bool   truncated;               /**< true if more results available. */
    int    cursor;                  /**< Cursor for next page. */
} worker_r2_list_result_t;

/** Create an R2 bucket handle. */
worker_r2_bucket_t *worker_r2_bucket_create(const char *bucket_name);

/** Destroy an R2 bucket and free all stored objects. */
void worker_r2_bucket_destroy(worker_r2_bucket_t *bucket);

/** Get the bucket name. */
const char *worker_r2_bucket_get_name(const worker_r2_bucket_t *bucket);

/** Get an object with body. Returns NULL if not found. Caller frees. */
worker_r2_object_t *worker_r2_get(worker_r2_bucket_t *bucket, const char *key);

/** Get object metadata only (no body). Returns NULL if not found. */
worker_r2_object_t *worker_r2_head(worker_r2_bucket_t *bucket, const char *key);

/** Store an object. Returns 0 on success. */
int worker_r2_put(worker_r2_bucket_t *bucket, const char *key,
                  const uint8_t *body, size_t body_len,
                  const worker_r2_put_options_t *opts);

/** Delete an object. Returns 0 on success, -1 if not found. */
int worker_r2_delete(worker_r2_bucket_t *bucket, const char *key);

/** List objects with optional prefix/limit/cursor filtering. */
worker_r2_list_result_t *worker_r2_list(worker_r2_bucket_t *bucket,
                                        const worker_r2_list_options_t *opts);

/** Free an R2 object. */
void worker_r2_object_destroy(worker_r2_object_t *obj);

/** Free an R2 list result (including all contained objects). */
void worker_r2_list_result_destroy(worker_r2_list_result_t *result);

/* ===== Cloudflare D1 Database API ===== */

/** Result of a D1 query execution. */
typedef struct {
    bool          success;        /**< Whether the statement succeeded. */
    json_value_t *results;        /**< JSON array of row objects (for SELECT). */
    int           meta_changes;   /**< Number of rows affected (INSERT/DELETE). */
} worker_d1_result_t;

/** Create a D1 database handle. */
worker_d1_t *worker_d1_create(const char *database_name);

/** Destroy a D1 database and free all in-memory tables. */
void worker_d1_destroy(worker_d1_t *db);

/** Get the database name. */
const char *worker_d1_get_name(const worker_d1_t *db);

/** Prepare a SQL statement.  Returns a statement handle. */
worker_d1_stmt_t *worker_d1_prepare(worker_d1_t *db, const char *sql);

/** Destroy a prepared statement. */
void worker_d1_stmt_destroy(worker_d1_stmt_t *stmt);

/** Bind a parameter (1-based index) to a statement. Pass NULL for SQL NULL. */
int worker_d1_stmt_bind(worker_d1_stmt_t *stmt, int index, const char *value);

/** Execute a prepared statement and return full result with metadata. */
worker_d1_result_t *worker_d1_stmt_run(worker_d1_stmt_t *stmt);

/** Execute a statement and return only the first row as a JSON object. */
json_value_t *worker_d1_stmt_first(worker_d1_stmt_t *stmt);

/** Execute a statement and return all rows as a JSON array. */
json_value_t *worker_d1_stmt_all(worker_d1_stmt_t *stmt);

/**
 * Execute raw SQL (no parameter binding).
 * Useful for DDL statements like CREATE TABLE.
 */
worker_d1_result_t *worker_d1_exec(worker_d1_t *db, const char *sql);

/**
 * Execute a batch of prepared statements in sequence.
 * Returns an array of results on success.
 * On failure returns NULL; statements executed before the failure are not
 * guaranteed to be rolled back.
 */
worker_d1_result_t **worker_d1_batch(worker_d1_t *db,
                                     worker_d1_stmt_t **stmts, int count,
                                     int *out_count);

/** Destroy a D1 result. */
void worker_d1_result_destroy(worker_d1_result_t *result);

/* ===== Cloudflare Queues API ===== */

/** A single queue message. */
typedef struct {
    char   *id;         /**< Unique message ID. */
    char   *body;       /**< Message body. */
    size_t  body_len;   /**< Body length. */
    int64_t timestamp;  /**< Enqueue timestamp (Unix epoch). */
    int     attempts;   /**< Delivery attempt count. */
    bool    acked;      /**< Whether message has been acknowledged. */
} worker_queue_message_t;

/** A batch of consumed messages. */
typedef struct {
    worker_queue_message_t **messages;  /**< Array of message pointers. */
    int                      count;     /**< Number of messages in batch. */
    char                    *queue_name;/**< Owned copy of the source queue name. */
} worker_queue_batch_t;

/** Create a queue handle. */
worker_queue_t *worker_queue_create(const char *queue_name);

/** Destroy a queue and free all pending messages. */
void worker_queue_destroy(worker_queue_t *q);

/** Get the queue name. */
const char *worker_queue_get_name(const worker_queue_t *q);

/** Get the current queue depth (pending messages). */
int worker_queue_get_depth(const worker_queue_t *q);

/** Enqueue a single message (binary body). */
int worker_queue_send(worker_queue_t *q, const char *body, size_t body_len);

/** Enqueue a text message. */
int worker_queue_send_text(worker_queue_t *q, const char *text);

/** Enqueue a JSON value (serialised automatically). */
int worker_queue_send_json(worker_queue_t *q, json_value_t *json);

/** Enqueue a batch of messages (up to 100). Atomic: all or none. */
int worker_queue_send_batch(worker_queue_t *q, const char **bodies,
                            const size_t *lengths, int count);

/**
 * Consume messages from the queue in a batch.
 * @param max_batch       Max messages to dequeue (1–100).
 * @param max_wait_seconds Ignored in in-memory impl.
 * @return Batch of messages.  Caller must destroy with
 *         worker_queue_batch_destroy().
 */
worker_queue_batch_t *worker_queue_consume(worker_queue_t *q, int max_batch,
                                           int max_wait_seconds);

/** Acknowledge a consumed message (marks as processed). */
void worker_queue_message_ack(worker_queue_message_t *msg);

/** Destroy a single queue message. */
void worker_queue_message_destroy(worker_queue_message_t *msg);

/** Destroy a consumed batch (and all messages within). */
void worker_queue_batch_destroy(worker_queue_batch_t *batch);

/* ===== WebAssembly Runtime API ===== */

/**
 * Get library version string (WASM-exported).
 */
WASM_EXPORT const char *wasm_weblib_version(void);

/**
 * Get WASM capability descriptor string.
 * Returns a versioned identifier with parenthesized capability list,
 * e.g. "weblib-wasm/1.0 (json,router,template,validation,cookie,body_parser,compression)"
 */
WASM_EXPORT const char *wasm_weblib_capabilities(void);

/**
 * Check whether a named capability is available in WASM builds.
 * @param name  Capability name (e.g. "json", "router")
 * @return true if the capability is available
 */
WASM_EXPORT bool wasm_weblib_has_capability(const char *name);

/* WASM JSON wrappers */
WASM_EXPORT json_value_t *wasm_json_parse(const char *input);
WASM_EXPORT char         *wasm_json_stringify(json_value_t *value);
WASM_EXPORT json_value_t *wasm_json_object_create(void);
WASM_EXPORT json_value_t *wasm_json_array_create(void);
WASM_EXPORT json_value_t *wasm_json_string_create(const char *value);
WASM_EXPORT json_value_t *wasm_json_number_create(double value);
WASM_EXPORT json_value_t *wasm_json_bool_create(bool value);
WASM_EXPORT json_value_t *wasm_json_null_create(void);
WASM_EXPORT void          wasm_json_object_set(json_value_t *obj,
                                               const char *key,
                                               json_value_t *value);
WASM_EXPORT json_value_t *wasm_json_object_get(json_value_t *obj,
                                               const char *key);
WASM_EXPORT void          wasm_json_array_push(json_value_t *arr,
                                               json_value_t *value);
WASM_EXPORT void          wasm_json_free(json_value_t *value);

/* WASM Router wrappers */
WASM_EXPORT router_t *wasm_router_create(void);
WASM_EXPORT void      wasm_router_destroy(router_t *router);
WASM_EXPORT int       wasm_router_add_route(router_t *router,
                                            http_method_t method,
                                            const char *path,
                                            route_handler_t handler);

/* WASM Input Validation wrappers */
WASM_EXPORT bool wasm_validate_email(const char *email);
WASM_EXPORT bool wasm_validate_integer(const char *str, int *out);
WASM_EXPORT bool wasm_validate_string_length(const char *str,
                                             size_t min, size_t max);

/* WASM Template wrappers */
WASM_EXPORT template_context_t *wasm_template_context_create(void);
WASM_EXPORT void  wasm_template_context_set(template_context_t *ctx,
                                            const char *name,
                                            const char *value);
WASM_EXPORT char *wasm_template_render(const char *tmpl,
                                       template_context_t *ctx);
WASM_EXPORT void  wasm_template_context_destroy(template_context_t *ctx);

/* WASM Memory helper (free strings returned by wasm_json_stringify, etc.) */
WASM_EXPORT void wasm_free(void *ptr);

#ifdef __cplusplus
}
#endif

#endif /* KAMRAN_K_H */
