/*
 * test_tls.c — smoke test for the experimental TLS layer's build wiring.
 *
 * Built and run only when configured with -DWEBLIB_ENABLE_TLS=ON. It asserts that
 * the build wiring (option -> -DWEBLIB_TLS -> compiled native source -> linkable
 * symbol) works and that the layer honestly labels itself as experimental. There
 * is no cryptography to test yet.
 */
#include "tls.h"
#include <stdio.h>
#include <string.h>

int main(void) {
    int failures = 0;

#ifndef WEBLIB_TLS
    /* This executable is only added to the build when WEBLIB_TLS is defined; if we
     * somehow got here without it, the wiring is broken. */
    printf("FAIL: test_tls built without WEBLIB_TLS defined\n");
    return 1;
#else
    const char *info = tls_build_info();

    printf("TLS build info: %s\n", info ? info : "(null)");

    if (info == NULL) {
        printf("FAIL: tls_build_info() returned NULL\n");
        failures++;
    } else {
        if (info[0] == '\0') {
            printf("FAIL: tls_build_info() returned an empty string\n");
            failures++;
        }
        /* The banner must keep advertising its unaudited/experimental status so the
         * scaffold can never be mistaken for a production TLS stack. */
        if (strstr(info, "EXPERIMENTAL") == NULL) {
            printf("FAIL: build info does not carry the EXPERIMENTAL label\n");
            failures++;
        }
        if (strstr(info, "UNAUDITED") == NULL) {
            printf("FAIL: build info does not carry the UNAUDITED label\n");
            failures++;
        }
    }

    if (failures == 0) {
        printf("PASS: TLS build wiring verified (build-info symbol links and is labelled)\n");
    }
    return failures == 0 ? 0 : 1;
#endif
}
