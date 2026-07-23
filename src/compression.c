/*
 * compression.c - HTTP response compression implementation
 *
 * This file implements HTTP response compression with gzip encoding.
 * Features a simplified DEFLATE implementation (RFC 1951) with:
 * - Stored blocks (uncompressed but valid DEFLATE)
 * - Fixed Huffman coding (for basic compression)
 * - Gzip wrapper format (RFC 1952)
 * - Content negotiation via Accept-Encoding
 *
 * Pure C11, no external dependencies (no zlib), POSIX + standard library only.
 */

#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <ctype.h>
#include <pthread.h>
#include "kamran.k"

/* ============================================================================
 * Constants and Data Structures
 * ============================================================================ */

/* Compression levels */
typedef enum {
    COMPRESS_NONE = 0,    /* No compression (stored blocks only) */
    COMPRESS_FAST = 1,    /* Fixed Huffman encoding */
    COMPRESS_DEFAULT = 6  /* Default compression level (currently same as FAST) */
} compress_level_t;

/* Compression result */
typedef struct {
    uint8_t *data;      /* Compressed data (caller must free) */
    size_t size;         /* Compressed data size */
} compress_result_t;

/* Bit writer for Huffman coding */
typedef struct {
    uint8_t *buf;
    size_t capacity;
    size_t byte_pos;
    int bit_pos;    /* 0-7, bits filled in current byte from LSB */
} bit_writer_t;

/* Gzip header (10 bytes) */
static const uint8_t gzip_header[10] = {
    0x1f, 0x8b, 0x08, 0x00,  /* Magic, Method (deflate), Flags */
    0x00, 0x00, 0x00, 0x00,  /* MTIME (0) */
    0x00, 0xff               /* XFL, OS (unknown) */
};

/* CRC32 lookup table */
static uint32_t _crc32_table[256];
static pthread_once_t _crc32_once = PTHREAD_ONCE_INIT;

/* Minimum size for compression (bytes) */
#define MIN_COMPRESS_SIZE 256

/* Maximum DEFLATE stored block size */
#define MAX_STORED_BLOCK_SIZE 65535

/* Minimum compression ratio (percent of original size) to justify sending
 * compressed.  If compressed_size >= original_size * COMPRESSION_MIN_RATIO / 100
 * the response is sent uncompressed. */
#define COMPRESSION_MIN_RATIO 95

/* ============================================================================
 * CRC32 Implementation
 * ============================================================================ */

/**
 * Initialize CRC32 lookup table
 * Polynomial: 0xEDB88320 (reflected)
 */
static void _crc32_init_table(void) {
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t crc = i;
        for (int j = 0; j < 8; j++) {
            if (crc & 1) {
                crc = (crc >> 1) ^ 0xEDB88320;
            } else {
                crc = crc >> 1;
            }
        }
        _crc32_table[i] = crc;
    }
}

/**
 * Compute CRC-32 checksum per RFC 1952
 * @param data Input data
 * @param len Data length
 * @return CRC-32 checksum
 */
uint32_t crc32_compute(const uint8_t *data, size_t len) {
    if (!data) {
        return 0;
    }
    
    pthread_once(&_crc32_once, _crc32_init_table);
    
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < len; i++) {
        crc = (crc >> 8) ^ _crc32_table[(crc ^ data[i]) & 0xFF];
    }
    return crc ^ 0xFFFFFFFF;
}

/* ============================================================================
 * Bit Writer Helper
 * ============================================================================ */

/**
 * Initialize bit writer
 */
static void _bit_writer_init(bit_writer_t *bw, uint8_t *buf, size_t capacity) {
    bw->buf = buf;
    bw->capacity = capacity;
    bw->byte_pos = 0;
    bw->bit_pos = 0;
    if (capacity > 0) {
        buf[0] = 0;
    }
}

/**
 * Write bits to buffer (LSB first)
 * @param bw Bit writer
 * @param bits Bits to write
 * @param nbits Number of bits (1-16)
 * @return true on success, false if buffer full
 */
