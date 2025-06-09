cmake_minimum_required(VERSION 3.10)
project(RUDP)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Source and header includes
file(GLOB_RECURSE SOURCE_FILES "src/*.cpp")

# Library target
# add_library(RUDP ${SOURCE_FILES})
# target_include_directories(RUDP PUBLIC ${CMAKE_SOURCE_DIR}/include)

# Executable test target
add_executable(RUDPTest test/main.cpp)
target_include_directories(RUDPTest PRIVATE ${CMAKE_SOURCE_DIR}/include) # 可選