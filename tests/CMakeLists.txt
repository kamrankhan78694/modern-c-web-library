# Tests CMakeLists.txt

# Test executable
add_executable(test_weblib test_weblib.c)
target_link_libraries(test_weblib weblib ${PLATFORM_LIBS})

# Add test to CTest
add_test(NAME WebLibTests COMMAND test_weblib)
