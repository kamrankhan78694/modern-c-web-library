#include "weblib.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
#include <ctype.h>
#include <strings.h>

#define MAX_CONNECTIONS 128
#define READ_BUFFER_SIZE 8192
#define MAX_REQUEST_LINE_LEN 4096
#define MAX_HEADER_LINE_LEN 8192
#define MAX_HEADER_COUNT 100
#define MAX_HEADER_BYTES 16384
#define MAX_BODY_BYTES (1024 * 1024) /* 1 MiB */
#define MAX_REQUEST_BUFFER (MAX_BODY_BYTES + MAX_HEADER_BYTES)

typedef struct http_header_node {
    char *name;      /* lower-case for lookup */
    char *raw_name;  /* original casing for serialization */
    char *value;
    struct http_header_node *next;
} http_header_node_t;

typedef struct http_param_node {
    char *key;
    char *value;
    struct http_param_node *next;
} http_param_node_t;

typedef enum {
    PARSE_STATE_REQUEST_LINE,
    PARSE_STATE_HEADERS,
    PARSE_STATE_BODY,
    PARSE_STATE_CHUNK_SIZE,
    PARSE_STATE_CHUNK_DATA,
    PARSE_STATE_CHUNK_CRLF,
    PARSE_STATE_CHUNK_TRAILERS,
    PARSE_STATE_COMPLETE,
    PARSE_STATE_ERROR
} parse_state_t;

typedef enum {
    PARSER_INCOMPLETE = 0,
    PARSER_COMPLETE,
    PARSER_ERROR
} parser_result_t;

typedef struct http_parser {
    parse_state_t state;
    http_request_t *req;
    char *buffer;
    size_t buffer_len;
    size_t buffer_cap;
    size_t total_bytes;
    size_t header_count;
    size_t content_length;
    size_t body_received;
    size_t body_capacity;
    bool chunked;
    size_t current_chunk_size;
    size_t current_chunk_received;
    bool keep_alive;
    bool seen_host;
    http_status_t error_status;
    const char *error_message;
} http_parser_t;

/* Internal server structure */
struct http_server {
    int socket_fd;
    uint16_t port;
    router_t *router;
    bool running;
    pthread_t accept_thread;
    
    /* Async I/O support */
    bool async_mode;
    event_loop_t *event_loop;
};

/* Connection handler data */
typedef struct {
    int client_fd;
    http_server_t *server;
    http_parser_t parser;
    http_request_t *request;
    http_response_t *response;
} connection_t;

/* Async connection context */
typedef struct async_connection {
    int client_fd;
    http_server_t *server;
    http_parser_t parser;
    http_request_t *request;
    http_response_t *response;
    char *header_buf;
    size_t header_len;
    size_t header_sent;
    size_t body_sent;
    bool parser_initialized;
    bool response_ready;
    bool keep_alive;
    bool closing;
} async_connection_t;

/* Forward declarations */
static void *accept_connections(void *arg);
static void *handle_connection(void *arg);
static bool response_forces_close(http_response_t *res);
static void send_response(int client_fd, http_response_t *res, bool keep_alive);
static void send_error_response(int client_fd, http_status_t status, const char *message);
static http_request_t *http_request_create(void);
static void http_request_destroy(http_request_t *req);
static http_response_t *http_response_create(void);
static void http_response_destroy(http_response_t *res);

static void http_parser_init(http_parser_t *parser, http_request_t *req);
static void http_parser_reset(http_parser_t *parser, http_request_t *req, bool preserve_buffer);
static void http_parser_destroy(http_parser_t *parser);
static parser_result_t http_parser_execute(http_parser_t *parser, const char *data, size_t len);

static void header_list_free(http_header_node_t *head);
static http_header_node_t *header_list_find(http_header_node_t *head, const char *name);
static int header_list_add(http_header_node_t **head_ref, const char *name, const char *raw_name, const char *value, bool replace_existing);
static int append_body_data(http_parser_t *parser, const char *data, size_t len);
static void param_list_free(http_param_node_t *head);
static const char *param_list_get(http_param_node_t *head, const char *key);
static int param_list_set(http_param_node_t **head_ref, const char *key, const char *value);

static int set_nonblocking(int fd);
static void async_accept_handler(int fd, int events, void *user_data);
static void async_read_handler(int fd, int events, void *user_data);
static void async_write_handler(int fd, int events, void *user_data);
static void free_async_connection(async_connection_t *conn);
static bool async_on_parser_result(async_connection_t *conn, int fd, parser_result_t result);

/* Create HTTP server */
http_server_t *http_server_create(void) {
    http_server_t *server = (http_server_t *)calloc(1, sizeof(http_server_t));
    if (!server) {
        return NULL;
    }
    
    server->socket_fd = -1;
    server->running = false;
    server->router = NULL;
    server->async_mode = false;
    server->event_loop = NULL;
    
    return server;
}

/* Start listening on port */
int http_server_listen(http_server_t *server, uint16_t port) {
    if (!server) {
        return -1;
    }
    
    /* Create socket */
    server->socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server->socket_fd < 0) {
        perror("socket creation failed");
        return -1;
    }
    
    /* Set socket options */
    int opt = 1;
    if (setsockopt(server->socket_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt failed");
        close(server->socket_fd);
        return -1;
    }
    
    /* Bind socket */
    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);
    
    if (bind(server->socket_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind failed");
        close(server->socket_fd);
        return -1;
    }
    
    /* Listen for connections */
    if (listen(server->socket_fd, MAX_CONNECTIONS) < 0) {
        perror("listen failed");
        close(server->socket_fd);
        return -1;
    }
    
    server->port = port;
    server->running = true;
    
    if (server->async_mode) {
        /* Async mode - use event loop */
        printf("HTTP server listening on port %d (async mode)\n", port);
        
        /* Set server socket to non-blocking */
        if (set_nonblocking(server->socket_fd) < 0) {
            perror("Failed to set server socket to non-blocking");
            close(server->socket_fd);
            return -1;
        }
        
        /* Add server socket to event loop */
        if (event_loop_add_fd(server->event_loop, server->socket_fd, EVENT_READ, 
                             async_accept_handler, server) < 0) {
            fprintf(stderr, "Failed to add server socket to event loop\n");
            close(server->socket_fd);
            return -1;
        }
        
        /* Run event loop in main thread */
        event_loop_run(server->event_loop);
    } else {
        /* Traditional threaded mode */
        printf("HTTP server listening on port %d (threaded mode)\n", port);
        
        /* Start accept thread */
        if (pthread_create(&server->accept_thread, NULL, accept_connections, server) != 0) {
            perror("pthread_create failed");
            server->running = false;
            close(server->socket_fd);
            return -1;
        }
    }
    
    return 0;
}

