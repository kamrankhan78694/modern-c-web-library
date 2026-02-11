/**
 * middleware_static.c - Static File Serving Middleware
 * 
 * Provides static file serving with:
 * - MIME type detection
 * - Path traversal prevention
 * - ETag generation and validation
 * - Cache control headers
 * - Conditional requests (304 Not Modified)
 */

#include "weblib.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <time.h>
#include <ctype.h>
#include <errno.h>

#ifdef _WIN32
    #include <windows.h>
    #define PATH_MAX MAX_PATH
    #define realpath(N,R) _fullpath((R),(N),PATH_MAX)
    #define stat _stat
    #define S_ISREG(m) (((m) & S_IFMT) == S_IFREG)
    #define S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)
#else
    #include <limits.h>
    #include <unistd.h>
#endif

/* Maximum file size to serve: 50MB */
#define MAX_FILE_SIZE (50 * 1024 * 1024)

/* MIME type mapping entry */
typedef struct mime_entry {
    const char *extension;
    const char *mime_type;
} mime_entry_t;

/* MIME type lookup table */
static const mime_entry_t mime_types[] = {
    {".html", "text/html"},
    {".htm", "text/html"},
    {".css", "text/css"},
    {".js", "application/javascript"},
    {".json", "application/json"},
    {".xml", "application/xml"},
    {".txt", "text/plain"},
    {".png", "image/png"},
    {".jpg", "image/jpeg"},
    {".jpeg", "image/jpeg"},
    {".gif", "image/gif"},
    {".svg", "image/svg+xml"},
    {".ico", "image/x-icon"},
    {".pdf", "application/pdf"},
    {".woff", "font/woff"},
    {".woff2", "font/woff2"},
    {".ttf", "font/ttf"},
    {NULL, NULL}
};

/* Static middleware configuration (stored globally) */
static struct {
    char *root_dir;
    char *index_file;
    int cache_max_age;
    bool enable_etag;
    bool initialized;
} _static_config = {0};

/**
 * Get file extension from path (including the dot)
 */
static const char *_get_file_extension(const char *path) {
    if (!path) {
        return NULL;
    }
    
    const char *dot = strrchr(path, '.');
    if (!dot || dot == path) {
        return NULL;
    }
    
    return dot;
}

/**
 * Get MIME type for file extension
 */
static const char *_get_mime_type(const char *path) {
    const char *ext = _get_file_extension(path);
    if (!ext) {
        return "application/octet-stream";
    }
    
    /* Case-insensitive comparison */
    for (int i = 0; mime_types[i].extension != NULL; i++) {
        const char *mime_ext = mime_types[i].extension;
        size_t len = strlen(mime_ext);
        
        /* Simple case-insensitive comparison */
        bool match = true;
        for (size_t j = 0; j < len; j++) {
            if (tolower((unsigned char)ext[j]) != tolower((unsigned char)mime_ext[j])) {
                match = false;
                break;
            }
        }
        
        if (match && ext[len] == '\0') {
            return mime_types[i].mime_type;
        }
    }
    
    return "application/octet-stream";
}

/**
 * Generate ETag from file stats (simple hash of size + mtime)
 */
static void _generate_etag(struct stat *st, char *etag, size_t etag_size) {
    if (!st || !etag || etag_size < 32) {
        return;
    }
    
    /* Simple ETag: "size-mtime" in hex */
    snprintf(etag, etag_size, "\"%lx-%lx\"",
             (unsigned long)st->st_size,
             (unsigned long)st->st_mtime);
}

/**
 * Format time as HTTP date string (RFC 7231)
 */
static void _format_http_date(time_t t, char *buf, size_t buf_size) {
    if (!buf || buf_size < 30) {
        return;
    }
    
    struct tm tm_buf;
    struct tm *tm = gmtime_r(&t, &tm_buf);
    if (!tm) {
        buf[0] = '\0';
        return;
    }
    
    strftime(buf, buf_size, "%a, %d %b %Y %H:%M:%S GMT", tm);
}

/**
 * Check if path is within root directory (path traversal prevention)
 */
static bool _is_path_safe(const char *root, const char *path) {
    if (!root || !path) {
        return false;
    }
    
    char resolved_root[PATH_MAX];
    char resolved_path[PATH_MAX];
    
    /* Resolve both paths to absolute paths */
    if (!realpath(root, resolved_root)) {
        return false;
    }
    
    if (!realpath(path, resolved_path)) {
        /* Path doesn't exist yet or error - check parent directory */
        return false;
    }
    
    /* Check if resolved_path starts with resolved_root */
    size_t root_len = strlen(resolved_root);
    if (strncmp(resolved_root, resolved_path, root_len) != 0) {
        return false;
    }
    
    /* Ensure it's either exact match or has path separator */
    if (resolved_path[root_len] != '\0' && 
        resolved_path[root_len] != '/' && 
        resolved_path[root_len] != '\\') {
        return false;
    }
    
    return true;
}

/**
 * Read entire file into memory
 */
static char *_read_file(const char *filepath, size_t *out_size) {
    if (!filepath || !out_size) {
        return NULL;
    }
    
    FILE *file = fopen(filepath, "rb");
    if (!file) {
        return NULL;
    }
    
    /* Get file size */
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return NULL;
    }
    
    long size = ftell(file);
    if (size < 0 || size > MAX_FILE_SIZE) {
        fclose(file);
        return NULL;
    }
    
    if (fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }
    
    /* Allocate buffer */
    char *buffer = malloc((size_t)size);
    if (!buffer) {
        fclose(file);
        return NULL;
    }
    
    /* Read file */
    size_t bytes_read = fread(buffer, 1, (size_t)size, file);
    fclose(file);
    
    if (bytes_read != (size_t)size) {
        free(buffer);
        return NULL;
    }
    
    *out_size = (size_t)size;
    return buffer;
}

