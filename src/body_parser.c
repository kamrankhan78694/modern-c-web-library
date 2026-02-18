/*
 * Modern C Web Library - Body Parser
 * Copyright (c) 2024
 *
 * Handles parsing of HTTP request bodies:
 * - application/x-www-form-urlencoded
 * - multipart/form-data (RFC 7578)
 * - File uploads with size limits and sanitization
 */

#include "weblib.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>

/* Configuration limits */
#define MAX_FORM_FIELDS 256
#define MAX_UPLOAD_SIZE (10 * 1024 * 1024)  /* 10MB */
#define MAX_BOUNDARY_LENGTH 256
#define MAX_HEADER_LINE 4096
#define MAX_FILENAME_LENGTH 255

/* Content-Type constants */
#define CONTENT_TYPE_URLENCODED "application/x-www-form-urlencoded"
#define CONTENT_TYPE_MULTIPART "multipart/form-data"

/* Internal helper function declarations */
static body_parser_data_t *_get_or_create_parser_data(http_request_t *req);
static int _parse_urlencoded(http_request_t *req, body_parser_data_t *data);
static int _parse_multipart(http_request_t *req, body_parser_data_t *data);
static char *_extract_boundary(const char *content_type);
static int _url_decode(const char *src, char *dest, size_t dest_size);
static char *_sanitize_filename(const char *filename);
static void _add_form_field(body_parser_data_t *data, const char *name, const char *value);
static void _add_file(body_parser_data_t *data, http_uploaded_file_t *file);
static int _parse_multipart_part(const uint8_t *part_data, size_t part_size,
                                  body_parser_data_t *data);
static char *_parse_multipart_headers(const uint8_t *data, size_t size,
                                      char **content_type, char **field_name,
                                      char **filename, size_t *header_end);
static char *_extract_quoted_value(const char *str, const char *param);
static void _trim_whitespace(char *str);

/*
 * Get or create body parser data attached to request
 */
static body_parser_data_t *_get_or_create_parser_data(http_request_t *req) {
    if (!req) {
        return NULL;
    }

    /* Check if already exists in user_data */
    if (req->user_data) {
        body_parser_data_t *data = (body_parser_data_t *)req->user_data;
        return data;
    }

    /* Create new parser data */
    body_parser_data_t *data = calloc(1, sizeof(body_parser_data_t));
    if (!data) {
        return NULL;
    }

    data->fields = NULL;
    data->files = NULL;
    data->parsed = false;

    req->user_data = data;
    return data;
}

/*
 * URL decode a percent-encoded string
 * Returns 0 on success, -1 on error
 */
static int _url_decode(const char *src, char *dest, size_t dest_size) {
    if (!src || !dest || dest_size == 0) {
        return -1;
    }

    size_t src_len = strlen(src);
    size_t dest_idx = 0;

    for (size_t i = 0; i < src_len && dest_idx < dest_size - 1; i++) {
        if (src[i] == '%' && i + 2 < src_len) {
            /* Hex decode */
            char hex[3] = {src[i + 1], src[i + 2], '\0'};
            char *endptr;
            long val = strtol(hex, &endptr, 16);
            
            if (endptr == hex + 2) {
                dest[dest_idx++] = (char)val;
                i += 2;
            } else {
                /* Invalid hex sequence, copy literal */
                dest[dest_idx++] = src[i];
            }
        } else if (src[i] == '+') {
            /* Space encoding */
            dest[dest_idx++] = ' ';
        } else {
            dest[dest_idx++] = src[i];
        }
    }

    dest[dest_idx] = '\0';
    return 0;
}

/*
 * Sanitize filename by removing path separators and dangerous characters
 */
static char *_sanitize_filename(const char *filename) {
    if (!filename) {
        return NULL;
    }

    size_t len = strlen(filename);
    if (len == 0 || len > MAX_FILENAME_LENGTH) {
        return NULL;
    }

    char *sanitized = malloc(len + 1);
    if (!sanitized) {
        return NULL;
    }

    size_t j = 0;
    for (size_t i = 0; i < len && j < MAX_FILENAME_LENGTH; i++) {
        char c = filename[i];
        
        /* Skip path separators and null bytes */
        if (c == '/' || c == '\\' || c == '\0') {
            continue;
        }
        
        /* Skip control characters */
        if (c < 32 || c == 127) {
            continue;
        }
        
        sanitized[j++] = c;
    }
    
    sanitized[j] = '\0';
    
    /* If empty after sanitization, return NULL */
    if (j == 0) {
        free(sanitized);
        return NULL;
    }
    
    return sanitized;
}

