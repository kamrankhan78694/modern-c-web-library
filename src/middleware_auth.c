/**
 * middleware_auth.c - Authentication Middleware for Modern C Web Library
 * 
 * Implements authentication middleware supporting:
 * - Basic Authentication (RFC 7617)
 * - API Key Authentication
 * - JWT Authentication with HMAC-SHA256 (RFC 7519)
 * 
 * All cryptographic primitives are implemented from scratch:
 * - Base64/Base64URL decode (RFC 4648)
 * - SHA-256 (FIPS 180-4)
 * - HMAC-SHA256 (RFC 2104)
 * 
 * Copyright (c) 2024 Modern C Web Library
 * Licensed under MIT License
 */

#include "kamran.k"
#include "crypto/sha256.h"   /* SHA-256 + HMAC-SHA256 (promoted to shared module) */
#include "crypto/base64.h"   /* standard Base64 decode (promoted to shared module) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>

/* ===== Base64URL Decoding (RFC 4648) — standard Base64 lives in crypto/base64 ===== */

static const unsigned char base64url_decode_table[256] = {
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 62, 64, 64,
    52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 64, 64, 64, 64, 64, 64,
    64,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14,
    15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 64, 64, 64, 64, 63,
    64, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
    41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64
};

/**
 * base64url_decode - Decode Base64URL string (RFC 4648)
 * @input: Base64URL encoded string (no padding)
 * @input_len: Length of input string
 * @output: Buffer to store decoded data
 * @output_len: Size of output buffer
 * 
 * Returns: Number of decoded bytes, or -1 on error
 */
static int base64url_decode(const char *input, size_t input_len, unsigned char *output, size_t output_len)
{
    if (!input || !output || input_len == 0) {
        return -1;
    }

    size_t i = 0, j = 0;
    unsigned char buf[4];
    int buf_count = 0;

    for (i = 0; i < input_len; i++) {
        unsigned char c = (unsigned char)input[i];
        unsigned char val = base64url_decode_table[c];
        
        if (val == 64) {
            return -1; /* Invalid character */
        }
        
        buf[buf_count++] = val;
        
        if (buf_count == 4) {
            if (j + 3 > output_len) {
                return -1;
            }
            
            output[j++] = (buf[0] << 2) | (buf[1] >> 4);
            output[j++] = (buf[1] << 4) | (buf[2] >> 2);
            output[j++] = (buf[2] << 6) | buf[3];
            
            buf_count = 0;
        }
    }
    
    /* Handle remaining bytes (no padding in Base64URL) */
    if (buf_count >= 2) {
        if (j + 1 > output_len) {
            return -1;
        }
        output[j++] = (buf[0] << 2) | (buf[1] >> 4);
        
        if (buf_count >= 3) {
            if (j + 1 > output_len) {
                return -1;
            }
            output[j++] = (buf[1] << 4) | (buf[2] >> 2);
        }
    }
    
    return (int)j;
}

/* ===== Basic Authentication Middleware ===== */

static basic_auth_config_t *g_basic_auth_config = NULL;

/**
 * parse_basic_auth - Parse Basic Authentication header
 * @auth_header: Authorization header value
 * @username: Output buffer for username
 * @username_size: Size of username buffer
 * @password: Output buffer for password
 * @password_size: Size of password buffer
 * 
 * Returns: true on success, false on error
 */
static bool parse_basic_auth(const char *auth_header,
                            char *username, size_t username_size,
                            char *password, size_t password_size)
{
    const char *encoded;
    unsigned char decoded[512];
    int decoded_len;
    const char *colon;
    size_t username_len, password_len;

    if (!auth_header || strncmp(auth_header, "Basic ", 6) != 0) {
        return false;
    }

    encoded = auth_header + 6;
    
    /* Skip whitespace */
    while (*encoded == ' ' || *encoded == '\t') {
        encoded++;
    }

    /* Decode Base64 */
    decoded_len = base64_decode(encoded, strlen(encoded), decoded, sizeof(decoded) - 1);
    if (decoded_len < 0) {
        return false;
    }
    
    decoded[decoded_len] = '\0';

    /* Find colon separator */
    colon = strchr((char *)decoded, ':');
    if (!colon) {
        return false;
    }

    username_len = colon - (char *)decoded;
    password_len = decoded_len - username_len - 1;

    if (username_len >= username_size || password_len >= password_size) {
        return false;
    }

    memcpy(username, decoded, username_len);
    username[username_len] = '\0';
    
    memcpy(password, colon + 1, password_len);
    password[password_len] = '\0';

    /* Clear decoded credentials from stack to prevent memory disclosure */
    secure_zero(decoded, sizeof(decoded));

    return true;
}

