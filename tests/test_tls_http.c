/*
 * test_tls_http.c — end-to-end http_server_enable_tls integration.
 * Built only with -DWEBLIB_ENABLE_TLS=ON.
 *
 * Starts a real threaded http_server with TLS enabled on a loopback port, its
 * per-connection ephemeral key + ServerHello random pinned (via the internal test
 * RNG hook) to the independent oracle's known-answer values. A client socket then
 * drives a full TLS 1.3 handshake with the oracle's KAT vectors, sends an encrypted
 * HTTP request, and checks the encrypted HTTP response — proving the handshake-on-
 * accept and the TLS-routed request/response path both work.
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#ifdef WEBLIB_TLS
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#include "kamran.k"
#include "record.h"
#include "handshake.h"   /* TLS_HS_FINISHED, TLS_CONTENT_* */
#endif

static int g_failures = 0;
static void check_true(const char *label, int cond) {
    if (cond) {
        printf("PASS: %s\n", label);
    } else {
        printf("FAIL: %s\n", label);
        g_failures++;
    }
}

#ifdef WEBLIB_TLS

/* ==== oracle-derived known-answer vectors (subset) ==== */
static const uint8_t KAT_CH[144] = {
    0x01, 0x00, 0x00, 0x8c, 0x03, 0x03, 0xc1, 0xc1, 0xc1, 0xc1, 0xc1, 0xc1,
    0xc1, 0xc1, 0xc1, 0xc1, 0xc1, 0xc1, 0xc1, 0xc1, 0xc1, 0xc1, 0xc1, 0xc1,
    0xc1, 0xc1, 0xc1, 0xc1, 0xc1, 0xc1, 0xc1, 0xc1, 0xc1, 0xc1, 0xc1, 0xc1,
    0xc1, 0xc1, 0x20, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
    0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13, 0x14,
    0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 0x00,
    0x02, 0x13, 0x03, 0x01, 0x00, 0x00, 0x41, 0x00, 0x2b, 0x00, 0x03, 0x02,
    0x03, 0x04, 0x00, 0x0a, 0x00, 0x04, 0x00, 0x02, 0x00, 0x1d, 0x00, 0x0d,
    0x00, 0x04, 0x00, 0x02, 0x08, 0x07, 0x00, 0x33, 0x00, 0x26, 0x00, 0x24,
    0x00, 0x1d, 0x00, 0x20, 0x79, 0xa6, 0x31, 0xee, 0xde, 0x1b, 0xf9, 0xc9,
    0x8f, 0x12, 0x03, 0x2c, 0xde, 0xad, 0xd0, 0xe7, 0xa0, 0x79, 0x39, 0x8f,
    0xc7, 0x86, 0xb8, 0x8c, 0xc8, 0x46, 0xec, 0x89, 0xaf, 0x85, 0xa5, 0x1a,
};
static const uint8_t KAT_SERVER_EPH[32] = {
    0x60, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6a, 0x6b,
    0x6c, 0x6d, 0x6e, 0x6f, 0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77,
    0x78, 0x79, 0x7a, 0x7b, 0x7c, 0x7d, 0x7e, 0x7f,
};
static const uint8_t KAT_SERVER_RND[32] = {
    0x5e, 0x5e, 0x5e, 0x5e, 0x5e, 0x5e, 0x5e, 0x5e, 0x5e, 0x5e, 0x5e, 0x5e,
    0x5e, 0x5e, 0x5e, 0x5e, 0x5e, 0x5e, 0x5e, 0x5e, 0x5e, 0x5e, 0x5e, 0x5e,
    0x5e, 0x5e, 0x5e, 0x5e, 0x5e, 0x5e, 0x5e, 0x5e,
};
static const uint8_t KAT_SERVER_HS_KEY[32] = {
    0x41, 0xc1, 0xe2, 0xdb, 0x19, 0x3b, 0xc8, 0x84, 0x1e, 0xb5, 0xc6, 0x4a,
    0xe3, 0x08, 0x09, 0x30, 0x6e, 0x8a, 0xc8, 0x2e, 0x33, 0xc7, 0xd5, 0xfe,
    0x64, 0xce, 0x20, 0x6f, 0x2f, 0x32, 0xd6, 0xa4,
};
static const uint8_t KAT_SERVER_HS_IV[12] = {
    0x38, 0x9c, 0xa9, 0x76, 0x72, 0x06, 0x7e, 0x0c, 0x73, 0x31, 0xf0, 0x64,
};
static const uint8_t KAT_CLIENT_HS_KEY[32] = {
    0x5d, 0x09, 0xb3, 0x22, 0x93, 0x47, 0xca, 0x9f, 0x67, 0x0f, 0x6b, 0x98,
    0x8d, 0x30, 0xdc, 0xa3, 0x4f, 0xc4, 0x45, 0x77, 0x05, 0xf4, 0x5d, 0x30,
    0xd2, 0xb5, 0x19, 0x39, 0x5f, 0x0b, 0x85, 0xd2,
};
static const uint8_t KAT_CLIENT_HS_IV[12] = {
    0xa3, 0x18, 0xa3, 0xeb, 0x91, 0x9e, 0xb2, 0x74, 0x50, 0x5a, 0x6c, 0x9b,
};
static const uint8_t KAT_CLIENT_FINISHED_VD[32] = {
    0xa3, 0xb6, 0x47, 0x2e, 0xd7, 0x60, 0xfd, 0xb0, 0xc3, 0x55, 0x79, 0xf0,
    0x8e, 0xdb, 0x75, 0xbc, 0x87, 0x59, 0x9e, 0xa8, 0x51, 0xd1, 0x93, 0x22,
    0x85, 0xce, 0x6f, 0x29, 0x0d, 0x0b, 0xf7, 0x4f,
};
static const uint8_t KAT_CLIENT_AP_KEY[32] = {
    0x28, 0xc8, 0x92, 0x97, 0x21, 0xfa, 0x74, 0x2e, 0xc8, 0x0a, 0x78, 0x1b,
    0xed, 0xa8, 0x12, 0x2d, 0x28, 0xad, 0x03, 0xc3, 0x04, 0xdc, 0x8d, 0x8b,
    0x07, 0xa6, 0xbe, 0x6a, 0x35, 0x76, 0x8e, 0xa4,
};
static const uint8_t KAT_CLIENT_AP_IV[12] = {
    0xa3, 0xd7, 0xd4, 0x06, 0x4c, 0x40, 0x49, 0xc6, 0xab, 0x8a, 0x76, 0xc5,
};
static const uint8_t KAT_SERVER_AP_KEY[32] = {
    0x36, 0xe0, 0x70, 0xfc, 0x68, 0xdd, 0xab, 0x6d, 0xa2, 0x8f, 0xa6, 0x3b,
    0xe1, 0xac, 0x73, 0x6b, 0x01, 0x63, 0x57, 0xb7, 0xe2, 0x3c, 0x72, 0x8a,
    0x0e, 0xdd, 0x46, 0x05, 0xe7, 0xc3, 0xf4, 0x1d,
};
static const uint8_t KAT_SERVER_AP_IV[12] = {
    0x39, 0x6b, 0x0d, 0x59, 0xec, 0xb3, 0x89, 0x34, 0x53, 0x87, 0x4a, 0xa4,
};

