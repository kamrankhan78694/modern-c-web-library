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
if(WIN32)
    # Windows-specific settings
    add_definitions(-D_WIN32_WINNT=0x0600)
    set(PLATFORM_LIBS ws2_32)
elseif(UNIX AND NOT APPLE)
    # Linux-specific settings
    set(PLATFORM_LIBS pthread)
elseif(APPLE)
    # macOS-specific settings
    set(PLATFORM_LIBS pthread)
endif()

# Library source files
set(WEBLIB_SOURCES
    src/http_server.c
    src/router.c
    src/json.c
    src/event_loop.c
    src/websocket.c
    src/async_websocket.c
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
    src/benchmark.c
    src/env_config.c
    src/security_utils.c
    src/middleware_security_headers.c
)

# Create static library
add_library(weblib STATIC ${WEBLIB_SOURCES})
target_include_directories(weblib
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
           $<INSTALL_INTERFACE:include>
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(weblib ${PLATFORM_LIBS})

# Create shared library (optional)
add_library(weblib_shared SHARED ${WEBLIB_SOURCES})
target_include_directories(weblib_shared
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
           $<INSTALL_INTERFACE:include>
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(weblib_shared ${PLATFORM_LIBS})

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
install(TARGETS weblib weblib_shared
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)

install(FILES include/weblib.h include/kamran.k include/db_pool.h DESTINATION include)

# Package configuration
set(CPACK_PACKAGE_NAME "modern-c-web-library")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Modern C library for building web backends")
set(CPACK_PACKAGE_VENDOR "Kamran Khan")

include(CPack)
