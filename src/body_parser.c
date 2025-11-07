#include "weblib.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* Internal structures for form data storage */
typedef struct form_field {
    char *name;
    char *value;
    char *filename;      /* For file uploads */
    char *content_type;  /* For file uploads */
    size_t value_size;   /* Actual size of value data */
    struct form_field *next;
} form_field_t;

typedef struct {
    form_field_t *fields;
} form_data_t;

/* Forward declarations */
static form_data_t *parse_urlencoded(const char *body, size_t length);
static form_data_t *parse_multipart(const char *body, size_t length, const char *boundary);
static char *url_decode(const char *str);
static void form_data_free(form_data_t *data);
static char *extract_boundary(const char *content_type);
static int hex_to_int(char c);

/* Parse request body based on Content-Type */
void *http_request_parse_body(http_request_t *req, const char *content_type) {
    if (!req || !req->body || req->body_length == 0 || !content_type) {
        return NULL;
    }
    
    /* Check if already parsed */
    if (req->form_data) {
        return req->form_data;
    }
    
    /* URL-encoded form data */
    if (strstr(content_type, "application/x-www-form-urlencoded") != NULL) {
        req->form_data = parse_urlencoded(req->body, req->body_length);
        return req->form_data;
    }
    
    /* Multipart form data */
    if (strstr(content_type, "multipart/form-data") != NULL) {
        char *boundary = extract_boundary(content_type);
        if (boundary) {
            req->form_data = parse_multipart(req->body, req->body_length, boundary);
            free(boundary);
            return req->form_data;
        }
    }
    
    return NULL;
}

/* Get form field value */
const char *http_request_get_form_field(http_request_t *req, const char *key) {
    if (!req || !key || !req->form_data) {
        return NULL;
    }
    
    form_data_t *data = (form_data_t *)req->form_data;
    form_field_t *field = data->fields;
    
    while (field) {
        if (field->name && strcmp(field->name, key) == 0) {
            return field->value;
        }
        field = field->next;
    }
    
    return NULL;
}

/* Get uploaded file */
const char *http_request_get_file(http_request_t *req, const char *field_name,
                                   const char **filename, const char **content_type,
                                   size_t *size) {
    if (!req || !field_name || !req->form_data) {
        return NULL;
    }
    
    form_data_t *data = (form_data_t *)req->form_data;
    form_field_t *field = data->fields;
    
    while (field) {
        if (field->name && strcmp(field->name, field_name) == 0 && field->filename) {
            if (filename) *filename = field->filename;
            if (content_type) *content_type = field->content_type;
            if (size) *size = field->value_size;
            return field->value;
        }
        field = field->next;
    }
    
    return NULL;
}

/* Free form data */
void http_request_free_form_data(http_request_t *req) {
    if (req && req->form_data) {
        form_data_free((form_data_t *)req->form_data);
        req->form_data = NULL;
    }
}

/* Parse URL-encoded form data */
static form_data_t *parse_urlencoded(const char *body, size_t length) {
    if (!body || length == 0) {
        return NULL;
    }
    
    form_data_t *data = (form_data_t *)calloc(1, sizeof(form_data_t));
    if (!data) {
        return NULL;
    }
    
    /* Make a copy of the body to parse */
    char *body_copy = (char *)malloc(length + 1);
    if (!body_copy) {
        free(data);
        return NULL;
    }
    memcpy(body_copy, body, length);
    body_copy[length] = '\0';
    
    /* Parse key=value pairs separated by & */
    char *saveptr = NULL;
    char *pair = strtok_r(body_copy, "&", &saveptr);
    form_field_t *last_field = NULL;
    
    while (pair) {
        char *equals = strchr(pair, '=');
        if (equals) {
            *equals = '\0';
            char *key = pair;
            char *value = equals + 1;
            
            /* URL decode key and value */
            char *decoded_key = url_decode(key);
            char *decoded_value = url_decode(value);
            
            if (decoded_key && decoded_value) {
                /* Create new field */
                form_field_t *field = (form_field_t *)calloc(1, sizeof(form_field_t));
                if (field) {
                    field->name = decoded_key;
                    field->value = decoded_value;
                    field->value_size = strlen(decoded_value);
                    field->next = NULL;
                    
                    if (!data->fields) {
                        data->fields = field;
                    } else {
                        last_field->next = field;
                    }
                    last_field = field;
                } else {
                    free(decoded_key);
                    free(decoded_value);
                }
            }
        }
        
        pair = strtok_r(NULL, "&", &saveptr);
    }
    
    free(body_copy);
    return data;
}