/**
 * basic_auth_handler - Basic authentication middleware handler
 */
static bool basic_auth_handler(http_request_t *req, http_response_t *res, void *user_data)
{
    basic_auth_config_t *config = user_data ? (basic_auth_config_t *)user_data : g_basic_auth_config;
    const char *auth_header;
    char username[256];
    char password[256];
    char www_auth[512];

    if (!config) {
        return false;
    }

    auth_header = http_request_get_header(req, "Authorization");
    if (!auth_header) {
        goto unauthorized;
    }

    if (!parse_basic_auth(auth_header, username, sizeof(username), password, sizeof(password))) {
        goto unauthorized;
    }

    /* Verify credentials via callback */
    if (!config->verify ||
        !config->verify(username, password, config->user_data)) {
        goto unauthorized;
    }

    /* Authentication successful */
    secure_zero(username, sizeof(username));
    secure_zero(password, sizeof(password));
    return true;

unauthorized:
    /* Wipe credentials from stack */
    secure_zero(username, sizeof(username));
    secure_zero(password, sizeof(password));

    /* Set WWW-Authenticate header — sanitize realm to prevent header injection.
     * Reject quotes, backslashes, and control characters (CR/LF). */
    if (config->realm && strchr(config->realm, '"') == NULL &&
        strchr(config->realm, '\\') == NULL &&
        strchr(config->realm, '\r') == NULL &&
        strchr(config->realm, '\n') == NULL) {
        snprintf(www_auth, sizeof(www_auth), "Basic realm=\"%s\"", config->realm);
    } else {
        snprintf(www_auth, sizeof(www_auth), "Basic realm=\"Restricted\"");
    }
    
    http_response_set_header(res, "WWW-Authenticate", www_auth);
    http_response_send_text(res, HTTP_UNAUTHORIZED, "401 Unauthorized");
    
    return false;
}

/**
 * basic_auth_middleware_create - Create Basic Auth middleware
 */
middleware_fn_t basic_auth_middleware_create(const basic_auth_config_t *config)
{
    if (!config || !config->verify) {
        return NULL;
    }

    /* Destroy existing config if any */
    basic_auth_middleware_destroy();

    /* Allocate and store configuration */
    g_basic_auth_config = malloc(sizeof(basic_auth_config_t));
    if (!g_basic_auth_config) {
        return NULL;
    }

    memcpy(g_basic_auth_config, config, sizeof(basic_auth_config_t));
    
    /* Duplicate realm string if provided */
    if (config->realm) {
        g_basic_auth_config->realm = strdup(config->realm);
        if (!g_basic_auth_config->realm) {
            free(g_basic_auth_config);
            g_basic_auth_config = NULL;
            return NULL;
        }
    }

    return basic_auth_handler;
}

/**
 * basic_auth_middleware_destroy - Cleanup Basic Auth middleware
 */
void basic_auth_middleware_destroy(void)
{
    if (g_basic_auth_config) {
        if (g_basic_auth_config->realm) {
            free((void *)g_basic_auth_config->realm);
        }
        free(g_basic_auth_config);
        g_basic_auth_config = NULL;
    }
}

/* ===== API Key Authentication Middleware ===== */

static apikey_auth_config_t *g_apikey_auth_config = NULL;

/**
 * apikey_auth_handler - API Key authentication middleware handler
 */
