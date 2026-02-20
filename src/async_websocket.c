#include "weblib.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/socket.h>

/* Constants */
#define ASYNC_WS_MAX_CONNECTIONS 1024
#define ASYNC_WS_READ_BUFFER_SIZE 4096
#define WS_HEADER_MAX_SIZE 14

/* WebSocket opcodes (internal) */
typedef enum {
    WS_OPCODE_TEXT = 0x1,
    WS_OPCODE_BINARY = 0x2,
    WS_OPCODE_CLOSE = 0x8,
    WS_OPCODE_PING = 0x9,
    WS_OPCODE_PONG = 0xA
} ws_opcode_t;

/* Write queue node */
typedef struct ws_write_node {
    uint8_t *data;
    size_t len;
    size_t sent;
    struct ws_write_node *next;
} ws_write_node_t;

/* Async WebSocket connection wrapper */
typedef struct async_ws_conn {
    websocket_connection_t *ws;   /* Underlying WebSocket connection */
    event_loop_t *loop;           /* Event loop */
    ws_write_node_t *write_head;  /* Write queue */
    ws_write_node_t *write_tail;
    bool registered;              /* Is fd registered in event loop? */
} async_ws_conn_t;

/* Async WebSocket manager */
typedef struct async_ws_manager {
    event_loop_t *loop;
    async_ws_conn_t **connections;
    size_t connection_count;
    size_t connection_capacity;
    websocket_message_cb_t on_message;
    websocket_close_cb_t on_close;
    websocket_error_cb_t on_error;
    websocket_connect_cb_t on_connect;
    void *user_data;
} async_ws_manager_t;

/* Forward declarations */
static void _async_ws_event_handler(int fd, int events, void *user_data);
static int _write_queue_push(async_ws_conn_t *conn, const uint8_t *data, size_t len);
static int _write_queue_drain(async_ws_conn_t *conn);
static void _write_queue_free(async_ws_conn_t *conn);
static async_ws_conn_t *_find_connection(async_ws_manager_t *mgr, websocket_connection_t *ws);
static int _encode_ws_frame(ws_message_type_t type, const void *data, size_t len, uint8_t **out_frame, size_t *out_len);
static int _make_socket_nonblocking(int fd);

/* Create async WebSocket manager */
async_ws_manager_t *async_ws_manager_create(event_loop_t *loop) {
    if (!loop) {
        return NULL;
    }
    
    async_ws_manager_t *mgr = (async_ws_manager_t *)calloc(1, sizeof(async_ws_manager_t));
    if (!mgr) {
        return NULL;
    }
    
    mgr->loop = loop;
    mgr->connection_capacity = 16;
    mgr->connections = (async_ws_conn_t **)calloc(mgr->connection_capacity, sizeof(async_ws_conn_t *));
    if (!mgr->connections) {
        free(mgr);
        return NULL;
    }
    
    return mgr;
}

/* Destroy async WebSocket manager */
void async_ws_manager_destroy(async_ws_manager_t *mgr) {
    if (!mgr) {
        return;
    }
    
    /* Close all connections */
    for (size_t i = 0; i < mgr->connection_count; i++) {
        if (mgr->connections[i]) {
            async_ws_conn_t *conn = mgr->connections[i];
            
            /* Unregister from event loop */
            if (conn->registered && conn->ws) {
                int fd = websocket_get_fd(conn->ws);
                if (fd >= 0) {
                    event_loop_remove_fd(mgr->loop, fd);
                }
            }
            
            /* Free write queue */
            _write_queue_free(conn);
            
            /* Note: We don't destroy the websocket_connection_t itself,
             * as it's owned by the caller */
            
            free(conn);
        }
    }
    
    free(mgr->connections);
    free(mgr);
}