/* Stop the server */
void http_server_stop(http_server_t *server) {
    if (!server || !server->running) {
        return;
    }
    
    server->running = false;
    
    if (server->async_mode) {
        /* Stop event loop */
        if (server->event_loop) {
            event_loop_stop(server->event_loop);
        }
    } else {
        /* Stop accept thread */
        pthread_join(server->accept_thread, NULL);
    }
    
    if (server->socket_fd >= 0) {
        close(server->socket_fd);
        server->socket_fd = -1;
    }
    
    printf("HTTP server stopped\n");
}

/* Destroy server */
void http_server_destroy(http_server_t *server) {
    if (!server) {
        return;
    }
    
    if (server->running) {
        http_server_stop(server);
    }
    
    if (server->event_loop) {
        event_loop_destroy(server->event_loop);
        server->event_loop = NULL;
    }
    
    free(server);
}

/* Set router */
void http_server_set_router(http_server_t *server, router_t *router) {
    if (server) {
        server->router = router;
    }
}

/* Accept connections thread */
static void *accept_connections(void *arg) {
    http_server_t *server = (http_server_t *)arg;
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    
    while (server->running) {
        int client_fd = accept(server->socket_fd, (struct sockaddr *)&client_addr, &client_len);
        
        if (client_fd < 0) {
            if (server->running) {
                perror("accept failed");
            }
            continue;
        }
        
        /* Create connection handler thread */
        connection_t *conn = (connection_t *)calloc(1, sizeof(connection_t));
        if (conn) {
            conn->client_fd = client_fd;
            conn->server = server;
            
            pthread_t thread;
            if (pthread_create(&thread, NULL, handle_connection, conn) != 0) {
                perror("pthread_create for connection failed");
                close(client_fd);
                free(conn);
            } else {
                pthread_detach(thread);
            }
        } else {
            close(client_fd);
        }
    }
    
    return NULL;
}

/* Handle single connection */
static void *handle_connection(void *arg) {
    connection_t *conn = (connection_t *)arg;
    int client_fd = conn->client_fd;
    http_server_t *server = conn->server;
    bool parser_initialized = false;
    bool connection_open = true;
    char read_buf[READ_BUFFER_SIZE];

    while (connection_open) {
        parser_result_t result = PARSER_INCOMPLETE;
        bool keep_alive = false;

        conn->request = http_request_create();
        conn->response = http_response_create();
        if (!conn->request || !conn->response) {
            send_error_response(client_fd, HTTP_INTERNAL_ERROR, "Internal Server Error");
            connection_open = false;
            goto iteration_cleanup;
        }

        if (!parser_initialized) {
            http_parser_init(&conn->parser, conn->request);
            parser_initialized = true;
            if (conn->parser.state == PARSE_STATE_ERROR) {
                send_error_response(client_fd, conn->parser.error_status, conn->parser.error_message);
                connection_open = false;
                goto iteration_cleanup;
            }
        } else {
            http_parser_reset(&conn->parser, conn->request, true);
        }

        if (conn->parser.buffer_len > 0) {
            result = http_parser_execute(&conn->parser, NULL, 0);
        }

        while (result == PARSER_INCOMPLETE) {
            ssize_t bytes_read = recv(client_fd, read_buf, sizeof(read_buf), 0);
            if (bytes_read < 0) {
                if (errno == EINTR) {
                    continue;
                }
                perror("recv failed");
                send_error_response(client_fd, HTTP_INTERNAL_ERROR, "Socket read error");
                connection_open = false;
                goto iteration_cleanup;
            }
            if (bytes_read == 0) {
                connection_open = false;
                goto iteration_cleanup;
            }

            result = http_parser_execute(&conn->parser, read_buf, (size_t)bytes_read);
        }

        if (result == PARSER_ERROR) {
            http_status_t status = conn->parser.error_status ? conn->parser.error_status : HTTP_BAD_REQUEST;
            send_error_response(client_fd, status, conn->parser.error_message);
            connection_open = false;
            goto iteration_cleanup;
        }

        if (server->router) {
            if (router_route(server->router, conn->request, conn->response) < 0) {
                http_response_send_text(conn->response, HTTP_NOT_FOUND, "Not Found");
            }
        } else {
            http_response_send_text(conn->response, HTTP_NOT_FOUND, "Not Found");
        }

        keep_alive = conn->parser.keep_alive && !response_forces_close(conn->response);
        send_response(client_fd, conn->response, keep_alive);

        if (!keep_alive) {
            connection_open = false;
        }

    iteration_cleanup:
        http_request_destroy(conn->request);
        http_response_destroy(conn->response);
        conn->request = NULL;
        conn->response = NULL;

        if (!connection_open) {
            break;
        }
    }

    if (parser_initialized) {
        http_parser_destroy(&conn->parser);
    }
    close(client_fd);
    free(conn);
    return NULL;
}

/* Parse HTTP request */
static char *lowercase_dup(const char *src) {
    if (!src) {
        return NULL;
    }
    size_t len = strlen(src);
    char *dest = (char *)malloc(len + 1);
    if (!dest) {
        return NULL;
    }
    for (size_t i = 0; i < len; i++) {
        dest[i] = (char)tolower((unsigned char)src[i]);
    }
    dest[len] = '\0';
    return dest;
}

static const char *status_reason_phrase(http_status_t status) {
    switch (status) {
        case HTTP_OK: return "OK";
        case HTTP_CREATED: return "Created";
        case HTTP_ACCEPTED: return "Accepted";
        case HTTP_NO_CONTENT: return "No Content";
        case HTTP_BAD_REQUEST: return "Bad Request";
        case HTTP_UNAUTHORIZED: return "Unauthorized";
        case HTTP_FORBIDDEN: return "Forbidden";
        case HTTP_NOT_FOUND: return "Not Found";
        case HTTP_METHOD_NOT_ALLOWED: return "Method Not Allowed";
        case HTTP_INTERNAL_ERROR: return "Internal Server Error";
        case HTTP_NOT_IMPLEMENTED: return "Not Implemented";
        case HTTP_BAD_GATEWAY: return "Bad Gateway";
        case HTTP_SERVICE_UNAVAILABLE: return "Service Unavailable";
        default: return "OK";
    }
}

