#include "weblib.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* Internal structures */
typedef struct json_object_entry {
    char *key;
    json_value_t *value;
    struct json_object_entry *next;
} json_object_entry_t;

typedef struct json_array_item {
    json_value_t *value;
    struct json_array_item *next;
} json_array_item_t;

/* Internal functions */
static void skip_whitespace(const char **str);
static json_value_t *parse_value(const char **str);
static json_value_t *parse_object(const char **str);
static json_value_t *parse_array(const char **str);
static json_value_t *parse_string(const char **str);
static json_value_t *parse_number(const char **str);
static json_value_t *parse_bool(const char **str);
static json_value_t *parse_null(const char **str);
static void stringify_value(json_value_t *value, char **output, size_t *capacity, size_t *length);

/* Parse JSON string */
json_value_t *json_parse(const char *json_str) {
    if (!json_str) {
        return NULL;
    }
    
    const char *ptr = json_str;
    skip_whitespace(&ptr);
    
    return parse_value(&ptr);
}

/* Create JSON object */
json_value_t *json_object_create(void) {
    json_value_t *value = (json_value_t *)calloc(1, sizeof(json_value_t));
    if (!value) {
        return NULL;
    }
    
    value->type = JSON_OBJECT;
    value->data.object_val = NULL;
    
    return value;
}

/* Set object property */
void json_object_set(json_value_t *obj, const char *key, json_value_t *value) {
    if (!obj || obj->type != JSON_OBJECT || !key || !value) {
        return;
    }
    
    json_object_entry_t *entry = (json_object_entry_t *)malloc(sizeof(json_object_entry_t));
    if (!entry) {
        return;
    }
    
    entry->key = strdup(key);
    entry->value = value;
    entry->next = (json_object_entry_t *)obj->data.object_val;
    obj->data.object_val = entry;
}

/* Get object property */
json_value_t *json_object_get(json_value_t *obj, const char *key) {
    if (!obj || obj->type != JSON_OBJECT || !key) {
        return NULL;
    }
    
    json_object_entry_t *entry = (json_object_entry_t *)obj->data.object_val;
    while (entry) {
        if (strcmp(entry->key, key) == 0) {
            return entry->value;
        }
        entry = entry->next;
    }
    
    return NULL;
}

/* Create string value */
json_value_t *json_string_create(const char *str) {
    if (!str) {
        return NULL;
    }
    
    json_value_t *value = (json_value_t *)calloc(1, sizeof(json_value_t));
    if (!value) {
        return NULL;
    }
    
    value->type = JSON_STRING;
    value->data.string_val = strdup(str);
    
    return value;
}

/* Create number value */
json_value_t *json_number_create(double num) {
    json_value_t *value = (json_value_t *)calloc(1, sizeof(json_value_t));
    if (!value) {
        return NULL;
    }
    
    value->type = JSON_NUMBER;
    value->data.number_val = num;
    
    return value;
}

/* Create boolean value */
json_value_t *json_bool_create(bool val) {
    json_value_t *value = (json_value_t *)calloc(1, sizeof(json_value_t));
    if (!value) {
        return NULL;
    }
    
    value->type = JSON_BOOL;
    value->data.bool_val = val;
    
    return value;
}

/* Stringify JSON */
char *json_stringify(json_value_t *value) {
    if (!value) {
        return NULL;
    }
    
    size_t capacity = 256;
    size_t length = 0;
    char *output = (char *)malloc(capacity);
    if (!output) {
        return NULL;
    }
    
    stringify_value(value, &output, &capacity, &length);
    
    return output;
}

/* Free JSON value */
void json_value_free(json_value_t *value) {
    if (!value) {
        return;
    }
    
    switch (value->type) {
        case JSON_STRING:
            free(value->data.string_val);
            break;
            
        case JSON_OBJECT: {
            json_object_entry_t *entry = (json_object_entry_t *)value->data.object_val;
            while (entry) {
                json_object_entry_t *next = entry->next;
                free(entry->key);
                json_value_free(entry->value);
                free(entry);
                entry = next;
            }
            break;
        }
        
        case JSON_ARRAY: {
            json_array_item_t *item = (json_array_item_t *)value->data.array_val;
            while (item) {
                json_array_item_t *next = item->next;
                json_value_free(item->value);
                free(item);
                item = next;
            }
            break;
        }
        
        default:
            break;
    }
    
    free(value);
}

/* Send JSON response */
void http_response_send_json(http_response_t *res, http_status_t status, json_value_t *json) {
    if (!res || !json) {
        return;
    }
    
    char *json_str = json_stringify(json);
    if (json_str) {
        res->status = status;
        res->body = json_str;
        res->body_length = strlen(json_str);
    }
}

/* Internal parsing functions */
static void skip_whitespace(const char **str) {
    while (**str && isspace(**str)) {
        (*str)++;
    }
}

static json_value_t *parse_value(const char **str) {
    skip_whitespace(str);
    
    if (!**str) {
        return NULL;
    }
    
    switch (**str) {
        case '{':
            return parse_object(str);
        case '[':
            return parse_array(str);
        case '"':
            return parse_string(str);
        case 't':
        case 'f':
            return parse_bool(str);
        case 'n':
            return parse_null(str);
        default:
            if (**str == '-' || isdigit(**str)) {
                return parse_number(str);
            }
            return NULL;
    }
}

