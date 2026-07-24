/*
 * ed25519_key.h — parse Ed25519 keys from their DER encodings. EXPERIMENTAL /
 * UNAUDITED.
 *
 * Part of the experimental pure-C TLS layer; compiled only under
 * -DWEBLIB_ENABLE_TLS=ON. Extracts the raw 32-byte Ed25519 seed / public key
 * from the two DER structures a TLS server loads:
 *   - a PKCS#8 OneAsymmetricKey (RFC 5958 / RFC 8410 §7) private key, and
 *   - a SubjectPublicKeyInfo (RFC 8410 §4) public key.
 * Built on the bounds-checked DER reader (der.h); every field is validated
 * (Ed25519 OID 1.3.101.112, exact key lengths) before any bytes are returned.
 * Exercised by tests/test_tls_parse.c.
 */
#ifndef WEBLIB_TLS_ED25519_KEY_H
#define WEBLIB_TLS_ED25519_KEY_H

#ifdef WEBLIB_TLS

#include <stddef.h>
#include <stdint.h>

/*
 * Parse a PKCS#8 Ed25519 private key (DER) and copy its 32-byte seed to `seed`.
 * Returns 0 on success, -1 if the structure is not a well-formed Ed25519 PKCS#8
 * key (wrong OID, wrong lengths, malformed DER). `seed` is written only on
 * success.
 */
int ed25519_parse_pkcs8(const uint8_t *der, size_t der_len, uint8_t seed[32]);

/*
 * Parse an Ed25519 SubjectPublicKeyInfo (DER) and copy its 32-byte public key to
 * `pub`. Returns 0 on success, -1 otherwise. `pub` is written only on success.
 */
int ed25519_parse_spki(const uint8_t *der, size_t der_len, uint8_t pub[32]);

#endif /* WEBLIB_TLS */
#endif /* WEBLIB_TLS_ED25519_KEY_H */