/*
 * Add a form field to the parser data
 */
static void _add_form_field(body_parser_data_t *data, const char *name, const char *value) {
    if (!data || !name || !value) {
        return;
    }

    http_form_field_t *field = malloc(sizeof(http_form_field_t));
    if (!field) {
        return;
    }

    field->name = strdup(name);
    field->value = strdup(value);
    if (!field->name || !field->value) {
        free(field->name);
        free(field->value);
        free(field);
        return;
    }
    field->next = data->fields;
    data->fields = field;
}

/*
 * Add an uploaded file to the parser data
 */
static void _add_file(body_parser_data_t *data, http_uploaded_file_t *file) {
    if (!data || !file) {
        return;
    }

    file->next = data->files;
    data->files = file;
}

/*
 * Trim leading and trailing whitespace from string (in-place)
 */
static void _trim_whitespace(char *str) {
    if (!str) {
        return;
    }

    /* Trim leading */
    char *start = str;
    while (*start && isspace((unsigned char)*start)) {
        start++;
    }

    if (*start == '\0') {
        str[0] = '\0';
        return;
    }

    /* Trim trailing */
    char *end = start + strlen(start) - 1;
    while (end > start && isspace((unsigned char)*end)) {
        *end = '\0';
        end--;
    }

    /* Move trimmed string to beginning */
    if (start != str) {
        memmove(str, start, strlen(start) + 1);
    }
}

/*
 * Parse application/x-www-form-urlencoded body
 */
static int _parse_urlencoded(http_request_t *req, body_parser_data_t *data) {
    if (!req || !req->body || !data) {
        return -1;
    }

    /* Make a copy since we'll modify it */
    char *body_copy = strdup(req->body);
    if (!body_copy) {
        return -1;
    }

    int field_count = 0;
    char *saveptr;
    char *pair = strtok_r(body_copy, "&", &saveptr);

    while (pair && field_count < MAX_FORM_FIELDS) {
        /* Split key=value */
        char *eq = strchr(pair, '=');
        if (eq) {
            *eq = '\0';
            char *key = pair;
            char *value = eq + 1;

            /* URL decode both key and value */
            char decoded_key[1024];
            char decoded_value[4096];

            if (_url_decode(key, decoded_key, sizeof(decoded_key)) == 0 &&
                _url_decode(value, decoded_value, sizeof(decoded_value)) == 0) {
                _add_form_field(data, decoded_key, decoded_value);
                field_count++;
            }
        }

        pair = strtok_r(NULL, "&", &saveptr);
    }

    free(body_copy);
    return 0;
}

/*
 * Extract boundary string from Content-Type header
 * Returns allocated string or NULL
 */
static char *_extract_boundary(const char *content_type) {
    if (!content_type) {
        return NULL;
    }

    /* Find "boundary=" parameter */
    const char *boundary_start = strstr(content_type, "boundary=");
    if (!boundary_start) {
        return NULL;
    }

    boundary_start += 9; /* strlen("boundary=") */

    /* Handle quoted boundary */
    if (*boundary_start == '"') {
        boundary_start++;
        const char *quote_end = strchr(boundary_start, '"');
        if (!quote_end) {
            return NULL;
        }
        size_t len = quote_end - boundary_start;
        if (len == 0 || len >= MAX_BOUNDARY_LENGTH) {
            return NULL;
        }
        char *boundary = malloc(len + 1);
        if (!boundary) {
            return NULL;
        }
        memcpy(boundary, boundary_start, len);
        boundary[len] = '\0';
        return boundary;
    }

    /* Unquoted boundary - read until semicolon or end */
    const char *boundary_end = boundary_start;
    while (*boundary_end && *boundary_end != ';' && *boundary_end != ' ') {
        boundary_end++;
    }

    size_t len = boundary_end - boundary_start;
    if (len == 0 || len >= MAX_BOUNDARY_LENGTH) {
        return NULL;
    }

    char *boundary = malloc(len + 1);
    if (!boundary) {
        return NULL;
    }
    memcpy(boundary, boundary_start, len);
    boundary[len] = '\0';
    return boundary;
}

/*
 * Extract a quoted parameter value from a header string.
 * Ensures word-boundary matching to avoid matching "name=" inside "filename=".
 */
