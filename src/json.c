#include "kamran.k"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>   /* isfinite (builtin macro; no libm link) */
#include <locale.h> /* localeconv (normalize decimal separator to '.') */

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

/* Maximum nesting depth to prevent stack overflow */
#define JSON_MAX_DEPTH 512

/* Internal functions */
static void skip_whitespace(const char **str);
static json_value_t *parse_value(const char **str, int depth);
static json_value_t *parse_object(const char **str, int depth);
static json_value_t *parse_array(const char **str, int depth);
static json_value_t *parse_string(const char **str);
static json_value_t *parse_number(const char **str);
static json_value_t *parse_bool(const char **str);
static json_value_t *parse_null(const char **str);
static bool stringify_value(json_value_t *value, char **output, size_t *capacity, size_t *length);
static bool ensure_stringify_capacity(char **output, size_t *capacity, size_t length, size_t needed);

/* Parse JSON string */
json_value_t *json_parse(const char *json_str) {
    if (!json_str) {
        return NULL;
    }
    
    const char *ptr = json_str;
    skip_whitespace(&ptr);
    
    json_value_t *result = parse_value(&ptr, 0);
    if (!result) {
        return NULL;
    }

    /* Reject trailing garbage */
    skip_whitespace(&ptr);
    if (*ptr != '\0') {
        json_value_free(result);
        return NULL;
    }

    return result;
}

/* Create JSON null value */
json_value_t *json_null_create(void) {
    json_value_t *value = (json_value_t *)calloc(1, sizeof(json_value_t));
    if (!value) {
        return NULL;
    }
    value->type = JSON_NULL;
    return value;
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
    
    /* Check if key already exists and replace it */
    json_object_entry_t *existing = (json_object_entry_t *)obj->data.object_val;
    
    while (existing) {
        if (strcmp(existing->key, key) == 0) {
            /* Key exists, replace value */
            json_value_free(existing->value);
            existing->value = value;
            return;
        }
        existing = existing->next;
    }
    
    /* Key doesn't exist, add new entry */
    json_object_entry_t *entry = (json_object_entry_t *)malloc(sizeof(json_object_entry_t));
    if (!entry) {
        json_value_free(value);
        return;
    }
    
    entry->key = strdup(key);
    if (!entry->key) {
        free(entry);
        json_value_free(value);
        return;
    }
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
    if (!value->data.string_val) {
        free(value);
        return NULL;
    }
    
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

/* Create array value */
json_value_t *json_array_create(void) {
    json_value_t *value = (json_value_t *)calloc(1, sizeof(json_value_t));
    if (!value) {
        return NULL;
    }
    value->type = JSON_ARRAY;
    value->data.array_val = NULL;
    return value;
}

/* Append value to array */
int json_array_append(json_value_t *arr, json_value_t *value) {
    if (!arr || arr->type != JSON_ARRAY || !value) {
        return -1;
    }

    json_array_item_t *item = (json_array_item_t *)malloc(sizeof(json_array_item_t));
    if (!item) {
        return -1;
    }
    item->value = value;
    item->next = NULL;

    if (!arr->data.array_val) {
        arr->data.array_val = item;
    } else {
        json_array_item_t *tail = (json_array_item_t *)arr->data.array_val;
        while (tail->next) {
            tail = tail->next;
        }
        tail->next = item;
    }
    return 0;
}

/* Get array element by index */
json_value_t *json_array_get(json_value_t *arr, size_t index) {
    if (!arr || arr->type != JSON_ARRAY) {
        return NULL;
    }

    json_array_item_t *item = (json_array_item_t *)arr->data.array_val;
    size_t i = 0;
    while (item) {
        if (i == index) {
            return item->value;
        }
        item = item->next;
        i++;
    }
    return NULL;
}

/* Get array length */
size_t json_array_length(json_value_t *arr) {
    if (!arr || arr->type != JSON_ARRAY) {
        return 0;
    }

    size_t count = 0;
    json_array_item_t *item = (json_array_item_t *)arr->data.array_val;
    while (item) {
        count++;
        item = item->next;
    }
    return count;
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
    
    if (!stringify_value(value, &output, &capacity, &length)) {
        /* stringify_value failed, free buffer */
        free(output);
        return NULL;
    }
    
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
        /* Free existing body to prevent memory leak */
        if (res->body) {
            free(res->body);
        }
        res->body = json_str;
        res->body_length = strlen(json_str);
        http_response_set_header(res, "Content-Type", "application/json; charset=utf-8");
    }
}

