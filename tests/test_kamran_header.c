#include "kamran.k"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    const char *signature = weblib_kamran_signature();

    assert(signature != NULL);
    assert(strstr(signature, WEBLIB_AUTHOR_KAMRAN) != NULL);

    /* ---- Semantic Versioning tests ---- */

    /* Macro sanity: components are non-negative integers */
    assert(WEBLIB_VERSION_MAJOR >= 0);
    assert(WEBLIB_VERSION_MINOR >= 0);
    assert(WEBLIB_VERSION_PATCH >= 0);

    /* WEBLIB_VERSION string matches the individual components */
    {
        char expected[64];
        snprintf(expected, sizeof(expected), "%d.%d.%d",
                 WEBLIB_VERSION_MAJOR, WEBLIB_VERSION_MINOR,
                 WEBLIB_VERSION_PATCH);
        assert(strcmp(WEBLIB_VERSION, expected) == 0);
    }

    /* WEBLIB_VERSION_NUMBER encodes correctly */
    assert(WEBLIB_VERSION_NUMBER ==
           WEBLIB_VERSION_ENCODE(WEBLIB_VERSION_MAJOR,
                                 WEBLIB_VERSION_MINOR,
                                 WEBLIB_VERSION_PATCH));

    /* Version comparison helper works as expected */
    assert(WEBLIB_VERSION_ENCODE(1, 0, 0) < WEBLIB_VERSION_ENCODE(1, 0, 1));
    assert(WEBLIB_VERSION_ENCODE(1, 0, 0) < WEBLIB_VERSION_ENCODE(1, 1, 0));
    assert(WEBLIB_VERSION_ENCODE(1, 0, 0) < WEBLIB_VERSION_ENCODE(2, 0, 0));
    assert(WEBLIB_VERSION_ENCODE(0, 9, 9) < WEBLIB_VERSION_ENCODE(1, 0, 0));

    /* Runtime version API */
    assert(weblib_version() != NULL);
    assert(strcmp(weblib_version(), WEBLIB_VERSION) == 0);

    /* weblib_version_components returns correct values */
    {
        int maj = -1, min = -1, pat = -1;
        weblib_version_components(&maj, &min, &pat);
        assert(maj == WEBLIB_VERSION_MAJOR);
        assert(min == WEBLIB_VERSION_MINOR);
        assert(pat == WEBLIB_VERSION_PATCH);
    }

    /* NULL pointers are tolerated */
    weblib_version_components(NULL, NULL, NULL);

    /* Signature string contains the full semver version */
    assert(strstr(signature, WEBLIB_VERSION) != NULL);

    printf("kamran.k alias OK — version %s (semver tests passed)\n",
           WEBLIB_VERSION);
    return 0;
}
