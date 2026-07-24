/*
 * ed25519_key.c — parse Ed25519 keys from DER. EXPERIMENTAL / UNAUDITED.
 * See ed25519_key.h.
 *
 * Compiled only under -DWEBLIB_ENABLE_TLS=ON. Walks the PKCS#8 and
 * SubjectPublicKeyInfo structures with the bounds-checked DER reader, checking
 * the Ed25519 algorithm OID and the exact key sizes before copying any bytes.
 * The DER templates were cross-checked against OpenSSL (it parses the same bytes
 * as valid Ed25519 keys and derives the matching public key).
 */
#include "ed25519_key.h"

#ifdef WEBLIB_TLS

#include "der.h"
#include <string.h>

/* id-Ed25519 (RFC 8410 §3) = 1.3.101.112, DER OID value bytes. */
static const uint8_t ED25519_OID[3] = { 0x2B, 0x65, 0x70 };

/* Match an AlgorithmIdentifier SEQUENCE carrying exactly the Ed25519 OID.
 * RFC 8410 §3 requires the parameters field to be absent; a lone OID satisfies
 * that. Returns 0 on match, -1 otherwise. Advances `seq` past the algorithm. */
static int expect_ed25519_alg(der_reader_t *seq) {
    der_reader_t alg;
    const uint8_t *oid;
    size_t oid_len;
    if (der_enter(seq, DER_TAG_SEQUENCE, &alg) != 0) {
        return -1;
    }
    if (der_expect(&alg, DER_TAG_OID, &oid, &oid_len) != 0) {
        return -1;
    }
    if (oid_len != sizeof ED25519_OID || memcmp(oid, ED25519_OID, oid_len) != 0) {
        return -1;
    }
    /* RFC 8410 §3: the Ed25519 AlgorithmIdentifier parameters MUST be absent, so
     * the SEQUENCE must hold nothing after the OID. */
    if (!der_at_end(&alg)) {
        return -1;
    }
    return 0;
}

int ed25519_parse_pkcs8(const uint8_t *der, size_t der_len, uint8_t seed[32]) {
    der_reader_t top, seq, priv;
    const uint8_t *v;
    size_t vlen;

    if (der == NULL || seed == NULL) {
        return -1;
    }

    der_init(&top, der, der_len);

    /* OneAsymmetricKey ::= SEQUENCE { version, privateKeyAlgorithm, privateKey } */
    if (der_enter(&top, DER_TAG_SEQUENCE, &seq) != 0) {
        return -1;
    }

    /* version INTEGER: 0 (v1) or 1 (v2, which may carry an optional public key). */
    if (der_expect(&seq, DER_TAG_INTEGER, &v, &vlen) != 0) {
        return -1;
    }
    if (vlen != 1 || (v[0] != 0x00 && v[0] != 0x01)) {
        return -1;
    }

    if (expect_ed25519_alg(&seq) != 0) {
        return -1;
    }

    /* privateKey ::= OCTET STRING wrapping CurvePrivateKey ::= OCTET STRING(32). */
    if (der_enter(&seq, DER_TAG_OCTET_STRING, &priv) != 0) {
        return -1;
    }
    if (der_expect(&priv, DER_TAG_OCTET_STRING, &v, &vlen) != 0) {
        return -1;
    }
    if (vlen != 32) {
        return -1;
    }
    /* The CurvePrivateKey wrapper holds only the 32-byte seed, and nothing may
     * follow the OneAsymmetricKey SEQUENCE. (The outer SEQUENCE itself may carry
     * v2's optional attributes/publicKey, so it is not required to be empty.) */
    if (!der_at_end(&priv) || !der_at_end(&top)) {
        return -1;
    }

    memcpy(seed, v, 32);
    return 0;
}

int ed25519_parse_spki(const uint8_t *der, size_t der_len, uint8_t pub[32]) {
    der_reader_t top, seq;
    const uint8_t *v;
    size_t vlen;

    if (der == NULL || pub == NULL) {
        return -1;
    }

    der_init(&top, der, der_len);

    /* SubjectPublicKeyInfo ::= SEQUENCE { algorithm, subjectPublicKey } */
    if (der_enter(&top, DER_TAG_SEQUENCE, &seq) != 0) {
        return -1;
    }

    if (expect_ed25519_alg(&seq) != 0) {
        return -1;
    }

    /* subjectPublicKey ::= BIT STRING. For a key the value is one leading
     * "unused bits" octet (0x00) followed by the 32 raw public-key bytes. */
    if (der_expect(&seq, DER_TAG_BIT_STRING, &v, &vlen) != 0) {
        return -1;
    }
    if (vlen != 33 || v[0] != 0x00) {
        return -1;
    }
    /* SubjectPublicKeyInfo is exactly { algorithm, subjectPublicKey }, and
     * nothing may follow it. */
    if (!der_at_end(&seq) || !der_at_end(&top)) {
        return -1;
    }

    memcpy(pub, v + 1, 32);
    return 0;
}

#endif /* WEBLIB_TLS */