/* Internal parsing functions */
static void skip_whitespace(const char **str) {
    while (**str && isspace((unsigned char)**str)) {
        (*str)++;
    }
}

static json_value_t *parse_value(const char **str, int depth) {
    if (depth > JSON_MAX_DEPTH) {
        return NULL;
    }
    skip_whitespace(str);
    
    if (!**str) {
        return NULL;
    }
    
    switch (**str) {
        case '{':
            return parse_object(str, depth);
        case '[':
            return parse_array(str, depth);
        case '"':
            return parse_string(str);
        case 't':
        case 'f':
            return parse_bool(str);
        case 'n':
            return parse_null(str);
        default:
            if (**str == '-' || isdigit((unsigned char)**str)) {
                return parse_number(str);
            }
            return NULL;
    }
}

static json_value_t *parse_object(const char **str, int depth) {
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
        json_value_t *value = parse_value(str, depth + 1);
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
            return obj;
        } else {
            json_value_free(obj);
            return NULL;
        }
    }
    
    /* Unterminated object - missing closing '}' */
    json_value_free(obj);
    return NULL;
}

static json_value_t *parse_array(const char **str, int depth) {
    json_value_t *arr = json_array_create();
    if (!arr) {
        return NULL;
    }

    (*str)++; /* Skip '[' */
    skip_whitespace(str);

    if (**str == ']') {
        (*str)++;
        return arr;
    }

    while (**str) {
        json_value_t *value = parse_value(str, depth + 1);
        if (!value) {
            json_value_free(arr);
            return NULL;
        }

        if (json_array_append(arr, value) < 0) {
            json_value_free(value);
            json_value_free(arr);
            return NULL;
        }

        skip_whitespace(str);

        if (**str == ',') {
            (*str)++;
            skip_whitespace(str);
        } else if (**str == ']') {
            (*str)++;
            return arr;
        } else {
            json_value_free(arr);
            return NULL;
        }
    }

    /* Unterminated array - missing closing ']' */
    json_value_free(arr);
    return NULL;
}

