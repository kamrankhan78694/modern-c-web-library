#include "weblib.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>

#define MAX_CONNECTIONS 128
#define BUFFER_SIZE 8192

/* Internal server structure */
struct http_server {
    int socket_fd;
    uint16_t port;
    router_t *router;
    bool running;
    pthread_t accept_thread;
};

/* Connection handler data */
typedef struct {
    int client_fd;
    http_server_t *server;
} connection_t;

/* Forward declarations */
static void *accept_connections(void *arg);
static void *handle_connection(void *arg);
static http_request_t *parse_request(const char *buffer);
static void send_response(int client_fd, http_response_t *res);
static void free_request(http_request_t *req);
static void free_response(http_response_t *res);

/* Create HTTP server */
http_server_t *http_server_create(void) {
    http_server_t *server = (http_server_t *)calloc(1, sizeof(http_server_t));
    if (!server) {
        return NULL;
    }
    
    server->socket_fd = -1;
    server->running = false;
    server->router = NULL;
    
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
    
    /* Start accept thread */
    if (pthread_create(&server->accept_thread, NULL, accept_connections, server) != 0) {
        perror("pthread_create failed");
        server->running = false;
        close(server->socket_fd);
        return -1;
    }
    
    printf("HTTP server listening on port %d\n", port);
    
    return 0;
}

/* Stop the server */
void http_server_stop(http_server_t *server) {
    if (!server || !server->running) {
        return;
    }
    
    server->running = false;
    
    if (server->socket_fd >= 0) {
        close(server->socket_fd);
        server->socket_fd = -1;
    }
    
    pthread_join(server->accept_thread, NULL);
    
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
    }
    
    req->path = strdup(path);
    
    /* TODO: Parse headers and body */
    
    return req;
}

/* Send HTTP response */
static void send_response(int client_fd, http_response_t *res) {
    if (!res || res->sent) {
        return;
    }
    
    char header[BUFFER_SIZE];
    int header_len = snprintf(header, BUFFER_SIZE,
        "HTTP/1.1 %d OK\r\n"
        "Content-Length: %zu\r\n"
        "Connection: close\r\n"
        "\r\n",
        res->status,
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
    res->body_length = strlen(text);
    res->body = strdup(text);
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
