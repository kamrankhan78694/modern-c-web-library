/*
 * base64.h — Base64 decode (RFC 4648).
 *
 * Shared internal utility. base64_decode was previously file-static inside
 * middleware_auth.c (HTTP Basic authentication); it is promoted here so the
 * pure-C TLS layer's PEM reader can reuse the same single implementation rather
 * than duplicating it. Target-neutral (pure logic, no OS I/O) and compiled into
 * every build, WASM included.
 *
 * Not part of the public API (`include/kamran.k`); it lives under src/ and is
 * for internal use by the library's own subsystems.
 */
#ifndef WEBLIB_CRYPTO_BASE64_H
#define WEBLIB_CRYPTO_BASE64_H

#include <stddef.h>

/*
 * Decode standard Base64 (RFC 4648, the '+' '/' alphabet) from `input`
 * (`input_len` bytes) into `output` (capacity `output_len` bytes). ASCII
 * whitespace is skipped, so PEM line-wrapped bodies decode directly; decoding
 * stops at the first '=' padding byte. Returns the number of decoded bytes
 * (always >= 1) on success, or -1 on error.
 *
 * Errors (all return -1, fail-closed): a NULL argument or input_len == 0; an
 * invalid alphabet byte; a truncated group (a lone trailing Base64 symbol); more
 * than two '=' padding bytes, or any non-whitespace data after the padding; an
 * output buffer too small; or an input that decodes to zero bytes (all
 * whitespace / padding only). Callers therefore never need to separately check
 * for an empty, truncated, or trailing-garbage result.
 */
int base64_decode(const char *input, size_t input_len,
                  unsigned char *output, size_t output_len);

#endif /* WEBLIB_CRYPTO_BASE64_H */
