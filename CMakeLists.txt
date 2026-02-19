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
option(RUDP_BUILD_TESTS "Build unit tests" ON)

# -----------------------
# Dependencies (via vcpkg toolchain)
# -----------------------
find_package(Threads REQUIRED)
find_package(spdlog CONFIG REQUIRED)

if (RUDP_BUILD_TESTS)
  enable_testing()
  find_package(GTest CONFIG REQUIRED)
endif()

# -----------------------
# Library target (headers-only core)
# -----------------------
add_library(rudp_core STATIC
  src/Session.cpp
)

target_include_directories(rudp_core PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(rudp_core PUBLIC
  Threads::Threads
  spdlog::spdlog
)

# Warnings (apply to consumers)
if (MSVC)
  target_compile_options(rudp_core INTERFACE /W4 /permissive-)
else()
  target_compile_options(rudp_core INTERFACE -Wall -Wextra -Wpedantic)
endif()

# -----------------------
# App
# -----------------------
add_executable(rudp_app
  src/main.cpp
)

target_link_libraries(rudp_app PRIVATE rudp_core)

# -----------------------
# Tests
# -----------------------
if (RUDP_BUILD_TESTS)
  add_executable(unit_tests
    tests/test_utils_endian.cpp
    tests/test_codec_header_v1.cpp
    tests/test_session_client_handshake.cpp
    tests/test_session_tick.cpp
  )

  target_link_libraries(unit_tests PRIVATE
    rudp_core
    GTest::gtest_main
  )

  include(GoogleTest)
  gtest_discover_tests(unit_tests)
endif()
