/*
 * hkdf.h — HKDF-SHA256 (RFC 5869) and TLS 1.3 HKDF-Expand-Label (RFC 8446 §7.1).
 * EXPERIMENTAL / UNAUDITED.
 *
 * Part of the experimental pure-C TLS layer; compiled only under
 * -DWEBLIB_ENABLE_TLS=ON (native-only). This is the key-derivation core of the
 * TLS 1.3 key schedule for the SHA-256 cipher suite. Built on the shared SHA-256 /
 * HMAC-SHA256 module (src/crypto/sha256). Verified against RFC 5869 §A.1/A.3 and
 * the RFC 8448 TLS 1.3 trace (early_secret / derived) — see tests/test_tls_crypto.c.
 *
 * Hash is fixed to SHA-256, so all PRK/secret values are 32 bytes.
 */
#ifndef WEBLIB_TLS_HKDF_H
#define WEBLIB_TLS_HKDF_H

#ifdef WEBLIB_TLS

#include <stddef.h>
#include <stdint.h>

#define HKDF_SHA256_LEN 32   /* SHA-256 output size */

/*
 * HKDF-Extract (RFC 5869 §2.2): PRK = HMAC-SHA256(salt, ikm). `salt` may be NULL
 * with salt_len 0, which (as HMAC zero-pads the key) is equivalent to the RFC's
 * "salt not provided -> HashLen zero bytes". `prk` receives HKDF_SHA256_LEN bytes.
 */
void hkdf_extract(const uint8_t *salt, size_t salt_len,
                  const uint8_t *ikm, size_t ikm_len,
                  uint8_t prk[HKDF_SHA256_LEN]);

/*
 * HKDF-Expand (RFC 5869 §2.3): expand `prk` and `info` into `okm_len` bytes.
 * Returns 1 on success, 0 if okm_len > 255*HKDF_SHA256_LEN or on allocation
 * failure. Intermediate key material is wiped before returning.
 */
int hkdf_expand(const uint8_t prk[HKDF_SHA256_LEN],
                const uint8_t *info, size_t info_len,
                uint8_t *okm, size_t okm_len);

/*
 * HKDF-Expand-Label (RFC 8446 §7.1): builds the HkdfLabel struct
 *   struct { uint16 length; opaque label<7..255> = "tls13 "+Label; opaque context<0..255>; }
 * and calls HKDF-Expand. `label` is the bare label without the "tls13 " prefix
 * (e.g. "derived"); its full length including the prefix must be 7..255. `context`
 * is usually a transcript hash and may be NULL with context_len 0. Returns 1 on
 * success, 0 on a length-constraint violation or allocation failure.
 */
int hkdf_expand_label(const uint8_t secret[HKDF_SHA256_LEN],
                      const char *label, size_t label_len,
                      const uint8_t *context, size_t context_len,
                      uint8_t *out, size_t out_len);

#endif /* WEBLIB_TLS */
#endif /* WEBLIB_TLS_HKDF_H */