static char *_extract_quoted_value(const char *str, const char *param) {
    if (!str || !param) {
        return NULL;
    }

    char search[128];
    snprintf(search, sizeof(search), "%s=", param);
    size_t search_len = strlen(search);
    
    const char *start = str;
    while ((start = strstr(start, search)) != NULL) {
        /* Verify word boundary: must be at start or preceded by space/semicolon */
        if (start != str) {
            char prev = *(start - 1);
            if (prev != ' ' && prev != ';' && prev != '\t') {
                start += search_len;
                continue;
            }
        }
        break;
    }
    if (!start) {
        return NULL;
    }
    
    start += search_len;
    
    /* Handle quoted value */
    if (*start == '"') {
        start++;
        const char *end = strchr(start, '"');
        if (!end) {
            return NULL;
        }
        size_t len = end - start;
        char *value = malloc(len + 1);
        if (!value) {
            return NULL;
        }
        memcpy(value, start, len);
        value[len] = '\0';
        return value;
    }
    
    /* Unquoted value - read until semicolon, space, or end */
    const char *end = start;
    while (*end && *end != ';' && *end != ' ' && *end != '\r' && *end != '\n') {
        end++;
    }
    
    size_t len = end - start;
    if (len == 0) {
        return NULL;
    }
    
    char *value = malloc(len + 1);
    if (!value) {
        return NULL;
    }
    memcpy(value, start, len);
    value[len] = '\0';
    return value;
}

/*
 * Parse headers from a multipart part
 * Returns allocated Content-Disposition value, sets other parameters
 */
static char *_parse_multipart_headers(const uint8_t *data, size_t size,
                                      char **content_type, char **field_name,
                                      char **filename, size_t *header_end) {
    if (!data || !header_end) {
        return NULL;
    }

    *header_end = 0;
    if (content_type) *content_type = NULL;
    if (field_name) *field_name = NULL;
    if (filename) *filename = NULL;

    /* Find end of headers (double CRLF) */
    if (size < 4) {
        return NULL;
    }
    for (size_t i = 0; i < size - 3; i++) {
        if (data[i] == '\r' && data[i+1] == '\n' &&
            data[i+2] == '\r' && data[i+3] == '\n') {
            *header_end = i + 4;
            break;
        }
        /* Also handle LF only */
        if (i < size - 1 && data[i] == '\n' && data[i+1] == '\n') {
            *header_end = i + 2;
            break;
        }
    }

    if (*header_end == 0 || *header_end >= size) {
        return NULL;
    }

    /* Copy headers to null-terminated string for parsing */
    char *headers = malloc(*header_end + 1);
    if (!headers) {
        return NULL;
    }
    memcpy(headers, data, *header_end);
    headers[*header_end] = '\0';

    /* Parse line by line */
    char *line = headers;
    char *content_disposition = NULL;
    
    while (line && *line) {
        char *line_end = strstr(line, "\r\n");
        if (!line_end) {
            line_end = strchr(line, '\n');
        }
        
        if (line_end) {
            *line_end = '\0';
        }

        /* Check for Content-Disposition header */
        if (strncasecmp(line, "Content-Disposition:", 20) == 0) {
            char *value = line + 20;
            _trim_whitespace(value);
            content_disposition = strdup(value);
            
            /* Extract name parameter */
            if (field_name) {
                *field_name = _extract_quoted_value(value, "name");
            }
            
            /* Extract filename parameter */
            if (filename) {
                *filename = _extract_quoted_value(value, "filename");
            }
        }
        /* Check for Content-Type header */
        else if (strncasecmp(line, "Content-Type:", 13) == 0) {
            if (content_type) {
                char *value = line + 13;
                _trim_whitespace(value);
                *content_type = strdup(value);
            }
        }

        if (!line_end) {
            break;
        }
        
        line = line_end + 1;
        if (*line == '\n') {
            line++;
        }
    }

    free(headers);
    return content_disposition;
}

/*
 * Parse a single multipart part (headers + body)
 */
