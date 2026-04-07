/*
 * weblib_internal.h - Private internal definitions shared across
 *                     library source files.
 *
 * This header exposes internal struct layouts and helper routines that
 * are NOT part of the public API (kamran.k) but are needed by multiple
 * translation units (e.g. worker_runtime.c needs to free the same
 * header/param linked lists that http_server.c creates).
 *
 * Copyright (c) 2024 Modern C Web Library
 * Licensed under MIT License
 */

#ifndef WEBLIB_INTERNAL_H
#define WEBLIB_INTERNAL_H

#include "kamran.k"
#include <stdlib.h>
#include <string.h>

/* ===== Internal linked-list node types ===== */

/*
 * Header linked-list node.
 * The void *headers field in http_request_t / http_response_t
 * points to a singly-linked list of these nodes.
 */
typedef struct http_header_node {
    char *name;      /* lower-case for lookup */
    char *raw_name;  /* original casing for serialisation */
    char *value;
    struct http_header_node *next;
} http_header_node_t;

/*
 * Route-parameter linked-list node.
 * The void *params field in http_request_t points to a singly-linked
 * list of these nodes.
 */
typedef struct http_param_node {
    char *key;
    char *value;
    struct http_param_node *next;
} http_param_node_t;

/* ===== Shared free helpers ===== */

static inline void weblib_header_list_free(http_header_node_t *head) {
    while (head) {
        http_header_node_t *next = head->next;
        free(head->name);
        free(head->raw_name);
        free(head->value);
        free(head);
        head = next;
    }
}

static inline void weblib_param_list_free(http_param_node_t *head) {
    while (head) {
        http_param_node_t *next = head->next;
        free(head->key);
        free(head->value);
        free(head);
        head = next;
    }
}

#endif /* WEBLIB_INTERNAL_H */
