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

    # Worker runtime + infrastructure test executable
    add_executable(test_worker test_worker.c)
    target_link_libraries(test_worker weblib ${PLATFORM_LIBS})

    # Add worker test to CTest
    add_test(NAME WorkerTests COMMAND test_worker)

    # Experimental TLS scaffold smoke test -- only when TLS is enabled. Verifies the
    # option -> -DWEBLIB_TLS -> compiled native source -> linkable symbol wiring.
    if(WEBLIB_ENABLE_TLS)
        add_executable(test_tls test_tls.c)
        target_include_directories(test_tls PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src/tls)
        target_compile_definitions(test_tls PRIVATE WEBLIB_TLS)
        target_link_libraries(test_tls weblib ${PLATFORM_LIBS})
        add_test(NAME TlsTests COMMAND test_tls)

        # TLS crypto primitive known-answer tests (ChaCha20, ... each vs its RFC vector)
        add_executable(test_tls_crypto test_tls_crypto.c)
        target_include_directories(test_tls_crypto PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src/tls)
        target_compile_definitions(test_tls_crypto PRIVATE WEBLIB_TLS)
        target_link_libraries(test_tls_crypto weblib ${PLATFORM_LIBS})
        add_test(NAME TlsCryptoTests COMMAND test_tls_crypto)
    endif()
endif()

# WASM capability test (runs on both native and WASM builds)
add_executable(test_wasm test_wasm.c)
target_link_libraries(test_wasm weblib ${PLATFORM_LIBS})

add_test(NAME WasmTests COMMAND test_wasm)
