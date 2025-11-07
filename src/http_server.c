#include "weblib.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
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

/* Header storage */
typedef struct header_entry {
    char *key;
    char *value;
    struct header_entry *next;
} header_entry_t;

typedef struct {
    header_entry_t *entries;
} header_map_t;

/* Forward declarations */
static void *accept_connections(void *arg);
static void *handle_connection(void *arg);
static http_request_t *parse_request(const char *buffer, size_t buffer_size);
static void send_response(int client_fd, http_response_t *res);
static void free_request(http_request_t *req);
static void free_response(http_response_t *res);
static header_map_t *header_map_create(void);
static void header_map_set(header_map_t *map, const char *key, const char *value);
static const char *header_map_get(header_map_t *map, const char *key);
static void header_map_free(header_map_t *map);

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
            http_request_t *req = parse_request(buffer, bytes_read);
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
static http_request_t *parse_request(const char *buffer, size_t buffer_size) {
    http_request_t *req = (http_request_t *)calloc(1, sizeof(http_request_t));
    if (!req) {
        return NULL;
    }
    
    const char *line_start = buffer;
    const char *line_end;
    
    /* Parse first line: METHOD /path HTTP/1.1 */
    line_end = strstr(line_start, "\r\n");
    if (!line_end) {
        line_end = strstr(line_start, "\n");
    }
    if (!line_end) {
        free(req);
        return NULL;
    }
    
    char method_str[16];
    char path[1024];
    
    if (sscanf(line_start, "%15s %1023s", method_str, path) != 2) {
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
    if (!req->path) {
        free(req->query_string);
        free(req);
        return NULL;
    }
    
    /* Parse headers */
    req->headers = header_map_create();
    if (!req->headers) {
        free(req->path);
        free(req->query_string);
        free(req);
        return NULL;
    }
    
    /* Move to headers */
    line_start = line_end;
    if (*line_start == '\r') line_start++;
    if (*line_start == '\n') line_start++;
    
    /* Parse header lines */
    while (line_start < buffer + buffer_size) {
        line_end = strstr(line_start, "\r\n");
        if (!line_end) {
            line_end = strstr(line_start, "\n");
        }
        if (!line_end) {
            break;
        }
        
        /* Empty line indicates end of headers */
        if (line_end == line_start || (line_end == line_start + 1 && *line_start == '\r')) {
            line_start = line_end;
            if (*line_start == '\r') line_start++;
            if (*line_start == '\n') line_start++;
            break;
        }
        
        /* Parse header: Key: Value */
        const char *colon = memchr(line_start, ':', line_end - line_start);
        if (colon) {
            size_t key_len = colon - line_start;
            const char *value_start = colon + 1;
            
            /* Skip leading whitespace in value */
            while (value_start < line_end && (*value_start == ' ' || *value_start == '\t')) {
                value_start++;
            }
            
            size_t value_len = line_end - value_start;
            
            char *key = (char *)malloc(key_len + 1);
            char *value = (char *)malloc(value_len + 1);
            
            if (key && value) {
                memcpy(key, line_start, key_len);
                key[key_len] = '\0';
                memcpy(value, value_start, value_len);
                value[value_len] = '\0';
                
                header_map_set((header_map_t *)req->headers, key, value);
                free(key);
                free(value);
            } else {
                free(key);
                free(value);
            }
        }
        
        /* Move to next line */
        line_start = line_end;
        if (*line_start == '\r') line_start++;
        if (*line_start == '\n') line_start++;
    }
    
    /* Parse body if present */
    const char *body_start = line_start;
    size_t body_len = buffer + buffer_size - body_start;
    
    if (body_len > 0) {
        req->body = (char *)malloc(body_len + 1);
        if (req->body) {
            memcpy(req->body, body_start, body_len);
            req->body[body_len] = '\0';
            req->body_length = body_len;
            
            /* Parse form data if Content-Type is set */
            const char *content_type = header_map_get((header_map_t *)req->headers, "Content-Type");
            if (content_type) {
                http_request_parse_body(req, content_type);
            }
        }
    }
    
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
    
    if (req->headers) {
        header_map_free((header_map_t *)req->headers);
    }
    
    if (req->form_data) {
        http_request_free_form_data(req);
    }
    
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
    if (!req || !req->headers || !key) {
        return NULL;
    }
    return header_map_get((header_map_t *)req->headers, key);
}

const char *http_request_get_param(http_request_t *req, const char *key) {
    /* TODO: Implement parameter retrieval */
    (void)req;
    (void)key;
    return NULL;
}

/* Header map implementation */
static header_map_t *header_map_create(void) {
    header_map_t *map = (header_map_t *)calloc(1, sizeof(header_map_t));
    return map;
}

static void header_map_set(header_map_t *map, const char *key, const char *value) {
    if (!map || !key || !value) {
        return;
    }
    
    /* Check if key already exists */
    header_entry_t *entry = map->entries;
    while (entry) {
        if (strcasecmp(entry->key, key) == 0) {
            /* Update existing entry */
            free(entry->value);
            entry->value = strdup(value);
            return;
        }
        entry = entry->next;
    }
    
    /* Create new entry */
    entry = (header_entry_t *)malloc(sizeof(header_entry_t));
    if (entry) {
        entry->key = strdup(key);
        entry->value = strdup(value);
        entry->next = map->entries;
        map->entries = entry;
    }
}

static const char *header_map_get(header_map_t *map, const char *key) {
    if (!map || !key) {
        return NULL;
    }
    
    header_entry_t *entry = map->entries;
    while (entry) {
        if (strcasecmp(entry->key, key) == 0) {
            return entry->value;
        }
        entry = entry->next;
    }
    
    return NULL;
}

static void header_map_free(header_map_t *map) {
    if (!map) {
        return;
    }
    
    header_entry_t *entry = map->entries;
    while (entry) {
        header_entry_t *next = entry->next;
        free(entry->key);
        free(entry->value);
        free(entry);
        entry = next;
    }
    
    free(map);
}