static json_value_t *parse_object(const char **str) {
    json_value_t *obj = json_object_create();
    if (!obj) {
        return NULL;
    }
    
    (*str)++; /* Skip '{' */
    skip_whitespace(str);
    
    if (**str == '}') {
        (*str)++;
        return obj;
    }
    
    while (**str) {
        skip_whitespace(str);
        
        /* Parse key */
        if (**str != '"') {
            json_value_free(obj);
            return NULL;
        }
        
        json_value_t *key_val = parse_string(str);
        if (!key_val) {
            json_value_free(obj);
            return NULL;
        }
        
        skip_whitespace(str);
        
        if (**str != ':') {
            json_value_free(key_val);
            json_value_free(obj);
            return NULL;
        }
        (*str)++;
        
        /* Parse value */
        json_value_t *value = parse_value(str);
        if (!value) {
            json_value_free(key_val);
            json_value_free(obj);
            return NULL;
        }
        
        json_object_set(obj, key_val->data.string_val, value);
        json_value_free(key_val);
        
        skip_whitespace(str);
        
        if (**str == ',') {
            (*str)++;
        } else if (**str == '}') {
            (*str)++;
            break;
        } else {
            json_value_free(obj);
            return NULL;
        }
    }
    
    return obj;
}

static json_value_t *parse_array(const char **str) {
    /* Simplified array parsing - not fully implemented */
    (*str)++; /* Skip '[' */
    skip_whitespace(str);
    
    if (**str == ']') {
        (*str)++;
        json_value_t *arr = (json_value_t *)calloc(1, sizeof(json_value_t));
        if (arr) {
            arr->type = JSON_ARRAY;
        }
        return arr;
    }
    
    /* TODO: Implement full array parsing */
    return NULL;
}

static json_value_t *parse_string(const char **str) {
    (*str)++; /* Skip opening quote */
    
    const char *start = *str;
    while (**str && **str != '"') {
        if (**str == '\\') {
            (*str)++;
            if (**str) {
                (*str)++;
            }
        } else {
            (*str)++;
        }
    }
    
    if (**str != '"') {
        return NULL;
    }
    
    size_t len = *str - start;
    char *string_val = (char *)malloc(len + 1);
    if (!string_val) {
        return NULL;
    }
    
    strncpy(string_val, start, len);
    string_val[len] = '\0';
    
    (*str)++; /* Skip closing quote */
    
    json_value_t *value = (json_value_t *)calloc(1, sizeof(json_value_t));
    if (!value) {
        free(string_val);
        return NULL;
    }
    
    value->type = JSON_STRING;
    value->data.string_val = string_val;
    
    return value;
}

static json_value_t *parse_number(const char **str) {
    char *end;
    double num = strtod(*str, &end);
    
    if (end == *str) {
        return NULL;
    }
    
    *str = end;
    
    return json_number_create(num);
}

static json_value_t *parse_bool(const char **str) {
    if (strncmp(*str, "true", 4) == 0) {
        *str += 4;
        return json_bool_create(true);
    } else if (strncmp(*str, "false", 5) == 0) {
        *str += 5;
        return json_bool_create(false);
    }
    
    return NULL;
}

static json_value_t *parse_null(const char **str) {
    if (strncmp(*str, "null", 4) == 0) {
        *str += 4;
        json_value_t *value = (json_value_t *)calloc(1, sizeof(json_value_t));
        if (value) {
            value->type = JSON_NULL;
        }
        return value;
    }
    
    return NULL;
}

static void stringify_value(json_value_t *value, char **output, size_t *capacity, size_t *length) {
    if (!value || !output || !*output) {
        return;
    }
    
    /* Ensure capacity */
    while (*length + 256 > *capacity) {
        *capacity *= 2;
        *output = (char *)realloc(*output, *capacity);
        if (!*output) {
            return;
        }
    }
    
    switch (value->type) {
        case JSON_NULL:
            *length += sprintf(*output + *length, "null");
            break;
            
        case JSON_BOOL:
            *length += sprintf(*output + *length, "%s", value->data.bool_val ? "true" : "false");
            break;
            
        case JSON_NUMBER:
            *length += sprintf(*output + *length, "%g", value->data.number_val);
            break;
            
        case JSON_STRING:
            *length += sprintf(*output + *length, "\"%s\"", value->data.string_val);
            break;
            
        case JSON_OBJECT: {
            *length += sprintf(*output + *length, "{");
            json_object_entry_t *entry = (json_object_entry_t *)value->data.object_val;
            bool first = true;
            while (entry) {
                if (!first) {
                    *length += sprintf(*output + *length, ",");
                }
                *length += sprintf(*output + *length, "\"%s\":", entry->key);
                stringify_value(entry->value, output, capacity, length);
                entry = entry->next;
                first = false;
            }
            *length += sprintf(*output + *length, "}");
            break;
        }
            
        case JSON_ARRAY:
            /* Simplified - empty array */
            *length += sprintf(*output + *length, "[]");
            break;
    }
}
