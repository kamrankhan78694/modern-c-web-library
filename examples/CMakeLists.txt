# Examples CMakeLists.txt

# Simple HTTP server example
add_executable(simple_server simple_server.c)
target_link_libraries(simple_server weblib ${PLATFORM_LIBS})

# Template engine demo
add_executable(template_demo template_demo.c)
target_link_libraries(template_demo weblib ${PLATFORM_LIBS})

# Installation
install(TARGETS simple_server template_demo DESTINATION bin)