/* Cert = the KAT cert blob; key = PKCS#8 Ed25519 wrapping the KAT seed (precomputed). */
static const char CERT_PEM[] =
    "-----BEGIN CERTIFICATE-----\n"
    "oKGio6SlpqeoqaqrrK2ur7CxsrO0tba3uLm6u7y9vr+goaKjpKWmp6ipqqusra6v"
    "sLGys7S1tre4ubq7vL2+v6ChoqOkpaanqKmqq6ytrq+wsbKztLW2t7i5uru8vb6/\n"
    "-----END CERTIFICATE-----\n";
static const char KEY_PEM[] =
    "-----BEGIN PRIVATE KEY-----\n"
    "MC4CAQAwBQYDK2VwBCIEIAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8g\n"
    "-----END PRIVATE KEY-----\n";

/* Internal test hook (defined in http_server.c, not part of the public API). */
extern void http_server__tls_test_rng(int (*fn)(void *buf, size_t len));

static int g_rng_call = 0;
static int test_rng(void *buf, size_t len) {
    if (len != 32) {
        return -1;
    }
    /* tls_do_handshake asks for the ephemeral key first, then the ServerHello
     * random — supply the KAT values in that order so the handshake is deterministic. */
    memcpy(buf, (g_rng_call++ % 2 == 0) ? KAT_SERVER_EPH : KAT_SERVER_RND, 32);
    return 0;
}

