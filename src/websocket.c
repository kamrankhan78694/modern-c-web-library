#include "weblib.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <ctype.h>

/* Case-insensitive substring search (portable implementation) */
static const char *_strcasestr(const char *haystack, const char *needle) {
    if (!needle || !*needle) return haystack;
    
    size_t needle_len = strlen(needle);
    for (; *haystack; haystack++) {
        if (strncasecmp(haystack, needle, needle_len) == 0) {
            return haystack;
        }
    }
    return NULL;
}

/* WebSocket protocol constants (RFC 6455) */
#define WS_GUID "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
#define WS_FRAME_MAX_SIZE 65536
#define WS_HEADER_MAX_SIZE 14

/* WebSocket opcodes */
typedef enum {
    WS_OPCODE_CONTINUATION = 0x0,
    WS_OPCODE_TEXT = 0x1,
    WS_OPCODE_BINARY = 0x2,
    WS_OPCODE_CLOSE = 0x8,
    WS_OPCODE_PING = 0x9,
    WS_OPCODE_PONG = 0xA
} ws_opcode_t;

/* WebSocket frame structure */
typedef struct {
    bool fin;
    bool rsv1;
    bool rsv2;
    bool rsv3;
    ws_opcode_t opcode;
    bool masked;
    uint64_t payload_length;
    uint8_t masking_key[4];
    uint8_t *payload;
} ws_frame_t;

/* WebSocket connection state */
typedef enum {
    WS_STATE_CONNECTING,
    WS_STATE_OPEN,
    WS_STATE_CLOSING,
    WS_STATE_CLOSED
} ws_state_t;

/* WebSocket connection structure */
struct websocket_connection {
    int fd;
    ws_state_t state;
    void *user_data;
    websocket_message_cb_t on_message;
    websocket_close_cb_t on_close;
    websocket_error_cb_t on_error;
    
    /* Frame parsing state */
    uint8_t *buffer;
    size_t buffer_len;
    size_t buffer_capacity;
    
    /* Fragmentation support */
    ws_opcode_t fragment_opcode;
    uint8_t *fragment_buffer;
    size_t fragment_len;
    size_t fragment_capacity;
};

/* WebSocket server structure */
struct websocket_server {
    http_server_t *http_server;
    websocket_connection_t **connections;
    size_t connection_count;
    size_t connection_capacity;
    websocket_connect_cb_t on_connect;
    void *user_data;
};

/* SHA-1 implementation for WebSocket handshake */
typedef struct {
    uint32_t state[5];
    uint32_t count[2];
    uint8_t buffer[64];
} sha1_ctx_t;

/* Base64 encoding for WebSocket accept key */
static const char base64_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/* SHA-1 Helper Functions */
#define SHA1_ROTLEFT(a, b) (((a) << (b)) | ((a) >> (32 - (b))))

static void sha1_transform(sha1_ctx_t *ctx, const uint8_t data[]) {
    uint32_t a, b, c, d, e, i, j, t, m[80];
    
    for (i = 0, j = 0; i < 16; ++i, j += 4)
        m[i] = (data[j] << 24) + (data[j + 1] << 16) + (data[j + 2] << 8) + (data[j + 3]);
    for (; i < 80; ++i)
        m[i] = SHA1_ROTLEFT(m[i - 3] ^ m[i - 8] ^ m[i - 14] ^ m[i - 16], 1);
    
    a = ctx->state[0];
    b = ctx->state[1];
    c = ctx->state[2];
    d = ctx->state[3];
    e = ctx->state[4];
    
    for (i = 0; i < 20; ++i) {
        t = SHA1_ROTLEFT(a, 5) + ((b & c) ^ (~b & d)) + e + 0x5A827999 + m[i];
        e = d;
        d = c;
        c = SHA1_ROTLEFT(b, 30);
        b = a;
        a = t;
    }
    for (; i < 40; ++i) {
        t = SHA1_ROTLEFT(a, 5) + (b ^ c ^ d) + e + 0x6ED9EBA1 + m[i];
        e = d;
        d = c;
        c = SHA1_ROTLEFT(b, 30);
        b = a;
        a = t;
    }
    for (; i < 60; ++i) {
        t = SHA1_ROTLEFT(a, 5) + ((b & c) ^ (b & d) ^ (c & d)) + e + 0x8F1BBCDC + m[i];
        e = d;
        d = c;
        c = SHA1_ROTLEFT(b, 30);
        b = a;
        a = t;
    }
    for (; i < 80; ++i) {
        t = SHA1_ROTLEFT(a, 5) + (b ^ c ^ d) + e + 0xCA62C1D6 + m[i];
        e = d;
        d = c;
        c = SHA1_ROTLEFT(b, 30);
        b = a;
        a = t;
    }
    
    ctx->state[0] += a;
    ctx->state[1] += b;
    ctx->state[2] += c;
    ctx->state[3] += d;
    ctx->state[4] += e;
}

