# Examples CMakeLists.txt

if(NOT EMSCRIPTEN)
    # Simple HTTP server example
    add_executable(simple_server simple_server.c)
    target_link_libraries(simple_server weblib ${PLATFORM_LIBS})

    # Async HTTP server example
    add_executable(async_server async_server.c)
    target_link_libraries(async_server weblib ${PLATFORM_LIBS})

    # WebSocket echo server example
    add_executable(websocket_echo_server websocket_echo_server.c)
    target_link_libraries(websocket_echo_server weblib ${PLATFORM_LIBS})

    # Async WebSocket echo server example
    add_executable(async_websocket_echo_server async_websocket_echo_server.c)
    target_link_libraries(async_websocket_echo_server weblib ${PLATFORM_LIBS})

    # REST API server example
    add_executable(rest_api_server rest_api_server.c)
    target_link_libraries(rest_api_server weblib ${PLATFORM_LIBS})

    # Installation
    install(TARGETS simple_server async_server websocket_echo_server async_websocket_echo_server rest_api_server DESTINATION bin)
endif()

# WASM example (runs on both native and WASM builds)
add_executable(wasm_example wasm_example.c)
target_link_libraries(wasm_example weblib ${PLATFORM_LIBS})
