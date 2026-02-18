/*
 * Cookie Handling Implementation
 * Part of Modern C Web Library - Phase 5.2
 *
 * Provides HTTP cookie parsing and setting functionality:
 * - Parse cookies from Cookie request header
 * - Set cookies with Set-Cookie response header
 * - Cookie deletion helper
 *
 * Pure C implementation with zero external dependencies.
 */

#include "weblib.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

/* Maximum size for a Set-Cookie header value */
#define MAX_COOKIE_HEADER_SIZE 4096

/* Maximum size for a single cookie value */
#define MAX_COOKIE_VALUE_SIZE 1024

/* 
 * Thread-local storage for cookie values (if available)
 * Falls back to static storage for non-C11 environments
 */
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L && !defined(__STDC_NO_THREADS__)
#include <threads.h>
static thread_local char _cookie_value_buffer[MAX_COOKIE_VALUE_SIZE];
#elif defined(__GNUC__) || defined(__clang__)
static __thread char _cookie_value_buffer[MAX_COOKIE_VALUE_SIZE];
#else
static char _cookie_value_buffer[MAX_COOKIE_VALUE_SIZE];
#endif

/**
 * Helper: Skip leading whitespace in a string
 */
static const char *_skip_whitespace(const char *str) {
    if (!str) {
        return NULL;
    }
    while (*str && isspace((unsigned char)*str)) {
        str++;
    }
    return str;
}

/**
 * Helper: Trim trailing whitespace from a string (modifies buffer)
 */
static void _trim_trailing_whitespace(char *str) {
    if (!str) {
        return;
    }
    size_t len = strlen(str);
    while (len > 0 && isspace((unsigned char)str[len - 1])) {
        str[--len] = '\0';
    }
}

/**
 * Get a cookie value from the request by name.
 *
 * Parses the Cookie header and returns the value of the named cookie.
 * The Cookie header format is: "name1=value1; name2=value2; ..."
 *
 * @param req The HTTP request
 * @param name The cookie name to look for
 * @return Pointer to cookie value (valid until next call), or NULL if not found
 *
 * Note: The returned pointer is valid only until the next call to this function
 *       in the same thread. Copy the value if you need to keep it longer.
 */
const char *http_request_get_cookie(http_request_t *req, const char *name) {
    if (!req || !name || !*name) {
        return NULL;
    }

    /* Get the Cookie header from the request */
    const char *cookie_header = http_request_get_header(req, "Cookie");
    if (!cookie_header || !*cookie_header) {
        return NULL;
    }

    size_t name_len = strlen(name);
    const char *ptr = cookie_header;

    /* Parse: Cookie: name1=value1; name2=value2; ... */
    while (*ptr) {
        /* Skip any leading whitespace */
        ptr = _skip_whitespace(ptr);
        if (!*ptr) {
            break;
        }

        /* Find the '=' separator */
        const char *eq = strchr(ptr, '=');
        if (!eq) {
            break; /* Malformed cookie header */
        }

        /* Calculate the length of current cookie name (trimming trailing spaces) */
        const char *name_end = eq;
        while (name_end > ptr && isspace((unsigned char)*(name_end - 1))) {
            name_end--;
        }
        size_t current_name_len = name_end - ptr;

        /* Check if this is the cookie we're looking for */
        if (current_name_len == name_len && strncmp(ptr, name, name_len) == 0) {
            /* Found it - extract the value */
            const char *value_start = eq + 1;
            
            /* Skip leading whitespace in value */
            while (*value_start && isspace((unsigned char)*value_start)) {
                value_start++;
            }

            /* Find the end of the value (semicolon or end of string) */
            const char *value_end = strchr(value_start, ';');
            if (!value_end) {
                value_end = value_start + strlen(value_start);
            }

            /* Copy value to buffer */
            size_t value_len = value_end - value_start;
            if (value_len >= sizeof(_cookie_value_buffer)) {
                value_len = sizeof(_cookie_value_buffer) - 1;
            }

            if (value_len > 0) {
                strncpy(_cookie_value_buffer, value_start, value_len);
                _cookie_value_buffer[value_len] = '\0';
                
                /* Trim trailing whitespace */
                _trim_trailing_whitespace(_cookie_value_buffer);
                
                return _cookie_value_buffer;
            } else {
                /* Empty cookie value */
                _cookie_value_buffer[0] = '\0';
                return _cookie_value_buffer;
            }
        }

        /* Move to the next cookie */
        const char *semicolon = strchr(ptr, ';');
        if (semicolon) {
            ptr = semicolon + 1;
        } else {
            break; /* No more cookies */
        }
    }

    /* Cookie not found */
    return NULL;
}

/**
 * Set a cookie on the response with optional attributes.
 *
 * Builds a Set-Cookie header with the provided name, value, and options.
 * Format: "Set-Cookie: name=value; Path=/; Domain=...; Max-Age=...; Secure; HttpOnly; SameSite=..."
 *
 * @param res The HTTP response
 * @param name The cookie name (required)
 * @param value The cookie value (required)
 * @param options Cookie options (path, domain, max-age, flags), or NULL for defaults
 *
 * Default options: Path=/, session cookie (no Max-Age), no Secure/HttpOnly, no SameSite
 *
 * NOTE: Current implementation replaces any existing Set-Cookie header.
 *       To set multiple cookies, call this function multiple times or use
 *       http_response_set_header directly with unique header keys.
 *       A future enhancement will support multiple Set-Cookie headers properly.
 */