static void sha1_init(sha1_ctx_t *ctx) {
    ctx->state[0] = 0x67452301;
    ctx->state[1] = 0xEFCDAB89;
    ctx->state[2] = 0x98BADCFE;
    ctx->state[3] = 0x10325476;
    ctx->state[4] = 0xC3D2E1F0;
    ctx->count[0] = ctx->count[1] = 0;
}

static void sha1_update(sha1_ctx_t *ctx, const uint8_t data[], size_t len) {
    size_t i;
    uint32_t j;
    
    j = ctx->count[0];
    if ((ctx->count[0] += len << 3) < j)
        ctx->count[1]++;
    ctx->count[1] += (len >> 29);
    j = (j >> 3) & 63;
    
    if ((j + len) > 63) {
        memcpy(&ctx->buffer[j], data, (i = 64 - j));
        sha1_transform(ctx, ctx->buffer);
        for (; i + 63 < len; i += 64)
            sha1_transform(ctx, &data[i]);
        j = 0;
    } else {
        i = 0;
    }
    
    memcpy(&ctx->buffer[j], &data[i], len - i);
}

static void sha1_final(sha1_ctx_t *ctx, uint8_t hash[]) {
    uint32_t i;
    uint8_t finalcount[8];
    
    for (i = 0; i < 8; ++i)
        finalcount[i] = (uint8_t)((ctx->count[(i >= 4 ? 0 : 1)] >> ((3 - (i & 3)) * 8)) & 255);
    
    sha1_update(ctx, (const uint8_t *)"\200", 1);
    while ((ctx->count[0] & 504) != 448)
        sha1_update(ctx, (const uint8_t *)"\0", 1);
    
    sha1_update(ctx, finalcount, 8);
    for (i = 0; i < 20; ++i)
        hash[i] = (uint8_t)((ctx->state[i >> 2] >> ((3 - (i & 3)) * 8)) & 255);
}

/* Base64 encoding */
static char *base64_encode(const uint8_t *data, size_t input_length) {
    size_t output_length = 4 * ((input_length + 2) / 3);
    char *encoded = (char *)malloc(output_length + 1);
    if (!encoded) return NULL;
    
    size_t i, j;
    for (i = 0, j = 0; i < input_length;) {
        uint32_t octet_a = i < input_length ? data[i++] : 0;
        uint32_t octet_b = i < input_length ? data[i++] : 0;
        uint32_t octet_c = i < input_length ? data[i++] : 0;
        uint32_t triple = (octet_a << 16) + (octet_b << 8) + octet_c;
        
        encoded[j++] = base64_table[(triple >> 18) & 0x3F];
        encoded[j++] = base64_table[(triple >> 12) & 0x3F];
        encoded[j++] = base64_table[(triple >> 6) & 0x3F];
        encoded[j++] = base64_table[triple & 0x3F];
    }
    
    /* Add padding */
    size_t mod = input_length % 3;
    if (mod == 1) {
        encoded[output_length - 1] = '=';
        encoded[output_length - 2] = '=';
    } else if (mod == 2) {
        encoded[output_length - 1] = '=';
    }
    
    encoded[output_length] = '\0';
    return encoded;
}

/* Generate WebSocket accept key */
static char *ws_generate_accept_key(const char *client_key) {
    char combined[128];
    snprintf(combined, sizeof(combined), "%s%s", client_key, WS_GUID);
    
    sha1_ctx_t ctx;
    uint8_t hash[20];
    sha1_init(&ctx);
    sha1_update(&ctx, (const uint8_t *)combined, strlen(combined));
    sha1_final(&ctx, hash);
    
    return base64_encode(hash, 20);
}

