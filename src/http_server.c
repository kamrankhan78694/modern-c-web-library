#include "weblib.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>
#include <fcntl.h>

#define MAX_CONNECTIONS 128
#define BUFFER_SIZE 8192

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
} connection_t;

/* Async connection context */
typedef struct async_connection {
    int client_fd;
    http_server_t *server;
    char buffer[BUFFER_SIZE];
    size_t buffer_pos;
    http_request_t *request;
    http_response_t *response;
    bool request_complete;
} async_connection_t;

/* Forward declarations */
static void *accept_connections(void *arg);
static void *handle_connection(void *arg);
static http_request_t *parse_request(const char *buffer);
static void send_response(int client_fd, http_response_t *res);
static void free_request(http_request_t *req);
static void free_response(http_response_t *res);

/* Async I/O functions */
static int set_nonblocking(int fd);
static void async_accept_handler(int fd, int events, void *user_data);
static void async_read_handler(int fd, int events, void *user_data);
static void async_write_handler(int fd, int events, void *user_data);
static void free_async_connection(async_connection_t *conn);

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
        connection_t *conn = (connection_t *)malloc(sizeof(connection_t));
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
    char buffer[BUFFER_SIZE];
    
    /* Read request */
    ssize_t bytes_read = recv(conn->client_fd, buffer, BUFFER_SIZE - 1, 0);
    if (bytes_read > 0) {
        buffer[bytes_read] = '\0';
        
        /* Check for oversized request */
        if (bytes_read == BUFFER_SIZE - 1) {
            /* Request too large, send 413 Payload Too Large */
            http_response_t *res = (http_response_t *)calloc(1, sizeof(http_response_t));
            if (res) {
                http_response_send_text(res, 413, "Payload Too Large");
                send_response(conn->client_fd, res);
                free_response(res);
            }
        } else {
            /* Parse request */
            http_request_t *req = parse_request(buffer);
            if (req) {
                /* Create response */
                http_response_t *res = (http_response_t *)calloc(1, sizeof(http_response_t));
                if (res) {
                    res->status = HTTP_OK;
                    
                    /* Route request */
                    if (conn->server->router) {
                        router_route(conn->server->router, req, res);
                    } else {
                        /* No router configured, send 404 */
                        http_response_send_text(res, HTTP_NOT_FOUND, "Not Found");
                    }
                    
                    /* Send response */
                    send_response(conn->client_fd, res);
                    free_response(res);
                }
                
                free_request(req);
            }
        }
    }
    
    close(conn->client_fd);
    free(conn);
    
    return NULL;
}

/* Parse HTTP request */
static http_request_t *parse_request(const char *buffer) {
    http_request_t *req = (http_request_t *)calloc(1, sizeof(http_request_t));
    if (!req) {
        return NULL;
    }
    
    /* Parse first line: METHOD /path HTTP/1.1 */
    char method_str[16];
    char path[1024];
    
    if (sscanf(buffer, "%15s %1023s", method_str, path) != 2) {
        free(req);
        return NULL;
    }
    
    /* Parse method */
    if (strcmp(method_str, "GET") == 0) {
        req->method = HTTP_GET;
    } else if (strcmp(method_str, "POST") == 0) {
        req->method = HTTP_POST;
    } else if (strcmp(method_str, "PUT") == 0) {
        req->method = HTTP_PUT;
    } else if (strcmp(method_str, "DELETE") == 0) {
        req->method = HTTP_DELETE;
    } else if (strcmp(method_str, "PATCH") == 0) {
        req->method = HTTP_PATCH;
    } else if (strcmp(method_str, "HEAD") == 0) {
        req->method = HTTP_HEAD;
    } else if (strcmp(method_str, "OPTIONS") == 0) {
        req->method = HTTP_OPTIONS;
    } else {
        req->method = HTTP_GET;
    }
    
    /* Parse path and query string */
    char *query_start = strchr(path, '?');
    if (query_start) {
        *query_start = '\0';
        req->query_string = strdup(query_start + 1);
        if (!req->query_string) {
            free(req);
            return NULL;
        }
    }
    
    req->path = strdup(path);
    if (!req->path) {
        free(req->query_string);
        free(req);
        return NULL;
    }
    
    /* TODO: Parse headers and body */
    
    return req;
}

/* Send HTTP response */
static void send_response(int client_fd, http_response_t *res) {
    if (!res || res->sent) {
        return;
    }
    
    /* Get status text based on status code */
    const char *status_text = "OK";
    switch (res->status) {
        case HTTP_OK: status_text = "OK"; break;
        case HTTP_CREATED: status_text = "Created"; break;
        case HTTP_ACCEPTED: status_text = "Accepted"; break;
        case HTTP_NO_CONTENT: status_text = "No Content"; break;
        case HTTP_BAD_REQUEST: status_text = "Bad Request"; break;
        case HTTP_UNAUTHORIZED: status_text = "Unauthorized"; break;
        case HTTP_FORBIDDEN: status_text = "Forbidden"; break;
        case HTTP_NOT_FOUND: status_text = "Not Found"; break;
        case HTTP_METHOD_NOT_ALLOWED: status_text = "Method Not Allowed"; break;
        case HTTP_INTERNAL_ERROR: status_text = "Internal Server Error"; break;
        case HTTP_NOT_IMPLEMENTED: status_text = "Not Implemented"; break;
        case HTTP_BAD_GATEWAY: status_text = "Bad Gateway"; break;
        case HTTP_SERVICE_UNAVAILABLE: status_text = "Service Unavailable"; break;
        default: status_text = "OK"; break;
    }
    
    char header[BUFFER_SIZE];
    int header_len = snprintf(header, BUFFER_SIZE,
        "HTTP/1.1 %d %s\r\n"
        "Content-Length: %zu\r\n"
        "Connection: close\r\n"
        "\r\n",
        res->status,
        status_text,
        res->body_length);
    
    send(client_fd, header, header_len, 0);
    
    if (res->body && res->body_length > 0) {
        send(client_fd, res->body, res->body_length, 0);
    }
    
    res->sent = true;
}

