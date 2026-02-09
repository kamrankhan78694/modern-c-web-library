#include "weblib.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

void test_empty_array(void) {
    printf("Test 1: Empty array creation and stringification...\n");
    
    json_value_t *arr = json_array_create();
    assert(arr != NULL);
    assert(json_array_length(arr) == 0);
    
    char *json_str = json_stringify(arr);
    assert(json_str != NULL);
    printf("  Result: %s\n", json_str);
    assert(strcmp(json_str, "[]") == 0);
    
    free(json_str);
    json_value_free(arr);
    printf("  ✓ PASSED\n\n");
}

void test_array_with_numbers(void) {
    printf("Test 2: Array with numbers...\n");
    
    json_value_t *arr = json_array_create();
    assert(arr != NULL);
    
    json_array_append(arr, json_number_create(1.0));
    json_array_append(arr, json_number_create(2.0));
    json_array_append(arr, json_number_create(3.5));
    
    assert(json_array_length(arr) == 3);
    
    json_value_t *elem = json_array_get(arr, 0);
    assert(elem != NULL);
    assert(elem->type == JSON_NUMBER);
    assert(elem->data.number_val == 1.0);
    
    elem = json_array_get(arr, 2);
    assert(elem != NULL);
    assert(elem->data.number_val == 3.5);
    
    char *json_str = json_stringify(arr);
    assert(json_str != NULL);
    printf("  Result: %s\n", json_str);
    
    free(json_str);
    json_value_free(arr);
    printf("  ✓ PASSED\n\n");
}

void test_array_with_strings(void) {
    printf("Test 3: Array with strings...\n");
    
    json_value_t *arr = json_array_create();
    assert(arr != NULL);
    
    json_array_append(arr, json_string_create("hello"));
    json_array_append(arr, json_string_create("world"));
    json_array_append(arr, json_string_create("test"));
    
    assert(json_array_length(arr) == 3);
    
    json_value_t *elem = json_array_get(arr, 1);
    assert(elem != NULL);
    assert(elem->type == JSON_STRING);
    assert(strcmp(elem->data.string_val, "world") == 0);
    
    char *json_str = json_stringify(arr);
    assert(json_str != NULL);
    printf("  Result: %s\n", json_str);
    assert(strcmp(json_str, "[\"hello\",\"world\",\"test\"]") == 0);
    
    free(json_str);
    json_value_free(arr);
    printf("  ✓ PASSED\n\n");
}

void test_array_with_mixed_types(void) {
    printf("Test 4: Array with mixed types...\n");
    
    json_value_t *arr = json_array_create();
    assert(arr != NULL);
    
    json_array_append(arr, json_string_create("text"));
    json_array_append(arr, json_number_create(42.0));
    json_array_append(arr, json_bool_create(true));
    json_array_append(arr, json_bool_create(false));
    
    assert(json_array_length(arr) == 4);
    
    char *json_str = json_stringify(arr);
    assert(json_str != NULL);
    printf("  Result: %s\n", json_str);
    assert(strcmp(json_str, "[\"text\",42,true,false]") == 0);
    
    free(json_str);
    json_value_free(arr);
    printf("  ✓ PASSED\n\n");
}

void test_nested_arrays(void) {
    printf("Test 5: Nested arrays...\n");
    
    json_value_t *outer = json_array_create();
    json_value_t *inner1 = json_array_create();
    json_value_t *inner2 = json_array_create();
    
    json_array_append(inner1, json_number_create(1.0));
    json_array_append(inner1, json_number_create(2.0));
    
    json_array_append(inner2, json_number_create(3.0));
    json_array_append(inner2, json_number_create(4.0));
    
    json_array_append(outer, inner1);
    json_array_append(outer, inner2);
    
    assert(json_array_length(outer) == 2);
    
    json_value_t *first_inner = json_array_get(outer, 0);
    assert(first_inner != NULL);
    assert(first_inner->type == JSON_ARRAY);
    assert(json_array_length(first_inner) == 2);
    
    char *json_str = json_stringify(outer);
    assert(json_str != NULL);
    printf("  Result: %s\n", json_str);
    assert(strcmp(json_str, "[[1,2],[3,4]]") == 0);
    
    free(json_str);
    json_value_free(outer);
    printf("  ✓ PASSED\n\n");
}