/* WebSocket handshake handler */
bool websocket_handle_upgrade(http_request_t *req, http_response_t *res) {
    /* Verify WebSocket upgrade request */
    const char *upgrade = http_request_get_header(req, "Upgrade");
    const char *connection = http_request_get_header(req, "Connection");
    const char *ws_key = http_request_get_header(req, "Sec-WebSocket-Key");
    const char *ws_version = http_request_get_header(req, "Sec-WebSocket-Version");
    
    if (!upgrade || strcasecmp(upgrade, "websocket") != 0) {
        return false;
    }
    
    if (!connection || _strcasestr(connection, "Upgrade") == NULL) {
        return false;
    }
    
    if (!ws_key || strlen(ws_key) == 0) {
        http_response_send_text(res, HTTP_BAD_REQUEST, "Missing Sec-WebSocket-Key");
        return false;
    }
    
    if (!ws_version || strcmp(ws_version, "13") != 0) {
        http_response_send_text(res, HTTP_BAD_REQUEST, "Unsupported WebSocket version");
        return false;
    }
    
    /* Generate accept key */
    char *accept_key = ws_generate_accept_key(ws_key);
    if (!accept_key) {
        http_response_send_text(res, HTTP_INTERNAL_ERROR, "Failed to generate accept key");
        return false;
    }
    
    /* Send upgrade response */
    res->status = 101; /* Switching Protocols */
    http_response_set_header(res, "Upgrade", "websocket");
    http_response_set_header(res, "Connection", "Upgrade");
    http_response_set_header(res, "Sec-WebSocket-Accept", accept_key);
    
    free(accept_key);
    /* Note: Don't set res->sent = true here, let the server send the response */
    
    return true;
}

/* Create WebSocket connection */
websocket_connection_t *websocket_connection_create(int fd) {
    websocket_connection_t *conn = (websocket_connection_t *)calloc(1, sizeof(websocket_connection_t));
    if (!conn) return NULL;
    
    conn->fd = fd;
    conn->state = WS_STATE_OPEN;
    conn->buffer_capacity = WS_FRAME_MAX_SIZE;
    conn->buffer = (uint8_t *)malloc(conn->buffer_capacity);
    
    if (!conn->buffer) {
        free(conn);
        return NULL;
    }
    
    return conn;
}

/* Destroy WebSocket connection */
void websocket_connection_destroy(websocket_connection_t *conn) {
    if (!conn) return;
    
    if (conn->buffer) {
        free(conn->buffer);
    }
    
    if (conn->fragment_buffer) {
        free(conn->fragment_buffer);
    }
    
    free(conn);
}

/* Parse WebSocket frame header */
static int ws_parse_frame_header(const uint8_t *data, size_t len, ws_frame_t *frame) {
    if (len < 2) return -1;
    
    frame->fin = (data[0] & 0x80) != 0;
    frame->rsv1 = (data[0] & 0x40) != 0;
    frame->rsv2 = (data[0] & 0x20) != 0;
    frame->rsv3 = (data[0] & 0x10) != 0;
    frame->opcode = (ws_opcode_t)(data[0] & 0x0F);
    
    frame->masked = (data[1] & 0x80) != 0;
    uint8_t payload_len = data[1] & 0x7F;
    
    size_t header_size = 2;
    
    if (payload_len == 126) {
        if (len < 4) return -1;
        frame->payload_length = ((uint64_t)data[2] << 8) | data[3];
        header_size = 4;
    } else if (payload_len == 127) {
        if (len < 10) return -1;
        frame->payload_length = 0;
        for (int i = 0; i < 8; i++) {
            frame->payload_length = (frame->payload_length << 8) | data[2 + i];
        }
        header_size = 10;
    } else {
        frame->payload_length = payload_len;
    }
    
    if (frame->masked) {
        if (len < header_size + 4) return -1;
        memcpy(frame->masking_key, data + header_size, 4);
        header_size += 4;
    }
    
    return (int)header_size;
}

/* Unmask WebSocket payload */
static void ws_unmask_payload(uint8_t *payload, size_t len, const uint8_t mask[4]) {
    for (size_t i = 0; i < len; i++) {
        payload[i] ^= mask[i % 4];
    }
}

