/*
 * security_utils.c – Public security utility functions for weblib
 *
 * Pure C implementation – no external dependencies.
 * Provides constant-time comparison, secure memory wipe, and
 * cryptographically secure random byte generation.
 *
 * Author: kamran
 */

#include "kamran.k"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>

#if defined(_WIN32)
#include <windows.h>
#include <bcrypt.h>
#else
#include <errno.h>
#include <unistd.h>   /* ssize_t */
#endif

/* Choose the best available system CSPRNG per platform.  arc4random_buf() on
 * Apple/BSD and getrandom(2) on Linux both draw from the kernel CSPRNG without
 * a file descriptor, so they keep working under chroot / fd exhaustion /
 * minimal sandboxes where opening /dev/urandom would fail.  (macOS ships
 * <sys/random.h> too, but it declares getentropy, not getrandom — so getrandom
 * is gated on __linux__.) */
#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || \
    defined(__NetBSD__) || defined(__DragonFly__)
/* arc4random_buf() is declared in <stdlib.h>. */
#elif !defined(_WIN32)
#  if defined(__linux__) && defined(__has_include)
#    if __has_include(<sys/random.h>)
#      include <sys/random.h>
#      define WEBLIB_HAVE_GETRANDOM 1
#    endif
#  endif
#endif

/* ---- secure memory wipe ---------------------------------------------- */

void secure_zero(void *ptr, size_t len) {
    if (!ptr || len == 0) return;
    /* A volatile function pointer to memset forces the wipe to execute: the
     * compiler cannot prove the callee is memset, so it cannot elide the stores
     * as dead.  Portable across glibc/musl/macOS/BSD/Windows with no Annex-K
     * (__STDC_WANT_LIB_EXT1__ / memset_s) or platform-header dependency. */
    static void *(*const volatile memset_fn)(void *, int, size_t) = &memset;
    memset_fn(ptr, 0, len);
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

/*
 * Fill buf with len cryptographically secure random bytes.
 * Returns 0 on success, -1 on failure.  On failure the buffer contents are
 * unspecified and MUST NOT be used -- callers must fail closed and never fall
 * back to a predictable PRNG (rand/rand_r) for keys, tokens, or session IDs.
 */
int secure_random_bytes(void *buf, size_t len) {
    if (!buf || len == 0) return -1;

#if defined(_WIN32)
    NTSTATUS status = BCryptGenRandom(NULL, (PUCHAR)buf, (ULONG)len,
                                      BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    return NT_SUCCESS(status) ? 0 : -1;

#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || \
      defined(__NetBSD__) || defined(__DragonFly__)
    /* System CSPRNG; cannot fail and needs no file descriptor. */
    arc4random_buf(buf, len);
    return 0;

#else
    unsigned char *p = (unsigned char *)buf;
    size_t off = 0;

#if defined(WEBLIB_HAVE_GETRANDOM)
    while (off < len) {
        ssize_t r = getrandom(p + off, len - off, 0);
        if (r < 0) {
            if (errno == EINTR) continue;
            break;  /* getrandom unavailable at runtime -> try /dev/urandom */
        }
        off += (size_t)r;
    }
    if (off == len) return 0;
#endif

    /* Fallback: /dev/urandom, filling anything getrandom did not. */
    {
        FILE *fp = fopen("/dev/urandom", "rb");
        if (!fp) return -1;
        size_t got = fread(p + off, 1, len - off, fp);
        fclose(fp);
        return (off + got == len) ? 0 : -1;
    }
#endif
}
