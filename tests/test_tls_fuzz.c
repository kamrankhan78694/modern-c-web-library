/*
 * test_tls_fuzz.c — deterministic robustness fuzzer for the TLS untrusted-input path.
 * Built only with -DWEBLIB_ENABLE_TLS=ON.
 *
 * The ClientHello (and every record) is the first thing an unknown peer sends, so
 * the parsing/framing code must never crash, over-read, or exhibit UB on ANY input.
 * This drives three entry points with tens of thousands of random and mutated
 * inputs and only asserts "we got here without crashing" — the real value is running
 * it under ASan/UBSan, where any memory-safety or UB fault aborts the run:
 *
 *   - tls_parse_client_hello  (the ClientHello parser)
 *   - tls_khannection_recv    (record framing + state dispatch + everything downstream)
 *   - tls_record_open         (AEAD record deprotection)
 *
 * A fixed PRNG seed makes every run identical, so a failure is reproducible.
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#ifdef WEBLIB_TLS
#include "server_handshake.h"
#include "tls_khannection.h"
#include "record.h"
#include "handshake.h"
#endif

static int g_failures = 0;
static void check_true(const char *label, int cond) {
    if (cond) { printf("PASS: %s\n", label); }
    else { printf("FAIL: %s\n", label); g_failures++; }
}

#ifdef WEBLIB_TLS

/* A well-formed ClientHello (oracle-derived) used as the mutation seed. */
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

/* Deterministic PRNG (SplitMix64-style) so every run is identical + reproducible. */
static uint64_t g_rng;
static uint32_t rng32(void) {
    g_rng += 0x9e3779b97f4a7c15ULL;
    uint64_t z = g_rng;
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return (uint32_t)((z ^ (z >> 31)) & 0xffffffffu);
}

/* Run every untrusted-input entry point over one buffer. The only requirement is
 * that none of them crash / fault; return values are intentionally ignored. */
static void fuzz_buffer(const tls_server_config_t *cfg, const uint8_t *buf, size_t n,
                        uint8_t *out, size_t out_cap, uint8_t *app, size_t app_cap) {
    tls_client_hello_t ch;
    tls_khannection_t c;
    size_t ol = 0, al = 0, cl = 0;
    uint8_t ct = 0;

    (void)tls_parse_client_hello(buf, n, &ch);

    /* Feed the connection engine, sometimes split across two recvs to exercise the
     * record-reassembly path across calls. The split decision draws from the PRNG
     * (not the input) so every mode — including the record-framed one, whose inputs
     * always begin 0x16 — takes the split path on roughly half its iterations. */
    tls_khannection_init(&c, cfg);
    if (n >= 2 && (rng32() & 1)) {
        size_t half = n / 2;
        if (tls_khannection_recv(&c, buf, half, out, out_cap, &ol, app, app_cap, &al)
                == TLS_KHANNECTION_RC_OK) {
            (void)tls_khannection_recv(&c, buf + half, n - half, out, out_cap, &ol,
                                       app, app_cap, &al);
        }
    } else {
        (void)tls_khannection_recv(&c, buf, n, out, out_cap, &ol, app, app_cap, &al);
    }
    tls_khannection_wipe(&c);

    (void)tls_record_open(KAT_SERVER_HS_KEY, KAT_SERVER_HS_IV, 0, buf, n,
                          out, out_cap, &cl, &ct);
}

static void test_tls_fuzz(void) {
    tls_server_config_t cfg;
    static uint8_t out[TLS_RECORD_MAX_CIPHERTEXT + 4096];
    static uint8_t app[TLS_RECORD_MAX_PLAINTEXT + 4096];
    static uint8_t buf[3072];
    const int ITERS = 20000;   /* ~60k entry-point calls; keeps the suite fast while broad */
    int i;

    memset(&cfg, 0, sizeof cfg);
    cfg.cert_der = KAT_CERT;
    cfg.cert_len = sizeof KAT_CERT;
    cfg.ed25519_seed = KAT_ED_SEED;
    cfg.ed25519_pub = KAT_ED_PUB;
    cfg.server_eph_sk = KAT_SERVER_EPH;
    cfg.server_random = KAT_SERVER_RND;

    g_rng = 0xd1b54a32d192ed03ULL;   /* fixed seed */

    for (i = 0; i < ITERS; i++) {
        size_t n;
        int mode = (int)(rng32() % 3);
        if (mode == 0) {
            /* Pure random bytes of a random length. */
            n = rng32() % (sizeof buf + 1);
            for (size_t j = 0; j < n; j++) {
                buf[j] = (uint8_t)rng32();
            }
        } else if (mode == 1) {
            /* A mutated bare ClientHello handshake message. */
            n = sizeof KAT_CH;
            memcpy(buf, KAT_CH, n);
            if (rng32() & 1) {
                n = rng32() % (sizeof KAT_CH + 1);   /* random truncation */
            }
            int flips = 1 + (int)(rng32() % 8);
            for (int f = 0; f < flips && n > 0; f++) {
                buf[rng32() % n] ^= (uint8_t)(1 + rng32() % 255);
            }
        } else {
            /* A mutated ClientHello framed as a handshake record (fuzzes the record
             * header/length + the whole handshake path via the engine). */
            buf[0] = 0x16; buf[1] = 0x03; buf[2] = 0x03;
            buf[3] = (uint8_t)((sizeof KAT_CH) >> 8);
            buf[4] = (uint8_t)(sizeof KAT_CH);
            memcpy(buf + 5, KAT_CH, sizeof KAT_CH);
            n = 5 + sizeof KAT_CH;
            int flips = 1 + (int)(rng32() % 10);
            for (int f = 0; f < flips; f++) {
                buf[rng32() % n] ^= (uint8_t)(1 + rng32() % 255);
            }
        }
        fuzz_buffer(&cfg, buf, n, out, sizeof out, app, sizeof app);
    }

    check_true("fuzz: 20000 random/mutated inputs parsed without crash or fault", 1);
}
#endif /* WEBLIB_TLS */

int main(void) {
#ifndef WEBLIB_TLS
    printf("FAIL: test_tls_fuzz built without WEBLIB_TLS defined\n");
    return 1;
#else
    test_tls_fuzz();
    if (g_failures == 0) {
        printf("All TLS fuzz iterations passed.\n");
    }
    return g_failures == 0 ? 0 : 1;
#endif
}