static int hex_digit_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static json_value_t *parse_string(const char **str) {
    (*str)++; /* Skip opening quote */
    
    const char *start = *str;
    /* First pass: find end of string and compute max output length */
    size_t raw_len = 0;
    const char *p = *str;
    while (*p && *p != '"') {
        if (*p == '\\') {
            p++;
            if (!*p) return NULL;
            if (*p == 'u') {
                /* Need 4 hex digits after \u */
                for (int i = 1; i <= 4; i++) {
                    if (hex_digit_value(p[i]) < 0) return NULL;
                }
                /* Check if this is a high surrogate that requires a second \uXXXX */
                unsigned int cp = 0;
                for (int i = 1; i <= 4; i++) {
                    cp = (cp << 4) | (unsigned)hex_digit_value(p[i]);
                }
                p += 5; /* skip u + 4 hex digits */
                if (cp >= 0xD800 && cp <= 0xDBFF) {
                    /* Expect \uDC00-\uDFFF */
                    if (p[0] != '\\' || p[1] != 'u') return NULL;
                    for (int i = 2; i <= 5; i++) {
                        if (hex_digit_value(p[i]) < 0) return NULL;
                    }
                    p += 6; /* skip \uXXXX */
                }
            } else {
                /* Validate escape character */
                switch (*p) {
                    case '"': case '\\': case '/':
                    case 'b': case 'f': case 'n': case 'r': case 't':
                        p++;
                        break;
                    default:
                        return NULL; /* Invalid escape */
                }
            }
        } else {
            p++;
        }
    }
    
    if (*p != '"') {
        return NULL;
    }
    
    raw_len = p - start;
    /* Allocate output buffer: raw_len + 1 is always sufficient because decoding
     * can only shrink or maintain the length. Every escape sequence (\X = 2 raw
     * chars → 1 output char, \uXXXX = 6 raw chars → 1-3 UTF-8 output bytes). */
    char *string_val = (char *)malloc(raw_len + 1);
    if (!string_val) {
        return NULL;
    }
    
    /* Second pass: decode escape sequences */
    size_t out_idx = 0;
    p = start;
    while (p < start + raw_len) {
        if (*p == '\\') {
            p++;
            switch (*p) {
                case '"':  string_val[out_idx++] = '"'; break;
                case '\\': string_val[out_idx++] = '\\'; break;
                case '/':  string_val[out_idx++] = '/'; break;
                case 'b':  string_val[out_idx++] = '\b'; break;
                case 'f':  string_val[out_idx++] = '\f'; break;
                case 'n':  string_val[out_idx++] = '\n'; break;
                case 'r':  string_val[out_idx++] = '\r'; break;
                case 't':  string_val[out_idx++] = '\t'; break;
                case 'u': {
                    unsigned int codepoint = 0;
                    for (int i = 1; i <= 4; i++) {
                        codepoint = (codepoint << 4) | (unsigned)hex_digit_value(p[i]);
                    }
                    p += 4; /* skip 4 hex digits (the 'u' is skipped by outer p++) */

                    /* Reject NUL byte — \u0000 would truncate C strings */
                    if (codepoint == 0) {
                        free(string_val);
                        return NULL;
                    }

                    /* Handle UTF-16 surrogate pairs (RFC 8259 §7) */
                    if (codepoint >= 0xD800 && codepoint <= 0xDBFF) {
                        /* High surrogate — must be followed by \uDC00-\uDFFF */
                        if (p[1] == '\\' && p[2] == 'u') {
                            unsigned int low = 0;
                            for (int i = 3; i <= 6; i++) {
                                low = (low << 4) | (unsigned)hex_digit_value(p[i]);
                            }
                            if (low >= 0xDC00 && low <= 0xDFFF) {
                                codepoint = 0x10000 + ((codepoint - 0xD800) << 10) + (low - 0xDC00);
                                p += 6; /* skip \uXXXX of the low surrogate */
                            } else {
                                /* Invalid low surrogate */
                                free(string_val);
                                return NULL;
                            }
                        } else {
                            /* Lone high surrogate */
                            free(string_val);
                            return NULL;
                        }
                    } else if (codepoint >= 0xDC00 && codepoint <= 0xDFFF) {
                        /* Lone low surrogate — invalid */
                        free(string_val);
                        return NULL;
                    }

                    /* Encode as UTF-8 */
                    if (codepoint <= 0x7F) {
                        string_val[out_idx++] = (char)codepoint;
                    } else if (codepoint <= 0x7FF) {
                        string_val[out_idx++] = (char)(0xC0 | (codepoint >> 6));
                        string_val[out_idx++] = (char)(0x80 | (codepoint & 0x3F));
                    } else if (codepoint <= 0xFFFF) {
                        string_val[out_idx++] = (char)(0xE0 | (codepoint >> 12));
                        string_val[out_idx++] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
                        string_val[out_idx++] = (char)(0x80 | (codepoint & 0x3F));
                    } else if (codepoint <= 0x10FFFF) {
                        string_val[out_idx++] = (char)(0xF0 | (codepoint >> 18));
                        string_val[out_idx++] = (char)(0x80 | ((codepoint >> 12) & 0x3F));
                        string_val[out_idx++] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
                        string_val[out_idx++] = (char)(0x80 | (codepoint & 0x3F));
                    }
                    break;
                }
                default:
                    /* Should not happen due to first pass validation */
                    string_val[out_idx++] = *p;
                    break;
            }
            p++;
        } else {
            string_val[out_idx++] = *p++;
        }
    }
    string_val[out_idx] = '\0';
    
    *str = start + raw_len + 1; /* Skip past closing quote */
    
    json_value_t *value = (json_value_t *)calloc(1, sizeof(json_value_t));
    if (!value) {
        free(string_val);
        return NULL;
    }
    
    value->type = JSON_STRING;
    value->data.string_val = string_val;
    
    return value;
}