/* Set callbacks for async WebSocket manager */
void async_ws_manager_set_callbacks(async_ws_manager_t *mgr, 
                                     websocket_message_cb_t on_msg,
                                     websocket_close_cb_t on_close,
                                     websocket_error_cb_t on_error) {
    if (!mgr) {
        return;
    }
    
    mgr->on_message = on_msg;
    mgr->on_close = on_close;
    mgr->on_error = on_error;
}

/* Add WebSocket connection to async manager */
int async_ws_manager_add(async_ws_manager_t *mgr, websocket_connection_t *ws) {
    int fd = websocket_get_fd(ws);
    if (!mgr || !ws || fd < 0) {
        return -1;
    }
    
    /* Check if connection already exists */
    if (_find_connection(mgr, ws)) {
        return -1;
    }
    
    /* Expand connections array if needed */
    if (mgr->connection_count >= mgr->connection_capacity) {
        size_t new_capacity = mgr->connection_capacity * 2;
        if (new_capacity > ASYNC_WS_MAX_CONNECTIONS) {
            new_capacity = ASYNC_WS_MAX_CONNECTIONS;
        }
        if (mgr->connection_count >= new_capacity) {
            return -1;
        }
        
        async_ws_conn_t **new_connections = (async_ws_conn_t **)realloc(
            mgr->connections,
            new_capacity * sizeof(async_ws_conn_t *)
        );
        if (!new_connections) {
            return -1;
        }
        
        /* Zero out new slots */
        memset(new_connections + mgr->connection_capacity, 0,
               (new_capacity - mgr->connection_capacity) * sizeof(async_ws_conn_t *));
        
        mgr->connections = new_connections;
        mgr->connection_capacity = new_capacity;
    }
    
    /* Create async connection wrapper */
    async_ws_conn_t *conn = (async_ws_conn_t *)calloc(1, sizeof(async_ws_conn_t));
    if (!conn) {
        return -1;
    }
    
    conn->ws = ws;
    conn->loop = mgr->loop;
    conn->write_head = NULL;
    conn->write_tail = NULL;
    conn->registered = false;
    
    /* Make socket non-blocking */
    if (_make_socket_nonblocking(fd) < 0) {
        free(conn);
        return -1;
    }
    
    /* Register fd in event loop for reading */
    if (event_loop_add_fd(mgr->loop, fd, EVENT_READ, _async_ws_event_handler, conn) < 0) {
        free(conn);
        return -1;
    }
    conn->registered = true;
    
    /* Set callbacks on the WebSocket connection */
    if (mgr->on_message) {
        websocket_set_message_callback(ws, mgr->on_message);
    }
    if (mgr->on_close) {
        websocket_set_close_callback(ws, mgr->on_close);
    }
    if (mgr->on_error) {
        websocket_set_error_callback(ws, mgr->on_error);
    }
    
    /* Add to connections array */
    mgr->connections[mgr->connection_count++] = conn;
    
    return 0;
}

/* Remove WebSocket connection from async manager */
int async_ws_manager_remove(async_ws_manager_t *mgr, websocket_connection_t *ws) {
    if (!mgr || !ws) {
        return -1;
    }
    
    /* Find the connection */
    for (size_t i = 0; i < mgr->connection_count; i++) {
        if (mgr->connections[i] && mgr->connections[i]->ws == ws) {
            async_ws_conn_t *conn = mgr->connections[i];
            int fd = websocket_get_fd(ws);
            
            /* Unregister from event loop */
            if (conn->registered && fd >= 0) {
                event_loop_remove_fd(mgr->loop, fd);
                conn->registered = false;
            }
            
            /* Free write queue */
            _write_queue_free(conn);
            
            /* Remove from array by shifting remaining elements */
            free(conn);
            for (size_t j = i; j < mgr->connection_count - 1; j++) {
                mgr->connections[j] = mgr->connections[j + 1];
            }
            mgr->connections[mgr->connection_count - 1] = NULL;
            mgr->connection_count--;
            
            return 0;
        }
    }
    
    return -1;
}

