/*
 * tls_server.c — HTTPS example using the experimental pure-C TLS 1.3 layer.
 *
 * Build with -DWEBLIB_ENABLE_TLS=ON. Generate a self-signed Ed25519 cert first:
 *
 *   openssl genpkey -algorithm ed25519 -out key.pem
 *   openssl req -x509 -new -key key.pem -out cert.pem -days 365 -subj "/CN=localhost"
 *
 * Run:   ./tls_server [cert.pem] [key.pem] [port]     (defaults: cert.pem key.pem 8443)
 * Test:  curl -k https://localhost:8443/hello
 *        printf 'GET / HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n' \
 *            | openssl s_client -quiet -connect localhost:8443
 *
 * The profile is TLS_CHACHA20_POLY1305_SHA256 + X25519 + Ed25519, so the client
 * must accept an Ed25519 certificate (modern OpenSSL/browsers do). EXPERIMENTAL /
 * UNAUDITED — not for production.
 */
#include "kamran.k"
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>

static http_server_t *g_server = NULL;
static volatile sig_atomic_t shutdown_requested = 0;

static void signal_handler(int signum) {
    (void)signum;
    shutdown_requested = 1;
}

static void handle_root(http_request_t *req, http_response_t *res) {
    (void)req;
    http_response_send_text(res, HTTP_OK, "Hello over pure-C TLS 1.3!\n");
}

static void handle_hello(http_request_t *req, http_response_t *res) {
    (void)req;
    http_response_send_text(res, HTTP_OK, "Hello, World! (encrypted)\n");
}

/* A body larger than one TLS record's 2^14 plaintext limit, so the response must be
 * fragmented across records — exercised by tests/interop_openssl.sh against a real
 * OpenSSL client. The payload is a repeating A-Z pattern the script can verify. */
#define BIG_BODY_LEN 40000
static char g_big_body[BIG_BODY_LEN + 1];

static void handle_big(http_request_t *req, http_response_t *res) {
    (void)req;
    http_response_send_text(res, HTTP_OK, g_big_body);
}

/* Read an entire file into a NUL-terminated buffer; caller frees. */
static char *read_file(const char *path, size_t *len_out) {
    FILE *f = fopen(path, "rb");
    long sz;
    char *buf;
    size_t got;
    if (!f) {
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) != 0 || (sz = ftell(f)) < 0) {
        fclose(f);
        return NULL;
    }
    rewind(f);
    buf = (char *)malloc((size_t)sz + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    got = fread(buf, 1, (size_t)sz, f);
    buf[got] = '\0';
    *len_out = got;
    fclose(f);
    return buf;
}

int main(int argc, char **argv) {
    const char *cert_path = argc > 1 ? argv[1] : "cert.pem";
    const char *key_path = argc > 2 ? argv[2] : "key.pem";
    uint16_t port = (uint16_t)(argc > 3 ? atoi(argv[3]) : 8443);
    char *cert, *key;
    size_t cert_len = 0, key_len = 0;
    router_t *router;

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    cert = read_file(cert_path, &cert_len);
    key = read_file(key_path, &key_len);
    if (!cert || !key) {
        fprintf(stderr, "Failed to read cert '%s' or key '%s'.\n", cert_path, key_path);
        fprintf(stderr, "Generate them with:\n"
                        "  openssl genpkey -algorithm ed25519 -out key.pem\n"
                        "  openssl req -x509 -new -key key.pem -out cert.pem "
                        "-days 365 -subj \"/CN=localhost\"\n");
        free(cert);
        free(key);
        return 1;
    }

    g_server = http_server_create();
    router = router_create();
    if (!g_server || !router) {
        fprintf(stderr, "Failed to allocate server/router\n");
        free(cert);
        free(key);
        return 1;
    }
    router_add_route(router, HTTP_GET, "/", handle_root);
    router_add_route(router, HTTP_GET, "/hello", handle_hello);
    {
        int i;
        for (i = 0; i < BIG_BODY_LEN; i++) {
            g_big_body[i] = (char)('A' + (i % 26));
        }
        g_big_body[BIG_BODY_LEN] = '\0';
    }
    router_add_route(router, HTTP_GET, "/big", handle_big);
    http_server_set_router(g_server, router);

    if (http_server_enable_tls(g_server, cert, cert_len, key, key_len) != 0) {
        fprintf(stderr, "http_server_enable_tls failed — is this a -DWEBLIB_ENABLE_TLS=ON "
                        "build, and are the cert/key a valid Ed25519 pair?\n");
        free(cert);
        free(key);
        router_destroy(router);
        http_server_destroy(g_server);
        return 1;
    }
    /* enable_tls copied what it needs; the PEM buffers can go now. */
    free(cert);
    free(key);

    if (http_server_listen(g_server, port) < 0) {
        fprintf(stderr, "Failed to listen on port %u\n", (unsigned)port);
        router_destroy(router);
        http_server_destroy(g_server);
        return 1;
    }
    printf("HTTPS server (EXPERIMENTAL pure-C TLS 1.3) on https://localhost:%u/\n", (unsigned)port);
    printf("  GET /       GET /hello    GET /big (%d-byte body)    (Ctrl+C to stop)\n",
           BIG_BODY_LEN);
    fflush(stdout);

    while (!shutdown_requested) {
        sleep(1);
    }

    http_server_stop(g_server);
    http_server_destroy(g_server);
    router_destroy(router);
    return 0;
}