static void hello_handler(http_request_t *req, http_response_t *res) {
    (void)req;
    http_response_send_text(res, HTTP_OK, "TLS-OK");
}

/* ---- client-side socket + record helpers ---- */
static int write_all_c(int fd, const uint8_t *buf, size_t n) {
    size_t off = 0;
    while (off < n) {
        ssize_t s = send(fd, buf + off, n - off, 0);
        if (s < 0) { if (errno == EINTR) continue; return -1; }
        off += (size_t)s;
    }
    return 0;
}
static int read_n(int fd, uint8_t *buf, size_t n) {
    size_t off = 0;
    while (off < n) {
        ssize_t r = recv(fd, buf + off, n - off, 0);
        if (r < 0) { if (errno == EINTR) continue; return -1; }
        if (r == 0) return (int)off;
        off += (size_t)r;
    }
    return (int)off;
}
static int read_record(int fd, uint8_t *buf, size_t cap) {
    size_t body;
    if (cap < TLS_RECORD_HEADER_LEN) return -1;
    if (read_n(fd, buf, TLS_RECORD_HEADER_LEN) != (int)TLS_RECORD_HEADER_LEN) return -1;
    body = ((size_t)buf[3] << 8) | buf[4];
    if (TLS_RECORD_HEADER_LEN + body > cap) return -1;
    if (read_n(fd, buf + TLS_RECORD_HEADER_LEN, body) != (int)body) return -1;
    return (int)(TLS_RECORD_HEADER_LEN + body);
}

