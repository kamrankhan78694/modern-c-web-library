#include "weblib.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <limits.h>

#ifdef _WIN32
#define strcasecmp _stricmp
#endif

/* MIME types mapping */
typedef struct {
    const char *extension;
    const char *mime_type;
} mime_type_entry_t;

static const mime_type_entry_t mime_types[] = {
    {".html", "text/html"},
    {".htm", "text/html"},
    {".css", "text/css"},
    {".js", "application/javascript"},
    {".json", "application/json"},
    {".xml", "application/xml"},
    {".txt", "text/plain"},
    {".md", "text/markdown"},
    
    /* Images */
    {".png", "image/png"},
    {".jpg", "image/jpeg"},
    {".jpeg", "image/jpeg"},
    {".gif", "image/gif"},
    {".svg", "image/svg+xml"},
    {".ico", "image/x-icon"},
    {".webp", "image/webp"},
    
    /* Fonts */
    {".woff", "font/woff"},
    {".woff2", "font/woff2"},
    {".ttf", "font/ttf"},
    {".otf", "font/otf"},
    
    /* Other common types */
    {".pdf", "application/pdf"},
    {".zip", "application/zip"},
    {".tar", "application/x-tar"},
    {".gz", "application/gzip"},
    
    {NULL, NULL} /* Sentinel */
};

/* Get MIME type from file extension */
static const char *get_mime_type(const char *filepath) {
    const char *dot = strrchr(filepath, '.');
    if (!dot) {
        return "application/octet-stream";
    }
    
    for (size_t i = 0; mime_types[i].extension != NULL; i++) {
        if (strcasecmp(dot, mime_types[i].extension) == 0) {
            return mime_types[i].mime_type;
        }
    }
    
    return "application/octet-stream";
}

/* Read entire file into memory */
static char *read_file(const char *filepath, size_t *file_size) {
    FILE *file = fopen(filepath, "rb");
    if (!file) {
        return NULL;
    }
    
    /* Get file size */
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    if (size < 0) {
        fclose(file);
        return NULL;
    }
    
    /* Allocate buffer */
    char *buffer = (char *)malloc(size);
    if (!buffer) {
        fclose(file);
        return NULL;
    }
    
    /* Read file */
    size_t bytes_read = fread(buffer, 1, size, file);
    fclose(file);
    
    if (bytes_read != (size_t)size) {
        free(buffer);
        return NULL;
    }
    
    *file_size = size;
    return buffer;
}

/* Check if path is safe (no directory traversal) */
static bool is_safe_path(const char *root_dir, const char *filepath) {
    char real_root[PATH_MAX];
    char real_path[PATH_MAX];
    
    /* Get absolute paths */
    if (!realpath(root_dir, real_root)) {
        return false;
    }
    
    if (!realpath(filepath, real_path)) {
        /* File doesn't exist yet or path is invalid */
        return false;
    }
    
    /* Check if filepath is under root_dir */
    size_t root_len = strlen(real_root);
    if (strncmp(real_root, real_path, root_len) != 0) {
        return false;
    }
    
    return true;
}

/* Send file response */
int http_response_send_file(http_response_t *res, const char *filepath) {
    if (!res || !filepath) {
        return -1;
    }
    
    /* Check if file exists */
    struct stat st;
    if (stat(filepath, &st) != 0) {
        http_response_send_text(res, HTTP_NOT_FOUND, "File Not Found");
        return -1;
    }
    
    /* Check if it's a regular file */
    if (!S_ISREG(st.st_mode)) {
        http_response_send_text(res, HTTP_FORBIDDEN, "Not a Regular File");
        return -1;
    }
    
    /* Read file */
    size_t file_size;
    char *content = read_file(filepath, &file_size);
    if (!content) {
        http_response_send_text(res, HTTP_INTERNAL_ERROR, "Failed to Read File");
        return -1;
    }
    
    /* Set response */
    res->status = HTTP_OK;
    res->body = content;
    res->body_length = file_size;
    
    /* Set Content-Type header */
    const char *mime_type = get_mime_type(filepath);
    http_response_set_header(res, "Content-Type", mime_type);
    
    return 0;
}

/* Static file handler */
bool static_file_handler(http_request_t *req, http_response_t *res, const char *root_dir) {
    if (!req || !res || !root_dir) {
        return true;
    }
    
    /* Only handle GET and HEAD requests */
    if (req->method != HTTP_GET && req->method != HTTP_HEAD) {
        return true;
    }
    
    /* Check path length to prevent overflow */
    size_t root_len = strlen(root_dir);
    size_t path_len = strlen(req->path);
    if (root_len + path_len + 20 > PATH_MAX) { /* +20 for /index.html and null */
        return true; /* Path too long, skip */
    }
    
    /* Build file path */
    char filepath[PATH_MAX];
    int written = snprintf(filepath, PATH_MAX, "%s%s", root_dir, req->path);
    if (written < 0 || written >= PATH_MAX) {
        return true; /* Path truncated, skip */
    }
    
    /* If path ends with /, try index.html */
    if (req->path[path_len - 1] == '/') {
        written = snprintf(filepath, PATH_MAX, "%s%sindex.html", root_dir, req->path);
        if (written < 0 || written >= PATH_MAX) {
            return true; /* Path truncated, skip */
        }
    }
    
    /* Check if file exists */
    struct stat st;
    if (stat(filepath, &st) != 0) {
        return true; /* Not found, continue to next handler */
    }
    
    /* Check if it's a directory */
    if (S_ISDIR(st.st_mode)) {
        /* Try index.html in directory */
        char index_path[PATH_MAX];
        written = snprintf(index_path, PATH_MAX, "%s/index.html", filepath);
        if (written < 0 || written >= PATH_MAX) {
            return true; /* Path truncated, skip */
        }
        
        if (stat(index_path, &st) == 0 && S_ISREG(st.st_mode)) {
            strncpy(filepath, index_path, PATH_MAX - 1);
            filepath[PATH_MAX - 1] = '\0';
        } else {
            return true; /* Directory without index.html, continue */
        }
    }
    
    /* Security check - prevent directory traversal */
    if (!is_safe_path(root_dir, filepath)) {
        http_response_send_text(res, HTTP_FORBIDDEN, "Forbidden");
        return false;
    }
    
    /* Send file */
    if (http_response_send_file(res, filepath) == 0) {
        return false; /* File served, stop processing */
    }
    
    return true; /* Error serving file, continue */
}