static void header_list_free(http_header_node_t *head) {
    while (head) {
        http_header_node_t *next = head->next;
        free(head->name);
        free(head->raw_name);
        free(head->value);
        free(head);
        head = next;
    }
}

static http_header_node_t *header_list_find(http_header_node_t *head, const char *name_lower) {
    for (http_header_node_t *node = head; node; node = node->next) {
        if (strcmp(node->name, name_lower) == 0) {
            return node;
        }
    }
    return NULL;
}

static int header_list_add(http_header_node_t **head_ref, const char *name, const char *raw_name, const char *value, bool replace_existing) {
    if (!head_ref || !name || !value) {
        return -1;
    }

    char *lower = lowercase_dup(name);
    if (!lower) {
        return -1;
    }

    http_header_node_t *existing = header_list_find(*head_ref, lower);
    if (existing && replace_existing) {
        char *new_value = strdup(value);
        if (!new_value) {
            free(lower);
            return -1;
        }
        free(existing->value);
        existing->value = new_value;
        if (raw_name) {
            char *new_raw = strdup(raw_name);
            if (!new_raw) {
                free(lower);
                return -1;
            }
            free(existing->raw_name);
            existing->raw_name = new_raw;
        }
        free(lower);
        return 0;
    }

    http_header_node_t *node = (http_header_node_t *)calloc(1, sizeof(http_header_node_t));
    if (!node) {
        free(lower);
        return -1;
    }

    node->name = lower;
    node->raw_name = raw_name ? strdup(raw_name) : strdup(name);
    if (!node->raw_name) {
        free(node->name);
        free(node);
        return -1;
    }
    node->value = strdup(value);
    if (!node->value) {
        free(node->raw_name);
        free(node->name);
        free(node);
        return -1;
    }

    node->next = NULL;

    if (!*head_ref) {
        *head_ref = node;
    } else {
        http_header_node_t *tail = *head_ref;
        while (tail->next) {
            tail = tail->next;
        }
        tail->next = node;
    }
    return 0;
}

static void param_list_free(http_param_node_t *head) {
    while (head) {
        http_param_node_t *next = head->next;
        free(head->key);
        free(head->value);
        free(head);
        head = next;
    }
}

static const char *param_list_get(http_param_node_t *head, const char *key) {
    for (http_param_node_t *node = head; node; node = node->next) {
        if (strcmp(node->key, key) == 0) {
            return node->value;
        }
    }
    return NULL;
}

static int param_list_set(http_param_node_t **head_ref, const char *key, const char *value) {
    if (!head_ref || !key || !value) {
        return -1;
    }

    for (http_param_node_t *node = *head_ref; node; node = node->next) {
        if (strcmp(node->key, key) == 0) {
            char *new_val = strdup(value);
            if (!new_val) {
                return -1;
            }
            free(node->value);
            node->value = new_val;
            return 0;
        }
    }

    http_param_node_t *node = (http_param_node_t *)calloc(1, sizeof(http_param_node_t));
    if (!node) {
        return -1;
    }

    node->key = strdup(key);
    node->value = strdup(value);
    if (!node->key || !node->value) {
        free(node->key);
        free(node->value);
        free(node);
        return -1;
    }

    node->next = *head_ref;
    *head_ref = node;
    return 0;
}

static http_request_t *http_request_create(void) {
    http_request_t *req = (http_request_t *)calloc(1, sizeof(http_request_t));
    return req;
}

static void http_request_destroy(http_request_t *req) {
    if (!req) {
        return;
    }
    free(req->path);
    free(req->query_string);
    free(req->body);
    header_list_free((http_header_node_t *)req->headers);
    param_list_free((http_param_node_t *)req->params);
    free(req);
}

static http_response_t *http_response_create(void) {
    http_response_t *res = (http_response_t *)calloc(1, sizeof(http_response_t));
    if (res) {
        res->status = HTTP_OK;
    }
    return res;
}

static void http_response_destroy(http_response_t *res) {
    if (!res) {
        return;
    }
    header_list_free((http_header_node_t *)res->headers);
    free(res->body);
    free(res);
}