static bool apikey_auth_handler(http_request_t *req, http_response_t *res, void *user_data)
{
    apikey_auth_config_t *config = user_data ? (apikey_auth_config_t *)user_data : g_apikey_auth_config;
    const char *header_name;
    const char *api_key;

    if (!config || !config->verify) {
        return false;
    }

    header_name = config->header_name ? 
                  config->header_name : "X-API-Key";

    api_key = http_request_get_header(req, header_name);
    if (!api_key) {
        http_response_send_text(res, HTTP_FORBIDDEN, "403 Forbidden - Missing API Key");
        return false;
    }

    /* Verify API key via callback */
    if (!config->verify(api_key, config->user_data)) {
        http_response_send_text(res, HTTP_FORBIDDEN, "403 Forbidden - Invalid API Key");
        return false;
    }

    /* Authentication successful */
    return true;
}

/**
 * apikey_auth_middleware_create - Create API Key Auth middleware
 */
middleware_fn_t apikey_auth_middleware_create(const apikey_auth_config_t *config)
{
    if (!config || !config->verify) {
        return NULL;
    }

    /* Destroy existing config if any */
    apikey_auth_middleware_destroy();

    /* Allocate and store configuration */
    g_apikey_auth_config = malloc(sizeof(apikey_auth_config_t));
    if (!g_apikey_auth_config) {
        return NULL;
    }

    memcpy(g_apikey_auth_config, config, sizeof(apikey_auth_config_t));
    
    /* Duplicate header name if provided */
    if (config->header_name) {
        g_apikey_auth_config->header_name = strdup(config->header_name);
        if (!g_apikey_auth_config->header_name) {
            free(g_apikey_auth_config);
            g_apikey_auth_config = NULL;
            return NULL;
        }
    }

    return apikey_auth_handler;
}

/**
 * apikey_auth_middleware_destroy - Cleanup API Key Auth middleware
 */
void apikey_auth_middleware_destroy(void)
{
    if (g_apikey_auth_config) {
        if (g_apikey_auth_config->header_name) {
            free((void *)g_apikey_auth_config->header_name);
        }
        free(g_apikey_auth_config);
        g_apikey_auth_config = NULL;
    }
}

/* ===== JWT Authentication Middleware ===== */

static jwt_auth_config_t *g_jwt_auth_config = NULL;

/* Advance *pp past optional whitespace, exactly one ':' name/value separator, and
 * more optional whitespace — i.e. to the first byte of a JSON member's value.
 * Requiring precisely one colon keeps this a real structural parse: a member with
 * no separator ({"alg""HS256"}) or a repeated one ({"alg":::"HS256"}) is malformed
 * JSON and is rejected rather than leniently accepted. Returns false if the single
 * required colon is absent. */
static bool json_skip_to_value(const char **pp) {
    const char *v = *pp;
    while (*v == ' ' || *v == '\t' || *v == '\n' || *v == '\r') v++;
    if (*v != ':') {
        return false;                 /* missing name:value separator */
    }
    v++;
    while (*v == ' ' || *v == '\t' || *v == '\n' || *v == '\r') v++;
    *pp = v;
    return true;
}

/* Return true only if the JWT header's "alg" parameter is exactly the string
 * "HS256". A PARSED check (find the "alg" key, read its quoted value) rather than
 * a substring match: HMAC-SHA256 is the only algorithm this verifier supports,
 * and parsing the field — instead of accepting any header that merely contains
 * the text "HS256" — forecloses alg-confusion should other algorithms ever be
 * added. Rejects a missing alg, a non-string alg, or any value other than
 * "HS256". */
static bool jwt_header_alg_is_hs256(const char *header_json) {
    const char *p = header_json;
    const char *key = NULL;
    while ((p = strstr(p, "\"alg\"")) != NULL) {
        if (p == header_json || p[-1] == '{' || p[-1] == ',' ||
            p[-1] == ' ' || p[-1] == '\t' || p[-1] == '\n' || p[-1] == '\r') {
            key = p;
            break;
        }
        p += 5;
    }
    if (!key) {
        return false;                 /* no alg header -> reject */
    }
    const char *v = key + 5;          /* past the "alg" key token */
    if (!json_skip_to_value(&v)) {
        return false;                 /* malformed: no name:value separator */
    }
    if (*v != '"') {
        return false;                 /* alg value must be a JSON string */
    }
    v++;
    return strncmp(v, "HS256", 5) == 0 && v[5] == '"';
}

