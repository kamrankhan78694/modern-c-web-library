#include "kamran.k"
#include <stdio.h>
#include <string.h>

/*
 * Assertions that survive NDEBUG.
 *
 * This suite previously used the standard assert(), which the C library compiles
 * to nothing when NDEBUG is defined — and CMake defines NDEBUG for the Release /
 * RelWithDebInfo configurations, the latter being exactly what CI builds. Every
 * assertion here was therefore removed by the preprocessor in CI, so the suite
 * passed unconditionally: it would have reported success even if every invariant
 * below were false.
 *
 * CHECK()   — always evaluated; records a failure and keeps going, so one broken
 *             invariant does not hide the others.
 * REQUIRE() — for preconditions that later statements depend on (chiefly non-NULL
 *             pointers). Unlike CHECK() it returns immediately, because assert()
 *             used to abort here: continuing past a failed pointer check would
 *             dereference NULL, which is undefined behaviour rather than a
 *             reported test failure.
 */
static int g_failures = 0;
#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            printf("FAIL: %s (%s:%d)\n", #cond, __FILE__, __LINE__);      \
            g_failures++;                                                 \
        }                                                                 \
    } while (0)
#define REQUIRE(cond)                                                     \
    do {                                                                  \
        if (!(cond)) {                                                    \
            printf("FAIL (fatal): %s (%s:%d)\n", #cond, __FILE__, __LINE__); \
            return 1;                                                     \
        }                                                                 \
    } while (0)

int main(void) {
    const char *signature = weblib_kamran_signature();

    REQUIRE(signature != NULL);
    CHECK(strstr(signature, WEBLIB_AUTHOR_KAMRAN) != NULL);

    /* ---- Semantic Versioning tests ---- */

    /* Macro sanity: components are non-negative integers */
    CHECK(WEBLIB_VERSION_MAJOR >= 0);
    CHECK(WEBLIB_VERSION_MINOR >= 0);
    CHECK(WEBLIB_VERSION_PATCH >= 0);

    /* WEBLIB_VERSION string matches the individual components */
    {
        char expected[64];
        snprintf(expected, sizeof(expected), "%d.%d.%d",
                 WEBLIB_VERSION_MAJOR, WEBLIB_VERSION_MINOR,
                 WEBLIB_VERSION_PATCH);
        CHECK(strcmp(WEBLIB_VERSION, expected) == 0);
    }

    /* WEBLIB_VERSION_NUMBER encodes correctly */
    CHECK(WEBLIB_VERSION_NUMBER ==
           WEBLIB_VERSION_ENCODE(WEBLIB_VERSION_MAJOR,
                                 WEBLIB_VERSION_MINOR,
                                 WEBLIB_VERSION_PATCH));

    /* Version comparison helper works as expected */
    CHECK(WEBLIB_VERSION_ENCODE(1, 0, 0) < WEBLIB_VERSION_ENCODE(1, 0, 1));
    CHECK(WEBLIB_VERSION_ENCODE(1, 0, 0) < WEBLIB_VERSION_ENCODE(1, 1, 0));
    CHECK(WEBLIB_VERSION_ENCODE(1, 0, 0) < WEBLIB_VERSION_ENCODE(2, 0, 0));
    CHECK(WEBLIB_VERSION_ENCODE(0, 9, 9) < WEBLIB_VERSION_ENCODE(1, 0, 0));

    /* Runtime version API */
    REQUIRE(weblib_version() != NULL);
    CHECK(strcmp(weblib_version(), WEBLIB_VERSION) == 0);

    /* weblib_version_components returns correct values */
    {
        int maj = -1, min = -1, pat = -1;
        weblib_version_components(&maj, &min, &pat);
        CHECK(maj == WEBLIB_VERSION_MAJOR);
        CHECK(min == WEBLIB_VERSION_MINOR);
        CHECK(pat == WEBLIB_VERSION_PATCH);
    }

    /* NULL pointers are tolerated */
    weblib_version_components(NULL, NULL, NULL);

    /* Signature string contains the full semver version */
    CHECK(strstr(signature, WEBLIB_VERSION) != NULL);

    if (g_failures == 0) {
        printf("kamran.k alias OK — version %s (semver tests passed)\n",
               WEBLIB_VERSION);
    }
    return g_failures == 0 ? 0 : 1;
}
