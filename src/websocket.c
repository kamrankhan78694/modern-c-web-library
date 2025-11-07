#include "weblib.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <pthread.h>
#include <openssl/sha.h>
#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/buffer.h>
#include <arpa/inet.h>

#define WS_GUID "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
#define WS_FRAME_HEADER_SIZE 2
#define WS_MAX_FRAME_SIZE 65536

/* WebSocket opcodes */
typedef enum {
    WS_OPCODE_CONTINUATION = 0x0,
    WS_OPCODE_TEXT = 0x1,
    WS_OPCODE_BINARY = 0x2,
    WS_OPCODE_CLOSE = 0x8,
    WS_OPCODE_PING = 0x9,
    WS_OPCODE_PONG = 0xA
} ws_opcode_t;

/* WebSocket connection state */
typedef enum {
    WS_STATE_CONNECTING,
    WS_STATE_OPEN,
    WS_STATE_CLOSING,
    WS_STATE_CLOSED
} ws_state_t;

/* WebSocket connection structure */
struct websocket_conn {
    int socket_fd;
    ws_state_t state;
    websocket_handler_t handler;
    void *user_data;
    pthread_t read_thread;
    bool running;
};

/* WebSocket server structure */
struct websocket_server {
    int socket_fd;
    uint16_t port;
    websocket_handler_t handler;
    bool running;
    pthread_t accept_thread;
    void *user_data;
};

/* Forward declarations */
static void *ws_accept_connections(void *arg);
static void *ws_read_loop(void *arg);
static int ws_perform_handshake(int client_fd, const char *buffer);
static int ws_send_frame(int socket_fd, ws_opcode_t opcode, const uint8_t *payload, size_t length);
static int ws_read_frame(int socket_fd, ws_opcode_t *opcode, uint8_t **payload, size_t *length);
static char *base64_encode(const unsigned char *input, size_t length);

/* Create WebSocket server */
websocket_server_t *websocket_server_create(void) {
    websocket_server_t *server = (websocket_server_t *)calloc(1, sizeof(websocket_server_t));
    if (!server) {
        return NULL;
    }
    
    server->socket_fd = -1;
    server->running = false;
    server->handler = NULL;
    server->user_data = NULL;
    
    return server;
}

/* Set WebSocket handler */
void websocket_server_set_handler(websocket_server_t *server, websocket_handler_t handler) {
    if (server) {
        server->handler = handler;
    }
}

/* Set user data */
void websocket_server_set_user_data(websocket_server_t *server, void *user_data) {
    if (server) {
        server->user_data = user_data;
    }
}

/* Start WebSocket server */
int websocket_server_listen(websocket_server_t *server, uint16_t port) {
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
    if (listen(server->socket_fd, 128) < 0) {
        perror("listen failed");
        close(server->socket_fd);
        return -1;
    }
    
    server->port = port;
    server->running = true;
    
    /* Start accept thread */
    if (pthread_create(&server->accept_thread, NULL, ws_accept_connections, server) != 0) {
        perror("pthread_create failed");
        server->running = false;
        close(server->socket_fd);
        return -1;
    }
    
    printf("WebSocket server listening on port %d\n", port);
    
    return 0;
}

/* Stop WebSocket server */
void websocket_server_stop(websocket_server_t *server) {
    if (!server || !server->running) {
        return;
    }
    
    server->running = false;
    
    if (server->socket_fd >= 0) {
        close(server->socket_fd);
        server->socket_fd = -1;
    }
    
    pthread_join(server->accept_thread, NULL);
    
    printf("WebSocket server stopped\n");
}

/* Destroy WebSocket server */
void websocket_server_destroy(websocket_server_t *server) {
    if (!server) {
        return;
    }
    
    if (server->running) {
        websocket_server_stop(server);
    }
    
    free(server);
}

/* Accept connections thread */
static void *ws_accept_connections(void *arg) {
    websocket_server_t *server = (websocket_server_t *)arg;
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    char buffer[8192];
    
    while (server->running) {
        int client_fd = accept(server->socket_fd, (struct sockaddr *)&client_addr, &client_len);
        
        if (client_fd < 0) {
            if (server->running) {
                perror("accept failed");
            }
            continue;
        }
        
        /* Read handshake request */
        ssize_t bytes_read = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
        if (bytes_read > 0) {
            buffer[bytes_read] = '\0';
            
            /* Perform WebSocket handshake */
            if (ws_perform_handshake(client_fd, buffer) == 0) {
                /* Handshake successful, create connection */
                websocket_conn_t *conn = (websocket_conn_t *)calloc(1, sizeof(websocket_conn_t));
                if (conn) {
                    conn->socket_fd = client_fd;
                    conn->state = WS_STATE_OPEN;
                    conn->handler = server->handler;
                    conn->user_data = server->user_data;
                    conn->running = true;
                    
                    /* Start read loop thread */
                    if (pthread_create(&conn->read_thread, NULL, ws_read_loop, conn) != 0) {
                        perror("pthread_create for WebSocket connection failed");
                        close(client_fd);
                        free(conn);
                    } else {
                        pthread_detach(conn->read_thread);
                        
                        /* Notify handler of new connection */
                        if (conn->handler) {
                            conn->handler(conn, WS_EVENT_OPEN, NULL, 0);
                        }
                    }
                } else {
                    close(client_fd);
                }
            } else {
                /* Handshake failed */
                close(client_fd);
            }
        } else {
            close(client_fd);
        }
    }
    
    return NULL;
}