/**
 * Static file serving middleware handler
 */
static bool _static_file_handler(http_request_t *req, http_response_t *res) {
    if (!req || !res || !_static_config.initialized) {
        return true;  /* Continue to next middleware */
    }
    
    /* Only handle GET and HEAD requests */
    if (req->method != HTTP_GET && req->method != HTTP_HEAD) {
        return true;  /* Let other middleware handle it */
    }
    
    /* Build file path */
    const char *url_path = req->path;
    if (!url_path || url_path[0] != '/') {
        return true;  /* Invalid path, continue */
    }
    
    /* Skip leading slash */
    url_path++;
    
    /* If path is empty or ends with '/', use index file */
    bool use_index = (url_path[0] == '\0' || url_path[strlen(url_path) - 1] == '/');
    
    char filepath[PATH_MAX];
    if (use_index) {
        snprintf(filepath, sizeof(filepath), "%s/%s%s",
                _static_config.root_dir, url_path, _static_config.index_file);
    } else {
        snprintf(filepath, sizeof(filepath), "%s/%s",
                _static_config.root_dir, url_path);
    }
    
    /* Check path safety (prevent directory traversal) */
    if (!_is_path_safe(_static_config.root_dir, filepath)) {
        return true;  /* Unsafe path, let other middleware handle it */
    }
    
    /* Get file stats */
    struct stat st;
    if (stat(filepath, &st) != 0) {
        return true;  /* File doesn't exist, let other middleware handle it */
    }
    
    /* Only serve regular files */
    if (!S_ISREG(st.st_mode)) {
        return true;  /* Not a regular file */
    }
    
    /* Check file size */
    if (st.st_size > MAX_FILE_SIZE) {
        http_response_send_text(res, HTTP_BAD_REQUEST, "File too large");
        return false;
    }
    
    /* Generate ETag if enabled */
    char etag[64] = {0};
    if (_static_config.enable_etag) {
        _generate_etag(&st, etag, sizeof(etag));
        
        /* Check If-None-Match header for conditional request */
        const char *if_none_match = http_request_get_header(req, "If-None-Match");
        if (if_none_match && strcmp(if_none_match, etag) == 0) {
            /* ETag matches - return 304 Not Modified */
            res->status = HTTP_NOT_MODIFIED;
            
            /* Set required headers */
            http_response_set_header(res, "ETag", etag);
            
            char cache_control[64];
            snprintf(cache_control, sizeof(cache_control),
                    "public, max-age=%d", _static_config.cache_max_age);
            http_response_set_header(res, "Cache-Control", cache_control);
            
            return false;  /* Stop chain, response is ready */
        }
    }
    
    /* Read file content */
    size_t file_size;
    char *file_content = _read_file(filepath, &file_size);
    if (!file_content) {
        http_response_send_text(res, HTTP_INTERNAL_ERROR, "Failed to read file");
        return false;
    }
    
    /* Set response body directly (for binary files) */
    free(res->body);
    res->body = file_content;
    res->body_length = file_size;
    res->status = HTTP_OK;
    
    /* Set Content-Type header */
    const char *mime_type = _get_mime_type(filepath);
    http_response_set_header(res, "Content-Type", mime_type);
    
    /* Set Content-Length header */
    char content_length[32];
    snprintf(content_length, sizeof(content_length), "%zu", file_size);
    http_response_set_header(res, "Content-Length", content_length);
    
    /* Set Cache-Control header */
    char cache_control[64];
    snprintf(cache_control, sizeof(cache_control),
            "public, max-age=%d", _static_config.cache_max_age);
    http_response_set_header(res, "Cache-Control", cache_control);
    
    /* Set Last-Modified header */
    char last_modified[64];
    _format_http_date(st.st_mtime, last_modified, sizeof(last_modified));
    if (last_modified[0] != '\0') {
        http_response_set_header(res, "Last-Modified", last_modified);
    }
    
    /* Set ETag header if enabled */
    if (_static_config.enable_etag && etag[0] != '\0') {
        http_response_set_header(res, "ETag", etag);
    }
    
    return false;  /* Stop chain, file served */
}

/**
 * Create a static file serving middleware
 */
middleware_fn_t static_file_middleware_create(const static_file_config_t *config) {
    if (!config || !config->root_dir) {
        return NULL;
    }
    
    /* Check if root directory exists */
    struct stat st;
    if (stat(config->root_dir, &st) != 0 || !S_ISDIR(st.st_mode)) {
        fprintf(stderr, "Static file middleware: root directory '%s' does not exist or is not a directory\n",
                config->root_dir);
        return NULL;
    }
    
    /* Clean up any previous configuration */
    static_file_middleware_destroy();
    
    /* Store configuration */
    _static_config.root_dir = strdup(config->root_dir);
    if (!_static_config.root_dir) {
        return NULL;
    }
    
    _static_config.index_file = strdup(config->index_file ? config->index_file : "index.html");
    if (!_static_config.index_file) {
        free(_static_config.root_dir);
        _static_config.root_dir = NULL;
        return NULL;
    }
    
    _static_config.cache_max_age = config->cache_max_age > 0 ? config->cache_max_age : 3600;
    _static_config.enable_etag = config->enable_etag;
    _static_config.initialized = true;
    
    return _static_file_handler;
}

/**
 * Destroy static file middleware resources
 */
void static_file_middleware_destroy(void) {
    if (_static_config.root_dir) {
        free(_static_config.root_dir);
        _static_config.root_dir = NULL;
    }
    
    if (_static_config.index_file) {
        free(_static_config.index_file);
        _static_config.index_file = NULL;
    }
    
    _static_config.initialized = false;
}
