/*
 * chacha20poly1305.h — ChaCha20-Poly1305 AEAD (RFC 8439 §2.8). EXPERIMENTAL / UNAUDITED.
 *
 * Part of the experimental pure-C TLS layer; compiled only under
 * -DWEBLIB_ENABLE_TLS=ON (native-only). This is the AEAD of the
 * TLS_CHACHA20_POLY1305_SHA256 cipher suite, built from the chacha20 and
 * poly1305 primitives. Verified against the RFC 8439 §2.8.2 known-answer vector —
 * see tests/test_tls_crypto.c.
 *
 * The nonce is the 96-bit per-record nonce (in TLS 1.3, the sequence number XORed
 * with the write IV). A given (key, nonce) pair must NEVER be reused to seal two
 * different messages.
 */
#ifndef WEBLIB_TLS_CHACHA20POLY1305_H
#define WEBLIB_TLS_CHACHA20POLY1305_H

#ifdef WEBLIB_TLS

#include <stddef.h>
#include <stdint.h>

/*
 * Seal: encrypt `pt` (`pt_len` bytes) and authenticate it together with `aad`
 * (`aad_len` bytes). Writes `pt_len` bytes of ciphertext to `ct` and the 16-byte
 * authentication tag to `tag`. `ct` may equal `pt` (in-place). `aad` may be NULL
 * iff `aad_len` is 0. Returns 1 on success, 0 on internal allocation failure.
 */
int chacha20poly1305_seal(const uint8_t key[32], const uint8_t nonce[12],
                          const uint8_t *aad, size_t aad_len,
                          const uint8_t *pt, size_t pt_len,
                          uint8_t *ct, uint8_t tag[16]);

/*
 * Open: verify `tag` over `aad` + `ct`, and ONLY if it is authentic decrypt `ct`
 * (`ct_len` bytes) into `pt`. `pt` may equal `ct` (in-place). Returns 1 if the tag
 * is valid (and `pt` has been written), 0 otherwise — on failure `pt` is left
 * untouched. The tag comparison is constant-time; plaintext is never released on a
 * verification failure.
 */
int chacha20poly1305_open(const uint8_t key[32], const uint8_t nonce[12],
                          const uint8_t *aad, size_t aad_len,
                          const uint8_t *ct, size_t ct_len,
                          const uint8_t tag[16], uint8_t *pt);

#endif /* WEBLIB_TLS */
#endif /* WEBLIB_TLS_CHACHA20POLY1305_H */