static int send_all(int fd, const char *buf, size_t len) {
    size_t sent_total = 0;
    while (sent_total < len) {
        ssize_t sent = send(fd, buf + sent_total, len - sent_total, 0);
        if (sent < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        sent_total += (size_t)sent;
    }
    return 0;
}

static int serialize_response(http_response_t *res, bool keep_alive, char **header_out, size_t *header_len_out) {
    if (!res || !header_out || !header_len_out) {
        return -1;
    }

    if (res->status == 0) {
        res->status = HTTP_OK;
    }

    http_header_node_t **headers_ref = (http_header_node_t **)&res->headers;

    char date_buf[64];
    time_t now = time(NULL);
    struct tm tm_buf;
    struct tm *tm_info = gmtime_r(&now, &tm_buf);
    if (tm_info) {
        strftime(date_buf, sizeof(date_buf), "%a, %d %b %Y %H:%M:%S GMT", tm_info);
        if (header_list_add(headers_ref, "date", "Date", date_buf, true) < 0) {
            return -1;
        }
    }

    char content_length_buf[32];
    snprintf(content_length_buf, sizeof(content_length_buf), "%zu", res->body_length);
    if (header_list_add(headers_ref, "content-length", "Content-Length", content_length_buf, true) < 0) {
        return -1;
    }

    if (header_list_add(headers_ref, "connection", "Connection", keep_alive ? "keep-alive" : "close", true) < 0) {
        return -1;
    }

    size_t header_capacity = 256;
    size_t header_len = 0;
    char *header_buf = (char *)malloc(header_capacity);
    if (!header_buf) {
        return -1;
    }

    const char *reason = status_reason_phrase(res->status);
    int written = snprintf(header_buf, header_capacity, "HTTP/1.1 %d %s\r\n", res->status, reason);
    if (written < 0) {
        free(header_buf);
        return -1;
    }
    header_len = (size_t)written;

    for (http_header_node_t *node = *headers_ref; node; node = node->next) {
        size_t need = header_len + strlen(node->raw_name) + strlen(node->value) + 4;
        if (need >= header_capacity) {
            size_t new_cap = header_capacity * 2;
            while (need >= new_cap) {
                new_cap *= 2;
            }
            char *tmp = (char *)realloc(header_buf, new_cap);
            if (!tmp) {
                free(header_buf);
                return -1;
            }
            header_buf = tmp;
            header_capacity = new_cap;
        }
        header_len += (size_t)sprintf(header_buf + header_len, "%s: %s\r\n", node->raw_name, node->value);
    }

    if (header_len + 2 >= header_capacity) {
        char *tmp = (char *)realloc(header_buf, header_capacity + 2);
        if (!tmp) {
            free(header_buf);
            return -1;
        }
        header_buf = tmp;
        header_capacity += 2;
    }
    memcpy(header_buf + header_len, "\r\n", 2);
    header_len += 2;

    *header_out = header_buf;
    *header_len_out = header_len;
    return 0;
}

static bool response_forces_close(http_response_t *res) {
    if (!res || !res->headers) {
        return false;
    }
    for (http_header_node_t *node = (http_header_node_t *)res->headers; node; node = node->next) {
        if (strcmp(node->name, "connection") == 0) {
            if (strcasecmp(node->value, "close") == 0) {
                return true;
            }
            if (strcasecmp(node->value, "keep-alive") == 0) {
                return false;
            }
        }
    }
    return false;
}

static void send_response(int client_fd, http_response_t *res, bool keep_alive) {
    if (!res || res->sent) {
        return;
    }

    char *header_buf = NULL;
    size_t header_len = 0;
    if (serialize_response(res, keep_alive, &header_buf, &header_len) < 0) {
        return;
    }

    if (send_all(client_fd, header_buf, header_len) == 0) {
        if (res->body && res->body_length > 0) {
            send_all(client_fd, res->body, res->body_length);
        }
    }

    free(header_buf);
    res->sent = true;
}

static void send_error_response(int client_fd, http_status_t status, const char *message) {
    http_response_t *res = http_response_create();
    if (!res) {
        return;
    }
    const char *body = message ? message : "";
    http_response_send_text(res, status, body);
    send_response(client_fd, res, false);
    http_response_destroy(res);
}

void http_response_send_text(http_response_t *res, http_status_t status, const char *text) {
    if (!res || !text) {
        return;
    }
    char *copy = strdup(text);
    if (!copy) {
        return;
    }
    free(res->body);
    res->body = copy;
    res->body_length = strlen(text);
    res->status = status;
}

void http_response_set_header(http_response_t *res, const char *key, const char *value) {
    if (!res || !key || !value) {
        return;
    }
    header_list_add((http_header_node_t **)&res->headers, key, key, value, true);
}

const char *http_request_get_header(http_request_t *req, const char *key) {
    if (!req || !key) {
        return NULL;
    }
    char *lower = lowercase_dup(key);
    if (!lower) {
        return NULL;
    }
    http_header_node_t *node = header_list_find((http_header_node_t *)req->headers, lower);
    free(lower);
    return node ? node->value : NULL;
}

const char *http_request_get_param(http_request_t *req, const char *key) {
    if (!req || !key) {
        return NULL;
    }
    return param_list_get((http_param_node_t *)req->params, key);
}

int http_request_set_param(http_request_t *req, const char *key, const char *value) {
    if (!req || !key || !value) {
        return -1;
    }
    return param_list_set((http_param_node_t **)&req->params, key, value);
}

static void parser_set_error(http_parser_t *parser, http_status_t status, const char *message) {
    parser->state = PARSE_STATE_ERROR;
    parser->error_status = status;
    parser->error_message = message;
}

static int ensure_buffer_capacity(http_parser_t *parser, size_t additional) {
    size_t required = parser->buffer_len + additional;
    if (required > MAX_REQUEST_BUFFER) {
        parser_set_error(parser, (http_status_t)413, "Payload Too Large");
        return -1;
    }

    if (required <= parser->buffer_cap) {
        return 0;
    }

    size_t new_cap = parser->buffer_cap ? parser->buffer_cap : READ_BUFFER_SIZE;
    while (new_cap < required) {
        new_cap *= 2;
        if (new_cap > MAX_REQUEST_BUFFER) {
            new_cap = MAX_REQUEST_BUFFER;
            break;
        }
    }

    char *tmp = (char *)realloc(parser->buffer, new_cap);
    if (!tmp) {
        parser_set_error(parser, HTTP_INTERNAL_ERROR, "Out of memory");
        return -1;
    }

    parser->buffer = tmp;
    parser->buffer_cap = new_cap;
    return 0;
}

static ssize_t find_crlf(const char *buf, size_t len) {
    for (size_t i = 0; i + 1 < len; i++) {
        if (buf[i] == '\r' && buf[i + 1] == '\n') {
            return (ssize_t)i;
        }
    }
    return -1;
}

static void parser_consume(http_parser_t *parser, size_t count) {
    if (count == 0) {
        return;
    }
    if (count >= parser->buffer_len) {
        parser->buffer_len = 0;
        return;
    }
    memmove(parser->buffer, parser->buffer + count, parser->buffer_len - count);
    parser->buffer_len -= count;
}

static bool assign_http_method(http_request_t *req, const char *method) {
    static const struct {
        const char *name;
        http_method_t value;
    } methods[] = {
        {"GET", HTTP_GET},
        {"POST", HTTP_POST},
        {"PUT", HTTP_PUT},
        {"DELETE", HTTP_DELETE},
        {"PATCH", HTTP_PATCH},
        {"HEAD", HTTP_HEAD},
        {"OPTIONS", HTTP_OPTIONS}
    };

    for (size_t i = 0; i < sizeof(methods) / sizeof(methods[0]); i++) {
        if (strcmp(methods[i].name, method) == 0) {
            req->method = methods[i].value;
            return true;
        }
    }
    return false;
}

static int parse_request_line(http_parser_t *parser) {
    ssize_t crlf_index = find_crlf(parser->buffer, parser->buffer_len);
    if (crlf_index < 0) {
        return 0;
    }

    if ((size_t)crlf_index > MAX_REQUEST_LINE_LEN) {
        parser_set_error(parser, (http_status_t)414, "Request-URI Too Long");
        return -1;
    }

    char *line = (char *)malloc((size_t)crlf_index + 1);
    if (!line) {
        parser_set_error(parser, HTTP_INTERNAL_ERROR, "Out of memory");
        return -1;
    }
    memcpy(line, parser->buffer, (size_t)crlf_index);
    line[crlf_index] = '\0';

    char *method = line;
    char *sp1 = strchr(line, ' ');
    if (!sp1) {
        free(line);
        parser_set_error(parser, HTTP_BAD_REQUEST, "Malformed request line");
        return -1;
    }
    *sp1 = '\0';
    char *target = sp1 + 1;
    while (*target == ' ') {
        target++;
    }
    char *sp2 = strchr(target, ' ');
    if (!sp2) {
        free(line);
        parser_set_error(parser, HTTP_BAD_REQUEST, "Malformed request line");
        return -1;
    }
    *sp2 = '\0';
    char *version = sp2 + 1;
    while (*version == ' ') {
        version++;
    }

    if (!assign_http_method(parser->req, method)) {
        free(line);
        parser_set_error(parser, HTTP_NOT_IMPLEMENTED, "Unsupported HTTP method");
        return -1;
    }

    if (strlen(target) == 0 || target[0] != '/') {
        free(line);
        parser_set_error(parser, HTTP_BAD_REQUEST, "Invalid request target");
        return -1;
    }

    if (strlen(target) > MAX_REQUEST_LINE_LEN) {
        free(line);
        parser_set_error(parser, (http_status_t)414, "Request-URI Too Long");
        return -1;
    }

    char *query = strchr(target, '?');
    if (query) {
        *query = '\0';
        query++;
        free(parser->req->query_string);
        parser->req->query_string = strdup(query);
        if (!parser->req->query_string) {
            free(line);
            parser_set_error(parser, HTTP_INTERNAL_ERROR, "Out of memory");
            return -1;
        }
    }

    free(parser->req->path);
    parser->req->path = strdup(target);
    if (!parser->req->path) {
        free(line);
        parser_set_error(parser, HTTP_INTERNAL_ERROR, "Out of memory");
        return -1;
    }

    if (strcmp(version, "HTTP/1.1") == 0) {
        parser->keep_alive = true;
    } else if (strcmp(version, "HTTP/1.0") == 0) {
        parser->keep_alive = false;
    } else {
        free(line);
        parser_set_error(parser, HTTP_BAD_REQUEST, "Unsupported HTTP version");
        return -1;
    }

    free(line);
    parser_consume(parser, (size_t)crlf_index + 2);
    parser->state = PARSE_STATE_HEADERS;
    return 1;
}

static int parse_header_line(http_parser_t *parser, const char *line, size_t len) {
    const char *colon = memchr(line, ':', len);
    if (!colon) {
        parser_set_error(parser, HTTP_BAD_REQUEST, "Malformed header line");
        return -1;
    }

    size_t name_len = (size_t)(colon - line);
    while (name_len > 0 && isspace((unsigned char)line[name_len - 1])) {
        name_len--;
    }
    if (name_len == 0) {
        parser_set_error(parser, HTTP_BAD_REQUEST, "Empty header name");
        return -1;
    }

    const char *value_start = colon + 1;
    while ((size_t)(value_start - line) < len && (*value_start == ' ' || *value_start == '\t')) {
        value_start++;
    }
    const char *value_end = line + len;
    while (value_end > value_start && isspace((unsigned char)value_end[-1])) {
        value_end--;
    }

    size_t value_len = (size_t)(value_end - value_start);

    char *name_buf = (char *)malloc(name_len + 1);
    char *value_buf = (char *)malloc(value_len + 1);
    if (!name_buf || !value_buf) {
        free(name_buf);
        free(value_buf);
        parser_set_error(parser, HTTP_INTERNAL_ERROR, "Out of memory");
        return -1;
    }

    memcpy(name_buf, line, name_len);
    name_buf[name_len] = '\0';
    memcpy(value_buf, value_start, value_len);
    value_buf[value_len] = '\0';

    bool replace = (strcasecmp(name_buf, "set-cookie") != 0);
    if (header_list_add((http_header_node_t **)&parser->req->headers, name_buf, name_buf, value_buf, replace) < 0) {
        free(name_buf);
        free(value_buf);
        parser_set_error(parser, HTTP_INTERNAL_ERROR, "Out of memory");
        return -1;
    }

    if (strcasecmp(name_buf, "content-length") == 0) {
        char *endptr = NULL;
        unsigned long long val = strtoull(value_buf, &endptr, 10);
        if (endptr == value_buf || *endptr != '\0') {
            parser_set_error(parser, HTTP_BAD_REQUEST, "Invalid Content-Length header");
            return -1;
        }
        if (val > MAX_BODY_BYTES) {
            parser_set_error(parser, (http_status_t)413, "Payload Too Large");
            return -1;
        }
        parser->content_length = (size_t)val;
    } else if (strcasecmp(name_buf, "transfer-encoding") == 0) {
        if (strstr(value_buf, "chunked") != NULL) {
            parser->chunked = true;
            parser->content_length = 0;
        }
    } else if (strcasecmp(name_buf, "connection") == 0) {
        if (strcasestr(value_buf, "close")) {
            parser->keep_alive = false;
        } else if (strcasestr(value_buf, "keep-alive")) {
            parser->keep_alive = true;
        }
    } else if (strcasecmp(name_buf, "host") == 0) {
        parser->seen_host = true;
    }

    free(name_buf);
    free(value_buf);
    return 0;
}

static int append_body_data(http_parser_t *parser, const char *data, size_t len) {
    if (parser->body_received + len > MAX_BODY_BYTES) {
        parser_set_error(parser, (http_status_t)413, "Payload Too Large");
        return -1;
    }

    size_t needed = parser->body_received + len + 1;
    if (needed > parser->body_capacity) {
        size_t new_cap = parser->body_capacity ? parser->body_capacity : READ_BUFFER_SIZE;
        while (new_cap < needed) {
            new_cap *= 2;
            if (new_cap > MAX_BODY_BYTES + 1) {
                new_cap = MAX_BODY_BYTES + 1;
                break;
            }
        }
        char *tmp = (char *)realloc(parser->req->body, new_cap);
        if (!tmp) {
            parser_set_error(parser, HTTP_INTERNAL_ERROR, "Out of memory");
            return -1;
        }
        parser->req->body = tmp;
        parser->body_capacity = new_cap;
    }

    memcpy(parser->req->body + parser->body_received, data, len);
    parser->body_received += len;
    parser->req->body[parser->body_received] = '\0';
    parser->req->body_length = parser->body_received;
    return 0;
}

static int parse_headers(http_parser_t *parser) {
    while (1) {
        ssize_t crlf_index = find_crlf(parser->buffer, parser->buffer_len);
        if (crlf_index < 0) {
            if (parser->buffer_len > MAX_HEADER_BYTES) {
                parser_set_error(parser, (http_status_t)431, "Request Header Fields Too Large");
                return -1;
            }
            return 0;
        }

        if (crlf_index == 0) {
            parser_consume(parser, 2);
            if (parser->keep_alive && !parser->seen_host) {
                parser_set_error(parser, HTTP_BAD_REQUEST, "Missing Host header");
                return -1;
            }
            if (parser->chunked) {
                parser->state = PARSE_STATE_CHUNK_SIZE;
            } else if (parser->content_length > 0) {
                parser->state = PARSE_STATE_BODY;
            } else {
                parser->state = PARSE_STATE_COMPLETE;
            }
            return 1;
        }

        if ((size_t)crlf_index > MAX_HEADER_LINE_LEN) {
            parser_set_error(parser, (http_status_t)431, "Header line too long");
            return -1;
        }

        if (parser->header_count >= MAX_HEADER_COUNT) {
            parser_set_error(parser, (http_status_t)431, "Too many headers");
            return -1;
        }

        int result = parse_header_line(parser, parser->buffer, (size_t)crlf_index);
        if (result < 0) {
            return -1;
        }
        parser->header_count++;
        parser_consume(parser, (size_t)crlf_index + 2);
    }
}

static int parse_chunk_size(http_parser_t *parser) {
    ssize_t crlf_index = find_crlf(parser->buffer, parser->buffer_len);
    if (crlf_index < 0) {
        return 0;
    }

    char *line = (char *)malloc((size_t)crlf_index + 1);
    if (!line) {
        parser_set_error(parser, HTTP_INTERNAL_ERROR, "Out of memory");
        return -1;
    }
    memcpy(line, parser->buffer, (size_t)crlf_index);
    line[crlf_index] = '\0';

    char *endptr = NULL;
    unsigned long chunk_size = strtoul(line, &endptr, 16);
    if (endptr == line) {
        free(line);
        parser_set_error(parser, HTTP_BAD_REQUEST, "Invalid chunk size");
        return -1;
    }

    parser_consume(parser, (size_t)crlf_index + 2);
    free(line);

    parser->current_chunk_size = chunk_size;
    parser->current_chunk_received = 0;

    if (chunk_size == 0) {
        parser->state = PARSE_STATE_CHUNK_TRAILERS;
    } else {
        parser->state = PARSE_STATE_CHUNK_DATA;
    }
    return 1;
}

static int parse_chunk_data(http_parser_t *parser) {
    size_t remaining = parser->current_chunk_size - parser->current_chunk_received;
    if (remaining == 0) {
        parser->state = PARSE_STATE_CHUNK_CRLF;
        return 1;
    }

    size_t to_copy = parser->buffer_len < remaining ? parser->buffer_len : remaining;
    if (to_copy == 0) {
        return 0;
    }

    if (append_body_data(parser, parser->buffer, to_copy) < 0) {
        return -1;
    }
    parser_consume(parser, to_copy);
    parser->current_chunk_received += to_copy;

    if (parser->current_chunk_received == parser->current_chunk_size) {
        parser->state = PARSE_STATE_CHUNK_CRLF;
    }
    return 1;
}

static int parse_chunk_crlf(http_parser_t *parser) {
    if (parser->buffer_len < 2) {
        return 0;
    }
    if (parser->buffer[0] != '\r' || parser->buffer[1] != '\n') {
        parser_set_error(parser, HTTP_BAD_REQUEST, "Malformed chunk data");
        return -1;
    }
    parser_consume(parser, 2);
    parser->state = PARSE_STATE_CHUNK_SIZE;
    return 1;
}

static int parse_chunk_trailers(http_parser_t *parser) {
    ssize_t crlf_index = find_crlf(parser->buffer, parser->buffer_len);
    if (crlf_index < 0) {
        return 0;
    }
    if (crlf_index == 0) {
        parser_consume(parser, 2);
        parser->state = PARSE_STATE_COMPLETE;
        parser->req->body_length = parser->body_received;
        return 1;
    }
    /* Ignore trailer headers for now */
    parser_consume(parser, (size_t)crlf_index + 2);
    return 1;
}

static void http_parser_init(http_parser_t *parser, http_request_t *req) {
    memset(parser, 0, sizeof(*parser));
    parser->state = PARSE_STATE_REQUEST_LINE;
    parser->req = req;
    parser->buffer_cap = READ_BUFFER_SIZE;
    parser->buffer = (char *)malloc(parser->buffer_cap);
    if (!parser->buffer) {
        parser_set_error(parser, HTTP_INTERNAL_ERROR, "Out of memory");
    }
}

static void http_parser_reset(http_parser_t *parser, http_request_t *req, bool preserve_buffer) {
    parser->state = PARSE_STATE_REQUEST_LINE;
    parser->req = req;
    if (!preserve_buffer) {
        parser->buffer_len = 0;
        parser->total_bytes = 0;
    } else {
        if (parser->buffer_len > MAX_REQUEST_BUFFER) {
            /* Safety clamp if leftover somehow exceeds limits */
            parser->buffer_len = MAX_REQUEST_BUFFER;
        }
        parser->total_bytes = parser->buffer_len;
    }
    parser->header_count = 0;
    parser->content_length = 0;
    parser->body_received = 0;
    parser->body_capacity = 0;
    parser->chunked = false;
    parser->current_chunk_size = 0;
    parser->current_chunk_received = 0;
    parser->keep_alive = false;
    parser->seen_host = false;
    parser->error_status = HTTP_BAD_REQUEST;
    parser->error_message = NULL;
    if (parser->req) {
        parser->req->body_length = 0;
    }
}

static void http_parser_destroy(http_parser_t *parser) {
    free(parser->buffer);
    parser->buffer = NULL;
    parser->buffer_cap = 0;
}

static parser_result_t http_parser_execute(http_parser_t *parser, const char *data, size_t len) {
    if (!parser || parser->state == PARSE_STATE_ERROR) {
        return PARSER_ERROR;
    }

    if (len > 0) {
        if (ensure_buffer_capacity(parser, len) < 0) {
            return PARSER_ERROR;
        }
        memcpy(parser->buffer + parser->buffer_len, data, len);
        parser->buffer_len += len;
        parser->total_bytes += len;
    }

    while (parser->state != PARSE_STATE_ERROR && parser->state != PARSE_STATE_COMPLETE) {
        switch (parser->state) {
            case PARSE_STATE_REQUEST_LINE:
                if (parse_request_line(parser) <= 0) {
                    return parser->state == PARSE_STATE_ERROR ? PARSER_ERROR : PARSER_INCOMPLETE;
                }
                break;
            case PARSE_STATE_HEADERS: {
                int result = parse_headers(parser);
                if (result < 0) {
                    return PARSER_ERROR;
                }
                if (result == 0) {
                    return PARSER_INCOMPLETE;
                }
                break;
            }
            case PARSE_STATE_BODY: {
                size_t remaining = parser->content_length - parser->body_received;
                size_t to_copy = parser->buffer_len < remaining ? parser->buffer_len : remaining;
                if (to_copy == 0) {
                    return PARSER_INCOMPLETE;
                }
                if (append_body_data(parser, parser->buffer, to_copy) < 0) {
                    return PARSER_ERROR;
                }
                parser_consume(parser, to_copy);
                if (parser->body_received == parser->content_length) {
                    parser->state = PARSE_STATE_COMPLETE;
                    parser->req->body_length = parser->body_received;
                    return PARSER_COMPLETE;
                }
                break;
            }
            case PARSE_STATE_CHUNK_SIZE: {
                int result = parse_chunk_size(parser);
                if (result <= 0) {
                    return result < 0 ? PARSER_ERROR : PARSER_INCOMPLETE;
                }
                break;
            }
            case PARSE_STATE_CHUNK_DATA: {
                int result = parse_chunk_data(parser);
                if (result <= 0) {
                    return result < 0 ? PARSER_ERROR : PARSER_INCOMPLETE;
                }
                break;
            }
            case PARSE_STATE_CHUNK_CRLF: {
                int result = parse_chunk_crlf(parser);
                if (result <= 0) {
                    return result < 0 ? PARSER_ERROR : PARSER_INCOMPLETE;
                }
                break;
            }
            case PARSE_STATE_CHUNK_TRAILERS: {
                int result = parse_chunk_trailers(parser);
                if (result <= 0) {
                    return result < 0 ? PARSER_ERROR : PARSER_INCOMPLETE;
                }
                break;
            }
            default:
                return PARSER_ERROR;
        }
    }

    if (parser->state == PARSE_STATE_COMPLETE) {
        return PARSER_COMPLETE;
    }
    return PARSER_ERROR;
}

/* Enable/disable async I/O mode */
int http_server_set_async(http_server_t *server, bool enable) {
    if (!server) {
        return -1;
    }
    
    if (server->running) {
        fprintf(stderr, "Cannot change async mode while server is running\n");
        return -1;
    }
    
    if (enable && !server->event_loop) {
        /* Create event loop */
        server->event_loop = event_loop_create();
        if (!server->event_loop) {
            fprintf(stderr, "Failed to create event loop\n");
            return -1;
        }
    } else if (!enable && server->event_loop) {
        /* Destroy event loop */
        event_loop_destroy(server->event_loop);
        server->event_loop = NULL;
    }
    
    server->async_mode = enable;
    return 0;
}

/* Get event loop */
event_loop_t *http_server_get_event_loop(http_server_t *server) {
    if (!server || !server->async_mode) {
        return NULL;
    }
    return server->event_loop;
}

/* Set non-blocking mode on a file descriptor */
static int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        return -1;
    }
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