static int _parse_multipart_part(const uint8_t *part_data, size_t part_size,
                                  body_parser_data_t *data) {
    if (!part_data || part_size == 0 || !data) {
        return -1;
    }

    char *content_type = NULL;
    char *field_name = NULL;
    char *filename = NULL;
    size_t header_end = 0;

    /* Parse headers */
    char *disposition = _parse_multipart_headers(part_data, part_size,
                                                 &content_type, &field_name,
                                                 &filename, &header_end);

    if (!disposition || !field_name || header_end == 0 || header_end >= part_size) {
        free(disposition);
        free(content_type);
        free(field_name);
        free(filename);
        return -1;
    }

    /* Get body data */
    const uint8_t *body_data = part_data + header_end;
    size_t body_size = part_size - header_end;

    /* Check if this is a file upload */
    if (filename && strlen(filename) > 0) {
        /* File upload */
        if (body_size > MAX_UPLOAD_SIZE) {
            free(disposition);
            free(content_type);
            free(field_name);
            free(filename);
            return -1;
        }

        /* Sanitize filename */
        char *safe_filename = _sanitize_filename(filename);
        free(filename);
        
        if (!safe_filename) {
            free(disposition);
            free(content_type);
            free(field_name);
            return -1;
        }

        /* Create uploaded file structure */
        http_uploaded_file_t *file = calloc(1, sizeof(http_uploaded_file_t));
        if (!file) {
            free(disposition);
            free(content_type);
            free(field_name);
            free(safe_filename);
            return -1;
        }

        file->field_name = field_name;
        file->filename = safe_filename;
        file->content_type = content_type ? content_type : strdup("application/octet-stream");
        file->size = body_size;
        
        /* Copy file data */
        if (body_size > 0) {
            file->data = malloc(body_size);
            if (!file->data) {
                free(file->field_name);
                free(file->filename);
                free(file->content_type);
                free(file);
                free(disposition);
                return -1;
            }
            memcpy(file->data, body_data, body_size);
        } else {
            file->data = NULL;
        }

        _add_file(data, file);
    } else {
        /* Regular form field */
        free(filename);
        free(content_type);
        
        /* Convert body to null-terminated string */
        char *value = malloc(body_size + 1);
        if (!value) {
            free(disposition);
            free(field_name);
            return -1;
        }
        memcpy(value, body_data, body_size);
        value[body_size] = '\0';

        _add_form_field(data, field_name, value);
        free(value);
        free(field_name);
    }

    free(disposition);
    return 0;
}

/*
 * Parse multipart/form-data body
 */
static int _parse_multipart(http_request_t *req, body_parser_data_t *data) {
    if (!req || !req->body || !data) {
        return -1;
    }

    /* Get Content-Type header */
    const char *content_type = http_request_get_header(req, "Content-Type");
    if (!content_type) {
        return -1;
    }

    /* Extract boundary */
    char *boundary = _extract_boundary(content_type);
    if (!boundary) {
        return -1;
    }

    /* Construct full boundary markers */
    char *boundary_start = malloc(strlen(boundary) + 5);
    char *boundary_end = malloc(strlen(boundary) + 7);
    if (!boundary_start || !boundary_end) {
        free(boundary);
        free(boundary_start);
        free(boundary_end);
        return -1;
    }

    snprintf(boundary_start, strlen(boundary) + 5, "--%s", boundary);
    snprintf(boundary_end, strlen(boundary) + 7, "--%s--", boundary);

    size_t boundary_start_len = strlen(boundary_start);
    size_t boundary_end_len = strlen(boundary_end);

    const uint8_t *body = (const uint8_t *)req->body;
    size_t body_len = req->body_length;

    /* Find parts between boundaries */
    size_t pos = 0;
    int part_count = 0;

    /* Skip preamble (before first boundary) */
    while (pos < body_len) {
        if (pos + boundary_start_len <= body_len &&
            memcmp(body + pos, boundary_start, boundary_start_len) == 0) {
            pos += boundary_start_len;
            /* Skip CRLF after boundary */
            if (pos < body_len && body[pos] == '\r') pos++;
            if (pos < body_len && body[pos] == '\n') pos++;
            break;
        }
        pos++;
    }

    /* Parse each part */
    while (pos < body_len && part_count < MAX_FORM_FIELDS) {
        /* Find next boundary */
        size_t part_start = pos;
        size_t part_end = part_start;
        bool found_end = false;

        for (size_t i = part_start; i < body_len; i++) {
            /* Check for boundary_end (final boundary) */
            if (i + boundary_end_len <= body_len &&
                memcmp(body + i, boundary_end, boundary_end_len) == 0) {
                /* Remove trailing CRLF before boundary */
                part_end = i;
                if (part_end >= 2 && body[part_end - 2] == '\r' && body[part_end - 1] == '\n') {
                    part_end -= 2;
                } else if (part_end >= 1 && body[part_end - 1] == '\n') {
                    part_end -= 1;
                }
                found_end = true;
                break;
            }
            /* Check for boundary_start (next part) */
            if (i + boundary_start_len <= body_len &&
                memcmp(body + i, boundary_start, boundary_start_len) == 0) {
                /* Remove trailing CRLF before boundary */
                part_end = i;
                if (part_end >= 2 && body[part_end - 2] == '\r' && body[part_end - 1] == '\n') {
                    part_end -= 2;
                } else if (part_end >= 1 && body[part_end - 1] == '\n') {
                    part_end -= 1;
                }
                pos = i + boundary_start_len;
                /* Skip CRLF after boundary */
                if (pos < body_len && body[pos] == '\r') pos++;
                if (pos < body_len && body[pos] == '\n') pos++;
                break;
            }
        }

        if (part_end > part_start) {
            size_t part_size = part_end - part_start;
            _parse_multipart_part(body + part_start, part_size, data);
            part_count++;
        }

        if (found_end || part_end == part_start) {
            break;
        }
    }

    free(boundary);
    free(boundary_start);
    free(boundary_end);

    return 0;
}

