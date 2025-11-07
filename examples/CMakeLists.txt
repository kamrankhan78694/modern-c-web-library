# Examples CMakeLists.txt

# Simple HTTP server example
add_executable(simple_server simple_server.c)
target_link_libraries(simple_server weblib ${PLATFORM_LIBS})

# Database connection pool example
add_executable(db_pool_example db_pool_example.c)
target_link_libraries(db_pool_example weblib ${PLATFORM_LIBS})

# Installation
install(TARGETS simple_server db_pool_example DESTINATION bin)
