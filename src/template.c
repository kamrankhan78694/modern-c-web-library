/* Enable POSIX features for strdup */
#define _POSIX_C_SOURCE 200809L

#include "weblib.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define INITIAL_BUFFER_SIZE 4096
#define MAX_VAR_NAME 256

/* Template context for variable storage */
struct template_context {
    void *variables;  /* Hash map of variables */
    size_t var_count;
};

/* Simple hash map entry */
typedef struct var_entry {
    char *key;
    char *value;
    struct var_entry *next;
} var_entry_t;

#define HASH_SIZE 256

/* Simple hash function */
static unsigned int hash(const char *str) {
    unsigned int hash = 5381;
    int c;
    while ((c = *str++)) {
        hash = ((hash << 5) + hash) + c;
    }
    return hash % HASH_SIZE;
}

/* Create template context */
template_context_t *template_context_create(void) {
    template_context_t *ctx = (template_context_t *)calloc(1, sizeof(template_context_t));
    if (!ctx) {
        return NULL;
    }
    
    ctx->variables = calloc(HASH_SIZE, sizeof(var_entry_t *));
    if (!ctx->variables) {
        free(ctx);
        return NULL;
    }
    
    ctx->var_count = 0;
    return ctx;
}

/* Set variable in context */
void template_context_set(template_context_t *ctx, const char *key, const char *value) {
    if (!ctx || !key || !value || !ctx->variables) {
        return;
    }
    
    var_entry_t **map = (var_entry_t **)ctx->variables;
    unsigned int idx = hash(key);
    
    /* Check if key already exists */
    var_entry_t *entry = map[idx];
    while (entry) {
        if (strcmp(entry->key, key) == 0) {
            /* Update existing value */
            free(entry->value);
            entry->value = strdup(value);
            return;
        }
        entry = entry->next;
    }
    
    /* Create new entry */
    var_entry_t *new_entry = (var_entry_t *)malloc(sizeof(var_entry_t));
    if (!new_entry) {
        return;
    }
    
    new_entry->key = strdup(key);
    new_entry->value = strdup(value);
    new_entry->next = map[idx];
    map[idx] = new_entry;
    ctx->var_count++;
}

/* Get variable from context */
const char *template_context_get(template_context_t *ctx, const char *key) {
    if (!ctx || !key || !ctx->variables) {
        return NULL;
    }
    
    var_entry_t **map = (var_entry_t **)ctx->variables;
    unsigned int idx = hash(key);
    
    var_entry_t *entry = map[idx];
    while (entry) {
        if (strcmp(entry->key, key) == 0) {
            return entry->value;
        }
        entry = entry->next;
    }
    
    return NULL;
}

/* Destroy template context */
void template_context_destroy(template_context_t *ctx) {
    if (!ctx) {
        return;
    }
    
    if (ctx->variables) {
        var_entry_t **map = (var_entry_t **)ctx->variables;
        for (int i = 0; i < HASH_SIZE; i++) {
            var_entry_t *entry = map[i];
            while (entry) {
                var_entry_t *next = entry->next;
                free(entry->key);
                free(entry->value);
                free(entry);
                entry = next;
            }
        }
        free(ctx->variables);
    }
    
    free(ctx);
}

/* Dynamic string buffer for building output */
typedef struct {
    char *data;
    size_t size;
    size_t capacity;
} string_buffer_t;

static string_buffer_t *buffer_create(void) {
    string_buffer_t *buf = (string_buffer_t *)malloc(sizeof(string_buffer_t));
    if (!buf) {
        return NULL;
    }
    
    buf->data = (char *)malloc(INITIAL_BUFFER_SIZE);
    if (!buf->data) {
        free(buf);
        return NULL;
    }
    
    buf->data[0] = '\0';
    buf->size = 0;
    buf->capacity = INITIAL_BUFFER_SIZE;
    
    return buf;
}