/* Send WebSocket frame */
int websocket_send(websocket_connection_t *conn, ws_message_type_t type, const void *data, size_t len) {
    if (!conn || conn->state != WS_STATE_OPEN) {
        return -1;
    }
    
    uint8_t header[WS_HEADER_MAX_SIZE];
    size_t header_len = 0;
    
    /* Set FIN and opcode */
    ws_opcode_t opcode = (type == WS_MESSAGE_TEXT) ? WS_OPCODE_TEXT : WS_OPCODE_BINARY;
    header[0] = 0x80 | opcode;
    
    /* Set payload length */
    if (len < 126) {
        header[1] = (uint8_t)len;
        header_len = 2;
    } else if (len < 65536) {
        header[1] = 126;
        header[2] = (len >> 8) & 0xFF;
        header[3] = len & 0xFF;
        header_len = 4;
    } else {
        header[1] = 127;
        for (int i = 7; i >= 0; i--) {
            header[2 + i] = (len >> (8 * (7 - i))) & 0xFF;
        }
        header_len = 10;
    }
    
    /* Server-to-client frames are not masked */
    
    /* Send header */
    ssize_t sent = send(conn->fd, header, header_len, 0);
    if (sent < 0) {
        return -1;
    }
    
    /* Send payload */
    if (len > 0) {
        sent = send(conn->fd, data, len, 0);
        if (sent < 0) {
            return -1;
        }
    }
    
    return 0;
}

/* Send WebSocket text message */
int websocket_send_text(websocket_connection_t *conn, const char *text) {
    return websocket_send(conn, WS_MESSAGE_TEXT, text, strlen(text));
}

/* Send WebSocket binary message */
int websocket_send_binary(websocket_connection_t *conn, const void *data, size_t len) {
    return websocket_send(conn, WS_MESSAGE_BINARY, data, len);
}

/* Send WebSocket ping */
int websocket_send_ping(websocket_connection_t *conn, const void *data, size_t len) {
    if (!conn || conn->state != WS_STATE_OPEN) {
        return -1;
    }
    
    uint8_t header[2];
    header[0] = 0x80 | WS_OPCODE_PING;
    header[1] = (uint8_t)len;
    
    send(conn->fd, header, 2, 0);
    if (len > 0) {
        send(conn->fd, data, len, 0);
    }
    
    return 0;
}

/* Send WebSocket pong */
int websocket_send_pong(websocket_connection_t *conn, const void *data, size_t len) {
    if (!conn || conn->state != WS_STATE_OPEN) {
        return -1;
    }
    
    uint8_t header[2];
    header[0] = 0x80 | WS_OPCODE_PONG;
    header[1] = (uint8_t)len;
    
    send(conn->fd, header, 2, 0);
    if (len > 0) {
        send(conn->fd, data, len, 0);
    }
    
    return 0;
}

/* Close WebSocket connection */
int websocket_close(websocket_connection_t *conn, uint16_t code, const char *reason) {
    if (!conn || conn->state == WS_STATE_CLOSED) {
        return -1;
    }
    
    conn->state = WS_STATE_CLOSING;
    
    uint8_t close_frame[125];
    close_frame[0] = 0x80 | WS_OPCODE_CLOSE;
    
    size_t payload_len = 2;
    close_frame[2] = (code >> 8) & 0xFF;
    close_frame[3] = code & 0xFF;
    
    if (reason) {
        size_t reason_len = strlen(reason);
        if (reason_len > 123) reason_len = 123;
        memcpy(close_frame + 4, reason, reason_len);
        payload_len += reason_len;
    }
    
    close_frame[1] = (uint8_t)payload_len;
    
    send(conn->fd, close_frame, payload_len + 2, 0);
    
    conn->state = WS_STATE_CLOSED;
    close(conn->fd);
    
    return 0;
}