static bool _is_valid_cookie_name(const char *name) {
    for (const char *p = name; *p; p++) {
        unsigned char c = (unsigned char)*p;
        if (c <= 32 || c == 127 || c == '(' || c == ')' || c == '<' || c == '>' ||
            c == '@' || c == ',' || c == ';' || c == '\\' || c == '"' ||
            c == '/' || c == '[' || c == ']' || c == '?' || c == '=' ||
            c == '{' || c == '}' || c == '\t') {
            return false;
        }
    }
    return true;
}

static bool _is_valid_cookie_value(const char *value) {
    for (const char *p = value; *p; p++) {
        unsigned char c = (unsigned char)*p;
        if (c == ';' || c == '\\' || c == '"' || c == '\r' || c == '\n') {
            return false;
        }
    }
    return true;
}

void http_response_set_cookie(http_response_t *res, const char *name,
                              const char *value, const cookie_options_t *options) {
    if (!res || !name || !*name || !value) {
        return;
    }

    if (!_is_valid_cookie_name(name) || !_is_valid_cookie_value(value)) {
        return;
    }

    char cookie_header[MAX_COOKIE_HEADER_SIZE];
    int offset = 0;

    /* Build the Set-Cookie header: name=value */
    offset = snprintf(cookie_header, sizeof(cookie_header), "%s=%s", name, value);
    if (offset < 0 || offset >= (int)sizeof(cookie_header)) {
        return; /* Buffer overflow */
    }

    /* Add Path attribute (default: "/") */
    if (options && options->path && options->path[0]) {
        offset += snprintf(cookie_header + offset, sizeof(cookie_header) - offset,
                          "; Path=%s", options->path);
    } else {
        offset += snprintf(cookie_header + offset, sizeof(cookie_header) - offset,
                          "; Path=/");
    }
    if (offset >= (int)sizeof(cookie_header)) {
        return; /* Buffer overflow */
    }

    /* Add Domain attribute if specified */
    if (options && options->domain && options->domain[0]) {
        offset += snprintf(cookie_header + offset, sizeof(cookie_header) - offset,
                          "; Domain=%s", options->domain);
        if (offset >= (int)sizeof(cookie_header)) {
            return;
        }
    }

    /* Add Max-Age attribute if specified (>= 0) */
    if (options && options->max_age >= 0) {
        offset += snprintf(cookie_header + offset, sizeof(cookie_header) - offset,
                          "; Max-Age=%d", options->max_age);
        if (offset >= (int)sizeof(cookie_header)) {
            return;
        }
    }

    /* Add Secure flag if enabled */
    if (options && options->secure) {
        offset += snprintf(cookie_header + offset, sizeof(cookie_header) - offset,
                          "; Secure");
        if (offset >= (int)sizeof(cookie_header)) {
            return;
        }
    }

    /* Add HttpOnly flag if enabled */
    if (options && options->http_only) {
        offset += snprintf(cookie_header + offset, sizeof(cookie_header) - offset,
                          "; HttpOnly");
        if (offset >= (int)sizeof(cookie_header)) {
            return;
        }
    }

    /* Add SameSite attribute if specified */
    if (options && options->same_site && options->same_site[0]) {
        /* Validate SameSite value (should be "Strict", "Lax", or "None") */
        const char *same_site = options->same_site;
        if (strcmp(same_site, "Strict") == 0 ||
            strcmp(same_site, "Lax") == 0 ||
            strcmp(same_site, "None") == 0) {
            offset += snprintf(cookie_header + offset, sizeof(cookie_header) - offset,
                              "; SameSite=%s", same_site);
            if (offset >= (int)sizeof(cookie_header)) {
                return;
            }
        }
    }

    /* Set the Set-Cookie header on the response */
    http_response_set_header(res, "Set-Cookie", cookie_header);
}

/**
 * Delete a cookie by setting it to expire immediately.
 *
 * This is a convenience function that sets the cookie with an empty value
 * and Max-Age=0, causing the browser to delete it.
 *
 * @param res The HTTP response
 * @param name The cookie name to delete
 *
 * Note: The cookie's path and domain must match the original cookie for
 *       the browser to delete it properly. If needed, use http_response_set_cookie
 *       directly with appropriate options.
 */
void http_response_delete_cookie(http_response_t *res, const char *name) {
    if (!res || !name || !*name) {
        return;
    }

    /* Set cookie with Max-Age=0 and empty value to delete it */
    cookie_options_t options = {
        .domain = NULL,
        .path = "/",
        .max_age = 0,       /* Expire immediately */
        .secure = false,
        .http_only = false,
        .same_site = NULL
    };

    http_response_set_cookie(res, name, "", &options);
}