/* Async accept handler */
static void async_accept_handler(int fd, int events, void *user_data) {
    http_server_t *server = (http_server_t *)user_data;
    
    if (events & EVENT_ERROR) {
        fprintf(stderr, "Error on server socket\n");
        return;
    }
    
    if (!(events & EVENT_READ)) {
        return;
    }
    
    /* Accept new connection */
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    
    int client_fd = accept(fd, (struct sockaddr *)&client_addr, &client_len);
    if (client_fd < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            perror("accept failed");
        }
        return;
    }
    
    /* Set client socket to non-blocking */
    if (set_nonblocking(client_fd) < 0) {
        perror("Failed to set client socket to non-blocking");
        close(client_fd);
        return;
    }
    
    /* Create async connection context */
    async_connection_t *conn = (async_connection_t *)calloc(1, sizeof(async_connection_t));
    if (!conn) {
        fprintf(stderr, "Failed to allocate connection context\n");
        close(client_fd);
        return;
    }

    conn->client_fd = client_fd;
    conn->server = server;
    conn->request = http_request_create();
    conn->response = http_response_create();
    conn->header_buf = NULL;
    conn->header_len = 0;
    conn->header_sent = 0;
    conn->body_sent = 0;
    conn->parser_initialized = false;
    conn->response_ready = false;
    conn->keep_alive = false;
    conn->closing = false;
    if (!conn->request || !conn->response) {
        send_error_response(client_fd, HTTP_INTERNAL_ERROR, "Internal Server Error");
        free_async_connection(conn);
        return;
    }

    http_parser_init(&conn->parser, conn->request);
    conn->parser_initialized = true;
    if (conn->parser.state == PARSE_STATE_ERROR) {
        http_status_t status = conn->parser.error_status ? conn->parser.error_status : HTTP_INTERNAL_ERROR;
        send_error_response(client_fd, status, conn->parser.error_message);
        free_async_connection(conn);
        return;
    }

    if (event_loop_add_fd(server->event_loop, client_fd, EVENT_READ,
                         async_read_handler, conn) < 0) {
        fprintf(stderr, "Failed to add client socket to event loop\n");
        free_async_connection(conn);
        return;
    }
}

