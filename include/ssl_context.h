#ifndef SSL_CONTEXT_H
#define SSL_CONTEXT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

/* Forward declaration of SSL context */
typedef struct ssl_context ssl_context_t;

/**
 * SSL/TLS configuration options
 */
typedef struct {
    const char *cert_file;      /* Path to certificate file (PEM format) */
    const char *key_file;       /* Path to private key file (PEM format) */
    const char *ca_file;        /* Optional: Path to CA certificate file for client verification */
    bool verify_peer;           /* Whether to verify client certificates */
    int min_tls_version;        /* Minimum TLS version (e.g., TLS1_2_VERSION) */
} ssl_config_t;

/**
 * Create a new SSL context for server use
 * @param config SSL configuration
 * @return SSL context or NULL on failure
 */
ssl_context_t *ssl_context_create(const ssl_config_t *config);

/**
 * Destroy SSL context and free resources
 * @param ctx SSL context
 */
void ssl_context_destroy(ssl_context_t *ctx);

/**
 * Accept SSL connection on a socket
 * @param ctx SSL context
 * @param socket_fd Socket file descriptor
 * @return SSL connection handle or NULL on failure
 */
void *ssl_accept(ssl_context_t *ctx, int socket_fd);

/**
 * Read data from SSL connection
 * @param ssl SSL connection handle
 * @param buffer Buffer to read into
 * @param length Maximum bytes to read
 * @return Number of bytes read, -1 on error
 */
int ssl_read(void *ssl, void *buffer, int length);

/**
 * Write data to SSL connection
 * @param ssl SSL connection handle
 * @param buffer Buffer to write from
 * @param length Number of bytes to write
 * @return Number of bytes written, -1 on error
 */
int ssl_write(void *ssl, const void *buffer, int length);

/**
 * Shutdown SSL connection
 * @param ssl SSL connection handle
 */
void ssl_shutdown(void *ssl);

/**
 * Free SSL connection
 * @param ssl SSL connection handle
 */
void ssl_free(void *ssl);

/**
 * Initialize OpenSSL library (call once at startup)
 */
void ssl_library_init(void);

/**
 * Cleanup OpenSSL library (call once at shutdown)
 */
void ssl_library_cleanup(void);

/**
 * Get last SSL error string
 * @return Error string
 */
const char *ssl_get_error_string(void);

#ifdef __cplusplus
}
#endif

#endif /* SSL_CONTEXT_H */
