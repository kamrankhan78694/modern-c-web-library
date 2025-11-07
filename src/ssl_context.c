#include "ssl_context.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/bio.h>

/* Internal SSL context structure */
struct ssl_context {
    SSL_CTX *ctx;
    ssl_config_t config;
};

/* Thread-safe error buffer */
static __thread char error_buffer[256];

/* Initialize OpenSSL library */
void ssl_library_init(void) {
    SSL_load_error_strings();
    SSL_library_init();
    OpenSSL_add_all_algorithms();
}

/* Cleanup OpenSSL library */
void ssl_library_cleanup(void) {
    EVP_cleanup();
    ERR_free_strings();
}

/* Get last SSL error */
const char *ssl_get_error_string(void) {
    unsigned long err = ERR_get_error();
    if (err == 0) {
        return "No error";
    }
    ERR_error_string_n(err, error_buffer, sizeof(error_buffer));
    return error_buffer;
}

/* Create SSL context */
ssl_context_t *ssl_context_create(const ssl_config_t *config) {
    if (!config || !config->cert_file || !config->key_file) {
        fprintf(stderr, "SSL: Invalid configuration\n");
        return NULL;
    }
    
    ssl_context_t *ssl_ctx = (ssl_context_t *)calloc(1, sizeof(ssl_context_t));
    if (!ssl_ctx) {
        return NULL;
    }
    
    /* Create SSL context with TLS server method */
    ssl_ctx->ctx = SSL_CTX_new(TLS_server_method());
    if (!ssl_ctx->ctx) {
        fprintf(stderr, "SSL: Failed to create SSL_CTX: %s\n", ssl_get_error_string());
        free(ssl_ctx);
        return NULL;
    }
    
    /* Set minimum TLS version if specified */
    if (config->min_tls_version > 0) {
        SSL_CTX_set_min_proto_version(ssl_ctx->ctx, config->min_tls_version);
    }
    
    /* Load certificate file */
    if (SSL_CTX_use_certificate_file(ssl_ctx->ctx, config->cert_file, SSL_FILETYPE_PEM) <= 0) {
        fprintf(stderr, "SSL: Failed to load certificate from %s: %s\n", 
                config->cert_file, ssl_get_error_string());
        SSL_CTX_free(ssl_ctx->ctx);
        free(ssl_ctx);
        return NULL;
    }
    
    /* Load private key file */
    if (SSL_CTX_use_PrivateKey_file(ssl_ctx->ctx, config->key_file, SSL_FILETYPE_PEM) <= 0) {
        fprintf(stderr, "SSL: Failed to load private key from %s: %s\n", 
                config->key_file, ssl_get_error_string());
        SSL_CTX_free(ssl_ctx->ctx);
        free(ssl_ctx);
        return NULL;
    }
    
    /* Verify private key matches certificate */
    if (!SSL_CTX_check_private_key(ssl_ctx->ctx)) {
        fprintf(stderr, "SSL: Private key does not match certificate: %s\n", 
                ssl_get_error_string());
        SSL_CTX_free(ssl_ctx->ctx);
        free(ssl_ctx);
        return NULL;
    }
    
    /* Load CA certificate for client verification if provided */
    if (config->ca_file) {
        if (!SSL_CTX_load_verify_locations(ssl_ctx->ctx, config->ca_file, NULL)) {
            fprintf(stderr, "SSL: Failed to load CA certificate from %s: %s\n", 
                    config->ca_file, ssl_get_error_string());
            SSL_CTX_free(ssl_ctx->ctx);
            free(ssl_ctx);
            return NULL;
        }
        
        if (config->verify_peer) {
            SSL_CTX_set_verify(ssl_ctx->ctx, SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT, NULL);
        }
    }
    
    /* Set options for security */
    SSL_CTX_set_options(ssl_ctx->ctx, SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3);
    
    /* Copy configuration */
    ssl_ctx->config = *config;
    if (config->cert_file) {
        ssl_ctx->config.cert_file = strdup(config->cert_file);
    }
    if (config->key_file) {
        ssl_ctx->config.key_file = strdup(config->key_file);
    }
    if (config->ca_file) {
        ssl_ctx->config.ca_file = strdup(config->ca_file);
    }
    
    return ssl_ctx;
}

/* Destroy SSL context */
void ssl_context_destroy(ssl_context_t *ctx) {
    if (!ctx) {
        return;
    }
    
    if (ctx->ctx) {
        SSL_CTX_free(ctx->ctx);
    }
    
    free((void *)ctx->config.cert_file);
    free((void *)ctx->config.key_file);
    free((void *)ctx->config.ca_file);
    free(ctx);
}

/* Accept SSL connection */
void *ssl_accept(ssl_context_t *ctx, int socket_fd) {
    if (!ctx || !ctx->ctx) {
        return NULL;
    }
    
    SSL *ssl = SSL_new(ctx->ctx);
    if (!ssl) {
        fprintf(stderr, "SSL: Failed to create SSL: %s\n", ssl_get_error_string());
        return NULL;
    }
    
    /* Set socket file descriptor */
    if (SSL_set_fd(ssl, socket_fd) != 1) {
        fprintf(stderr, "SSL: Failed to set fd: %s\n", ssl_get_error_string());
        SSL_free(ssl);
        return NULL;
    }
    
    /* Perform SSL handshake */
    int ret = SSL_accept(ssl);
    if (ret <= 0) {
        int err = SSL_get_error(ssl, ret);
        fprintf(stderr, "SSL: Handshake failed (error %d): %s\n", err, ssl_get_error_string());
        SSL_free(ssl);
        return NULL;
    }
    
    return ssl;
}

/* Read from SSL connection */
int ssl_read(void *ssl, void *buffer, int length) {
    if (!ssl || !buffer || length <= 0) {
        return -1;
    }
    
    int bytes = SSL_read((SSL *)ssl, buffer, length);
    if (bytes < 0) {
        int err = SSL_get_error((SSL *)ssl, bytes);
        if (err != SSL_ERROR_WANT_READ && err != SSL_ERROR_WANT_WRITE) {
            fprintf(stderr, "SSL: Read error (error %d): %s\n", err, ssl_get_error_string());
        }
    }
    
    return bytes;
}

/* Write to SSL connection */
int ssl_write(void *ssl, const void *buffer, int length) {
    if (!ssl || !buffer || length <= 0) {
        return -1;
    }
    
    int bytes = SSL_write((SSL *)ssl, buffer, length);
    if (bytes < 0) {
        int err = SSL_get_error((SSL *)ssl, bytes);
        if (err != SSL_ERROR_WANT_READ && err != SSL_ERROR_WANT_WRITE) {
            fprintf(stderr, "SSL: Write error (error %d): %s\n", err, ssl_get_error_string());
        }
    }
    
    return bytes;
}

/* Shutdown SSL connection */
void ssl_shutdown(void *ssl) {
    if (!ssl) {
        return;
    }
    
    /* Perform bidirectional shutdown */
    int ret = SSL_shutdown((SSL *)ssl);
    if (ret == 0) {
        /* First phase of shutdown complete, need second call */
        SSL_shutdown((SSL *)ssl);
    }
}

/* Free SSL connection */
void ssl_free(void *ssl) {
    if (ssl) {
        SSL_free((SSL *)ssl);
    }
}
