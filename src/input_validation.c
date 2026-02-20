/**
 * @file input_validation.c
 * @brief Input validation helpers — Phase 8
 *
 * Pure C, zero external dependencies.
 * All functions accept NULL safely (treating NULL as invalid input).
 */

#include "weblib.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>

/* ── input_validate_length ─────────────────────────────────────────────── */

bool input_validate_length(const char *str, size_t min_len, size_t max_len) {
    if (str == NULL) return false;
    size_t len = strlen(str);
    return (len >= min_len && len <= max_len);
}

/* ── input_validate_charset ────────────────────────────────────────────── */

bool input_validate_charset(const char *str, const char *allowed_chars) {
    if (str == NULL || allowed_chars == NULL) return false;
    for (const char *p = str; *p != '\0'; p++) {
        if (strchr(allowed_chars, *p) == NULL) {
            return false;
        }
    }
    return true;
}

/* ── input_validate_integer ────────────────────────────────────────────── */

bool input_validate_integer(const char *str, long long min_val, long long max_val,
                             long long *out_val) {
    if (str == NULL || str[0] == '\0') return false;
    /* Reject leading whitespace */
    if (isspace((unsigned char)str[0])) return false;

    char *endptr = NULL;
    errno = 0;
    long long val = strtoll(str, &endptr, 10);

    /* Must consume the entire string */
    if (endptr == str || *endptr != '\0') return false;
    /* Overflow */
    if (errno == ERANGE) return false;
    /* Range check */
    if (val < min_val || val > max_val) return false;

    if (out_val) *out_val = val;
    return true;
}

/* ── input_validate_email ──────────────────────────────────────────────── */

bool input_validate_email(const char *str) {
    if (str == NULL || str[0] == '\0') return false;

    /* Find '@' */
    const char *at = strchr(str, '@');
    if (at == NULL) return false;       /* no '@' */
    if (at == str) return false;        /* empty local-part */
    if (at[1] == '\0') return false;    /* empty domain */

    /* Domain must contain at least one '.' after '@' */
    const char *dot = strchr(at + 1, '.');
    if (dot == NULL) return false;
    if (dot == at + 1) return false;    /* dot immediately after '@' */
    if (dot[1] == '\0') return false;   /* nothing after dot */

    /* Reject multiple '@' */
    if (strchr(at + 1, '@') != NULL) return false;

    return true;
}

/* ── input_is_alphanumeric ─────────────────────────────────────────────── */

bool input_is_alphanumeric(const char *str) {
    if (str == NULL || str[0] == '\0') return false;
    for (const char *p = str; *p != '\0'; p++) {
        if (!isalnum((unsigned char)*p)) return false;
    }
    return true;
}

/* ── input_sanitize_html ──────────────────────────────────────────────── */

/**
 * @brief HTML entity table
 */
static const struct {
    char c;
    const char *entity;
    size_t entity_len;
} HTML_ENTITIES[] = {
    { '&', "&amp;",  5 },
    { '"', "&quot;", 6 },
    { '\'', "&#39;", 5 },
    { '<', "&lt;",   4 },
    { '>', "&gt;",   4 },
};
#define HTML_ENTITY_COUNT (sizeof(HTML_ENTITIES) / sizeof(HTML_ENTITIES[0]))

char *input_sanitize_html(const char *str) {
    if (str == NULL) return NULL;

    /* First pass: calculate required output length */
    size_t out_len = 0;
    for (const char *p = str; *p != '\0'; p++) {
        bool found = false;
        for (size_t i = 0; i < HTML_ENTITY_COUNT; i++) {
            if (*p == HTML_ENTITIES[i].c) {
                out_len += HTML_ENTITIES[i].entity_len;
                found = true;
                break;
            }
        }
        if (!found) out_len++;
    }

    char *out = (char *)malloc(out_len + 1);
    if (!out) return NULL;

    /* Second pass: fill output */
    char *dst = out;
    for (const char *p = str; *p != '\0'; p++) {
        bool found = false;
        for (size_t i = 0; i < HTML_ENTITY_COUNT; i++) {
            if (*p == HTML_ENTITIES[i].c) {
                memcpy(dst, HTML_ENTITIES[i].entity, HTML_ENTITIES[i].entity_len);
                dst += HTML_ENTITIES[i].entity_len;
                found = true;
                break;
            }
        }
        if (!found) {
            *dst++ = *p;
        }
    }
    *dst = '\0';
    return out;
}