/*
 * Parse the request body based on Content-Type header
 */
int http_request_parse_body(http_request_t *req) {
    if (!req) {
        return -1;
    }

    /* Get or create parser data */
    body_parser_data_t *data = _get_or_create_parser_data(req);
    if (!data) {
        return -1;
    }

    /* Already parsed? */
    if (data->parsed) {
        return 0;
    }

    /* No body to parse */
    if (!req->body || req->body_length == 0) {
        data->parsed = true;
        return 0;
    }

    /* Get Content-Type header */
    const char *content_type = http_request_get_header(req, "Content-Type");
    if (!content_type) {
        /* No Content-Type, assume raw body */
        data->parsed = true;
        return 0;
    }

    int result = 0;

    /* Parse based on Content-Type */
    if (strncmp(content_type, CONTENT_TYPE_URLENCODED, strlen(CONTENT_TYPE_URLENCODED)) == 0) {
        result = _parse_urlencoded(req, data);
    } else if (strncmp(content_type, CONTENT_TYPE_MULTIPART, strlen(CONTENT_TYPE_MULTIPART)) == 0) {
        result = _parse_multipart(req, data);
    } else {
        /* Unsupported Content-Type, but not an error */
        result = 0;
    }

    data->parsed = true;
    return result;
}

/*
 * Get a form field value
 */
const char *http_request_get_form_field(http_request_t *req, const char *name) {
    if (!req || !name) {
        return NULL;
    }

    /* Ensure body is parsed */
    body_parser_data_t *data = (body_parser_data_t *)req->user_data;
    if (!data || !data->parsed) {
        if (http_request_parse_body(req) != 0) {
            return NULL;
        }
        data = (body_parser_data_t *)req->user_data;
        if (!data) {
            return NULL;
        }
    }

    /* Search for field */
    http_form_field_t *field = data->fields;
    while (field) {
        if (field->name && strcmp(field->name, name) == 0) {
            return field->value;
        }
        field = field->next;
    }

    return NULL;
}

/*
 * Get an uploaded file
 */
http_uploaded_file_t *http_request_get_file(http_request_t *req, const char *field_name) {
    if (!req || !field_name) {
        return NULL;
    }

    /* Ensure body is parsed */
    body_parser_data_t *data = (body_parser_data_t *)req->user_data;
    if (!data || !data->parsed) {
        if (http_request_parse_body(req) != 0) {
            return NULL;
        }
        data = (body_parser_data_t *)req->user_data;
        if (!data) {
            return NULL;
        }
    }

    /* Search for file */
    http_uploaded_file_t *file = data->files;
    while (file) {
        if (file->field_name && strcmp(file->field_name, field_name) == 0) {
            return file;
        }
        file = file->next;
    }

    return NULL;
}

/*
 * Free body parser resources
 */
void body_parser_data_free(body_parser_data_t *data) {
    if (!data) {
        return;
    }

    /* Free form fields */
    http_form_field_t *field = data->fields;
    while (field) {
        http_form_field_t *next = field->next;
        free(field->name);
        free(field->value);
        free(field);
        field = next;
    }

    /* Free uploaded files */
    http_uploaded_file_t *file = data->files;
    while (file) {
        http_uploaded_file_t *next = file->next;
        free(file->field_name);
        free(file->filename);
        free(file->content_type);
        free(file->data);
        free(file);
        file = next;
    }

    free(data);
}
