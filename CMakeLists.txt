cmake_minimum_required(VERSION 3.24)
project(local_ai_runtime LANGUAGES CXX)

if(POLICY CMP0167)
    cmake_policy(SET CMP0167 NEW)
endif()

option(LAR_BUILD_TESTS "Build Local AI Runtime tests" ON)
set(LAR_VERSION "0.1.0-dev" CACHE STRING "Version string embedded in the runtime (release workflow overrides from the tag)")
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Boost 1.74 QUIET)
if(NOT Boost_FOUND)
    message(FATAL_ERROR
        "Boost 1.74+ headers were not found. On Windows, run scripts/build.ps1 (it bootstraps vcpkg and installs manifest dependencies). "
        "On Ubuntu/Debian, install them with: sudo apt-get install build-essential cmake libboost-dev libcurl4-openssl-dev libarchive-dev libssl-dev. "
        "If using CMake directly on Windows, pass -DCMAKE_TOOLCHAIN_FILE=<vcpkg-root>/scripts/buildsystems/vcpkg.cmake.")
endif()
find_package(Threads REQUIRED)
find_package(LibArchive REQUIRED)
find_package(OpenSSL REQUIRED)
if(WIN32)
    find_package(CURL REQUIRED)
    set(LAR_CURL_TARGET CURL::libcurl)
else()
    # Prefer pkg-config on Unix so static curl builds include all transitive
    # codec/TLS dependencies instead of failing at link time with DSO errors.
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LIBCURL REQUIRED IMPORTED_TARGET libcurl)
    set(LAR_CURL_TARGET PkgConfig::LIBCURL)
endif()

add_library(lar_core
    src/config.cpp
    src/process.cpp
    src/downloads.cpp
)
target_include_directories(lar_core PUBLIC include ${Boost_INCLUDE_DIRS})
target_compile_definitions(lar_core PUBLIC BOOST_ERROR_CODE_HEADER_ONLY BOOST_SYSTEM_NO_DEPRECATED)
target_link_libraries(lar_core PUBLIC Threads::Threads ${LAR_CURL_TARGET} LibArchive::LibArchive OpenSSL::Crypto)
if(WIN32)
    target_link_libraries(lar_core PUBLIC ws2_32)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # Expose glibc's posix_spawn_file_actions_addchdir_np declaration.
    target_compile_definitions(lar_core PRIVATE _GNU_SOURCE)
endif()

add_executable(local-ai-runtime src/main.cpp src/server.cpp)
target_include_directories(local-ai-runtime PRIVATE include ${Boost_INCLUDE_DIRS})
target_link_libraries(local-ai-runtime PRIVATE lar_core)
target_compile_definitions(local-ai-runtime PRIVATE
    LAR_VERSION="${LAR_VERSION}"
    LAR_DEFAULT_CONFIG="config/engines.json"
    LAR_EXAMPLE_CONFIG="config/engines.example.json"
    LAR_DEFAULT_WEB_DIR="web"
)

if(MSVC)
    foreach(target lar_core local-ai-runtime)
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8)
    endforeach()
else()
    foreach(target lar_core local-ai-runtime)
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
    endforeach()
endif()

if(LAR_BUILD_TESTS)
    enable_testing()
    add_executable(lar_fake_engine tests/fake_engine.cpp)
    add_executable(lar_fake_http_engine tests/fake_http_engine.cpp)
    target_include_directories(lar_fake_http_engine PRIVATE ${Boost_INCLUDE_DIRS})
    target_compile_definitions(lar_fake_http_engine PRIVATE BOOST_ERROR_CODE_HEADER_ONLY BOOST_SYSTEM_NO_DEPRECATED)
    target_link_libraries(lar_fake_http_engine PRIVATE Threads::Threads)
    if(WIN32)
        target_compile_definitions(lar_fake_http_engine PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
        target_link_libraries(lar_fake_http_engine PRIVATE ws2_32)
    endif()
    add_executable(lar_core_tests tests/core_tests.cpp)
    target_link_libraries(lar_core_tests PRIVATE lar_core)
    # MSVC must decode UTF-8 source literals as UTF-8 in test targets too.
    # The core tests assert UTF-8 model-path preservation using non-ASCII text;
    # without /utf-8 the literal may be decoded using the runner's active code page.
    if(MSVC)
        foreach(target lar_fake_engine lar_fake_http_engine lar_core_tests)
            target_compile_options(${target} PRIVATE /utf-8)
        endforeach()
    endif()
    target_compile_definitions(lar_core_tests PRIVATE
        LAR_TEST_CONFIG="${CMAKE_CURRENT_SOURCE_DIR}/config/engines.example.json"
        LAR_FAKE_ENGINE="$<TARGET_FILE:lar_fake_engine>"
    )
    add_dependencies(lar_core_tests lar_fake_engine)
    add_test(NAME lar_core_tests COMMAND lar_core_tests)

    # Full HTTP smoke coverage uses only Python's standard library. Core tests
    # remain available on machines without Python; CI installs Python explicitly.
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_Interpreter_FOUND)
        add_test(
            NAME lar_http_smoke
            COMMAND "${Python3_EXECUTABLE}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/tests/http_smoke.py"
                    --runtime "$<TARGET_FILE:local-ai-runtime>"
                    --fake-engine "$<TARGET_FILE:lar_fake_http_engine>"
                    --web-dir "${CMAKE_CURRENT_SOURCE_DIR}/web"
        )
        set_tests_properties(lar_http_smoke PROPERTIES
            TIMEOUT 60
            WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
        )
    else()
        message(STATUS "Python 3 not found: the optional HTTP smoke test will be skipped.")
    endif()
endif()