static bool async_on_parser_result(async_connection_t *conn, int fd, parser_result_t result) {
    if (!conn || !conn->server) {
        return false;
    }

    http_server_t *server = conn->server;
    bool keep_alive = false;

    if (result == PARSER_ERROR) {
        http_status_t status = conn->parser.error_status ? conn->parser.error_status : HTTP_BAD_REQUEST;
        const char *message = conn->parser.error_message ? conn->parser.error_message : "Bad Request";
        http_response_send_text(conn->response, status, message);
        keep_alive = false;
    } else if (result == PARSER_COMPLETE) {
        if (server->router) {
            if (router_route(server->router, conn->request, conn->response) < 0) {
                http_response_send_text(conn->response, HTTP_NOT_FOUND, "Not Found");
            }
        } else {
            http_response_send_text(conn->response, HTTP_NOT_FOUND, "Not Found");
        }
        keep_alive = conn->parser.keep_alive && !response_forces_close(conn->response);
    } else {
        return true;
    }

    if (conn->header_buf) {
        free(conn->header_buf);
        conn->header_buf = NULL;
    }
    conn->header_len = 0;
    conn->header_sent = 0;
    conn->body_sent = 0;

    if (serialize_response(conn->response, keep_alive, &conn->header_buf, &conn->header_len) < 0) {
        event_loop_remove_fd(server->event_loop, fd);
        free_async_connection(conn);
        return false;
    }

    conn->keep_alive = keep_alive;
    conn->closing = !keep_alive;
    conn->response_ready = true;

    if (event_loop_modify_fd(server->event_loop, fd, EVENT_WRITE) < 0) {
        event_loop_remove_fd(server->event_loop, fd);
        free_async_connection(conn);
        return false;
    }

    return true;
}