/* Parse multipart form data */
static form_data_t *parse_multipart(const char *body, size_t length, const char *boundary) {
    if (!body || length == 0 || !boundary) {
        return NULL;
    }
    
    form_data_t *data = (form_data_t *)calloc(1, sizeof(form_data_t));
    if (!data) {
        return NULL;
    }
    
    /* Create boundary markers */
    char *boundary_start = (char *)malloc(strlen(boundary) + 3);
    char *boundary_end = (char *)malloc(strlen(boundary) + 5);
    if (!boundary_start || !boundary_end) {
        free(boundary_start);
        free(boundary_end);
        free(data);
        return NULL;
    }
    sprintf(boundary_start, "--%s", boundary);
    sprintf(boundary_end, "--%s--", boundary);
    
    size_t boundary_len = strlen(boundary_start);
    const char *pos = body;
    const char *end = body + length;
    form_field_t *last_field = NULL;
    
    /* Find first boundary */
    pos = strstr(pos, boundary_start);
    if (!pos) {
        free(boundary_start);
        free(boundary_end);
        free(data);
        return NULL;
    }
    
    while (pos && pos < end) {
        /* Skip boundary and CRLF */
        pos += boundary_len;
        if (pos >= end) break;
        
        /* Skip CRLF after boundary */
        if (*pos == '\r') pos++;
        if (*pos == '\n') pos++;
        
        /* Check if this is the end boundary */
        if (strncmp(pos - boundary_len, boundary_end, strlen(boundary_end)) == 0) {
            break;
        }
        
        /* Parse headers */
        char *field_name = NULL;
        char *filename = NULL;
        char *content_type_val = NULL;
        const char *headers_end = strstr(pos, "\r\n\r\n");
        if (!headers_end) {
            headers_end = strstr(pos, "\n\n");
        }
        
        if (headers_end) {
            const char *header_line = pos;
            
            while (header_line < headers_end) {
                const char *line_end = strstr(header_line, "\r\n");
                if (!line_end || line_end > headers_end) {
                    line_end = strstr(header_line, "\n");
                }
                if (!line_end || line_end > headers_end) {
                    line_end = headers_end;
                }
                
                /* Parse Content-Disposition header */
                if (strncmp(header_line, "Content-Disposition:", 20) == 0) {
                    /* Extract name */
                    const char *name_start = strstr(header_line, "name=\"");
                    if (name_start && name_start < line_end) {
                        name_start += 6;
                        const char *name_end = strchr(name_start, '"');
                        if (name_end && name_end < line_end) {
                            size_t name_len = name_end - name_start;
                            field_name = (char *)malloc(name_len + 1);
                            if (field_name) {
                                memcpy(field_name, name_start, name_len);
                                field_name[name_len] = '\0';
                            }
                        }
                    }
                    
                    /* Extract filename if present */
                    const char *filename_start = strstr(header_line, "filename=\"");
                    if (filename_start && filename_start < line_end) {
                        filename_start += 10;
                        const char *filename_end = strchr(filename_start, '"');
                        if (filename_end && filename_end < line_end) {
                            size_t filename_len = filename_end - filename_start;
                            filename = (char *)malloc(filename_len + 1);
                            if (filename) {
                                memcpy(filename, filename_start, filename_len);
                                filename[filename_len] = '\0';
                            }
                        }
                    }
                }
                
                /* Parse Content-Type header */
                if (strncmp(header_line, "Content-Type:", 13) == 0) {
                    const char *type_start = header_line + 13;
                    while (*type_start == ' ' || *type_start == '\t') type_start++;
                    
                    size_t type_len = line_end - type_start;
                    content_type_val = (char *)malloc(type_len + 1);
                    if (content_type_val) {
                        memcpy(content_type_val, type_start, type_len);
                        content_type_val[type_len] = '\0';
                        /* Remove trailing whitespace */
                        char *end_trim = content_type_val + type_len - 1;
                        while (end_trim > content_type_val && (*end_trim == '\r' || *end_trim == '\n' || *end_trim == ' ')) {
                            *end_trim = '\0';
                            end_trim--;
                        }
                    }
                }
                
                header_line = line_end;
                if (*header_line == '\r') header_line++;
                if (*header_line == '\n') header_line++;
            }
            
            /* Skip to content */
            pos = headers_end;
            if (*pos == '\r') pos++;
            if (*pos == '\n') pos++;
            if (*pos == '\r') pos++;
            if (*pos == '\n') pos++;
            
            /* Find next boundary */
            const char *next_boundary = strstr(pos, boundary_start);
            if (!next_boundary) {
                next_boundary = end;
            }
            
            /* Extract value */
            size_t value_len = next_boundary - pos;
            /* Remove trailing CRLF before boundary */
            if (value_len >= 2 && pos[value_len - 2] == '\r' && pos[value_len - 1] == '\n') {
                value_len -= 2;
            } else if (value_len >= 1 && pos[value_len - 1] == '\n') {
                value_len -= 1;
            }
            
            if (field_name) {
                form_field_t *field = (form_field_t *)calloc(1, sizeof(form_field_t));
                if (field) {
                    field->name = field_name;
                    field->filename = filename;
                    field->content_type = content_type_val;
                    field->value = (char *)malloc(value_len + 1);
                    if (field->value) {
                        memcpy(field->value, pos, value_len);
                        field->value[value_len] = '\0';
                        field->value_size = value_len;
                    }
                    field->next = NULL;
                    
                    if (!data->fields) {
                        data->fields = field;
                    } else {
                        last_field->next = field;
                    }
                    last_field = field;
                } else {
                    free(field_name);
                    free(filename);
                    free(content_type_val);
                }
            } else {
                free(filename);
                free(content_type_val);
            }
            
            pos = next_boundary;
        } else {
            break;
        }
    }
    
    free(boundary_start);
    free(boundary_end);
    return data;
}

