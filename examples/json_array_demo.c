/**
 * JSON Array API Demonstration
 * 
 * This example demonstrates the complete JSON array API including:
 * - Creating arrays
 * - Appending elements
 * - Getting elements by index
 * - Getting array length
 * - Nested arrays
 * - Parsing arrays from JSON strings
 * - Serializing arrays to JSON strings
 */

#include "weblib.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void demo_basic_array(void) {
    printf("=== Basic Array Creation ===\n");
    
    // Create an empty array
    json_value_t *arr = json_array_create();
    
    // Append some numbers
    json_array_append(arr, json_number_create(10.0));
    json_array_append(arr, json_number_create(20.0));
    json_array_append(arr, json_number_create(30.0));
    
    // Get array length
    printf("Array length: %zu\n", json_array_length(arr));
    
    // Access elements
    json_value_t *elem = json_array_get(arr, 0);
    if (elem && elem->type == JSON_NUMBER) {
        printf("First element: %.0f\n", elem->data.number_val);
    }
    
    elem = json_array_get(arr, 1);
    if (elem && elem->type == JSON_NUMBER) {
        printf("Second element: %.0f\n", elem->data.number_val);
    }
    
    // Serialize to JSON
    char *json_str = json_stringify(arr);
    printf("JSON: %s\n\n", json_str);
    
    free(json_str);
    json_value_free(arr);
}

void demo_mixed_array(void) {
    printf("=== Mixed Type Array ===\n");
    
    json_value_t *arr = json_array_create();
    
    // Add different types
    json_array_append(arr, json_string_create("Hello"));
    json_array_append(arr, json_number_create(42.0));
    json_array_append(arr, json_bool_create(true));
    json_array_append(arr, json_string_create("World"));
    
    printf("Array length: %zu\n", json_array_length(arr));
    
    // Serialize
    char *json_str = json_stringify(arr);
    printf("JSON: %s\n\n", json_str);
    
    free(json_str);
    json_value_free(arr);
}

void demo_nested_arrays(void) {
    printf("=== Nested Arrays ===\n");
    
    // Create outer array
    json_value_t *matrix = json_array_create();
    
    // Create first row
    json_value_t *row1 = json_array_create();
    json_array_append(row1, json_number_create(1.0));
    json_array_append(row1, json_number_create(2.0));
    json_array_append(row1, json_number_create(3.0));
    
    // Create second row
    json_value_t *row2 = json_array_create();
    json_array_append(row2, json_number_create(4.0));
    json_array_append(row2, json_number_create(5.0));
    json_array_append(row2, json_number_create(6.0));
    
    // Add rows to matrix
    json_array_append(matrix, row1);
    json_array_append(matrix, row2);
    
    printf("Matrix dimensions: %zu x %zu\n", 
           json_array_length(matrix),
           json_array_length(json_array_get(matrix, 0)));
    
    // Serialize
    char *json_str = json_stringify(matrix);
    printf("JSON: %s\n\n", json_str);
    
    free(json_str);
    json_value_free(matrix);
}

void demo_array_in_object(void) {
    printf("=== Array in Object ===\n");
    
    json_value_t *person = json_object_create();
    
    json_object_set(person, "name", json_string_create("Alice"));
    json_object_set(person, "age", json_number_create(30.0));
    
    // Create hobbies array
    json_value_t *hobbies = json_array_create();
    json_array_append(hobbies, json_string_create("reading"));
    json_array_append(hobbies, json_string_create("coding"));
    json_array_append(hobbies, json_string_create("gaming"));
    
    json_object_set(person, "hobbies", hobbies);
    
    // Serialize
    char *json_str = json_stringify(person);
    printf("JSON: %s\n\n", json_str);
    
    free(json_str);
    json_value_free(person);
}

void demo_parsing(void) {
    printf("=== Parsing JSON Arrays ===\n");
    
    // Parse a simple array
    const char *json1 = "[1, 2, 3, 4, 5]";
    json_value_t *arr1 = json_parse(json1);
    if (arr1) {
        printf("Parsed: %s\n", json1);
        printf("Length: %zu\n", json_array_length(arr1));
        
        // Access middle element
        json_value_t *mid = json_array_get(arr1, 2);
        if (mid && mid->type == JSON_NUMBER) {
            printf("Middle element: %.0f\n", mid->data.number_val);
        }
        
        json_value_free(arr1);
    }
    
    // Parse an array with mixed types
    const char *json2 = "[\"hello\", 123, true, false]";
    json_value_t *arr2 = json_parse(json2);
    if (arr2) {
        printf("\nParsed: %s\n", json2);
        printf("Length: %zu\n", json_array_length(arr2));
        
        // Iterate through elements
        for (size_t i = 0; i < json_array_length(arr2); i++) {
            json_value_t *elem = json_array_get(arr2, i);
            printf("  [%zu] type: ", i);
            switch (elem->type) {
                case JSON_STRING: printf("string\n"); break;
                case JSON_NUMBER: printf("number\n"); break;
                case JSON_BOOL: printf("boolean\n"); break;
                default: printf("other\n"); break;
            }
        }
        
        json_value_free(arr2);
    }
    
    // Parse nested array
    const char *json3 = "[[1, 2], [3, 4], [5, 6]]";
    json_value_t *arr3 = json_parse(json3);
    if (arr3) {
        printf("\nParsed: %s\n", json3);
        printf("Outer length: %zu\n", json_array_length(arr3));
        
        json_value_t *inner = json_array_get(arr3, 1);
        if (inner && inner->type == JSON_ARRAY) {
            printf("Inner array length: %zu\n", json_array_length(inner));
        }
        
        json_value_free(arr3);
    }
    
    printf("\n");
}

void demo_roundtrip(void) {
    printf("=== Roundtrip Test ===\n");
    
    // Create a complex structure
    json_value_t *data = json_object_create();
    
    json_object_set(data, "title", json_string_create("Shopping List"));
    
    json_value_t *items = json_array_create();
    json_array_append(items, json_string_create("milk"));
    json_array_append(items, json_string_create("bread"));
    json_array_append(items, json_string_create("eggs"));
    
    json_object_set(data, "items", items);
    json_object_set(data, "count", json_number_create(3.0));
    
    // Serialize
    char *json_str = json_stringify(data);
    printf("Original: %s\n", json_str);
    
    // Parse it back
    json_value_t *parsed = json_parse(json_str);
    
    // Serialize again
    char *json_str2 = json_stringify(parsed);
    printf("Roundtrip: %s\n", json_str2);
    
    // Verify items array
    json_value_t *parsed_items = json_object_get(parsed, "items");
    if (parsed_items) {
        printf("Items count: %zu\n", json_array_length(parsed_items));
        
        // Print all items
        for (size_t i = 0; i < json_array_length(parsed_items); i++) {
            json_value_t *item = json_array_get(parsed_items, i);
            if (item && item->type == JSON_STRING) {
                printf("  - %s\n", item->data.string_val);
            }
        }
    }
    
    free(json_str);
    free(json_str2);
    json_value_free(data);
    json_value_free(parsed);
    
    printf("\n");
}

int main(void) {
    printf("\n");
    printf("╔════════════════════════════════════════╗\n");
    printf("║  Modern C Web Library                  ║\n");
    printf("║  JSON Array API Demo                   ║\n");
    printf("╚════════════════════════════════════════╝\n");
    printf("\n");
    
    demo_basic_array();
    demo_mixed_array();
    demo_nested_arrays();
    demo_array_in_object();
    demo_parsing();
    demo_roundtrip();
    
    printf("✓ All demonstrations completed successfully!\n\n");
    
    return 0;
}