/* Tri-state result of looking up an integer JWT claim. A security-critical claim
 * that is PRESENT but malformed must not be silently ignored, so ABSENT (key not
 * found) is distinguished from INVALID (key found but not a well-formed integer). */
typedef enum {
    JWT_CLAIM_ABSENT = 0,   /* key not present in the payload */
    JWT_CLAIM_INVALID,      /* key present but its value is not a bare base-10 int */
    JWT_CLAIM_OK            /* key present with a valid integer; *out is set */
} jwt_claim_status_t;

/* Look up a base-10 integer JSON claim (e.g. `key` == "\"exp\"") in the flat JWT
 * payload. Matches only a real key — one preceded by '{', ',' or whitespace, not a
 * substring inside some string value. The value must be a bare integer followed only
 * by optional whitespace and a JSON object terminator (',' or '}'): a quoted string
 * ("exp":"123"), trailing garbage ("exp":123abc), or a missing ':' all yield INVALID
 * so the caller can reject the token rather than treat the claim as absent. */
static jwt_claim_status_t jwt_claim_int(const char *payload_json, const char *key, long long *out) {
    size_t keylen = strlen(key);
    const char *p = payload_json;
    const char *found = NULL;
    while ((p = strstr(p, key)) != NULL) {
        if (p == payload_json || p[-1] == '{' || p[-1] == ',' ||
            p[-1] == ' ' || p[-1] == '\t' || p[-1] == '\n' || p[-1] == '\r') {
            found = p;
            break;
        }
        p += keylen;
    }
    if (!found) {
        return JWT_CLAIM_ABSENT;
    }
    const char *v = found + keylen;
    if (!json_skip_to_value(&v)) {
        return JWT_CLAIM_INVALID;      /* no key:value separator */
    }
    /* A JSON number starts with '-' or a digit. strtol() would also accept a leading
     * '+' and further leading whitespace, neither of which is valid JSON, so gate the
     * first byte explicitly. */
    if (*v != '-' && !(*v >= '0' && *v <= '9')) {
        return JWT_CLAIM_INVALID;      /* not a bare number (quoted string, '+', ...) */
    }
    char *endptr = NULL;
    /* NumericDate (RFC 7519) can exceed 2^31 (year 2038+), and `long` is only 32-bit
     * on wasm32/LLP64 — parse into long long so exp/nbf are platform-independent. */
    long long val = strtoll(v, &endptr, 10);
    if (endptr == v) {
        return JWT_CLAIM_INVALID;      /* e.g. a lone '-' */
    }
    /* The integer must be terminated by optional whitespace then a value delimiter;
     * anything else (e.g. "123abc") is malformed. */
    const char *e = endptr;
    while (*e == ' ' || *e == '\t' || *e == '\n' || *e == '\r') {
        e++;
    }
    if (*e != ',' && *e != '}') {
        return JWT_CLAIM_INVALID;
    }
    *out = val;
    return JWT_CLAIM_OK;
}

/**
 * parse_jwt_token - Parse and verify JWT token
 * @token: JWT token string
 * @secret: HMAC secret key
 * @secret_len: Secret key length
 * @payload_out: Output buffer for decoded payload JSON (optional)
 * @payload_size: Size of payload buffer
 * 
 * Returns: true if token is valid, false otherwise
 */
