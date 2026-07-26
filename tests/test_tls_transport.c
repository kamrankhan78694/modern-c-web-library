/*
 * test_tls_transport.c — the blocking-socket TLS adapter over a real socketpair.
 * Built only with -DWEBLIB_ENABLE_TLS=ON.
 *
 * A server thread drives tls_transport (accept -> read one request -> write one
 * response -> close) on one end of a socketpair. The main thread plays a TLS 1.3
 * client on the other end using the independent-oracle known-answer vectors: it
 * sends the ClientHello, checks the server's flight opens under the oracle's
 * handshake key, sends the (KAT) Finished, then exchanges one encrypted
 * request/response. This exercises the full handshake + application data path over
 * actual sockets, cross-checked against a separate implementation's keys.
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#ifdef WEBLIB_TLS
#include <pthread.h>
#include <sys/socket.h>
#include <unistd.h>
#include <errno.h>
#include "tls_transport.h"
#include "tls_khannection.h"
#include "record.h"
#include "handshake.h"   /* TLS_HS_FINISHED */
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

/* ==== oracle-derived known-answer vectors (see scratchpad/server_hs_oracle.py) ==== */
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
static const uint8_t KAT_CERT[96] = {
    0xa0, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xab,
    0xac, 0xad, 0xae, 0xaf, 0xb0, 0xb1, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7,
    0xb8, 0xb9, 0xba, 0xbb, 0xbc, 0xbd, 0xbe, 0xbf, 0xa0, 0xa1, 0xa2, 0xa3,
    0xa4, 0xa5, 0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xab, 0xac, 0xad, 0xae, 0xaf,
    0xb0, 0xb1, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xbb,
    0xbc, 0xbd, 0xbe, 0xbf, 0xa0, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7,
    0xa8, 0xa9, 0xaa, 0xab, 0xac, 0xad, 0xae, 0xaf, 0xb0, 0xb1, 0xb2, 0xb3,
    0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xbb, 0xbc, 0xbd, 0xbe, 0xbf,
};
static const uint8_t KAT_ED_SEED[32] = {
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c,
    0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18,
    0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 0x20,
};
static const uint8_t KAT_ED_PUB[32] = {
    0x79, 0xb5, 0x56, 0x2e, 0x8f, 0xe6, 0x54, 0xf9, 0x40, 0x78, 0xb1, 0x12,
    0xe8, 0xa9, 0x8b, 0xa7, 0x90, 0x1f, 0x85, 0x3a, 0xe6, 0x95, 0xbe, 0xd7,
    0xe0, 0xe3, 0x91, 0x0b, 0xad, 0x04, 0x96, 0x64,
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

static const char SERVER_RESPONSE[] =
    "HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nhello";
static const char CLIENT_REQUEST[] = "GET / HTTP/1.1\r\n\r\n";

/* ---- blocking socket helpers (client side of the pair) ---- */
static int write_all_c(int fd, const uint8_t *buf, size_t n) {
    size_t off = 0;
    while (off < n) {
        ssize_t s = send(fd, buf + off, n - off, 0);
        if (s < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        off += (size_t)s;
    }
    return 0;
}
static int read_n(int fd, uint8_t *buf, size_t n) {
    size_t off = 0;
    while (off < n) {
        ssize_t r = recv(fd, buf + off, n - off, 0);
        if (r < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (r == 0) return (int)off;   /* short read: EOF */
        off += (size_t)r;
    }
    return (int)off;
}
/* Read exactly one TLS record (5-byte header + declared body). Returns total len. */
static int read_record(int fd, uint8_t *buf, size_t cap) {
    size_t body;
    if (cap < TLS_RECORD_HEADER_LEN) return -1;
    if (read_n(fd, buf, TLS_RECORD_HEADER_LEN) != (int)TLS_RECORD_HEADER_LEN) return -1;
    body = ((size_t)buf[3] << 8) | buf[4];
    if (TLS_RECORD_HEADER_LEN + body > cap) return -1;
    if (read_n(fd, buf + TLS_RECORD_HEADER_LEN, body) != (int)body) return -1;
    return (int)(TLS_RECORD_HEADER_LEN + body);
}

/* ---- server side, run on its own thread ---- */
typedef struct {
    int fd;
    int accept_rc;
    int write_rc;
    ssize_t req_len;
    char req[256];
} srv_result_t;

static void *server_thread(void *arg) {
    static tls_transport_t t;   /* ~40 KiB; one server, so a single static is fine */
    srv_result_t *r = (srv_result_t *)arg;
    tls_server_config_t cfg;

    memset(&cfg, 0, sizeof cfg);
    cfg.cert_der = KAT_CERT;
    cfg.cert_len = sizeof KAT_CERT;
    cfg.ed25519_seed = KAT_ED_SEED;
    cfg.ed25519_pub = KAT_ED_PUB;
    cfg.server_eph_sk = KAT_SERVER_EPH;
    cfg.server_random = KAT_SERVER_RND;

    r->accept_rc = tls_transport_accept(&t, r->fd, &cfg);
    if (r->accept_rc == 0) {
        r->req_len = tls_transport_read(&t, r->req, sizeof r->req - 1);
        if (r->req_len > 0) {
            r->req[r->req_len] = '\0';
        }
        r->write_rc = tls_transport_write(&t, SERVER_RESPONSE, sizeof SERVER_RESPONSE - 1);
    }
    tls_transport_close(&t);
    return NULL;
}

static void test_tls_transport(void) {
    int sv[2];
    pthread_t tid;
    srv_result_t res;
    int cfd;
    uint8_t ch[TLS_RECORD_HEADER_LEN + sizeof KAT_CH];
    uint8_t sh[600], flight[2048], resprec[512];
    uint8_t flightpt[512], resppt[256];
    uint8_t finmsg[4 + 32], rec[128];
    size_t rl = 0;
    int shl, fll, resl;
    size_t ptlen = 0;
    uint8_t ctype = 0;
    static const uint8_t ccs[6] = { 0x14, 0x03, 0x03, 0x00, 0x01, 0x01 };

    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0) {
        check_true("transport: socketpair created", 0);
        return;
    }
    res.fd = sv[0];
    res.accept_rc = -1;
    res.write_rc = -1;
    res.req_len = -1;
    cfd = sv[1];

    if (pthread_create(&tid, NULL, server_thread, &res) != 0) {
        check_true("transport: server thread started", 0);
        close(sv[0]);
        close(sv[1]);
        return;
    }

    /* 1. ClientHello. */
    ch[0] = TLS_CONTENT_HANDSHAKE;
    ch[1] = 0x03;
    ch[2] = 0x03;
    ch[3] = (uint8_t)((sizeof KAT_CH) >> 8);
    ch[4] = (uint8_t)(sizeof KAT_CH);
    memcpy(ch + TLS_RECORD_HEADER_LEN, KAT_CH, sizeof KAT_CH);
    check_true("transport: client sent ClientHello",
               write_all_c(cfd, ch, sizeof ch) == 0);

    /* 2. Read the server flight: a ServerHello record then a protected record. */
    shl = read_record(cfd, sh, sizeof sh);
    fll = read_record(cfd, flight, sizeof flight);
    check_true("transport: server sent ServerHello + protected flight",
               shl > 0 && sh[0] == 0x16 && fll > 0 && flight[0] == 0x17);
    check_true("transport: flight opens under the oracle handshake key",
               fll > 0
               && tls_record_open(KAT_SERVER_HS_KEY, KAT_SERVER_HS_IV, 0,
                                  flight, (size_t)fll, flightpt, sizeof flightpt,
                                  &ptlen, &ctype) == 1
               && ctype == TLS_CONTENT_HANDSHAKE);

    /* 3. ChangeCipherSpec (dropped) then the client Finished. */
    check_true("transport: client sent CCS", write_all_c(cfd, ccs, sizeof ccs) == 0);
    finmsg[0] = TLS_HS_FINISHED;
    finmsg[1] = 0x00;
    finmsg[2] = 0x00;
    finmsg[3] = 0x20;
    memcpy(finmsg + 4, KAT_CLIENT_FINISHED_VD, 32);
    check_true("transport: seal client Finished",
               tls_record_seal(KAT_CLIENT_HS_KEY, KAT_CLIENT_HS_IV, 0,
                               TLS_CONTENT_HANDSHAKE, finmsg, sizeof finmsg, 0,
                               rec, sizeof rec, &rl) == 1);
    check_true("transport: client sent Finished", write_all_c(cfd, rec, rl) == 0);

    /* 4. Encrypted application request. */
    check_true("transport: seal application request",
               tls_record_seal(KAT_CLIENT_AP_KEY, KAT_CLIENT_AP_IV, 0,
                               TLS_CONTENT_APPLICATION_DATA,
                               (const uint8_t *)CLIENT_REQUEST, sizeof CLIENT_REQUEST - 1,
                               0, rec, sizeof rec, &rl) == 1);
    check_true("transport: client sent request", write_all_c(cfd, rec, rl) == 0);

    /* 5. Read + decrypt the server's application response. */
    resl = read_record(cfd, resprec, sizeof resprec);
    check_true("transport: server response decrypts under the oracle app key",
               resl > 0 && resprec[0] == 0x17
               && tls_record_open(KAT_SERVER_AP_KEY, KAT_SERVER_AP_IV, 0,
                                  resprec, (size_t)resl, resppt, sizeof resppt,
                                  &ptlen, &ctype) == 1
               && ctype == TLS_CONTENT_APPLICATION_DATA
               && ptlen == sizeof SERVER_RESPONSE - 1
               && memcmp(resppt, SERVER_RESPONSE, ptlen) == 0);

    close(cfd);
    pthread_join(tid, NULL);

    /* 6. What the server observed. */
    check_true("transport: server completed the handshake", res.accept_rc == 0);
    check_true("transport: server decrypted the exact request",
               res.req_len == (ssize_t)(sizeof CLIENT_REQUEST - 1)
               && memcmp(res.req, CLIENT_REQUEST, (size_t)res.req_len) == 0);
    check_true("transport: server sent its response", res.write_rc == 0);

    close(sv[0]);
}
#endif /* WEBLIB_TLS */

int main(void) {
#ifndef WEBLIB_TLS
    printf("FAIL: test_tls_transport built without WEBLIB_TLS defined\n");
    return 1;
#else
    test_tls_transport();
    if (g_failures == 0) {
        printf("All TLS transport tests passed.\n");
    }
    return g_failures == 0 ? 0 : 1;
#endif
}
