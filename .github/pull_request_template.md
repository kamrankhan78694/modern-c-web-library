Title: <short summary>

## Summary
- What does this change do? Why is it needed?

## Related Issues
- Closes #<issue-number> (if applicable)

## Changes
- [ ] Code (C99/C11, no external runtime dependencies)
- [ ] Tests (a file under `tests/`; a *new* test file also needs `tests/CMakeLists.txt`)
- [ ] Docs (`README.md`, `docs/`, `CHANGELOG.md`) where applicable
- [ ] Examples updated if public API changed

## Public API changes
- Header (`include/kamran.k`) additions/removals/renames:
```c
// show proposed signatures or changes
```

## Implementation Notes
- Platform considerations (`__linux__`, `__APPLE__`, `_WIN32`, `__EMSCRIPTEN__`)
- Memory ownership and cleanup (create/destroy, `json_value_free`)
- Static limits respected (routes, events, timers, buffers)
- New source files: WASM-safe or `WEBLIB_SOURCES_NATIVE_ONLY`?

## Testing
```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
cd build && ctest --output-on-failure     # 7 suites with TLS off
```

## Checklist
- [ ] Follows project style and naming conventions
- [ ] Pure ISO C (C99/C11); no external libraries/frameworks added
- [ ] Builds clean under `-Wall -Wextra -pedantic` — no new warnings
- [ ] `ctest --output-on-failure` passes locally
- [ ] No memory leaks (valgrind or sanitizers, as applicable)
- [ ] Router/JSON/server ownership rules observed
- [ ] Docs updated if behaviour or APIs changed

## Does this PR touch `src/tls/`?
The TLS layer is hand-written, experimental and unaudited cryptography, so it carries a
higher bar. Delete this section if it does not apply; otherwise tick every box.

- [ ] TLS build passes: `-DWEBLIB_ENABLE_TLS=ON -DWEBLIB_TLS_TEST_HOOKS=ON` +
      `ctest --output-on-failure` (14 suites; a skipped `TlsInteropOpenssl` is not a pass)
- [ ] Sanitizer build passes: same options in a *separate* build dir, plus
      `-fsanitize=address,undefined -fno-omit-frame-pointer -g`, then
      `ctest --output-on-failure --no-tests=error -R '^Tls'` (without `--no-tests=error`,
      `ctest` exits 0 when the filter matches nothing)
- [ ] The EXPERIMENTAL / UNAUDITED caveat survives this change everywhere it appeared
- [ ] Handshake state machine: no message accepted out of order; every error path latches
      a terminal state and wipes secrets
- [ ] I read `src/tls/README.md` before changing the profile, the state machine or a primitive

## CI gates
`.github/workflows/ci.yml` runs `primary-checks` (Docker GCC build, full ctest, Valgrind),
then `clang-check`, `tls-check`, `macos-check` (pull requests only) and
`docker-image-check`. All must be green to merge.