/* Async read handler */
static void async_read_handler(int fd, int events, void *user_data) {
    async_connection_t *conn = (async_connection_t *)user_data;

    if (events & EVENT_ERROR) {
        fprintf(stderr, "Error on client socket\n");
        event_loop_remove_fd(conn->server->event_loop, fd);
        free_async_connection(conn);
        return;
    }

    if (!(events & EVENT_READ)) {
        return;
    }

    if (!conn->parser_initialized || conn->response_ready) {
        return;
    }

    parser_result_t result = PARSER_INCOMPLETE;
    if (conn->parser.buffer_len > 0) {
        result = http_parser_execute(&conn->parser, NULL, 0);
    }

    char read_buf[READ_BUFFER_SIZE];
    while (result == PARSER_INCOMPLETE) {
        ssize_t bytes_read = recv(fd, read_buf, sizeof(read_buf), 0);
        if (bytes_read < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            perror("recv failed");
            event_loop_remove_fd(conn->server->event_loop, fd);
            free_async_connection(conn);
            return;
        }

        if (bytes_read == 0) {
            event_loop_remove_fd(conn->server->event_loop, fd);
            free_async_connection(conn);
            return;
        }

        result = http_parser_execute(&conn->parser, read_buf, (size_t)bytes_read);
    }

    if (result == PARSER_INCOMPLETE) {
        return;
    }

    if (!async_on_parser_result(conn, fd, result)) {
        return;
    }

    if (conn->response_ready) {
        async_write_handler(fd, EVENT_WRITE, conn);
    }
}

