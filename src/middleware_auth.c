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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <ctype.h>
#include <time.h>

/* ===== Base64 Decoding (RFC 4648) ===== */

static const unsigned char base64_decode_table[256] = {
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 62, 64, 64, 64, 63,
    52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 64, 64, 64, 64, 64, 64,
    64,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14,
    15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 64, 64, 64, 64, 64,
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
 * base64_decode - Decode standard Base64 string
 * @input: Base64 encoded string
 * @input_len: Length of input string
 * @output: Buffer to store decoded data
 * @output_len: Size of output buffer
 * 
 * Returns: Number of decoded bytes, or -1 on error
 */
static int base64_decode(const char *input, size_t input_len, unsigned char *output, size_t output_len)
{
    if (!input || !output || input_len == 0) {
        return -1;
    }

    size_t i = 0, j = 0;
    unsigned char buf[4];
    int buf_count = 0;

    for (i = 0; i < input_len; i++) {
        unsigned char c = (unsigned char)input[i];
        
        /* Skip whitespace */
        if (isspace(c)) {
            continue;
        }
        
        /* Handle padding */
        if (c == '=') {
            break;
        }
        
        unsigned char val = base64_decode_table[c];
        if (val == 64) {
            return -1; /* Invalid character */
        }
        
        buf[buf_count++] = val;
        
        if (buf_count == 4) {
            if (j + 3 > output_len) {
                return -1; /* Output buffer too small */
            }
            
            output[j++] = (buf[0] << 2) | (buf[1] >> 4);
            output[j++] = (buf[1] << 4) | (buf[2] >> 2);
            output[j++] = (buf[2] << 6) | buf[3];
            
            buf_count = 0;
        }
    }
    
    /* Handle remaining bytes */
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

/* ===== SHA-256 Implementation (FIPS 180-4) ===== */

#define SHA256_BLOCK_SIZE 64
#define SHA256_DIGEST_SIZE 32

typedef struct {
    uint32_t state[8];
    uint64_t count;
    uint8_t buffer[SHA256_BLOCK_SIZE];
} sha256_ctx_t;

/* SHA-256 constants (first 32 bits of the fractional parts of cube roots of first 64 primes) */
static const uint32_t sha256_k[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

/* Rotate right macro */
#define ROTR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))

/* SHA-256 functions */
#define CH(x, y, z)  (((x) & (y)) ^ (~(x) & (z)))
#define MAJ(x, y, z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define EP0(x) (ROTR(x, 2) ^ ROTR(x, 13) ^ ROTR(x, 22))
#define EP1(x) (ROTR(x, 6) ^ ROTR(x, 11) ^ ROTR(x, 25))
#define SIG0(x) (ROTR(x, 7) ^ ROTR(x, 18) ^ ((x) >> 3))
#define SIG1(x) (ROTR(x, 17) ^ ROTR(x, 19) ^ ((x) >> 10))

/**
 * sha256_transform - Process one 512-bit block
 */
static void sha256_transform(sha256_ctx_t *ctx, const uint8_t *data)
{
    uint32_t a, b, c, d, e, f, g, h, t1, t2;
    uint32_t w[64];
    int i;

    /* Prepare message schedule */
    for (i = 0; i < 16; i++) {
        w[i] = ((uint32_t)data[i * 4] << 24) |
               ((uint32_t)data[i * 4 + 1] << 16) |
               ((uint32_t)data[i * 4 + 2] << 8) |
               ((uint32_t)data[i * 4 + 3]);
    }
    
    for (i = 16; i < 64; i++) {
        w[i] = SIG1(w[i - 2]) + w[i - 7] + SIG0(w[i - 15]) + w[i - 16];
    }

    /* Initialize working variables */
    a = ctx->state[0];
    b = ctx->state[1];
    c = ctx->state[2];
    d = ctx->state[3];
    e = ctx->state[4];
    f = ctx->state[5];
    g = ctx->state[6];
    h = ctx->state[7];

    /* Main loop */
    for (i = 0; i < 64; i++) {
        t1 = h + EP1(e) + CH(e, f, g) + sha256_k[i] + w[i];
        t2 = EP0(a) + MAJ(a, b, c);
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }

    /* Add compressed chunk to current hash value */
    ctx->state[0] += a;
    ctx->state[1] += b;
    ctx->state[2] += c;
    ctx->state[3] += d;
    ctx->state[4] += e;
    ctx->state[5] += f;
    ctx->state[6] += g;
    ctx->state[7] += h;
}

/**
 * sha256_init - Initialize SHA-256 context
 */
static void sha256_init(sha256_ctx_t *ctx)
{
    /* Initial hash values (first 32 bits of fractional parts of square roots of first 8 primes) */
    ctx->state[0] = 0x6a09e667;
    ctx->state[1] = 0xbb67ae85;
    ctx->state[2] = 0x3c6ef372;
    ctx->state[3] = 0xa54ff53a;
    ctx->state[4] = 0x510e527f;
    ctx->state[5] = 0x9b05688c;
    ctx->state[6] = 0x1f83d9ab;
    ctx->state[7] = 0x5be0cd19;
    ctx->count = 0;
}

/**
 * sha256_update - Add data to SHA-256 hash
 */
static void sha256_update(sha256_ctx_t *ctx, const uint8_t *data, size_t len)
{
    size_t index = (size_t)(ctx->count % SHA256_BLOCK_SIZE);

    ctx->count += len;

    /* Process any buffered data first */
    if (index > 0) {
        size_t space = SHA256_BLOCK_SIZE - index;
        if (len < space) {
            memcpy(&ctx->buffer[index], data, len);
            return;
        }
        memcpy(&ctx->buffer[index], data, space);
        sha256_transform(ctx, ctx->buffer);
        data += space;
        len -= space;
    }

    /* Process complete blocks */
    while (len >= SHA256_BLOCK_SIZE) {
        sha256_transform(ctx, data);
        data += SHA256_BLOCK_SIZE;
        len -= SHA256_BLOCK_SIZE;
    }

    /* Buffer remaining data */
    if (len > 0) {
        memcpy(ctx->buffer, data, len);
    }
}

/**
 * sha256_final - Finalize SHA-256 hash and produce digest
 */
static void sha256_final(sha256_ctx_t *ctx, uint8_t *digest)
{
    size_t i;
    size_t index = (size_t)(ctx->count % SHA256_BLOCK_SIZE);
    uint64_t bits = ctx->count * 8;

    /* Pad with 0x80 followed by zeros */
    ctx->buffer[index++] = 0x80;

    if (index > 56) {
        /* Not enough space for length, pad and process block */
        while (index < SHA256_BLOCK_SIZE) {
            ctx->buffer[index++] = 0x00;
        }
        sha256_transform(ctx, ctx->buffer);
        index = 0;
    }

    /* Pad with zeros */
    while (index < 56) {
        ctx->buffer[index++] = 0x00;
    }

    /* Append length in bits as big-endian 64-bit integer */
    ctx->buffer[56] = (uint8_t)(bits >> 56);
    ctx->buffer[57] = (uint8_t)(bits >> 48);
    ctx->buffer[58] = (uint8_t)(bits >> 40);
    ctx->buffer[59] = (uint8_t)(bits >> 32);
    ctx->buffer[60] = (uint8_t)(bits >> 24);
    ctx->buffer[61] = (uint8_t)(bits >> 16);
    ctx->buffer[62] = (uint8_t)(bits >> 8);
    ctx->buffer[63] = (uint8_t)(bits);

    sha256_transform(ctx, ctx->buffer);

    /* Produce final hash value (big-endian) */
    for (i = 0; i < 8; i++) {
        digest[i * 4] = (uint8_t)(ctx->state[i] >> 24);
        digest[i * 4 + 1] = (uint8_t)(ctx->state[i] >> 16);
        digest[i * 4 + 2] = (uint8_t)(ctx->state[i] >> 8);
        digest[i * 4 + 3] = (uint8_t)(ctx->state[i]);
    }
}

/**
 * sha256 - Compute SHA-256 hash of data
 */
static void sha256(const uint8_t *data, size_t len, uint8_t *digest)
{
    sha256_ctx_t ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, data, len);
    sha256_final(&ctx, digest);
}

/* ===== HMAC-SHA256 Implementation (RFC 2104) ===== */

/**
 * hmac_sha256 - Compute HMAC-SHA256
 * @key: Secret key
 * @key_len: Key length
 * @data: Data to authenticate
 * @data_len: Data length
 * @output: Buffer for 32-byte HMAC output
 */
static void hmac_sha256(const uint8_t *key, size_t key_len,
                        const uint8_t *data, size_t data_len,
                        uint8_t *output)
{
    sha256_ctx_t ctx;
    uint8_t k_pad[SHA256_BLOCK_SIZE];
    uint8_t tk[SHA256_DIGEST_SIZE];
    size_t i;

    /* If key is longer than block size, hash it first */
    if (key_len > SHA256_BLOCK_SIZE) {
        sha256(key, key_len, tk);
        key = tk;
        key_len = SHA256_DIGEST_SIZE;
    }

    /* Prepare inner padding (key XOR 0x36) */
    memset(k_pad, 0x36, SHA256_BLOCK_SIZE);
    for (i = 0; i < key_len; i++) {
        k_pad[i] ^= key[i];
    }

    /* Compute inner hash: H(K XOR ipad, text) */
    sha256_init(&ctx);
    sha256_update(&ctx, k_pad, SHA256_BLOCK_SIZE);
    sha256_update(&ctx, data, data_len);
    sha256_final(&ctx, output);

    /* Prepare outer padding (key XOR 0x5c) */
    memset(k_pad, 0x5c, SHA256_BLOCK_SIZE);
    for (i = 0; i < key_len; i++) {
        k_pad[i] ^= key[i];
    }

    /* Compute outer hash: H(K XOR opad, inner_hash) */
    sha256_init(&ctx);
    sha256_update(&ctx, k_pad, SHA256_BLOCK_SIZE);
    sha256_update(&ctx, output, SHA256_DIGEST_SIZE);
    sha256_final(&ctx, output);
}

/**
 * _auth_secure_compare - Constant-time memory comparison
 * Prevents timing attacks by always comparing all bytes
 */
static bool _auth_secure_compare(const uint8_t *a, const uint8_t *b, size_t len)
{
    uint8_t diff = 0;
    size_t i;
    
    for (i = 0; i < len; i++) {
        diff |= a[i] ^ b[i];
    }
    
    return diff == 0;
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
    memset(decoded, 0, sizeof(decoded));

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
    return true;

unauthorized:
    /* Set WWW-Authenticate header */
    if (config->realm) {
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

    /* Find separators */
    dot1 = strchr(token, '.');
    if (!dot1) {
        return false;
    }
    
    dot2 = strchr(dot1 + 1, '.');
    if (!dot2) {
        return false;
    }

    header_len = dot1 - token;
    payload_len = dot2 - dot1 - 1;
    signature_len = strlen(dot2 + 1);

    /* Decode header */
    decoded_len = base64url_decode(token, header_len, header_decoded, sizeof(header_decoded) - 1);
    if (decoded_len < 0) {
        return false;
    }
    header_decoded[decoded_len] = '\0';

    /* Verify algorithm is HS256 */
    if (!strstr((char *)header_decoded, "\"HS256\"")) {
        return false; /* Only HS256 supported */
    }

    /* Decode payload */
    decoded_len = base64url_decode(dot1 + 1, payload_len, payload_decoded, sizeof(payload_decoded) - 1);
    if (decoded_len < 0) {
        return false;
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
        return false;
    }

    /* Prepare signing input (header.payload) */
    signing_input_len = dot2 - token;
    if (signing_input_len >= sizeof(signing_input)) {
        return false;
    }
    memcpy(signing_input, token, signing_input_len);
    signing_input[signing_input_len] = '\0';

    /* Compute HMAC-SHA256 */
    hmac_sha256(secret, secret_len, (uint8_t *)signing_input, signing_input_len, computed_signature);

    /* Constant-time comparison to prevent timing attacks */
    if (!_auth_secure_compare(signature_decoded, computed_signature, SHA256_DIGEST_SIZE)) {
        return false;
    }

    /* Validate expiration claim if present */
    {
        const char *exp_str = strstr((const char *)payload_decoded, "\"exp\"");
        if (exp_str) {
            /* Skip to the value: past "exp" and any whitespace/colon */
            exp_str += 5; /* skip "exp"" */
            while (*exp_str == ' ' || *exp_str == ':' || *exp_str == '\t') {
                exp_str++;
            }
            char *endptr = NULL;
            long exp_val = strtol(exp_str, &endptr, 10);
            if (endptr != exp_str && exp_val > 0) {
                time_t now = time(NULL);
                if ((time_t)exp_val < now) {
                    return false; /* Token expired */
                }
            }
        }
    }

    return true;
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
            memset((void *)g_jwt_auth_config->secret, 0, g_jwt_auth_config->secret_len);
            free((void *)g_jwt_auth_config->secret);
        }
        if (g_jwt_auth_config->header_name) {
            free((void *)g_jwt_auth_config->header_name);
        }
        free(g_jwt_auth_config);
        g_jwt_auth_config = NULL;
    }
}
