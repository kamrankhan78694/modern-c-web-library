# Tests CMakeLists.txt

# Test executable
add_executable(test_weblib test_weblib.c)
target_link_libraries(test_weblib weblib ${PLATFORM_LIBS})

# Database pool test executable
add_executable(test_db_pool test_db_pool.c)
target_link_libraries(test_db_pool weblib ${PLATFORM_LIBS})

# Add test to CTest
add_test(NAME WebLibTests COMMAND test_weblib)
add_test(NAME DbPoolTests COMMAND test_db_pool)
