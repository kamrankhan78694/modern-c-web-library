# JSON Array API - Quick Reference

## Functions

### `json_array_create()`
Create an empty JSON array.
```c
json_value_t *arr = json_array_create();
```

### `json_array_append(arr, value)`
Append a value to the end of an array.
```c
int result = json_array_append(arr, json_string_create("hello"));
// Returns: 0 on success, -1 on failure
```

### `json_array_get(arr, index)`
Get element at specified index (0-based).
```c
json_value_t *elem = json_array_get(arr, 2);
// Returns: JSON value or NULL if out of bounds
```

### `json_array_length(arr)`
Get the number of elements in an array.
```c
size_t len = json_array_length(arr);
// Returns: Array length or 0 if not an array
```

## Complete Example

```c
#include "weblib.h"
#include <stdio.h>
#include <stdlib.h>

int main(void) {
    // Create array
    json_value_t *arr = json_array_create();
    
    // Add elements
    json_array_append(arr, json_number_create(42.0));
    json_array_append(arr, json_string_create("hello"));
    json_array_append(arr, json_bool_create(true));
    
    // Get array info
    printf("Length: %zu\n", json_array_length(arr));
    
    // Access element
    json_value_t *elem = json_array_get(arr, 0);
    if (elem && elem->type == JSON_NUMBER) {
        printf("First: %.0f\n", elem->data.number_val);
    }
    
    // Serialize
    char *json = json_stringify(arr);
    printf("JSON: %s\n", json);  // [42,"hello",true]
    free(json);
    
    // Parse
    json_value_t *parsed = json_parse("[1, 2, 3]");
    printf("Parsed length: %zu\n", json_array_length(parsed));
    
    // Cleanup
    json_value_free(arr);
    json_value_free(parsed);
    
    return 0;
}
```

## Compile & Run

```bash
gcc -o myapp myapp.c -I./include -L./build -lweblib -Wl,-rpath,./build
./myapp
```

## Nested Arrays

```c
json_value_t *matrix = json_array_create();

json_value_t *row1 = json_array_create();
json_array_append(row1, json_number_create(1.0));
json_array_append(row1, json_number_create(2.0));

json_value_t *row2 = json_array_create();
json_array_append(row2, json_number_create(3.0));
json_array_append(row2, json_number_create(4.0));

json_array_append(matrix, row1);
json_array_append(matrix, row2);

char *json = json_stringify(matrix);
printf("%s\n", json);  // [[1,2],[3,4]]

free(json);
json_value_free(matrix);
```

## Iteration Pattern

```c
json_value_t *arr = json_parse("[1, 2, 3, 4, 5]");

for (size_t i = 0; i < json_array_length(arr); i++) {
    json_value_t *elem = json_array_get(arr, i);
    if (elem && elem->type == JSON_NUMBER) {
        printf("arr[%zu] = %.0f\n", i, elem->data.number_val);
    }
}

json_value_free(arr);
```

## Error Handling

```c
json_value_t *arr = json_array_create();
if (!arr) {
    fprintf(stderr, "Failed to create array\n");
    return -1;
}

json_value_t *value = json_string_create("test");
if (json_array_append(arr, value) < 0) {
    fprintf(stderr, "Failed to append\n");
    json_value_free(value);
    json_value_free(arr);
    return -1;
}

// Access with bounds checking
size_t index = 10;
json_value_t *elem = json_array_get(arr, index);
if (!elem) {
    fprintf(stderr, "Index %zu out of bounds\n", index);
}

json_value_free(arr);
```

## Memory Management

✓ **DO**: Free the array when done
```c
json_value_t *arr = json_array_create();
// ... use array ...
json_value_free(arr);  // Frees array and all elements
```

✗ **DON'T**: Free elements manually
```c
json_value_t *arr = json_array_create();
json_value_t *elem = json_string_create("hello");
json_array_append(arr, elem);
// json_value_free(elem);  // NO! Will cause double-free
json_value_free(arr);  // This frees elem automatically
```

## Common Patterns

### Build JSON response
```c
json_value_t *response = json_object_create();
json_value_t *items = json_array_create();

json_array_append(items, json_string_create("item1"));
json_array_append(items, json_string_create("item2"));

json_object_set(response, "items", items);
json_object_set(response, "count", json_number_create(2.0));

char *json = json_stringify(response);
// {"count":2,"items":["item1","item2"]}
```

### Parse API response
```c
const char *api_response = "{\"users\":[\"alice\",\"bob\",\"charlie\"]}";
json_value_t *obj = json_parse(api_response);

json_value_t *users = json_object_get(obj, "users");
if (users && users->type == JSON_ARRAY) {
    for (size_t i = 0; i < json_array_length(users); i++) {
        json_value_t *user = json_array_get(users, i);
        if (user && user->type == JSON_STRING) {
            printf("User: %s\n", user->data.string_val);
        }
    }
}

json_value_free(obj);
```

## Type Checking

```c
json_value_t *value = json_parse("[1, 2, 3]");

if (value && value->type == JSON_ARRAY) {
    size_t len = json_array_length(value);
    printf("Array has %zu elements\n", len);
    
    for (size_t i = 0; i < len; i++) {
        json_value_t *elem = json_array_get(value, i);
        
        switch (elem->type) {
            case JSON_NUMBER:
                printf("Number: %.0f\n", elem->data.number_val);
                break;
            case JSON_STRING:
                printf("String: %s\n", elem->data.string_val);
                break;
            case JSON_BOOL:
                printf("Bool: %s\n", elem->data.bool_val ? "true" : "false");
                break;
            case JSON_NULL:
                printf("Null\n");
                break;
            case JSON_ARRAY:
                printf("Nested array\n");
                break;
            case JSON_OBJECT:
                printf("Object\n");
                break;
        }
    }
}

json_value_free(value);
```