/* Free request */
static void free_request(http_request_t *req) {
    if (!req) {
        return;
    }
    
    free(req->path);
    free(req->query_string);
    free(req->body);
    free(req);
}

/* Free response */
static void free_response(http_response_t *res) {
    if (!res) {
        return;
    }
    
    free(res->body);
    free(res);
}

/* Response helpers */
void http_response_send_text(http_response_t *res, http_status_t status, const char *text) {
    if (!res || !text) {
        return;
    }
    
    res->status = status;
    res->body = strdup(text);
    if (res->body) {
        res->body_length = strlen(text);
    } else {
        res->body_length = 0;
    }
}

void http_response_set_header(http_response_t *res, const char *key, const char *value) {
    /* TODO: Implement header storage */
    (void)res;
    (void)key;
    (void)value;
}

const char *http_request_get_header(http_request_t *req, const char *key) {
    /* TODO: Implement header retrieval */
    (void)req;
    (void)key;
    return NULL;
}

const char *http_request_get_param(http_request_t *req, const char *key) {
    /* TODO: Implement parameter retrieval */
    (void)req;
    (void)key;
    return NULL;
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
    conn->buffer_pos = 0;
    conn->request = NULL;
    conn->response = NULL;
    conn->request_complete = false;
    
    /* Add client socket to event loop for reading */
    if (event_loop_add_fd(server->event_loop, client_fd, EVENT_READ, 
                         async_read_handler, conn) < 0) {
        fprintf(stderr, "Failed to add client socket to event loop\n");
        free(conn);
        close(client_fd);
        return;
    }
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
    
    /* Read data from client */
    ssize_t bytes_read = recv(fd, conn->buffer + conn->buffer_pos, 
                              BUFFER_SIZE - conn->buffer_pos - 1, 0);
    
    if (bytes_read <= 0) {
        if (bytes_read == 0 || (errno != EAGAIN && errno != EWOULDBLOCK)) {
            /* Connection closed or error */
            event_loop_remove_fd(conn->server->event_loop, fd);
            free_async_connection(conn);
        }
        return;
    }
    
    conn->buffer_pos += bytes_read;
    conn->buffer[conn->buffer_pos] = '\0';
    
    /* Check if we have a complete HTTP request (ends with \r\n\r\n) */
    if (strstr(conn->buffer, "\r\n\r\n") != NULL) {
        conn->request_complete = true;
        
        /* Parse request */
        conn->request = parse_request(conn->buffer);
        if (!conn->request) {
            fprintf(stderr, "Failed to parse request\n");
            event_loop_remove_fd(conn->server->event_loop, fd);
            free_async_connection(conn);
            return;
        }
        
        /* Create response */
        conn->response = (http_response_t *)calloc(1, sizeof(http_response_t));
        if (!conn->response) {
            fprintf(stderr, "Failed to allocate response\n");
            event_loop_remove_fd(conn->server->event_loop, fd);
            free_async_connection(conn);
            return;
        }
        conn->response->status = HTTP_OK;
        
        /* Route request */
        if (conn->server->router) {
            router_route(conn->server->router, conn->request, conn->response);
        } else {
            http_response_send_text(conn->response, HTTP_NOT_FOUND, "Not Found");
        }
        
        /* Switch to write mode */
        if (event_loop_modify_fd(conn->server->event_loop, fd, EVENT_WRITE) < 0) {
            fprintf(stderr, "Failed to modify event for writing\n");
            event_loop_remove_fd(conn->server->event_loop, fd);
            free_async_connection(conn);
            return;
        }
        
        /* Immediately send response since we're switching to write */
        async_write_handler(fd, EVENT_WRITE, conn);
    } else if (conn->buffer_pos >= BUFFER_SIZE - 1) {
        /* Request too large */
        fprintf(stderr, "Request too large\n");
        event_loop_remove_fd(conn->server->event_loop, fd);
        free_async_connection(conn);
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
    
    /* Send response */
    if (conn->response) {
        send_response(fd, conn->response);
    }
    
    /* Close connection */
    event_loop_remove_fd(conn->server->event_loop, fd);
    free_async_connection(conn);
}

/* Free async connection */
static void free_async_connection(async_connection_t *conn) {
    if (!conn) {
        return;
    }
    
    if (conn->client_fd >= 0) {
        close(conn->client_fd);
    }
    
    if (conn->request) {
        free_request(conn->request);
    }
    
    if (conn->response) {
        free_response(conn->response);
    }
    
    free(conn);
}

