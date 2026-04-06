# Tests CMakeLists.txt

if(NOT EMSCRIPTEN)
    # Test executable
    add_executable(test_weblib test_weblib.c)
    target_include_directories(test_weblib PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
    target_link_libraries(test_weblib weblib ${PLATFORM_LIBS})

    # Add test to CTest
    add_test(NAME WebLibTests COMMAND test_weblib)

    # Header alias test executable
    add_executable(test_kamran_header test_kamran_header.c)
    target_link_libraries(test_kamran_header weblib ${PLATFORM_LIBS})

    # Add header alias test to CTest
    add_test(NAME KamranHeaderTests COMMAND test_kamran_header)

    # Async WebSocket test executable
    add_executable(test_async_websocket test_async_websocket.c)
    target_link_libraries(test_async_websocket weblib ${PLATFORM_LIBS})

    # Add async WebSocket test to CTest
    add_test(NAME AsyncWebSocketTests COMMAND test_async_websocket)

    # Stress test executable
    add_executable(test_stress test_stress.c)
    target_link_libraries(test_stress weblib ${PLATFORM_LIBS})

    # Add stress test to CTest
    add_test(NAME StressTests COMMAND test_stress)
endif()

# WASM capability test (runs on both native and WASM builds)
add_executable(test_wasm test_wasm.c)
target_link_libraries(test_wasm weblib ${PLATFORM_LIBS})

add_test(NAME WasmTests COMMAND test_wasm)
