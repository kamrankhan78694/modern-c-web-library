/*
 * chacha20poly1305.c — ChaCha20-Poly1305 AEAD (RFC 8439 §2.8). EXPERIMENTAL / UNAUDITED.
 *
 * Combines the chacha20 and poly1305 primitives. Compiled only under
 * -DWEBLIB_ENABLE_TLS=ON. Verified against RFC 8439 §2.8.2 in
 * tests/test_tls_crypto.c.
 *
 * Construction (RFC 8439 §2.8):
 *   otk        = chacha20_block(key, counter=0, nonce)[0..31]   (one-time Poly1305 key)
 *   ciphertext = chacha20_encrypt(key, counter=1, nonce, plaintext)
 *   mac_data   = aad || pad16(aad) || ct || pad16(ct) || le64(len aad) || le64(len ct)
 *   tag        = poly1305(otk, mac_data)
 */
#include "chacha20poly1305.h"

#ifdef WEBLIB_TLS

#include "chacha20.h"
#include "poly1305.h"
#include "kamran.k"   /* secure_zero, secure_compare */
#include <stdlib.h>
#include <string.h>

static void store64_le(uint8_t *p, uint64_t v) {
    int i;
    for (i = 0; i < 8; i++) {
        p[i] = (uint8_t)(v >> (8 * i));
    }
}

/*
 * Compute the Poly1305 tag over the AEAD MAC input for (aad, ct) under the
 * one-time key `otk`. The MAC input (aad, ciphertext, and 16 length bytes) is all
 * public data, so the scratch buffer is not secret; it is heap-allocated because
 * its size is unbounded. Returns 1 on success, 0 on allocation failure.
 */
static int aead_tag(const uint8_t otk[32],
                    const uint8_t *aad, size_t aad_len,
                    const uint8_t *ct, size_t ct_len,
                    uint8_t tag[16]) {
    size_t aad_pad, ct_pad, mac_len;
    uint8_t *m;
    size_t off = 0;

    /* Guard against size_t overflow when sizing the MAC input. Each pad adds < 16
     * bytes and the length block adds 16, so the total is at most aad_len + ct_len
     * + 46; an overflow here would under-size the malloc and lead to out-of-bounds
     * writes below. (In TLS these lengths are bounded by the record size, but the
     * primitive must be safe for any caller.) */
    if (ct_len > SIZE_MAX - 46 || aad_len > SIZE_MAX - 46 - ct_len) {
        return 0;
    }

    aad_pad = (16 - (aad_len % 16)) % 16;
    ct_pad  = (16 - (ct_len  % 16)) % 16;
    mac_len = aad_len + aad_pad + ct_len + ct_pad + 16;

    m = (uint8_t *)malloc(mac_len);
    if (m == NULL) {
        return 0;
    }

    if (aad_len > 0) {
        memcpy(m + off, aad, aad_len);
    }
    off += aad_len;
    memset(m + off, 0, aad_pad);
    off += aad_pad;
    if (ct_len > 0) {
        memcpy(m + off, ct, ct_len);
    }
    off += ct_len;
    memset(m + off, 0, ct_pad);
    off += ct_pad;
    store64_le(m + off, (uint64_t)aad_len);
    off += 8;
    store64_le(m + off, (uint64_t)ct_len);

    poly1305_mac(otk, m, mac_len, tag);

    free(m);
    return 1;
}

int chacha20poly1305_seal(const uint8_t key[32], const uint8_t nonce[12],
                          const uint8_t *aad, size_t aad_len,
                          const uint8_t *pt, size_t pt_len,
                          uint8_t *ct, uint8_t tag[16]) {
    uint8_t block0[64];
    int ok;

    /* One-time Poly1305 key = first 32 bytes of the counter-0 keystream block. */
    chacha20_block(key, 0, nonce, block0);

    /* Encrypt starting at block counter 1. */
    chacha20_encrypt(key, 1, nonce, pt, pt_len, ct);

    ok = aead_tag(block0, aad, aad_len, ct, pt_len, tag);

    secure_zero(block0, sizeof(block0));   /* wipe the derived Poly1305 key */
    return ok;
}

int chacha20poly1305_open(const uint8_t key[32], const uint8_t nonce[12],
                          const uint8_t *aad, size_t aad_len,
                          const uint8_t *ct, size_t ct_len,
                          const uint8_t tag[16], uint8_t *pt) {
    uint8_t block0[64];
    uint8_t computed_tag[16];

    chacha20_block(key, 0, nonce, block0);

    if (!aead_tag(block0, aad, aad_len, ct, ct_len, computed_tag)) {
        secure_zero(block0, sizeof(block0));
        return 0;                          /* allocation failure */
    }

    /* Verify the tag in constant time BEFORE decrypting — never release plaintext
     * on an authentication failure. */
    if (!secure_compare(computed_tag, tag, 16)) {
        secure_zero(block0, sizeof(block0));
        secure_zero(computed_tag, sizeof(computed_tag));
        return 0;                          /* authentication failed */
    }

    chacha20_encrypt(key, 1, nonce, ct, ct_len, pt);

    secure_zero(block0, sizeof(block0));
    secure_zero(computed_tag, sizeof(computed_tag));
    return 1;
}

#endif /* WEBLIB_TLS */