/* Process incoming WebSocket data */
int websocket_process_data(websocket_connection_t *conn, const uint8_t *data, size_t len) {
    if (!conn || conn->state != WS_STATE_OPEN) {
        return -1;
    }
    
    /* Add data to buffer */
    if (conn->buffer_len + len > conn->buffer_capacity) {
        size_t new_capacity = conn->buffer_capacity * 2;
        while (new_capacity < conn->buffer_len + len) {
            new_capacity *= 2;
        }
        
        uint8_t *new_buffer = (uint8_t *)realloc(conn->buffer, new_capacity);
        if (!new_buffer) {
            return -1;
        }
        
        conn->buffer = new_buffer;
        conn->buffer_capacity = new_capacity;
    }
    
    memcpy(conn->buffer + conn->buffer_len, data, len);
    conn->buffer_len += len;
    
    /* Process frames */
    while (conn->buffer_len > 0) {
        ws_frame_t frame = {0};
        int header_size = ws_parse_frame_header(conn->buffer, conn->buffer_len, &frame);
        
        if (header_size < 0) {
            /* Need more data */
            break;
        }
        
        size_t frame_size = header_size + frame.payload_length;
        if (conn->buffer_len < frame_size) {
            /* Need more data */
            break;
        }
        
        /* Extract payload */
        frame.payload = conn->buffer + header_size;
        
        /* Unmask if needed (client-to-server frames must be masked) */
        if (frame.masked) {
            ws_unmask_payload(frame.payload, frame.payload_length, frame.masking_key);
        }
        
        /* Handle frame based on opcode */
        switch (frame.opcode) {
            case WS_OPCODE_TEXT:
            case WS_OPCODE_BINARY:
                if (frame.fin) {
                    /* Complete message */
                    if (conn->on_message) {
                        ws_message_type_t msg_type = (frame.opcode == WS_OPCODE_TEXT) 
                            ? WS_MESSAGE_TEXT : WS_MESSAGE_BINARY;
                        conn->on_message(conn, msg_type, frame.payload, frame.payload_length);
                    }
                } else {
                    /* Start of fragmented message */
                    conn->fragment_opcode = frame.opcode;
                    conn->fragment_len = frame.payload_length;
                    conn->fragment_capacity = frame.payload_length * 2;
                    conn->fragment_buffer = (uint8_t *)malloc(conn->fragment_capacity);
                    if (conn->fragment_buffer) {
                        memcpy(conn->fragment_buffer, frame.payload, frame.payload_length);
                    }
                }
                break;
                
            case WS_OPCODE_CONTINUATION:
                if (conn->fragment_buffer) {
                    /* Append to fragmented message */
                    if (conn->fragment_len + frame.payload_length > conn->fragment_capacity) {
                        size_t new_cap = conn->fragment_capacity * 2;
                        uint8_t *new_buf = (uint8_t *)realloc(conn->fragment_buffer, new_cap);
                        if (new_buf) {
                            conn->fragment_buffer = new_buf;
                            conn->fragment_capacity = new_cap;
                        }
                    }
                    
                    if (conn->fragment_len + frame.payload_length <= conn->fragment_capacity) {
                        memcpy(conn->fragment_buffer + conn->fragment_len, frame.payload, frame.payload_length);
                        conn->fragment_len += frame.payload_length;
                    }
                    
                    if (frame.fin && conn->on_message) {
                        /* Complete fragmented message */
                        ws_message_type_t msg_type = (conn->fragment_opcode == WS_OPCODE_TEXT) 
                            ? WS_MESSAGE_TEXT : WS_MESSAGE_BINARY;
                        conn->on_message(conn, msg_type, conn->fragment_buffer, conn->fragment_len);
                        
                        free(conn->fragment_buffer);
                        conn->fragment_buffer = NULL;
                        conn->fragment_len = 0;
                        conn->fragment_capacity = 0;
                    }
                }
                break;
                
            case WS_OPCODE_CLOSE:
                /* Handle close frame */
                if (conn->on_close) {
                    uint16_t code = 1000;
                    if (frame.payload_length >= 2) {
                        code = (frame.payload[0] << 8) | frame.payload[1];
                    }
                    conn->on_close(conn, code);
                }
                websocket_close(conn, 1000, "Normal closure");
                break;
                
            case WS_OPCODE_PING:
                /* Respond with pong */
                websocket_send_pong(conn, frame.payload, frame.payload_length);
                break;
                
            case WS_OPCODE_PONG:
                /* Pong received, no action needed */
                break;
                
            default:
                /* Unknown opcode */
                if (conn->on_error) {
                    conn->on_error(conn, "Unknown opcode");
                }
                break;
        }
        
        /* Remove processed frame from buffer */
        memmove(conn->buffer, conn->buffer + frame_size, conn->buffer_len - frame_size);
        conn->buffer_len -= frame_size;
    }
    
    return 0;
}

/* Set message callback */
void websocket_set_message_callback(websocket_connection_t *conn, websocket_message_cb_t callback) {
    if (conn) {
        conn->on_message = callback;
    }
}

/* Set close callback */
void websocket_set_close_callback(websocket_connection_t *conn, websocket_close_cb_t callback) {
    if (conn) {
        conn->on_close = callback;
    }
}

/* Set error callback */
void websocket_set_error_callback(websocket_connection_t *conn, websocket_error_cb_t callback) {
    if (conn) {
        conn->on_error = callback;
    }
}

/* Set user data */
void websocket_set_user_data(websocket_connection_t *conn, void *user_data) {
    if (conn) {
        conn->user_data = user_data;
    }
}

/* Get user data */
void *websocket_get_user_data(websocket_connection_t *conn) {
    return conn ? conn->user_data : NULL;
}

/* Check if connection is open */
bool websocket_is_open(websocket_connection_t *conn) {
    return conn && conn->state == WS_STATE_OPEN;
}