/* strtod() for a pre-validated JSON number token [s, s+len). JSON always uses '.'
 * as its decimal separator, but strtod() honors LC_NUMERIC: under a locale whose
 * separator is not '.' (e.g. ',' in de_DE/fr_FR) it stops at the '.', so "3.14"
 * reads back as 3 and the caller's span check then rejects every non-integer
 * number. Swap the single '.' to the active locale's separator on a copy so strtod
 * consumes the whole token, then map the end pointer back into s. This mirrors the
 * output-side json_normalize_decimal(). The common '.'-locale case (C/POSIX/en_*)
 * takes a zero-copy fast path identical to a bare strtod(). On allocation failure
 * *end_out is set to s so the caller's `end != p` check fails the parse. */
static double json_strtod(const char *s, size_t len, char **end_out) {
    char sep = localeconv()->decimal_point[0];
    if (sep == '.' || sep == '\0') {
        return strtod(s, end_out);
    }
    char stackbuf[64];
    char *tmp = (len < sizeof(stackbuf)) ? stackbuf : (char *)malloc(len + 1);
    if (!tmp) {
        *end_out = (char *)s;
        return 0.0;
    }
    memcpy(tmp, s, len);
    tmp[len] = '\0';
    char *dot = (char *)memchr(tmp, '.', len);
    if (dot) *dot = sep;
    char *tend;
    double num = strtod(tmp, &tend);
    *end_out = (char *)s + (tend - tmp);   /* 1:1 copy, so the offset is preserved */
    if (tmp != stackbuf) {
        free(tmp);
    }
    return num;
}

static json_value_t *parse_number(const char **str) {
    const char *p = *str;

    /* RFC 8259 number: [ minus ] int [ frac ] [ exp ]
     * int = "0" / ( digit1-9 *DIGIT )
     * Reject hex, leading '+', leading '.', inf, nan */
    if (*p == '-') p++;
    if (!(*p >= '0' && *p <= '9')) return NULL;
    if (*p == '0' && p[1] >= '0' && p[1] <= '9') return NULL; /* no leading zeros */
    while (*p >= '0' && *p <= '9') p++;
    if (*p == '.') {
        p++;
        if (!(*p >= '0' && *p <= '9')) return NULL; /* digit required after '.' */
        while (*p >= '0' && *p <= '9') p++;
    }
    if (*p == 'e' || *p == 'E') {
        p++;
        if (*p == '+' || *p == '-') p++;
        if (!(*p >= '0' && *p <= '9')) return NULL;
        while (*p >= '0' && *p <= '9') p++;
    }
    
    char *end;
    double num = json_strtod(*str, (size_t)(p - *str), &end);

    if (end != p) {
        return NULL;
    }

    *str = end;

    return json_number_create(num);
}

static json_value_t *parse_bool(const char **str) {
    if (strncmp(*str, "true", 4) == 0 && !isalnum((unsigned char)(*str)[4])) {
        *str += 4;
        return json_bool_create(true);
    } else if (strncmp(*str, "false", 5) == 0 && !isalnum((unsigned char)(*str)[5])) {
        *str += 5;
        return json_bool_create(false);
    }
    
    return NULL;
}

static json_value_t *parse_null(const char **str) {
    if (strncmp(*str, "null", 4) == 0 && !isalnum((unsigned char)(*str)[4])) {
        *str += 4;
        return json_null_create();
    }
    
    return NULL;
}

static bool ensure_stringify_capacity(char **output, size_t *capacity, size_t length, size_t needed) {
    if (needed > SIZE_MAX - length) {
        return false; /* overflow */
    }
    if (length + needed <= *capacity) {
        return true;
    }
    size_t new_cap = *capacity;
    while (new_cap < length + needed) {
        if (new_cap > SIZE_MAX / 2) {
            new_cap = length + needed; /* clamp to exact requirement */
            break;
        }
        new_cap *= 2;
    }
    char *new_output = (char *)realloc(*output, new_cap);
    if (!new_output) {
        return false;
    }
    *output = new_output;
    *capacity = new_cap;
    return true;
}