/* Read loop for WebSocket connection */
static void *ws_read_loop(void *arg) {
    websocket_conn_t *conn = (websocket_conn_t *)arg;
    
    while (conn->running && conn->state == WS_STATE_OPEN) {
        ws_opcode_t opcode;
        uint8_t *payload = NULL;
        size_t length = 0;
        
        int result = ws_read_frame(conn->socket_fd, &opcode, &payload, &length);
        
        if (result < 0) {
            /* Error or connection closed */
            break;
        }
        
        /* Handle different opcodes */
        switch (opcode) {
            case WS_OPCODE_TEXT:
            case WS_OPCODE_BINARY:
                /* Data frame */
                if (conn->handler) {
                    ws_event_t event = (opcode == WS_OPCODE_TEXT) ? WS_EVENT_MESSAGE : WS_EVENT_BINARY;
                    conn->handler(conn, event, payload, length);
                }
                break;
                
            case WS_OPCODE_CLOSE:
                /* Close frame */
                conn->state = WS_STATE_CLOSING;
                ws_send_frame(conn->socket_fd, WS_OPCODE_CLOSE, NULL, 0);
                conn->running = false;
                
                if (conn->handler) {
                    conn->handler(conn, WS_EVENT_CLOSE, NULL, 0);
                }
                break;
                
            case WS_OPCODE_PING:
                /* Ping frame - respond with pong */
                ws_send_frame(conn->socket_fd, WS_OPCODE_PONG, payload, length);
                break;
                
            case WS_OPCODE_PONG:
                /* Pong frame - ignore for now */
                break;
                
            default:
                break;
        }
        
        free(payload);
    }
    
    /* Cleanup */
    conn->state = WS_STATE_CLOSED;
    close(conn->socket_fd);
    free(conn);
    
    return NULL;
}

/* Perform WebSocket handshake */
static int ws_perform_handshake(int client_fd, const char *buffer) {
    /* Extract Sec-WebSocket-Key from request */
    const char *key_header = "Sec-WebSocket-Key: ";
    const char *key_start = strstr(buffer, key_header);
    
    if (!key_start) {
        return -1;
    }
    
    key_start += strlen(key_header);
    const char *key_end = strstr(key_start, "\r\n");
    
    if (!key_end) {
        return -1;
    }
    
    char key[256];
    size_t key_len = key_end - key_start;
    if (key_len >= sizeof(key)) {
        return -1;
    }
    
    strncpy(key, key_start, key_len);
    key[key_len] = '\0';
    
    /* Concatenate key with GUID */
    char accept_key_input[512];
    snprintf(accept_key_input, sizeof(accept_key_input), "%s%s", key, WS_GUID);
    
    /* Compute SHA-1 hash */
    unsigned char hash[SHA_DIGEST_LENGTH];
    SHA1((unsigned char *)accept_key_input, strlen(accept_key_input), hash);
    
    /* Base64 encode the hash */
    char *accept_key = base64_encode(hash, SHA_DIGEST_LENGTH);
    if (!accept_key) {
        return -1;
    }
    
    /* Send handshake response */
    char response[1024];
    snprintf(response, sizeof(response),
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: %s\r\n"
        "\r\n",
        accept_key);
    
    send(client_fd, response, strlen(response), 0);
    free(accept_key);
    
    return 0;
}

/* Send WebSocket frame */
static int ws_send_frame(int socket_fd, ws_opcode_t opcode, const uint8_t *payload, size_t length) {
    uint8_t header[10];
    size_t header_size = 0;
    
    /* FIN bit set, opcode */
    header[0] = 0x80 | (opcode & 0x0F);
    
    /* Payload length */
    if (length < 126) {
        header[1] = length;
        header_size = 2;
    } else if (length < 65536) {
        header[1] = 126;
        header[2] = (length >> 8) & 0xFF;
        header[3] = length & 0xFF;
        header_size = 4;
    } else {
        header[1] = 127;
        /* For simplicity, we don't support very large frames */
        return -1;
    }
    
    /* Send header */
    if (send(socket_fd, header, header_size, 0) < 0) {
        return -1;
    }
    
    /* Send payload */
    if (length > 0 && payload) {
        if (send(socket_fd, payload, length, 0) < 0) {
            return -1;
        }
    }
    
    return 0;
}