static bool parse_jwt_token(const char *token,
                           const uint8_t *secret, size_t secret_len,
                           bool require_exp,
                           char *payload_out, size_t payload_size)
{
    const char *dot1, *dot2;
    size_t header_len, payload_len, signature_len;
    unsigned char header_decoded[512];
    unsigned char payload_decoded[2048];
    unsigned char signature_decoded[64];
    unsigned char computed_signature[SHA256_DIGEST_SIZE];
    int decoded_len;
    char signing_input[4096];
    size_t signing_input_len;
    bool result = false;

    /* Find separators */
    dot1 = strchr(token, '.');
    if (!dot1) {
        goto cleanup;
    }
    
    dot2 = strchr(dot1 + 1, '.');
    if (!dot2) {
        goto cleanup;
    }

    header_len = dot1 - token;
    payload_len = dot2 - dot1 - 1;
    signature_len = strlen(dot2 + 1);

    /* Decode header */
    decoded_len = base64url_decode(token, header_len, header_decoded, sizeof(header_decoded) - 1);
    if (decoded_len < 0) {
        goto cleanup;
    }
    header_decoded[decoded_len] = '\0';

    /* Verify the algorithm is exactly HS256 (a parsed field check, not a
     * substring match on the decoded header). */
    if (!jwt_header_alg_is_hs256((char *)header_decoded)) {
        goto cleanup; /* Only HS256 supported */
    }

    /* Decode payload */
    decoded_len = base64url_decode(dot1 + 1, payload_len, payload_decoded, sizeof(payload_decoded) - 1);
    if (decoded_len < 0) {
        goto cleanup;
    }
    payload_decoded[decoded_len] = '\0';

    /* Store payload if requested */
    if (payload_out && payload_size > 1) {
        size_t max_copy = payload_size - 1;
        size_t copy_len = (size_t)decoded_len < max_copy ? (size_t)decoded_len : max_copy;
        memcpy(payload_out, payload_decoded, copy_len);
        payload_out[copy_len] = '\0';
    }

    /* Decode signature */
    decoded_len = base64url_decode(dot2 + 1, signature_len, signature_decoded, sizeof(signature_decoded));
    if (decoded_len != SHA256_DIGEST_SIZE) {
        goto cleanup;
    }

    /* Prepare signing input (header.payload) */
    signing_input_len = dot2 - token;
    if (signing_input_len >= sizeof(signing_input)) {
        goto cleanup;
    }
    memcpy(signing_input, token, signing_input_len);
    signing_input[signing_input_len] = '\0';

    /* Compute HMAC-SHA256 */
    hmac_sha256(secret, secret_len, (uint8_t *)signing_input, signing_input_len, computed_signature);

    /* Constant-time comparison to prevent timing attacks */
    if (!secure_compare(signature_decoded, computed_signature, SHA256_DIGEST_SIZE)) {
        goto cleanup;
    }

    /* Validate the time-based claims. exp/nbf are matched as real JSON keys, not
     * substrings of a string value (jwt_claim_int). */
    {
        time_t now = time(NULL);
        long long exp_val = 0, nbf_val = 0;
        jwt_claim_status_t exp_st = jwt_claim_int((const char *)payload_decoded, "\"exp\"", &exp_val);
        jwt_claim_status_t nbf_st = jwt_claim_int((const char *)payload_decoded, "\"nbf\"", &nbf_val);

        /* A present-but-malformed security claim (non-integer, trailing garbage,
         * quoted string) is rejected outright — never silently treated as absent. */
        if (exp_st == JWT_CLAIM_INVALID || nbf_st == JWT_CLAIM_INVALID) {
            goto cleanup;
        }
        /* exp: RFC 7519 §4.1.4 requires the current time to be BEFORE exp, so reject
         * at or after it. No sign guard: a non-positive exp is at/before the epoch,
         * i.e. already expired. */
        if (exp_st == JWT_CLAIM_OK && (long long)now >= exp_val) {
            goto cleanup; /* Token expired */
        }
        /* Optionally require exp to be present at all (RFC 7519 leaves it OPTIONAL;
         * strict callers can opt in so a leaked exp-less token can't replay
         * forever). */
        if (require_exp && exp_st != JWT_CLAIM_OK) {
            goto cleanup; /* exp claim required but absent */
        }
        /* nbf: reject a token that is not yet valid (§4.1.5: not before nbf). */
        if (nbf_st == JWT_CLAIM_OK && (long long)now < nbf_val) {
            goto cleanup; /* Token not yet valid */
        }
    }

    result = true;

cleanup:
    /* Wipe sensitive key material from stack */
    secure_zero(header_decoded, sizeof(header_decoded));
    secure_zero(payload_decoded, sizeof(payload_decoded));
    secure_zero(signature_decoded, sizeof(signature_decoded));
    secure_zero(computed_signature, sizeof(computed_signature));
    secure_zero(signing_input, sizeof(signing_input));
    return result;
}

