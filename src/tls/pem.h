/*
 * pem.h — minimal PEM (RFC 7468) reader. EXPERIMENTAL / UNAUDITED.
 *
 * Part of the experimental pure-C TLS layer; compiled only under
 * -DWEBLIB_ENABLE_TLS=ON. Extracts the DER payload of a single
 * "-----BEGIN <label>-----" / "-----END <label>-----" block by Base64-decoding
 * the body between the markers (via the shared crypto/base64 decoder). Just
 * enough to load a server certificate and private key; not a general PEM library.
 *
 * The input is treated as untrusted text: all scanning is bounds-checked against
 * the caller's length (the buffer need not be NUL-terminated and may contain any
 * bytes). Exercised by tests/test_tls_parse.c.
 */
#ifndef WEBLIB_TLS_PEM_H
#define WEBLIB_TLS_PEM_H

#ifdef WEBLIB_TLS

#include <stddef.h>

/* Longest PEM label this reader accepts (e.g. "RSA PRIVATE KEY" is 15). */
#define PEM_MAX_LABEL 40

/*
 * Decode the first PEM block whose type is `label` (e.g. "CERTIFICATE",
 * "PRIVATE KEY") found in `pem` (`pem_len` bytes) into `out` (capacity
 * `out_cap`). On success returns 0 and writes the DER length to *out_len;
 * returns -1 on any error: label missing/too long, no matching END marker,
 * a Base64 error in the body, or `out` too small. `*out_len` is only written on
 * success.
 */
int pem_decode(const char *label, const char *pem, size_t pem_len,
               unsigned char *out, size_t out_cap, size_t *out_len);

#endif /* WEBLIB_TLS */
#endif /* WEBLIB_TLS_PEM_H */
