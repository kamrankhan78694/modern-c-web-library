#include "weblib.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* Create a new cookie */
http_cookie_t *http_cookie_create(const char *name, const char *value) {
    if (!name || !value) {
        return NULL;
    }
    
    http_cookie_t *cookie = (http_cookie_t *)calloc(1, sizeof(http_cookie_t));
    if (!cookie) {
        return NULL;
    }
    
    cookie->name = strdup(name);
    if (!cookie->name) {
        free(cookie);
        return NULL;
    }
    
    cookie->value = strdup(value);
    if (!cookie->value) {
        free(cookie->name);
        free(cookie);
        return NULL;
    }
    
    cookie->domain = NULL;
    cookie->path = NULL;
    cookie->max_age = -1;
    cookie->http_only = false;
    cookie->secure = false;
    cookie->same_site = NULL;
    cookie->next = NULL;
    
    return cookie;
}

/* Set cookie domain */
int http_cookie_set_domain(http_cookie_t *cookie, const char *domain) {
    if (!cookie || !domain) {
        return -1;
    }
    
    free(cookie->domain);
    cookie->domain = strdup(domain);
    
    return cookie->domain ? 0 : -1;
}

/* Set cookie path */
int http_cookie_set_path(http_cookie_t *cookie, const char *path) {
    if (!cookie || !path) {
        return -1;
    }
    
    free(cookie->path);
    cookie->path = strdup(path);
    
    return cookie->path ? 0 : -1;
}

/* Set cookie max-age */
void http_cookie_set_max_age(http_cookie_t *cookie, int max_age) {
    if (cookie) {
        cookie->max_age = max_age;
    }
}

/* Set cookie HttpOnly flag */
void http_cookie_set_http_only(http_cookie_t *cookie, bool http_only) {
    if (cookie) {
        cookie->http_only = http_only;
    }
}

/* Set cookie Secure flag */
void http_cookie_set_secure(http_cookie_t *cookie, bool secure) {
    if (cookie) {
        cookie->secure = secure;
    }
}

/* Set cookie SameSite attribute */
int http_cookie_set_same_site(http_cookie_t *cookie, const char *same_site) {
    if (!cookie || !same_site) {
        return -1;
    }
    
    /* Validate SameSite value */
    if (strcmp(same_site, "Strict") != 0 && 
        strcmp(same_site, "Lax") != 0 && 
        strcmp(same_site, "None") != 0) {
        return -1;
    }
    
    free(cookie->same_site);
    cookie->same_site = strdup(same_site);
    
    return cookie->same_site ? 0 : -1;
}

/* Free cookie */
void http_cookie_free(http_cookie_t *cookie) {
    if (!cookie) {
        return;
    }
    
    free(cookie->name);
    free(cookie->value);
    free(cookie->domain);
    free(cookie->path);
    free(cookie->same_site);
    
    /* Free next cookie in chain if exists */
    if (cookie->next) {
        http_cookie_free(cookie->next);
    }
    
    free(cookie);
}

/* Parse Cookie header and add to request */
void http_request_parse_cookies(http_request_t *req, const char *cookie_header) {
    if (!req || !cookie_header) {
        return;
    }
    
    /* Free existing cookies */
    if (req->cookies) {
        http_cookie_free(req->cookies);
        req->cookies = NULL;
    }
    
    /* Cookie header format: "name1=value1; name2=value2; name3=value3" */
    char *header_copy = strdup(cookie_header);
    if (!header_copy) {
        return;
    }
    
    http_cookie_t *last_cookie = NULL;
    char *saveptr = NULL;
    char *pair = strtok_r(header_copy, ";", &saveptr);
    
    while (pair) {
        /* Skip leading whitespace */
        while (*pair && isspace((unsigned char)*pair)) {
            pair++;
        }
        
        /* Find '=' separator */
        char *equals = strchr(pair, '=');
        if (equals) {
            *equals = '\0';
            char *name = pair;
            char *value = equals + 1;
            
            /* Create cookie */
            http_cookie_t *cookie = http_cookie_create(name, value);
            if (cookie) {
                /* Add to linked list */
                if (!req->cookies) {
                    req->cookies = cookie;
                    last_cookie = cookie;
                } else {
                    last_cookie->next = cookie;
                    last_cookie = cookie;
                }
            }
        }
        
        pair = strtok_r(NULL, ";", &saveptr);
    }
    
    free(header_copy);
}

/* Get cookie value from request */
const char *http_request_get_cookie(http_request_t *req, const char *name) {
    if (!req || !name) {
        return NULL;
    }
    
    http_cookie_t *cookie = req->cookies;
    while (cookie) {
        if (strcmp(cookie->name, name) == 0) {
            return cookie->value;
        }
        cookie = cookie->next;
    }
    
    return NULL;
}

/* Set cookie in response */
void http_response_set_cookie(http_response_t *res, http_cookie_t *cookie) {
    if (!res || !cookie) {
        return;
    }
    
    /* Add to linked list */
    if (!res->cookies) {
        res->cookies = cookie;
    } else {
        /* Find last cookie and append */
        http_cookie_t *last = res->cookies;
        while (last->next) {
            last = last->next;
        }
        last->next = cookie;
    }
}

/* Build Set-Cookie header string from cookie */
char *http_cookie_to_set_cookie_header(http_cookie_t *cookie) {
    if (!cookie) {
        return NULL;
    }
    
    /* Calculate required buffer size */
    size_t size = strlen(cookie->name) + strlen(cookie->value) + 128;
    if (cookie->domain) size += strlen(cookie->domain);
    if (cookie->path) size += strlen(cookie->path);
    if (cookie->same_site) size += strlen(cookie->same_site);
    
    char *header = (char *)malloc(size);
    if (!header) {
        return NULL;
    }
    
    /* Start with name=value */
    int len = snprintf(header, size, "%s=%s", cookie->name, cookie->value);
    
    /* Add optional attributes */
    if (cookie->domain) {
        len += snprintf(header + len, size - len, "; Domain=%s", cookie->domain);
    }
    
    if (cookie->path) {
        len += snprintf(header + len, size - len, "; Path=%s", cookie->path);
    }
    
    if (cookie->max_age >= 0) {
        len += snprintf(header + len, size - len, "; Max-Age=%d", cookie->max_age);
    }
    
    if (cookie->http_only) {
        len += snprintf(header + len, size - len, "; HttpOnly");
    }
    
    if (cookie->secure) {
        len += snprintf(header + len, size - len, "; Secure");
    }
    
    if (cookie->same_site) {
        len += snprintf(header + len, size - len, "; SameSite=%s", cookie->same_site);
    }
    
    return header;
}
