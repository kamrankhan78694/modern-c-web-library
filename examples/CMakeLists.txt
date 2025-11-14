# Examples CMakeLists.txt

# Simple HTTP server example
add_executable(simple_server simple_server.c)
target_link_libraries(simple_server weblib ${PLATFORM_LIBS})

# Async HTTP server example
add_executable(async_server async_server.c)
target_link_libraries(async_server weblib ${PLATFORM_LIBS})

# WebSocket echo server example
add_executable(websocket_echo_server websocket_echo_server.c)
target_link_libraries(websocket_echo_server weblib ${PLATFORM_LIBS})

# Installation
install(TARGETS simple_server async_server websocket_echo_server DESTINATION bin)