static bool _bit_writer_write(bit_writer_t *bw, uint16_t bits, int nbits) {
    for (int i = 0; i < nbits; i++) {
        if (bw->byte_pos >= bw->capacity) {
            return false;
        }
        
        if (bits & (1 << i)) {
            bw->buf[bw->byte_pos] |= (1 << bw->bit_pos);
        }
        
        bw->bit_pos++;
        if (bw->bit_pos == 8) {
            bw->bit_pos = 0;
            bw->byte_pos++;
            if (bw->byte_pos < bw->capacity) {
                bw->buf[bw->byte_pos] = 0;
            }
        }
    }
    return true;
}

/**
 * Flush bit writer and return final size
 */
static size_t _bit_writer_finish(bit_writer_t *bw) {
    if (bw->bit_pos > 0) {
        return bw->byte_pos + 1;
    }
    return bw->byte_pos;
}

/* ============================================================================
 * DEFLATE Implementation
 * ============================================================================ */

/**
 * Write DEFLATE stored block (uncompressed)
 * @param output Output buffer
 * @param output_capacity Output buffer capacity
 * @param input Input data
 * @param input_len Input length
 * @param is_final Is this the final block?
 * @return Number of bytes written, or 0 on error
 */
static size_t _deflate_write_stored_block(uint8_t *output, size_t output_capacity,
                                          const uint8_t *input, size_t input_len,
                                          bool is_final) {
    /* Stored block format:
     * BFINAL (1 bit) | BTYPE=00 (2 bits) | pad to byte boundary
     * LEN (2 bytes, little-endian)
     * NLEN (2 bytes, one's complement of LEN)
     * data (LEN bytes)
     */
    
    if (input_len > MAX_STORED_BLOCK_SIZE) {
        return 0;  /* Block too large */
    }
    
    size_t needed = 1 + 2 + 2 + input_len;  /* header + LEN + NLEN + data */
    if (needed > output_capacity) {
        return 0;  /* Not enough space */
    }
    
    size_t pos = 0;
    
    /* BFINAL | BTYPE=00 (3 bits total) */
    uint8_t header = is_final ? 0x01 : 0x00;  /* BFINAL=1, BTYPE=00 */
    output[pos++] = header;
    
    /* LEN (little-endian) */
    uint16_t len = (uint16_t)input_len;
    output[pos++] = len & 0xFF;
    output[pos++] = (len >> 8) & 0xFF;
    
    /* NLEN (one's complement) */
    uint16_t nlen = ~len;
    output[pos++] = nlen & 0xFF;
    output[pos++] = (nlen >> 8) & 0xFF;
    
    /* Data */
    memcpy(output + pos, input, input_len);
    pos += input_len;
    
    return pos;
}

/**
 * Write DEFLATE fixed Huffman block
 * Uses fixed Huffman codes from RFC 1951 Section 3.2.6
 * For simplicity, only encodes literals (no LZ77 compression)
 */
static size_t _deflate_write_fixed_huffman(uint8_t *output, size_t output_capacity,
                                           const uint8_t *input, size_t input_len,
                                           bool is_final) {
    /* Fixed Huffman code lengths:
     * Literal 0-143: 8 bits, codes 00110000 - 10111111
     * Literal 144-255: 9 bits, codes 110010000 - 111111111
     * Literal 256 (end): 7 bits, code 0000000
     */
    
    bit_writer_t bw;
    _bit_writer_init(&bw, output, output_capacity);
    
    /* Block header: BFINAL | BTYPE=01 */
    uint8_t header = is_final ? 0x01 : 0x00;  /* BFINAL */
    header |= (0x01 << 1);                     /* BTYPE=01 (fixed Huffman) */
    if (!_bit_writer_write(&bw, header, 3)) {
        return 0;
    }
    
    /* Encode each byte as a literal */
    for (size_t i = 0; i < input_len; i++) {
        uint8_t byte = input[i];
        
        if (byte <= 143) {
            /* 8-bit code: reverse bits of (0x30 + byte) */
            uint16_t code = 0x30 + byte;
            /* Reverse 8 bits */
            uint16_t reversed = 0;
            for (int b = 0; b < 8; b++) {
                if (code & (1 << (7 - b))) {
                    reversed |= (1 << b);
                }
            }
            if (!_bit_writer_write(&bw, reversed, 8)) {
                return 0;
            }
        } else {
            /* 9-bit code: reverse bits of (0x190 + byte - 144) */
            uint16_t code = 0x190 + (byte - 144);
            /* Reverse 9 bits */
            uint16_t reversed = 0;
            for (int b = 0; b < 9; b++) {
                if (code & (1 << (8 - b))) {
                    reversed |= (1 << b);
                }
            }
            if (!_bit_writer_write(&bw, reversed, 9)) {
                return 0;
            }
        }
    }
    
    /* End of block (256): 7-bit code 0000000 */
    if (!_bit_writer_write(&bw, 0x00, 7)) {
        return 0;
    }
    
    return _bit_writer_finish(&bw);
}

