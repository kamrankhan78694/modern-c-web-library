# Examples CMakeLists.txt

# Simple HTTP server example
add_executable(simple_server simple_server.c)
target_link_libraries(simple_server weblib ${PLATFORM_LIBS})

# WebSocket server example
add_executable(websocket_server websocket_server.c)
target_link_libraries(websocket_server weblib ${PLATFORM_LIBS})

# Installation
install(TARGETS simple_server websocket_server DESTINATION bin)
