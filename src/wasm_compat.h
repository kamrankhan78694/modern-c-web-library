/*
 * wasm_compat.h - WASM Compatibility Stubs
 *
 * Provides no-op stub definitions for pthread mutex and condition
 * variable operations when building for WebAssembly (Emscripten).
 * WASM environments are single-threaded, so synchronization
 * primitives are unnecessary but their type/macro definitions
 * are required to compile source files that use them.
 *
 * Include this header INSTEAD of <pthread.h> in source files that
 * only need mutex/cond operations.  The pattern is:
 *
 *   #ifdef __EMSCRIPTEN__
 *   #include "wasm_compat.h"
 *   #else
 *   #include <pthread.h>
 *   #endif
 *
 * Copyright (c) 2024 Modern C Web Library
 * Licensed under MIT License
 */

#ifndef WASM_COMPAT_H
#define WASM_COMPAT_H

#ifdef __EMSCRIPTEN__

/* WASM: single-threaded environment; all synchronization is no-op */

typedef int pthread_mutex_t;
typedef int pthread_cond_t;

#define pthread_mutex_init(m, a)    (0)
#define pthread_mutex_lock(m)       (0)
#define pthread_mutex_unlock(m)     (0)
#define pthread_mutex_destroy(m)    (0)

#define pthread_cond_init(c, a)     (0)
#define pthread_cond_wait(c, m)     (0)
#define pthread_cond_signal(c)      (0)
#define pthread_cond_broadcast(c)   (0)
#define pthread_cond_destroy(c)     (0)

#endif /* __EMSCRIPTEN__ */

#endif /* WASM_COMPAT_H */
