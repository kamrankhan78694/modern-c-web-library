# Examples CMakeLists.txt

# Simple HTTP server example
add_executable(simple_server simple_server.c)
target_link_libraries(simple_server weblib ${PLATFORM_LIBS})

# SSL/HTTPS server example
add_executable(ssl_server ssl_server.c)
target_link_libraries(ssl_server weblib ${PLATFORM_LIBS})

# Installation
install(TARGETS simple_server ssl_server DESTINATION bin)
