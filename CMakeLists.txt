cmake_minimum_required(VERSION 3.10)
project(ModernCWebLibrary VERSION 1.0.0 LANGUAGES C)

# Set C standard
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Compiler warnings
if(MSVC)
    add_compile_options(/W4)
else()
    add_compile_options(-Wall -Wextra -pedantic)
endif()

# Platform-specific settings
if(EMSCRIPTEN)
    # WebAssembly (Emscripten) settings
    set(PLATFORM_LIBS "")
    add_compile_options(-sWASM=1)
elseif(WIN32)
    # Windows-specific settings
    add_definitions(-D_WIN32_WINNT=0x0600)
    # bcrypt: BCryptGenRandom() used by secure_random_bytes()
    set(PLATFORM_LIBS ws2_32 bcrypt)
elseif(UNIX AND NOT APPLE)
    # Linux-specific settings
    set(PLATFORM_LIBS pthread)
elseif(APPLE)
    # macOS-specific settings
    set(PLATFORM_LIBS pthread)
endif()

# Library source files -- WASM-safe subset (pure logic, no OS I/O)
set(WEBLIB_SOURCES_WASM_SAFE
    src/http_server.c
    src/router.c
    src/json.c
    src/event_loop.c
    src/body_parser.c
    src/cookie.c
    src/middleware_cors.c
    src/middleware_ratelimit.c
    src/middleware_static.c
    src/session.c
    src/template.c
    src/middleware_auth.c
    src/db_pool.c
    src/thread_pool.c
    src/middleware_log.c
    src/middleware_error.c
    src/middleware_csrf.c
    src/input_validation.c
    src/health_check.c
    src/cache.c
    src/middleware_metrics.c
    src/compression.c
    src/env_config.c
    src/security_utils.c
    src/crypto/sha256.c
    src/middleware_security_headers.c
    src/wasm_runtime.c
    src/worker_runtime.c
    src/worker_kv.c
    src/worker_r2.c
    src/worker_d1.c
    src/worker_queues.c
)

# OS-dependent modules (sockets, real benchmarking) -- excluded from WASM builds
set(WEBLIB_SOURCES_NATIVE_ONLY
    src/websocket.c
    src/async_websocket.c
    src/benchmark.c
)

# Experimental pure-C TLS 1.3 layer (native-only, UNAUDITED). OFF by default: with
# the option off, nothing below is added and the build is byte-identical to a
# no-TLS build. Mirrors the WEBLIB_SOURCES_NATIVE_ONLY split (see src/tls/README.md).
option(WEBLIB_ENABLE_TLS "Build the experimental pure-C TLS 1.3 layer (native-only, UNAUDITED)" OFF)
set(WEBLIB_SOURCES_TLS
    src/tls/tls.c
    src/tls/chacha20.c
    src/tls/poly1305.c
    src/tls/chacha20poly1305.c
    src/tls/hkdf.c
)

if(EMSCRIPTEN)
    set(WEBLIB_SOURCES ${WEBLIB_SOURCES_WASM_SAFE})
else()
    set(WEBLIB_SOURCES ${WEBLIB_SOURCES_WASM_SAFE} ${WEBLIB_SOURCES_NATIVE_ONLY})
    if(WEBLIB_ENABLE_TLS)
        list(APPEND WEBLIB_SOURCES ${WEBLIB_SOURCES_TLS})
    endif()
endif()

# Create static library
add_library(weblib STATIC ${WEBLIB_SOURCES})
target_include_directories(weblib
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
           $<INSTALL_INTERFACE:include>
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(weblib ${PLATFORM_LIBS})

# Pass CMake project version to C code so a static assertion can verify that
# the version in the header (kamran.k) matches the one in CMakeLists.txt.
target_compile_definitions(weblib PRIVATE
    CMAKE_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    CMAKE_VERSION_MINOR=${PROJECT_VERSION_MINOR}
    CMAKE_VERSION_PATCH=${PROJECT_VERSION_PATCH}
)

# Experimental TLS layer: define WEBLIB_TLS only when explicitly enabled on a
# native target (its sources were added to WEBLIB_SOURCES above under the same guard).
if(WEBLIB_ENABLE_TLS AND NOT EMSCRIPTEN)
    target_compile_definitions(weblib PRIVATE WEBLIB_TLS)
endif()

if(NOT EMSCRIPTEN)
    # Create shared library (not supported for WASM)
    add_library(weblib_shared SHARED ${WEBLIB_SOURCES})
    target_include_directories(weblib_shared
        PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
               $<INSTALL_INTERFACE:include>
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    target_link_libraries(weblib_shared ${PLATFORM_LIBS})
    target_compile_definitions(weblib_shared PRIVATE
        CMAKE_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
        CMAKE_VERSION_MINOR=${PROJECT_VERSION_MINOR}
        CMAKE_VERSION_PATCH=${PROJECT_VERSION_PATCH}
    )
    if(WEBLIB_ENABLE_TLS)
        target_compile_definitions(weblib_shared PRIVATE WEBLIB_TLS)
    endif()
endif()

# Build examples
option(BUILD_EXAMPLES "Build example programs" ON)
if(BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# Build tests
option(BUILD_TESTS "Build test programs" ON)
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# Installation
if(EMSCRIPTEN)
    install(TARGETS weblib
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib
    )
else()
    install(TARGETS weblib weblib_shared
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib
    )
endif()

install(FILES include/kamran.k include/db_pool.h DESTINATION include)

# Package configuration
set(CPACK_PACKAGE_NAME "modern-c-web-library")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Modern C library for building web backends")
set(CPACK_PACKAGE_VENDOR "Kamran Khan")

include(CPack)