/* Non-blocking send */
int async_ws_send(async_ws_manager_t *mgr, websocket_connection_t *ws, 
                  ws_message_type_t type, const void *data, size_t len) {
    if (!mgr || !ws || !data) {
        return -1;
    }
    
    /* Find the connection */
    async_ws_conn_t *conn = _find_connection(mgr, ws);
    if (!conn) {
        return -1;
    }
    
    /* Encode WebSocket frame */
    uint8_t *frame = NULL;
    size_t frame_len = 0;
    if (_encode_ws_frame(type, data, len, &frame, &frame_len) < 0) {
        return -1;
    }
    
    bool was_empty = (conn->write_head == NULL);
    
    /* Add to write queue */
    if (_write_queue_push(conn, frame, frame_len) < 0) {
        free(frame);
        return -1;
    }
    
    free(frame);
    
    /* If write queue was empty, register for write events */
    if (was_empty && conn->registered) {
        int fd = websocket_get_fd(ws);
        if (event_loop_modify_fd(mgr->loop, fd, EVENT_READ | EVENT_WRITE) < 0) {
            return -1;
        }
    }
    
    return 0;
}

/* Get connection count */
size_t async_ws_manager_count(async_ws_manager_t *mgr) {
    if (!mgr) {
        return 0;
    }
    return mgr->connection_count;
}

/* Internal: Event handler callback */
static void _async_ws_event_handler(int fd, int events, void *user_data) {
    async_ws_conn_t *conn = (async_ws_conn_t *)user_data;
    if (!conn || !conn->ws) {
        return;
    }
    
    websocket_connection_t *ws = conn->ws;
    websocket_error_cb_t on_error = websocket_get_error_callback(ws);
    websocket_close_cb_t on_close = websocket_get_close_callback(ws);
    
    /* Handle read events */
    if (events & EVENT_READ) {
        uint8_t buf[ASYNC_WS_READ_BUFFER_SIZE];
        ssize_t nread = recv(fd, buf, sizeof(buf), 0);
        
        if (nread > 0) {
            /* Process received data */
            if (websocket_process_data(ws, buf, (size_t)nread) < 0) {
                /* Error processing data */
                if (on_error) {
                    on_error(ws, "Error processing WebSocket data");
                }
                /* Connection will be cleaned up by user */
            }
        } else if (nread == 0) {
            /* Connection closed by peer */
            if (on_close) {
                on_close(ws, 1006); /* Abnormal closure */
            }
        } else {
            /* Error on recv */
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                if (on_error) {
                    on_error(ws, "Socket read error");
                }
            }
        }
    }
    
    /* Handle write events */
    if (events & EVENT_WRITE) {
        if (_write_queue_drain(conn) < 0) {
            /* Error draining write queue */
            if (on_error) {
                on_error(ws, "Socket write error");
            }
        } else if (conn->write_head == NULL) {
            /* Write queue is now empty, switch back to read-only */
            if (conn->registered) {
                event_loop_modify_fd(conn->loop, fd, EVENT_READ);
            }
        }
    }
    
    /* Handle error events */
    if (events & EVENT_ERROR) {
        if (on_error) {
            on_error(ws, "Socket error");
        }
    }
}

/* Internal: Push data to write queue */
static int _write_queue_push(async_ws_conn_t *conn, const uint8_t *data, size_t len) {
    if (!conn || !data || len == 0) {
        return -1;
    }
    
    ws_write_node_t *node = (ws_write_node_t *)malloc(sizeof(ws_write_node_t));
    if (!node) {
        return -1;
    }
    
    node->data = (uint8_t *)malloc(len);
    if (!node->data) {
        free(node);
        return -1;
    }
    
    memcpy(node->data, data, len);
    node->len = len;
    node->sent = 0;
    node->next = NULL;
    
    /* Add to tail of queue */
    if (conn->write_tail) {
        conn->write_tail->next = node;
        conn->write_tail = node;
    } else {
        conn->write_head = node;
        conn->write_tail = node;
    }
    
    return 0;
}