/**
 * Compress data using DEFLATE algorithm
 * @param output Output buffer
 * @param output_capacity Output buffer capacity
 * @param input Input data
 * @param input_len Input length
 * @param level Compression level
 * @return Number of bytes written, or 0 on error
 */
static size_t _deflate_compress(uint8_t *output, size_t output_capacity,
                                const uint8_t *input, size_t input_len,
                                compress_level_t level) {
    if (!output || !input || input_len == 0) {
        return 0;
    }
    
    if (level == COMPRESS_NONE) {
        /* Use stored blocks */
        size_t total = 0;
        size_t remaining = input_len;
        const uint8_t *src = input;
        
        while (remaining > 0) {
            size_t block_size = remaining > MAX_STORED_BLOCK_SIZE ? 
                               MAX_STORED_BLOCK_SIZE : remaining;
            bool is_final = (remaining <= MAX_STORED_BLOCK_SIZE);
            
            size_t written = _deflate_write_stored_block(
                output + total,
                output_capacity - total,
                src,
                block_size,
                is_final
            );
            
            if (written == 0) {
                return 0;  /* Error */
            }
            
            total += written;
            src += block_size;
            remaining -= block_size;
        }
        
        return total;
    } else {
        /* Use fixed Huffman (COMPRESS_FAST or COMPRESS_DEFAULT) */
        /* For simplicity, use single block if it fits */
        if (input_len <= MAX_STORED_BLOCK_SIZE) {
            return _deflate_write_fixed_huffman(output, output_capacity,
                                               input, input_len, true);
        } else {
            /* Split into multiple blocks */
            size_t total = 0;
            size_t remaining = input_len;
            const uint8_t *src = input;
            
            while (remaining > 0) {
                size_t block_size = remaining > MAX_STORED_BLOCK_SIZE ? 
                                   MAX_STORED_BLOCK_SIZE : remaining;
                bool is_final = (remaining <= MAX_STORED_BLOCK_SIZE);
                
                size_t written = _deflate_write_fixed_huffman(
                    output + total,
                    output_capacity - total,
                    src,
                    block_size,
                    is_final
                );
                
                if (written == 0) {
                    return 0;  /* Error */
                }
                
                total += written;
                src += block_size;
                remaining -= block_size;
            }
            
            return total;
        }
    }
}

/* ============================================================================
 * Gzip Compression
 * ============================================================================ */

/**
 * Write 32-bit value in little-endian format
 */
static void _write_le32(uint8_t *buf, uint32_t value) {
    buf[0] = value & 0xFF;
    buf[1] = (value >> 8) & 0xFF;
    buf[2] = (value >> 16) & 0xFF;
    buf[3] = (value >> 24) & 0xFF;
}

/**
 * Compress data using gzip format (RFC 1952)
 * @param input Input data
 * @param input_len Input length
 * @param level Compression level
 * @return Compression result (data must be freed by caller)
 */
