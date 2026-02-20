# Tests CMakeLists.txt

# Test executable
add_executable(test_weblib test_weblib.c)
target_link_libraries(test_weblib weblib ${PLATFORM_LIBS})

# Add test to CTest
add_test(NAME WebLibTests COMMAND test_weblib)

# Async WebSocket test executable
add_executable(test_async_websocket test_async_websocket.c)
target_link_libraries(test_async_websocket weblib ${PLATFORM_LIBS})

# Add async WebSocket test to CTest
add_test(NAME AsyncWebSocketTests COMMAND test_async_websocket)
