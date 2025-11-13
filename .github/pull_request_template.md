Title: <short summary>

## Summary
- What does this change do? Why is it needed?

## Related Issues
- Closes #<issue-number> (if applicable)

## Changes
- [ ] Code (C99/C11, no external runtime dependencies)
- [ ] Tests (`tests/test_weblib.c` updated/added)
- [ ] Docs (README/wiki/examples) where applicable
- [ ] Examples updated if public API changed

## Public API changes
- Header (`include/weblib.h`) additions/removals/renames:
```c
// show proposed signatures or changes
```

## Implementation Notes
- Platform considerations (`__linux__`, `__APPLE__`, `_WIN32`)
- Memory ownership and cleanup (create/destroy, `json_value_free`)
- Static limits respected (routes, events, timers, buffers)

## Testing
- Unit/integration tests added or updated
- Manual validation steps:
```bash
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Debug
make && make test
```

## Checklist
- [ ] Follows project style and naming conventions
- [ ] Pure ISO C (C99/C11); no external libraries/frameworks added
- [ ] Builds on at least one of Linux/macOS/Windows
- [ ] No memory leaks (checked via valgrind/sanitizers when applicable)
- [ ] Router/JSON/server ownership rules observed
- [ ] Docs updated (README/wiki/examples) if behavior or APIs changed