/* Internal: Drain write queue */
static int _write_queue_drain(async_ws_conn_t *conn) {
    if (!conn || !conn->ws) {
        return -1;
    }
    
    int fd = websocket_get_fd(conn->ws);
    if (fd < 0) {
        return -1;
    }
    
    while (conn->write_head) {
        ws_write_node_t *node = conn->write_head;
        size_t remaining = node->len - node->sent;
        
        ssize_t nsent = send(fd, node->data + node->sent, remaining, 0);
        
        if (nsent < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                /* Would block, try again later */
                return 0;
            } else if (errno == EINTR) {
                /* Interrupted, retry */
                continue;
            } else {
                /* Error */
                return -1;
            }
        }
        
        node->sent += (size_t)nsent;
        
        if (node->sent >= node->len) {
            /* Node complete, remove from queue */
            conn->write_head = node->next;
            if (!conn->write_head) {
                conn->write_tail = NULL;
            }
            
            free(node->data);
            free(node);
        } else {
            /* Partial send, will retry later */
            return 0;
        }
    }
    
    return 0;
}

/* Internal: Free all write queue nodes */
static void _write_queue_free(async_ws_conn_t *conn) {
    if (!conn) {
        return;
    }
    
    ws_write_node_t *node = conn->write_head;
    while (node) {
        ws_write_node_t *next = node->next;
        free(node->data);
        free(node);
        node = next;
    }
    
    conn->write_head = NULL;
    conn->write_tail = NULL;
}

/* Internal: Find connection by WebSocket pointer */
static async_ws_conn_t *_find_connection(async_ws_manager_t *mgr, websocket_connection_t *ws) {
    if (!mgr || !ws) {
        return NULL;
    }
    
    for (size_t i = 0; i < mgr->connection_count; i++) {
        if (mgr->connections[i] && mgr->connections[i]->ws == ws) {
            return mgr->connections[i];
        }
    }
    
    return NULL;
}

/* Internal: Encode WebSocket frame (server-to-client, unmasked) */
static int _encode_ws_frame(ws_message_type_t type, const void *data, size_t len, 
                            uint8_t **out_frame, size_t *out_len) {
    if (!data || !out_frame || !out_len) {
        return -1;
    }
    
    uint8_t header[WS_HEADER_MAX_SIZE];
    size_t header_len = 0;
    
    /* Set FIN and opcode */
    ws_opcode_t opcode = (type == WS_MESSAGE_TEXT) ? WS_OPCODE_TEXT : WS_OPCODE_BINARY;
    header[0] = 0x80 | opcode;
    
    /* Set payload length (server frames are not masked) */
    if (len < 126) {
        header[1] = (uint8_t)len;
        header_len = 2;
    } else if (len < 65536) {
        header[1] = 126;
        header[2] = (len >> 8) & 0xFF;
        header[3] = len & 0xFF;
        header_len = 4;
    } else {
        uint64_t len64 = (uint64_t)len;
        header[1] = 127;
        /* RFC 6455: 64-bit extended payload length in network byte order (big-endian) */
        for (int i = 0; i < 8; i++) {
            header[9 - i] = (uint8_t)(len64 & 0xFF);
            len64 >>= 8;
        }
        header_len = 10;
    }
    
    /* Allocate frame buffer (header + payload) */
    size_t total_len = header_len + len;
    uint8_t *frame = (uint8_t *)malloc(total_len);
    if (!frame) {
        return -1;
    }
    
    /* Copy header and payload */
    memcpy(frame, header, header_len);
    if (len > 0) {
        memcpy(frame + header_len, data, len);
    }
    
    *out_frame = frame;
    *out_len = total_len;
    
    return 0;
}

/* Internal: Make socket non-blocking */
static int _make_socket_nonblocking(int fd) {
    if (fd < 0) {
        return -1;
    }
    
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        return -1;
    }
    
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        return -1;
    }
    
    return 0;
}