/**
 * jwt_auth_handler - JWT authentication middleware handler
 */
static bool jwt_auth_handler(http_request_t *req, http_response_t *res, void *user_data)
{
    jwt_auth_config_t *config = user_data ? (jwt_auth_config_t *)user_data : g_jwt_auth_config;
    const char *header_name;
    const char *auth_header;
    const char *token;
    char payload[2048];

    if (!config || !config->secret) {
        return false;
    }

    header_name = config->header_name ? 
                  config->header_name : "Authorization";

    auth_header = http_request_get_header(req, header_name);
    if (!auth_header) {
        goto unauthorized;
    }

    /* Extract token from "Bearer <token>" format */
    if (strncmp(auth_header, "Bearer ", 7) == 0) {
        token = auth_header + 7;
        /* Skip whitespace */
        while (*token == ' ' || *token == '\t') {
            token++;
        }
    } else {
        /* Try using the header value as-is */
        token = auth_header;
    }

    if (!*token) {
        goto unauthorized;
    }

    /* Parse and verify JWT */
    if (!parse_jwt_token(token,
                        (const uint8_t *)config->secret,
                        config->secret_len,
                        config->require_exp,
                        payload, sizeof(payload))) {
        goto unauthorized;
    }

    /* Store parsed payload in request user_data for application use */
    /* Note: Application should parse this JSON using json_parse() */
    /* This is a simple implementation - production code may want to parse and validate claims */
    
    /* Authentication successful */
    return true;

unauthorized:
    http_response_set_header(res, "WWW-Authenticate", "Bearer");
    http_response_send_text(res, HTTP_UNAUTHORIZED, "401 Unauthorized - Invalid or missing JWT");
    return false;
}

/**
 * jwt_auth_middleware_create - Create JWT Auth middleware
 */
middleware_fn_t jwt_auth_middleware_create(const jwt_auth_config_t *config)
{
    if (!config || !config->secret || config->secret_len == 0) {
        return NULL;
    }

    /* Destroy existing config if any */
    jwt_auth_middleware_destroy();

    /* Allocate and store configuration */
    g_jwt_auth_config = malloc(sizeof(jwt_auth_config_t));
    if (!g_jwt_auth_config) {
        return NULL;
    }

    memcpy(g_jwt_auth_config, config, sizeof(jwt_auth_config_t));
    
    /* Duplicate secret */
    g_jwt_auth_config->secret = malloc(config->secret_len);
    if (!g_jwt_auth_config->secret) {
        free(g_jwt_auth_config);
        g_jwt_auth_config = NULL;
        return NULL;
    }
    memcpy((void *)g_jwt_auth_config->secret, config->secret, config->secret_len);
    
    /* Duplicate header name if provided */
    if (config->header_name) {
        g_jwt_auth_config->header_name = strdup(config->header_name);
        if (!g_jwt_auth_config->header_name) {
            free((void *)g_jwt_auth_config->secret);
            free(g_jwt_auth_config);
            g_jwt_auth_config = NULL;
            return NULL;
        }
    }

    return jwt_auth_handler;
}

/**
 * jwt_auth_middleware_destroy - Cleanup JWT Auth middleware
 */
void jwt_auth_middleware_destroy(void)
{
    if (g_jwt_auth_config) {
        if (g_jwt_auth_config->secret) {
            /* Zero out secret before freeing for security */
            secure_zero((void *)g_jwt_auth_config->secret, g_jwt_auth_config->secret_len);
            free((void *)g_jwt_auth_config->secret);
        }
        if (g_jwt_auth_config->header_name) {
            free((void *)g_jwt_auth_config->header_name);
        }
        free(g_jwt_auth_config);
        g_jwt_auth_config = NULL;
    }
}