compress_result_t gzip_compress(const uint8_t *input, size_t input_len,
                                compress_level_t level) {
    compress_result_t result = {NULL, 0};
    
    if (!input || input_len == 0) {
        return result;
    }
    
    /* Estimate output size:
     * gzip header (10) + DEFLATE data (worst case: input + overhead) + 
     * CRC32 (4) + ISIZE (4)
     */
    size_t max_deflate_size;
    if (level == COMPRESS_NONE) {
        /* Stored blocks: each block has 5 bytes overhead */
        size_t num_blocks = (input_len + MAX_STORED_BLOCK_SIZE - 1) / MAX_STORED_BLOCK_SIZE;
        max_deflate_size = input_len + (num_blocks * 5);
    } else {
        /* Fixed Huffman: worst case is ~9 bits per byte + overhead */
        max_deflate_size = (input_len * 9 / 8) + input_len;  /* Conservative estimate */
    }
    
    size_t output_capacity = 10 + max_deflate_size + 8;
    uint8_t *output = malloc(output_capacity);
    if (!output) {
        return result;
    }
    
    size_t pos = 0;
    
    /* Write gzip header */
    memcpy(output + pos, gzip_header, 10);
    pos += 10;
    
    /* Write DEFLATE compressed data */
    size_t deflate_size = _deflate_compress(output + pos, output_capacity - pos - 8,
                                           input, input_len, level);
    if (deflate_size == 0) {
        free(output);
        return result;
    }
    pos += deflate_size;
    
    /* Write CRC32 (little-endian) */
    uint32_t crc = crc32_compute(input, input_len);
    _write_le32(output + pos, crc);
    pos += 4;
    
    /* Write ISIZE (original size, little-endian) */
    _write_le32(output + pos, (uint32_t)input_len);
    pos += 4;
    
    /* Resize buffer to actual size */
    uint8_t *final_output = realloc(output, pos);
    if (final_output) {
        output = final_output;
    }
    
    result.data = output;
    result.size = pos;
    return result;
}

/* ============================================================================
 * Content Negotiation
 * ============================================================================ */

/**
 * Check if content type should be compressed
 * @param content_type Content-Type header value
 * @param content_length Content length
 * @return true if content should be compressed
 */
bool compression_should_compress(const char *content_type, size_t content_length) {
    if (!content_type || content_length < MIN_COMPRESS_SIZE) {
        return false;
    }
    
    /* Compressible text types */
    const char *compressible[] = {
        "text/",
        "application/json",
        "application/javascript",
        "application/xml",
        "application/xhtml+xml",
        "application/rss+xml",
        "application/atom+xml",
        NULL
    };
    
    for (int i = 0; compressible[i] != NULL; i++) {
        if (strstr(content_type, compressible[i]) == content_type) {
            return true;
        }
    }
    
    /* Don't compress already-compressed types */
    const char *incompressible[] = {
        "image/",
        "audio/",
        "video/",
        "application/gzip",
        "application/zip",
        "application/x-rar",
        "application/pdf",
        NULL
    };
    
    for (int i = 0; incompressible[i] != NULL; i++) {
        if (strstr(content_type, incompressible[i]) == content_type) {
            return false;
        }
    }
    
    return false;
}

/**
 * Parse quality value from Accept-Encoding parameter
 * @param param Parameter string (e.g., "q=0.8")
 * @return Quality value (0.0 - 1.0), or 1.0 if not specified
 */
static double _parse_quality(const char *param) {
    if (!param) {
        return 1.0;
    }
    
    /* Skip whitespace */
    while (*param && isspace((unsigned char)*param)) {
        param++;
    }
    
    /* Check for q= */
    if (param[0] != 'q' || param[1] != '=') {
        return 1.0;
    }
    
    param += 2;
    char *endptr;
    double q = strtod(param, &endptr);
    
    if (endptr == param || q < 0.0) {
        return 1.0;
    }
    
    return q > 1.0 ? 1.0 : q;
}

/**
 * Negotiate compression encoding from Accept-Encoding header
 * @param accept_encoding Accept-Encoding header value
 * @return "gzip" if gzip is accepted, NULL otherwise
 */
