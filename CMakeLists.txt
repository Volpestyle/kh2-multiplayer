cmake_minimum_required(VERSION 3.20)
project(kh2coop LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---------------------------------------------------------------------------
# ENet — lightweight UDP networking library for game traffic.
# ---------------------------------------------------------------------------
include(FetchContent)
FetchContent_Declare(
    enet
    GIT_REPOSITORY https://github.com/lsalzman/enet.git
    GIT_TAG v1.3.17
)
FetchContent_MakeAvailable(enet)

# ---------------------------------------------------------------------------
# kh2coop_common — shared types, codec, and network client.
# ---------------------------------------------------------------------------
add_library(kh2coop_common
    common/src/Protocol.cpp
    common/src/Codec.cpp
    common/src/NetworkClient.cpp
)

target_include_directories(kh2coop_common PUBLIC
    common/include
    ${enet_SOURCE_DIR}/include
)

target_link_libraries(kh2coop_common PUBLIC enet)

# ---------------------------------------------------------------------------
# kh2coop_server_core — session host logic (library so tests can link it).
# ---------------------------------------------------------------------------
add_library(kh2coop_server_core
    server/src/SessionHost.cpp
)

target_include_directories(kh2coop_server_core PUBLIC
    server/include
)

target_link_libraries(kh2coop_server_core PUBLIC kh2coop_common)

# ---------------------------------------------------------------------------
# kh2coop_server — the host-authoritative relay executable.
# ---------------------------------------------------------------------------
add_executable(kh2coop_server
    server/src/ServerMain.cpp
)

target_link_libraries(kh2coop_server PRIVATE kh2coop_server_core)

# ---------------------------------------------------------------------------
# kh2coop_runtime_scaffold — placeholder for the injected runtime.
# ---------------------------------------------------------------------------
add_executable(kh2coop_runtime_scaffold
    runtime/src/RuntimeMain.cpp
)

target_include_directories(kh2coop_runtime_scaffold PRIVATE
    runtime/include
)

target_link_libraries(kh2coop_runtime_scaffold PRIVATE kh2coop_common)

# ---------------------------------------------------------------------------
# kh2coop_fake_sim — end-to-end test: 3 fake clients + server, no KH2.
# ---------------------------------------------------------------------------
add_executable(kh2coop_fake_sim
    tests/FakeSimulation.cpp
)

target_link_libraries(kh2coop_fake_sim PRIVATE kh2coop_server_core)