/* Read WebSocket frame */
static int ws_read_frame(int socket_fd, ws_opcode_t *opcode, uint8_t **payload, size_t *length) {
    uint8_t header[2];
    
    /* Read first 2 bytes */
    if (recv(socket_fd, header, 2, 0) != 2) {
        return -1;
    }
    
    /* Extract opcode */
    *opcode = header[0] & 0x0F;
    
    /* Extract payload length */
    uint64_t payload_len = header[1] & 0x7F;
    bool masked = (header[1] & 0x80) != 0;
    
    if (payload_len == 126) {
        uint8_t len_bytes[2];
        if (recv(socket_fd, len_bytes, 2, 0) != 2) {
            return -1;
        }
        payload_len = (len_bytes[0] << 8) | len_bytes[1];
    } else if (payload_len == 127) {
        uint8_t len_bytes[8];
        if (recv(socket_fd, len_bytes, 8, 0) != 8) {
            return -1;
        }
        /* For simplicity, we only support up to 32-bit lengths */
        payload_len = ((uint64_t)len_bytes[4] << 24) |
                     ((uint64_t)len_bytes[5] << 16) |
                     ((uint64_t)len_bytes[6] << 8) |
                     (uint64_t)len_bytes[7];
    }
    
    /* Read masking key if present */
    uint8_t mask[4] = {0};
    if (masked) {
        if (recv(socket_fd, mask, 4, 0) != 4) {
            return -1;
        }
    }
    
    /* Read payload */
    *length = payload_len;
    *payload = NULL;
    
    if (payload_len > 0) {
        *payload = (uint8_t *)malloc(payload_len + 1);
        if (!*payload) {
            return -1;
        }
        
        size_t total_read = 0;
        while (total_read < payload_len) {
            ssize_t bytes_read = recv(socket_fd, *payload + total_read, payload_len - total_read, 0);
            if (bytes_read <= 0) {
                free(*payload);
                *payload = NULL;
                return -1;
            }
            total_read += bytes_read;
        }
        
        /* Unmask payload if masked */
        if (masked) {
            for (size_t i = 0; i < payload_len; i++) {
                (*payload)[i] ^= mask[i % 4];
            }
        }
        
        /* Null-terminate for text frames */
        (*payload)[payload_len] = '\0';
    }
    
    return 0;
}

/* Base64 encode */
static char *base64_encode(const unsigned char *input, size_t length) {
    BIO *bio, *b64;
    BUF_MEM *buffer_ptr;
    
    b64 = BIO_new(BIO_f_base64());
    bio = BIO_new(BIO_s_mem());
    bio = BIO_push(b64, bio);
    
    BIO_set_flags(bio, BIO_FLAGS_BASE64_NO_NL);
    BIO_write(bio, input, length);
    BIO_flush(bio);
    
    BIO_get_mem_ptr(bio, &buffer_ptr);
    
    char *result = (char *)malloc(buffer_ptr->length + 1);
    if (result) {
        memcpy(result, buffer_ptr->data, buffer_ptr->length);
        result[buffer_ptr->length] = '\0';
    }
    
    BIO_free_all(bio);
    
    return result;
}

/* Send text message */
int websocket_send_text(websocket_conn_t *conn, const char *message) {
    if (!conn || conn->state != WS_STATE_OPEN) {
        return -1;
    }
    
    return ws_send_frame(conn->socket_fd, WS_OPCODE_TEXT, 
                        (const uint8_t *)message, strlen(message));
}

/* Send binary message */
int websocket_send_binary(websocket_conn_t *conn, const uint8_t *data, size_t length) {
    if (!conn || conn->state != WS_STATE_OPEN) {
        return -1;
    }
    
    return ws_send_frame(conn->socket_fd, WS_OPCODE_BINARY, data, length);
}

/* Close WebSocket connection */
void websocket_close(websocket_conn_t *conn) {
    if (!conn || conn->state == WS_STATE_CLOSED) {
        return;
    }
    
    if (conn->state == WS_STATE_OPEN) {
        conn->state = WS_STATE_CLOSING;
        ws_send_frame(conn->socket_fd, WS_OPCODE_CLOSE, NULL, 0);
    }
    
    conn->running = false;
}

/* Get user data from connection */
void *websocket_get_user_data(websocket_conn_t *conn) {
    return conn ? conn->user_data : NULL;
}

/* Set user data for connection */
void websocket_set_user_data(websocket_conn_t *conn, void *user_data) {
    if (conn) {
        conn->user_data = user_data;
    }
}