static void test_tls_http(void) {
    http_server_t *server;
    router_t *router;
    uint16_t port = 0;
    int listening = 0, p;
    int cfd;
    struct sockaddr_in addr;
    uint8_t ch[TLS_RECORD_HEADER_LEN + sizeof KAT_CH], sh[600], flight[2048], rec[512];
    uint8_t flightpt[512];
    size_t rl = 0, ptl = 0;
    uint8_t ctype = 0, finmsg[4 + 32];
    static const uint8_t ccs[6] = { 0x14, 0x03, 0x03, 0x00, 0x01, 0x01 };
    static const char REQ[] = "GET /hello HTTP/1.1\r\nHost: t\r\nConnection: close\r\n\r\n";
    char resp[4096];
    size_t resp_len = 0;
    uint64_t sseq = 0;
    int i, shl, fll;

    server = http_server_create();
    router = router_create();
    check_true("http-tls: server + router created", server != NULL && router != NULL);
    if (!server || !router) return;
    router_add_route(router, HTTP_GET, "/hello", hello_handler);
    http_server_set_router(server, router);

    g_rng_call = 0;
    http_server__tls_test_rng(test_rng);
    {
        int tls_rc = http_server_enable_tls(server, CERT_PEM, strlen(CERT_PEM),
                                            KEY_PEM, strlen(KEY_PEM));
        check_true("http-tls: enable_tls with a valid cert/key", tls_rc == 0);
        if (tls_rc != 0) {
            /* Bail: otherwise we would speak a TLS ClientHello to a plaintext server
             * and read_record() would block on the misframed HTTP response. */
            http_server_destroy(server);
            router_destroy(router);
            return;
        }
    }

    /* Enabling async after TLS must be refused — otherwise the async path would
     * silently serve plaintext on the HTTPS port (adversarial-review finding). */
    check_true("http-tls: set_async(true) rejected once TLS is enabled",
               http_server_set_async(server, true) == -1);

    for (p = 45443; p < 46443; p++) {
        if (http_server_listen(server, (uint16_t)p) == 0) {
            port = (uint16_t)p;
            listening = 1;
            break;
        }
    }
    check_true("http-tls: server listening on a loopback port", listening);
    if (!listening) { http_server_destroy(server); router_destroy(router); return; }

    cfd = socket(AF_INET, SOCK_STREAM, 0);
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    {
        int connected = (cfd >= 0
                         && connect(cfd, (struct sockaddr *)&addr, sizeof addr) == 0);
        check_true("http-tls: client connected", connected);
        if (!connected) {
            /* Bail cleanly rather than driving read_n()/read_record() on a dead fd. */
            if (cfd >= 0) close(cfd);
            http_server_stop(server);
            http_server_destroy(server);
            router_destroy(router);
            return;
        }
    }

    /* Handshake (KAT client). */
    ch[0] = TLS_CONTENT_HANDSHAKE;
    ch[1] = 0x03; ch[2] = 0x03;
    ch[3] = (uint8_t)((sizeof KAT_CH) >> 8);
    ch[4] = (uint8_t)(sizeof KAT_CH);
    memcpy(ch + TLS_RECORD_HEADER_LEN, KAT_CH, sizeof KAT_CH);
    write_all_c(cfd, ch, sizeof ch);
    shl = read_record(cfd, sh, sizeof sh);
    fll = read_record(cfd, flight, sizeof flight);
    check_true("http-tls: server flight opens under the oracle handshake key",
               shl > 0 && sh[0] == 0x16 && fll > 0 && flight[0] == 0x17
               && tls_record_open(KAT_SERVER_HS_KEY, KAT_SERVER_HS_IV, 0,
                                  flight, (size_t)fll, flightpt, sizeof flightpt,
                                  &ptl, &ctype) == 1
               && ctype == TLS_CONTENT_HANDSHAKE);

    write_all_c(cfd, ccs, sizeof ccs);
    finmsg[0] = TLS_HS_FINISHED; finmsg[1] = 0; finmsg[2] = 0; finmsg[3] = 0x20;
    memcpy(finmsg + 4, KAT_CLIENT_FINISHED_VD, 32);
    tls_record_seal(KAT_CLIENT_HS_KEY, KAT_CLIENT_HS_IV, 0, TLS_CONTENT_HANDSHAKE,
                    finmsg, sizeof finmsg, 0, rec, sizeof rec, &rl);
    write_all_c(cfd, rec, rl);

    /* Encrypted HTTP request. */
    tls_record_seal(KAT_CLIENT_AP_KEY, KAT_CLIENT_AP_IV, 0, TLS_CONTENT_APPLICATION_DATA,
                    (const uint8_t *)REQ, sizeof REQ - 1, 0, rec, sizeof rec, &rl);
    check_true("http-tls: sent encrypted HTTP request", write_all_c(cfd, rec, rl) == 0);

    /* Read + decrypt the encrypted HTTP response (header record, then body record). */
    for (i = 0; i < 6; i++) {
        uint8_t recbuf[4096], pt[4096];
        size_t pl = 0;
        uint8_t ct = 0;
        int r = read_record(cfd, recbuf, sizeof recbuf);
        if (r <= 0 || recbuf[0] != 0x17) break;
        if (!tls_record_open(KAT_SERVER_AP_KEY, KAT_SERVER_AP_IV, sseq,
                             recbuf, (size_t)r, pt, sizeof pt, &pl, &ct)) break;
        sseq++;
        if (ct == TLS_CONTENT_APPLICATION_DATA) {
            if (resp_len + pl < sizeof resp) {
                memcpy(resp + resp_len, pt, pl);
                resp_len += pl;
                resp[resp_len] = '\0';
            }
            if (strstr(resp, "TLS-OK") != NULL) break;   /* got the body */
        } else if (ct == TLS_CONTENT_ALERT) {
            break;   /* close_notify */
        }
    }
    resp[resp_len < sizeof resp ? resp_len : sizeof resp - 1] = '\0';
    check_true("http-tls: response is HTTP 200 over TLS", strstr(resp, "200") != NULL);
    check_true("http-tls: response body routed + encrypted correctly",
               strstr(resp, "TLS-OK") != NULL);

    close(cfd);
    http_server_stop(server);
    http_server_destroy(server);
    router_destroy(router);
}
#endif /* WEBLIB_TLS */

int main(void) {
#ifndef WEBLIB_TLS
    printf("FAIL: test_tls_http built without WEBLIB_TLS defined\n");
    return 1;
#else
    test_tls_http();
    if (g_failures == 0) {
        printf("All TLS http integration tests passed.\n");
    }
    return g_failures == 0 ? 0 : 1;
#endif
}