const char *compression_negotiate(const char *accept_encoding) {
    if (!accept_encoding) {
        return NULL;
    }
    
    /* Parse Accept-Encoding header
     * Format: "gzip, deflate, br;q=0.8, *;q=0.1"
     */
    
    bool gzip_found = false;
    double gzip_quality = 0.0;
    bool wildcard_found = false;
    double wildcard_quality = 0.0;
    
    /* Make a working copy */
    char *header = strdup(accept_encoding);
    if (!header) {
        return NULL;
    }
    
    /* Parse comma-separated encodings */
    char *saveptr;
    char *token = strtok_r(header, ",", &saveptr);
    
    while (token) {
        /* Skip leading whitespace */
        while (*token && isspace(*token)) {
            token++;
        }
        
        /* Find semicolon (quality separator) */
        char *semicolon = strchr(token, ';');
        double quality = 1.0;
        
        if (semicolon) {
            *semicolon = '\0';
            quality = _parse_quality(semicolon + 1);
        }
        
        /* Trim trailing whitespace from encoding name */
        char *end = token + strlen(token) - 1;
        while (end > token && isspace(*end)) {
            *end = '\0';
            end--;
        }
        
        /* Check encoding type */
        if (strcmp(token, "gzip") == 0) {
            gzip_found = true;
            gzip_quality = quality;
        } else if (strcmp(token, "*") == 0) {
            wildcard_found = true;
            wildcard_quality = quality;
        }
        
        token = strtok_r(NULL, ",", &saveptr);
    }
    
    free(header);
    
    /* Return gzip if explicitly accepted with quality > 0 */
    if (gzip_found && gzip_quality > 0.0) {
        return "gzip";
    }
    
    /* Return gzip if wildcard accepts it */
    if (!gzip_found && wildcard_found && wildcard_quality > 0.0) {
        return "gzip";
    }
    
    return NULL;
}

/* ============================================================================
 * HTTP Response Compression API
 * ============================================================================ */

/**
 * Send a compressed text response.
 *
 * If the accept_encoding header indicates gzip support and the content_type
 * is compressible, the body is gzip-compressed before being set on the
 * response.  Otherwise, the body is sent as-is via http_response_send_text().
 *
 * @param res             HTTP response object
 * @param status          HTTP status code
 * @param body            Response body text
 * @param body_len        Response body length (0 = use strlen)
 * @param content_type    Content-Type to check compressibility (e.g. "text/html")
 * @param accept_encoding Value of the Accept-Encoding request header (may be NULL)
 */
void http_response_send_compressed(http_response_t *res, http_status_t status,
                                   const char *body, size_t body_len,
                                   const char *content_type,
                                   const char *accept_encoding) {
    if (!res || !body) {
        return;
    }

    if (body_len == 0) {
        body_len = strlen(body);
    }

    /* Decide whether to compress */
    const char *encoding = compression_negotiate(accept_encoding);
    bool should_compress = (encoding != NULL) &&
                          compression_should_compress(
                              content_type ? content_type : "text/plain",
                              body_len);

    if (!should_compress) {
        /* Send uncompressed */
        http_response_send_text(res, status, body);
        return;
    }

    /* Compress the body */
    compress_result_t compressed = gzip_compress((const uint8_t *)body,
                                                 body_len, COMPRESS_FAST);

    if (!compressed.data || compressed.size >= (size_t)(body_len * COMPRESSION_MIN_RATIO / 100)) {
        /* Compression failed or not beneficial — send uncompressed */
        free(compressed.data);
        http_response_send_text(res, status, body);
        return;
    }

    /* Set compressed body directly on the response */
    free(res->body);
    res->body = (char *)compressed.data;   /* ownership transferred */
    res->body_length = compressed.size;
    res->status = status;

    /* Set headers */
    http_response_set_header(res, "Content-Encoding", "gzip");
    http_response_set_header(res, "Vary", "Accept-Encoding");
    if (content_type) {
        http_response_set_header(res, "Content-Type", content_type);
    }
}
