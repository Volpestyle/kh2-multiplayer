# ---------------------------------------------------------------------------
# kh2coop_inject — In-process DLL for friend AI replacement (Strategy B)
#
# This DLL loads into the KH2 process and hooks PerEntityUpdate to replace
# friend AI with player input. It can be loaded either:
#   1. As a Panacea plugin (place in the mod's dll/ folder)
#   2. Via manual DLL injection / LoadLibrary
#
# Dependencies:
#   - MinHook (fetched via FetchContent, built from source)
#   - XInput (Windows SDK) for gamepad reading
#   - KH2Offsets.hpp from the runtime module (compile-time constants only)
# ---------------------------------------------------------------------------

include(FetchContent)

FetchContent_Declare(
    minhook
    GIT_REPOSITORY https://github.com/TsudaKageyu/minhook.git
    GIT_TAG        v1.3.3
)
FetchContent_GetProperties(minhook)
if(NOT minhook_POPULATED)
    FetchContent_Populate(minhook)
endif()

# MinHook v1.3.3 has no CMakeLists.txt — build from source directly.
# Only 4 C files needed for x64.
add_library(minhook_lib STATIC
    ${minhook_SOURCE_DIR}/src/buffer.c
    ${minhook_SOURCE_DIR}/src/hook.c
    ${minhook_SOURCE_DIR}/src/trampoline.c
    ${minhook_SOURCE_DIR}/src/hde/hde64.c
)
target_include_directories(minhook_lib PUBLIC
    ${minhook_SOURCE_DIR}/include
    ${minhook_SOURCE_DIR}/src        # for buffer.h, trampoline.h
    ${minhook_SOURCE_DIR}/src/hde    # for hde64.h
)
set_target_properties(minhook_lib PROPERTIES
    C_STANDARD 11
)

# ---------------------------------------------------------------------------
# The inject DLL itself
# ---------------------------------------------------------------------------
add_library(kh2coop_inject SHARED
    src/DllMain.cpp
    src/EntityHook.cpp
)

target_include_directories(kh2coop_inject PRIVATE
    src
    ${CMAKE_SOURCE_DIR}/runtime/include   # KH2Offsets.hpp
    ${CMAKE_SOURCE_DIR}/common/include    # Types.hpp
)

target_link_libraries(kh2coop_inject PRIVATE minhook_lib)

if(WIN32)
    target_link_libraries(kh2coop_inject PRIVATE xinput)
endif()

target_compile_definitions(kh2coop_inject PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _CRT_SECURE_NO_WARNINGS
)

set_target_properties(kh2coop_inject PROPERTIES
    OUTPUT_NAME "kh2coop_inject"
    # Use a staging directory so we can build while the DLL is loaded in-game.
    # Copy the new build manually or restart KH2 to pick it up.
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/inject/staging"
    LIBRARY_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/inject/staging"
)
