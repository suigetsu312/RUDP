cmake_minimum_required(VERSION 3.20)

project(rudp_project VERSION 0.1.0 LANGUAGES CXX)

# -----------------------
# Global compile settings
# -----------------------
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# -----------------------
# Options
# -----------------------
option(RUDP_BUILD_TESTS "Build unit tests" OFF)

# -----------------------
# Dependencies
# -----------------------
find_package(Threads REQUIRED)

if (RUDP_BUILD_TESTS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests")
  enable_testing()
  find_package(GTest CONFIG REQUIRED)
endif()

# -----------------------
# Library target (core)
# -----------------------
add_library(rudp_core STATIC
  src/Protocol.cpp
  src/Utils.cpp
  src/Codec.cpp
  src/ConnectionStateMachine.cpp
  src/ServerSessionManager.cpp
  src/Session.cpp
  src/TxHandler.cpp
  src/RxHandler.cpp
)

target_include_directories(rudp_core PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

# Threads is part of the public surface if headers expose it (usually fine as PUBLIC).
target_link_libraries(rudp_core PUBLIC
  Threads::Threads
)

# Warnings: keep them local to the target (do NOT propagate to consumers).
if (MSVC)
  target_compile_options(rudp_core PRIVATE /W4 /permissive-)
else()
  target_compile_options(rudp_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

# -----------------------
# App
# -----------------------
add_executable(rudp_app src/main.cpp)

target_link_libraries(rudp_app PRIVATE
  rudp_core
)

# -----------------------
# Tests
# -----------------------
if (RUDP_BUILD_TESTS
    AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_utils_endian.cpp"
    AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_codec_header_v1.cpp")
  add_executable(unit_tests
    tests/test_utils_endian.cpp
    tests/test_codec_header_v1.cpp
    tests/test_connection_state_machine.cpp
    tests/test_rx_handler_wrap.cpp
    tests/test_server_session_manager.cpp
    tests/test_tx_handler_ack.cpp
    tests/test_session_skeleton.cpp
  )

  target_link_libraries(unit_tests PRIVATE
    rudp_core
    GTest::gtest_main
  )

  include(GoogleTest)
  gtest_discover_tests(unit_tests)
endif()
