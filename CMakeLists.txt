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

# Include directories
include_directories(${CMAKE_SOURCE_DIR}/include)

# Library source files
set(WEBLIB_SOURCES
    src/http_server.c
    src/router.c
    src/json.c
    src/event_loop.c
)

# Create static library
add_library(weblib STATIC ${WEBLIB_SOURCES})
target_link_libraries(weblib ${PLATFORM_LIBS})

# Create shared library (optional)
add_library(weblib_shared SHARED ${WEBLIB_SOURCES})
target_link_libraries(weblib_shared ${PLATFORM_LIBS})
set_target_properties(weblib_shared PROPERTIES OUTPUT_NAME weblib)

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

install(FILES include/weblib.h DESTINATION include)

# Package configuration
set(CPACK_PACKAGE_NAME "modern-c-web-library")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Modern C library for building web backends")
set(CPACK_PACKAGE_VENDOR "Kamran Khan")

include(CPack)