void test_array_parsing(void) {
    printf("Test 6: Parse JSON array from string...\n");
    
    const char *json_str = "[1,2,3,4,5]";
    json_value_t *arr = json_parse(json_str);
    assert(arr != NULL);
    assert(arr->type == JSON_ARRAY);
    assert(json_array_length(arr) == 5);
    
    json_value_t *elem = json_array_get(arr, 2);
    assert(elem != NULL);
    assert(elem->type == JSON_NUMBER);
    assert(elem->data.number_val == 3.0);
    
    char *stringified = json_stringify(arr);
    printf("  Input:  %s\n", json_str);
    printf("  Output: %s\n", stringified);
    assert(strcmp(stringified, "[1,2,3,4,5]") == 0);
    
    free(stringified);
    json_value_free(arr);
    printf("  ✓ PASSED\n\n");
}

void test_array_parsing_strings(void) {
    printf("Test 7: Parse JSON array with strings...\n");
    
    const char *json_str = "[\"apple\",\"banana\",\"cherry\"]";
    json_value_t *arr = json_parse(json_str);
    assert(arr != NULL);
    assert(arr->type == JSON_ARRAY);
    assert(json_array_length(arr) == 3);
    
    json_value_t *elem = json_array_get(arr, 1);
    assert(elem != NULL);
    assert(elem->type == JSON_STRING);
    assert(strcmp(elem->data.string_val, "banana") == 0);
    
    char *stringified = json_stringify(arr);
    printf("  Input:  %s\n", json_str);
    printf("  Output: %s\n", stringified);
    
    free(stringified);
    json_value_free(arr);
    printf("  ✓ PASSED\n\n");
}

void test_array_parsing_nested(void) {
    printf("Test 8: Parse nested JSON arrays...\n");
    
    const char *json_str = "[[1,2],[3,4],[5,6]]";
    json_value_t *arr = json_parse(json_str);
    assert(arr != NULL);
    assert(arr->type == JSON_ARRAY);
    assert(json_array_length(arr) == 3);
    
    json_value_t *inner = json_array_get(arr, 1);
    assert(inner != NULL);
    assert(inner->type == JSON_ARRAY);
    assert(json_array_length(inner) == 2);
    
    json_value_t *elem = json_array_get(inner, 0);
    assert(elem != NULL);
    assert(elem->type == JSON_NUMBER);
    assert(elem->data.number_val == 3.0);
    
    char *stringified = json_stringify(arr);
    printf("  Input:  %s\n", json_str);
    printf("  Output: %s\n", stringified);
    
    free(stringified);
    json_value_free(arr);
    printf("  ✓ PASSED\n\n");
}

void test_array_in_object(void) {
    printf("Test 9: Array in object...\n");
    
    json_value_t *obj = json_object_create();
    json_value_t *arr = json_array_create();
    
    json_array_append(arr, json_number_create(1.0));
    json_array_append(arr, json_number_create(2.0));
    json_array_append(arr, json_number_create(3.0));
    
    json_object_set(obj, "numbers", arr);
    json_object_set(obj, "name", json_string_create("test"));
    
    char *json_str = json_stringify(obj);
    assert(json_str != NULL);
    printf("  Result: %s\n", json_str);
    
    // Parse it back
    json_value_t *parsed = json_parse(json_str);
    assert(parsed != NULL);
    
    json_value_t *parsed_arr = json_object_get(parsed, "numbers");
    assert(parsed_arr != NULL);
    assert(parsed_arr->type == JSON_ARRAY);
    assert(json_array_length(parsed_arr) == 3);
    
    free(json_str);
    json_value_free(obj);
    json_value_free(parsed);
    printf("  ✓ PASSED\n\n");
}

void test_array_out_of_bounds(void) {
    printf("Test 10: Array out of bounds access...\n");
    
    json_value_t *arr = json_array_create();
    json_array_append(arr, json_number_create(1.0));
    json_array_append(arr, json_number_create(2.0));
    
    assert(json_array_length(arr) == 2);
    
    json_value_t *elem = json_array_get(arr, 10);
    assert(elem == NULL);
    
    json_value_free(arr);
    printf("  ✓ PASSED\n\n");
}

int main(void) {
    printf("=== JSON Array Implementation Tests ===\n\n");
    
    test_empty_array();
    test_array_with_numbers();
    test_array_with_strings();
    test_array_with_mixed_types();
    test_nested_arrays();
    test_array_parsing();
    test_array_parsing_strings();
    test_array_parsing_nested();
    test_array_in_object();
    test_array_out_of_bounds();
    
    printf("=== ALL TESTS PASSED ===\n");
    return 0;
}