static bool stringify_string(const char *str, char **output, size_t *capacity, size_t *length) {
    if (!ensure_stringify_capacity(output, capacity, *length, 2)) return false;
    (*output)[(*length)++] = '"';

    if (str) {
        while (*str) {
            /* Ensure space for worst case: \uXXXX = 6 chars */
            if (!ensure_stringify_capacity(output, capacity, *length, 8)) return false;

            unsigned char c = (unsigned char)*str;
            switch (*str) {
                case '"':  (*output)[(*length)++] = '\\'; (*output)[(*length)++] = '"'; break;
                case '\\': (*output)[(*length)++] = '\\'; (*output)[(*length)++] = '\\'; break;
                case '\b': (*output)[(*length)++] = '\\'; (*output)[(*length)++] = 'b'; break;
                case '\f': (*output)[(*length)++] = '\\'; (*output)[(*length)++] = 'f'; break;
                case '\n': (*output)[(*length)++] = '\\'; (*output)[(*length)++] = 'n'; break;
                case '\r': (*output)[(*length)++] = '\\'; (*output)[(*length)++] = 'r'; break;
                case '\t': (*output)[(*length)++] = '\\'; (*output)[(*length)++] = 't'; break;
                default:
                    if (c < 0x20) {
                        /* Escape control characters as \u00XX (exactly 6 chars) */
                        int written = snprintf(*output + *length, *capacity - *length, "\\u%04x", c);
                        if (written < 0 || (size_t)written >= *capacity - *length) return false;
                        *length += (size_t)written;
                    } else {
                        (*output)[(*length)++] = *str;
                    }
                    break;
            }
            str++;
        }
    }

    if (!ensure_stringify_capacity(output, capacity, *length, 2)) return false;
    (*output)[(*length)++] = '"';
    (*output)[*length] = '\0';
    return true;
}

/* JSON always uses '.' as the decimal separator, but snprintf("%g") emits the
 * program's active LC_NUMERIC separator (e.g. ',' under de_DE), which would be
 * invalid JSON. Normalize it in place (a 1:1 char swap, so length is unchanged)
 * so output is valid regardless of the host locale. Only the %g path can
 * produce a separator; integer/"null"/"-0" output has none. */
static void json_normalize_decimal(char *s) {
    char sep = localeconv()->decimal_point[0];
    if (sep != '\0' && sep != '.') {
        char *p = strchr(s, sep);
        if (p) *p = '.';
    }
}

/* Serialize a JSON number (double) losslessly into buf.
 *   - Integral values within a double's exact-integer range (|x| < 2^53) print
 *     with no decimal point or exponent (e.g. 1234567890, not 1.23457e+09).
 *   - Other finite values use the SHORTEST %g precision that reparses to the
 *     same double (round-trip exact), avoiding %g's default 6-significant-digit
 *     truncation.
 *   - Non-finite values (NaN/Infinity, which JSON cannot represent) become null
 *     rather than the invalid "nan"/"inf" that %g would emit.
 * Returns the written length, or -1 on error. */