static void buffer_append(string_buffer_t *buf, const char *str, size_t len) {
    if (!buf || !str) {
        return;
    }
    
    if (buf->size + len + 1 > buf->capacity) {
        size_t new_capacity = buf->capacity * 2;
        while (new_capacity < buf->size + len + 1) {
            new_capacity *= 2;
        }
        
        char *new_data = (char *)realloc(buf->data, new_capacity);
        if (!new_data) {
            return;
        }
        
        buf->data = new_data;
        buf->capacity = new_capacity;
    }
    
    memcpy(buf->data + buf->size, str, len);
    buf->size += len;
    buf->data[buf->size] = '\0';
}

static void buffer_append_str(string_buffer_t *buf, const char *str) {
    if (str) {
        buffer_append(buf, str, strlen(str));
    }
}

static char *buffer_to_string(string_buffer_t *buf) {
    if (!buf) {
        return NULL;
    }
    
    char *result = buf->data;
    free(buf);
    return result;
}

/* Extract variable name from template syntax {{ var }} */
static char *extract_var_name(const char *start, const char *end) {
    /* Skip opening {{ */
    start += 2;
    
    /* Skip whitespace */
    while (start < end && isspace(*start)) {
        start++;
    }
    
    /* Find end (before }}) */
    const char *var_end = end - 2;
    while (var_end > start && isspace(*(var_end - 1))) {
        var_end--;
    }
    
    if (var_end <= start) {
        return NULL;
    }
    
    size_t len = var_end - start;
    char *name = (char *)malloc(len + 1);
    if (!name) {
        return NULL;
    }
    
    memcpy(name, start, len);
    name[len] = '\0';
    
    return name;
}

/* Render template with context */
char *template_render(const char *template_str, template_context_t *ctx) {
    if (!template_str) {
        return NULL;
    }
    
    string_buffer_t *output = buffer_create();
    if (!output) {
        return NULL;
    }
    
    const char *p = template_str;
    const char *last = p;
    
    while (*p) {
        /* Look for {{ */
        if (p[0] == '{' && p[1] == '{') {
            /* Append text before variable */
            if (p > last) {
                buffer_append(output, last, p - last);
            }
            
            /* Find closing }} */
            const char *end = p + 2;
            while (*end && !(end[0] == '}' && end[1] == '}')) {
                end++;
            }
            
            if (*end == '\0') {
                /* No closing }}, treat as literal text */
                buffer_append_str(output, "{{");
                p += 2;
                last = p;
                continue;
            }
            
            /* Extract variable name */
            char *var_name = extract_var_name(p, end + 2);
            if (var_name) {
                /* Get variable value from context */
                const char *value = template_context_get(ctx, var_name);
                if (value) {
                    buffer_append_str(output, value);
                } else {
                    /* Variable not found, leave empty or use placeholder */
                    buffer_append_str(output, "");
                }
                free(var_name);
            }
            
            /* Move past }} */
            p = end + 2;
            last = p;
        } else {
            p++;
        }
    }
    
    /* Append remaining text */
    if (p > last) {
        buffer_append(output, last, p - last);
    }
    
    return buffer_to_string(output);
}

/* Load template from file */
char *template_load_file(const char *filename) {
    if (!filename) {
        return NULL;
    }
    
    FILE *file = fopen(filename, "rb");
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
    char *buffer = (char *)malloc(size + 1);
    if (!buffer) {
        fclose(file);
        return NULL;
    }
    
    /* Read file */
    size_t read = fread(buffer, 1, size, file);
    buffer[read] = '\0';
    
    fclose(file);
    return buffer;
}

/* Send template response */
void http_response_send_template(http_response_t *res, http_status_t status, 
                                  const char *template_str, template_context_t *ctx) {
    if (!res || !template_str) {
        return;
    }
    
    char *rendered = template_render(template_str, ctx);
    if (rendered) {
        http_response_send_text(res, status, rendered);
        free(rendered);
    } else {
        http_response_send_text(res, HTTP_INTERNAL_ERROR, "Template rendering failed");
    }
}