/* Async write handler */
static void async_write_handler(int fd, int events, void *user_data) {
    async_connection_t *conn = (async_connection_t *)user_data;
    
    if (events & EVENT_ERROR) {
        fprintf(stderr, "Error on client socket\n");
        event_loop_remove_fd(conn->server->event_loop, fd);
        free_async_connection(conn);
        return;
    }
    
    if (!(events & EVENT_WRITE)) {
        return;
    }
    if (!conn->response || !conn->response_ready || !conn->header_buf) {
        return;
    }

    for (;;) {
        while (conn->header_sent < conn->header_len) {
            ssize_t sent = send(fd, conn->header_buf + conn->header_sent,
                                conn->header_len - conn->header_sent, 0);
            if (sent < 0) {
                if (errno == EINTR) {
                    continue;
                }
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    return;
                }
                perror("send failed");
                event_loop_remove_fd(conn->server->event_loop, fd);
                free_async_connection(conn);
                return;
            }
            if (sent == 0) {
                event_loop_remove_fd(conn->server->event_loop, fd);
                free_async_connection(conn);
                return;
            }
            conn->header_sent += (size_t)sent;
        }

        while (conn->body_sent < conn->response->body_length) {
            if (!conn->response->body || conn->response->body_length == 0) {
                conn->body_sent = conn->response->body_length;
                break;
            }
            size_t remaining = conn->response->body_length - conn->body_sent;
            ssize_t sent = send(fd, conn->response->body + conn->body_sent, remaining, 0);
            if (sent < 0) {
                if (errno == EINTR) {
                    continue;
                }
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    return;
                }
                perror("send failed");
                event_loop_remove_fd(conn->server->event_loop, fd);
                free_async_connection(conn);
                return;
            }
            if (sent == 0) {
                event_loop_remove_fd(conn->server->event_loop, fd);
                free_async_connection(conn);
                return;
            }
            conn->body_sent += (size_t)sent;
            if ((size_t)sent < remaining) {
                return;
            }
        }

        if (conn->header_sent < conn->header_len || conn->body_sent < conn->response->body_length) {
            return;
        }

        conn->response->sent = true;
        free(conn->header_buf);
        conn->header_buf = NULL;
        conn->header_len = 0;
        conn->response_ready = false;

        bool reuse = conn->keep_alive && !conn->closing;

        if (!reuse) {
            event_loop_remove_fd(conn->server->event_loop, fd);
            free_async_connection(conn);
            return;
        }

        http_request_destroy(conn->request);
        http_response_destroy(conn->response);
        conn->request = http_request_create();
        conn->response = http_response_create();
        if (!conn->request || !conn->response) {
            event_loop_remove_fd(conn->server->event_loop, fd);
            free_async_connection(conn);
            return;
        }

        http_parser_reset(&conn->parser, conn->request, true);
        conn->keep_alive = false;
        conn->closing = false;
        conn->header_sent = 0;
        conn->body_sent = 0;

        parser_result_t result = PARSER_INCOMPLETE;
        if (conn->parser.buffer_len > 0) {
            result = http_parser_execute(&conn->parser, NULL, 0);
        }

        if (result == PARSER_INCOMPLETE) {
            if (event_loop_modify_fd(conn->server->event_loop, fd, EVENT_READ) < 0) {
                event_loop_remove_fd(conn->server->event_loop, fd);
                free_async_connection(conn);
            }
            return;
        }

        if (!async_on_parser_result(conn, fd, result)) {
            return;
        }

        if (!conn->response_ready || !conn->header_buf) {
            return;
        }

        /* Loop to send pipelined response immediately */
    }
}

/* Free async connection */
static void free_async_connection(async_connection_t *conn) {
    if (!conn) {
        return;
    }
    
    if (conn->client_fd >= 0) {
        close(conn->client_fd);
        conn->client_fd = -1;
    }

    free(conn->header_buf);
    conn->header_buf = NULL;

    http_parser_destroy(&conn->parser);
    http_request_destroy(conn->request);
    http_response_destroy(conn->response);
    free(conn);
}

