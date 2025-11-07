# Examples CMakeLists.txt

# Simple HTTP server example
add_executable(simple_server simple_server.c)
target_link_libraries(simple_server weblib ${PLATFORM_LIBS})

# Static file server example
add_executable(static_server static_server.c)
target_link_libraries(static_server weblib ${PLATFORM_LIBS})

# Installation
install(TARGETS simple_server static_server DESTINATION bin)
