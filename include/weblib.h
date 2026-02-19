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

#ifdef __cplusplus
}
#endif

#endif /* WEBLIB_H */
