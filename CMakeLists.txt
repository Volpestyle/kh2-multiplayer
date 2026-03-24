cmake_minimum_required(VERSION 3.20)
project(kh2coop LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(kh2coop_common
    common/src/Protocol.cpp
)

target_include_directories(kh2coop_common PUBLIC
    common/include
)

add_executable(kh2coop_server
    server/src/ServerMain.cpp
)

target_link_libraries(kh2coop_server PRIVATE kh2coop_common)

add_executable(kh2coop_runtime_scaffold
    runtime/src/RuntimeMain.cpp
)

target_include_directories(kh2coop_runtime_scaffold PRIVATE
    runtime/include
    common/include
)

target_link_libraries(kh2coop_runtime_scaffold PRIVATE kh2coop_common)
