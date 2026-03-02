/*
 * security_utils.c – Public security utility functions for weblib
 *
 * Pure C implementation – no external dependencies.
 * Provides constant-time comparison, secure memory wipe, and
 * cryptographically secure random byte generation.
 *
 * Author: kamran
 */

#include "weblib.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#if defined(_WIN32)
#include <bcrypt.h>
#endif

/* ---- secure memory wipe ---------------------------------------------- */

void secure_zero(void *ptr, size_t len) {
    if (!ptr || len == 0) return;
#if defined(__STDC_LIB_EXT1__) || defined(_WIN32)
    memset_s(ptr, len, 0, len);
#else
    volatile unsigned char *p = (volatile unsigned char *)ptr;
    while (len--) *p++ = 0;
#endif
}

/* ---- constant-time comparison ---------------------------------------- */

bool secure_compare(const void *a, const void *b, size_t len) {
    if (!a || !b) return false;
    if (len == 0) return true;

    const volatile unsigned char *pa = (const volatile unsigned char *)a;
    const volatile unsigned char *pb = (const volatile unsigned char *)b;
    volatile unsigned char diff = 0;

    for (size_t i = 0; i < len; i++) {
        diff |= pa[i] ^ pb[i];
    }
    return diff == 0;
}

/* ---- cryptographically secure random bytes --------------------------- */

int secure_random_bytes(void *buf, size_t len) {
    if (!buf || len == 0) return -1;

#if defined(_WIN32)
    /* Windows: BCryptGenRandom (Vista+) */
    NTSTATUS status = BCryptGenRandom(NULL, (PUCHAR)buf, (ULONG)len,
                                      BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    return NT_SUCCESS(status) ? 0 : -1;
#else
    /* POSIX: /dev/urandom (Linux, macOS, BSD) */
    FILE *fp = fopen("/dev/urandom", "rb");
    if (!fp) return -1;
    size_t got = fread(buf, 1, len, fp);
    fclose(fp);
    return (got == len) ? 0 : -1;
#endif
}
