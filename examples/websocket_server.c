#include "weblib.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>

/* Global server instance for signal handling */
static websocket_server_t *g_server = NULL;
static volatile sig_atomic_t shutdown_requested = 0;

/* Signal handler for graceful shutdown */
void signal_handler(int signum) {
    (void)signum;
    shutdown_requested = 1;
}

/* WebSocket event handler */
void websocket_handler(websocket_conn_t *conn, ws_event_t event, 
                      const uint8_t *data, size_t length) {
    switch (event) {
        case WS_EVENT_OPEN:
            printf("WebSocket connection opened\n");
            websocket_send_text(conn, "Welcome to the WebSocket server!");
            break;
            
        case WS_EVENT_MESSAGE:
            printf("Received text message (%zu bytes): %s\n", length, (const char *)data);
            
            /* Echo the message back */
            char response[1024];
            snprintf(response, sizeof(response), "Echo: %s", (const char *)data);
            websocket_send_text(conn, response);
            break;
            
        case WS_EVENT_BINARY:
            printf("Received binary message (%zu bytes)\n", length);
            
            /* Echo binary data back */
            websocket_send_binary(conn, data, length);
            break;
            
        case WS_EVENT_CLOSE:
            printf("WebSocket connection closed\n");
            break;
            
        case WS_EVENT_ERROR:
            printf("WebSocket error occurred\n");
            break;
    }
}

int main(int argc, char *argv[]) {
    uint16_t port = 9001;
    
    /* Parse command line arguments */
    if (argc > 1) {
        port = (uint16_t)atoi(argv[1]);
        if (port == 0) {
            fprintf(stderr, "Invalid port number\n");
            return 1;
        }
    }
    
    /* Setup signal handlers */
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    /* Create WebSocket server */
    g_server = websocket_server_create();
    if (!g_server) {
        fprintf(stderr, "Failed to create WebSocket server\n");
        return 1;
    }
    
    /* Set event handler */
    websocket_server_set_handler(g_server, websocket_handler);
    
    /* Start server */
    printf("Starting WebSocket server on port %d...\n", port);
    printf("Connect using a WebSocket client to ws://localhost:%d\n", port);
    printf("Press Ctrl+C to stop the server.\n\n");
    
    if (websocket_server_listen(g_server, port) < 0) {
        fprintf(stderr, "Failed to start WebSocket server\n");
        websocket_server_destroy(g_server);
        return 1;
    }
    
    /* Wait for server to stop */
    while (!shutdown_requested) {
        sleep(1);
    }
    
    /* Shutdown initiated */
    printf("\nShutting down...\n");
    websocket_server_stop(g_server);
    
    /* Cleanup */
    websocket_server_destroy(g_server);
    
    printf("WebSocket server stopped successfully\n");
    
    return 0;
}