/* URL decode helper */
static char *url_decode(const char *str) {
    if (!str) {
        return NULL;
    }
    
    size_t len = strlen(str);
    char *decoded = (char *)malloc(len + 1);
    if (!decoded) {
        return NULL;
    }
    
    size_t i = 0, j = 0;
    while (i < len) {
        if (str[i] == '%' && i + 2 < len && isxdigit(str[i + 1]) && isxdigit(str[i + 2])) {
            int high = hex_to_int(str[i + 1]);
            int low = hex_to_int(str[i + 2]);
            decoded[j++] = (char)((high << 4) | low);
            i += 3;
        } else if (str[i] == '+') {
            decoded[j++] = ' ';
            i++;
        } else {
            decoded[j++] = str[i++];
        }
    }
    decoded[j] = '\0';
    
    return decoded;
}

/* Convert hex character to integer */
static int hex_to_int(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 0;
}

/* Extract boundary from Content-Type header */
static char *extract_boundary(const char *content_type) {
    if (!content_type) {
        return NULL;
    }
    
    const char *boundary_start = strstr(content_type, "boundary=");
    if (!boundary_start) {
        return NULL;
    }
    
    boundary_start += 9; /* Skip "boundary=" */
    
    /* Handle quoted boundary */
    if (*boundary_start == '"') {
        boundary_start++;
        const char *boundary_end = strchr(boundary_start, '"');
        if (!boundary_end) {
            return NULL;
        }
        size_t len = boundary_end - boundary_start;
        char *boundary = (char *)malloc(len + 1);
        if (boundary) {
            memcpy(boundary, boundary_start, len);
            boundary[len] = '\0';
        }
        return boundary;
    } else {
        /* Unquoted boundary - read until ; or end */
        const char *boundary_end = boundary_start;
        while (*boundary_end && *boundary_end != ';' && *boundary_end != ' ' && *boundary_end != '\r' && *boundary_end != '\n') {
            boundary_end++;
        }
        size_t len = boundary_end - boundary_start;
        char *boundary = (char *)malloc(len + 1);
        if (boundary) {
            memcpy(boundary, boundary_start, len);
            boundary[len] = '\0';
        }
        return boundary;
    }
}

/* Free form data */
static void form_data_free(form_data_t *data) {
    if (!data) {
        return;
    }
    
    form_field_t *field = data->fields;
    while (field) {
        form_field_t *next = field->next;
        free(field->name);
        free(field->value);
        free(field->filename);
        free(field->content_type);
        free(field);
        field = next;
    }
    
    free(data);
}