static int json_format_number(double num, char *buf, size_t bufsz) {
    if (!isfinite(num)) {
        int n = snprintf(buf, bufsz, "null");
        return (n < 0 || (size_t)n >= bufsz) ? -1 : n;
    }
    /* Preserve the sign of zero for strict round-trip losslessness: -0.0 must
     * not collapse to "0" (the (long long) cast drops the sign bit). JSON
     * permits -0, which parses back to -0.0. */
    if (num == 0.0) {
        int n = signbit(num) ? snprintf(buf, bufsz, "-0")
                             : snprintf(buf, bufsz, "0");
        return (n < 0 || (size_t)n >= bufsz) ? -1 : n;
    }
    /* Exact integer that a double represents without loss (|x| <= 2^53): print
     * as an integer. The inclusive bound keeps the (long long) cast in range
     * (2^53 << LLONG_MAX). */
    if (num >= -9007199254740992.0 && num <= 9007199254740992.0 &&
        num == (double)(long long)num) {
        int n = snprintf(buf, bufsz, "%lld", (long long)num);
        return (n < 0 || (size_t)n >= bufsz) ? -1 : n;
    }
    for (int prec = 15; prec <= 17; prec++) {
        int n = snprintf(buf, bufsz, "%.*g", prec, num);
        if (n < 0 || (size_t)n >= bufsz) return -1;
        if (strtod(buf, NULL) == num) {   /* strtod honors the same LC_NUMERIC */
            json_normalize_decimal(buf);  /* -> '.' so output is valid JSON */
            return n;
        }
    }
    int n = snprintf(buf, bufsz, "%.17g", num);   /* fallback: full precision */
    if (n < 0 || (size_t)n >= bufsz) return -1;
    json_normalize_decimal(buf);
    return n;
}

static bool stringify_value(json_value_t *value, char **output, size_t *capacity, size_t *length) {
    if (!value || !output || !*output) {
        return false;
    }
    
    switch (value->type) {
        case JSON_NULL:
            if (!ensure_stringify_capacity(output, capacity, *length, 8)) return false;
            memcpy(*output + *length, "null", 4);
            *length += 4;
            (*output)[*length] = '\0';
            break;
            
        case JSON_BOOL: {
            const char *s = value->data.bool_val ? "true" : "false";
            size_t slen = strlen(s);
            if (!ensure_stringify_capacity(output, capacity, *length, slen + 1)) return false;
            memcpy(*output + *length, s, slen);
            *length += slen;
            (*output)[*length] = '\0';
            break;
        }
            
        case JSON_NUMBER: {
            char numbuf[40];   /* max: "-1.2345678901234567e-308" (~24) or 20-digit int */
            int nlen = json_format_number(value->data.number_val, numbuf, sizeof(numbuf));
            if (nlen < 0) return false;
            if (!ensure_stringify_capacity(output, capacity, *length, (size_t)nlen + 1)) return false;
            memcpy(*output + *length, numbuf, (size_t)nlen);
            *length += (size_t)nlen;
            (*output)[*length] = '\0';
            break;
        }
            
        case JSON_STRING: {
            if (!stringify_string(value->data.string_val, output, capacity, length)) return false;
            break;
        }
            
        case JSON_OBJECT: {
            if (!ensure_stringify_capacity(output, capacity, *length, 2)) return false;
            (*output)[(*length)++] = '{';
            json_object_entry_t *entry = (json_object_entry_t *)value->data.object_val;
            bool first = true;
            while (entry) {
                if (!first) {
                    if (!ensure_stringify_capacity(output, capacity, *length, 2)) return false;
                    (*output)[(*length)++] = ',';
                }
                /* Escape key properly */
                if (!stringify_string(entry->key, output, capacity, length)) return false;
                if (!ensure_stringify_capacity(output, capacity, *length, 2)) return false;
                (*output)[(*length)++] = ':';
                if (!stringify_value(entry->value, output, capacity, length)) {
                    return false;
                }
                entry = entry->next;
                first = false;
            }
            if (!ensure_stringify_capacity(output, capacity, *length, 2)) return false;
            (*output)[(*length)++] = '}';
            (*output)[*length] = '\0';
            break;
        }
            
        case JSON_ARRAY: {
            if (!ensure_stringify_capacity(output, capacity, *length, 2)) return false;
            (*output)[(*length)++] = '[';
            json_array_item_t *item = (json_array_item_t *)value->data.array_val;
            bool first = true;
            while (item) {
                if (!first) {
                    if (!ensure_stringify_capacity(output, capacity, *length, 2)) return false;
                    (*output)[(*length)++] = ',';
                }
                if (!stringify_value(item->value, output, capacity, length)) {
                    return false;
                }
                item = item->next;
                first = false;
            }
            if (!ensure_stringify_capacity(output, capacity, *length, 2)) return false;
            (*output)[(*length)++] = ']';
            (*output)[*length] = '\0';
            break;
        }
    }
    
    return true;
}
