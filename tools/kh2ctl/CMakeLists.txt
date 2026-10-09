add_executable(kh2ctl
    src/main.cpp
    src/world_resync.cpp
)

target_link_libraries(kh2ctl PRIVATE
    kh2coop_runtime
    user32
    ole32
    dbghelp
)

option(KH2COOP_PORTABLE_PACKAGE "Require package-root cwd and keep canonical rig state there" OFF)
if(KH2COOP_PORTABLE_PACKAGE)
    target_compile_definitions(kh2ctl PRIVATE KH2COOP_PORTABLE_PACKAGE=1)
else()
    target_compile_definitions(kh2ctl PRIVATE KH2COOP_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
endif()

# Internal rig product, never shipped by build_friend.py. Shares the portable
# launcher's package-root ownership registry but exposes only scoped diagnostics.
add_executable(kh2ctl_diagnostic EXCLUDE_FROM_ALL
    src/main.cpp
    src/world_resync.cpp
)
target_link_libraries(kh2ctl_diagnostic PRIVATE kh2coop_runtime user32 ole32 dbghelp)
target_compile_definitions(kh2ctl_diagnostic PRIVATE
    KH2COOP_PORTABLE_PACKAGE=1 KH2COOP_INTERNAL_DIAGNOSTIC=1)

# Separately reviewed internal media capability. Existing targets are unchanged.
add_executable(kh2ctl_clip EXCLUDE_FROM_ALL src/main.cpp src/world_resync.cpp)
target_link_libraries(kh2ctl_clip PRIVATE kh2coop_runtime user32 ole32 dbghelp bcrypt)
target_compile_definitions(kh2ctl_clip PRIVATE
    KH2COOP_PORTABLE_PACKAGE=1 KH2COOP_INTERNAL_DIAGNOSTIC=1 KH2COOP_INTERNAL_CLIP=1)

add_executable(kh2ctl_clip_test EXCLUDE_FROM_ALL src/main.cpp src/world_resync.cpp)
target_link_libraries(kh2ctl_clip_test PRIVATE kh2coop_runtime user32 ole32 dbghelp bcrypt)
target_compile_definitions(kh2ctl_clip_test PRIVATE KH2COOP_PORTABLE_PACKAGE=1
    KH2COOP_INTERNAL_DIAGNOSTIC=1 KH2COOP_INTERNAL_CLIP=1 KH2COOP_CLIP_OFFLINE_TEST=1)
if(MSVC)
    target_compile_options(kh2ctl_clip PRIVATE /W4 /permissive-)
    target_compile_options(kh2ctl_clip_test PRIVATE /W4 /permissive-)
else()
    target_compile_options(kh2ctl_clip PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(kh2ctl_clip_test PRIVATE -Wall -Wextra -Wpedantic)
endif()
set(KH2_CLIP_TEST_ENCODER "" CACHE FILEPATH "Pinned offline media-control ffmpeg")
set(KH2_CLIP_TEST_ENCODER_SHA256 "" CACHE STRING "Pinned offline media-control encoder SHA256")
# These explicit targets are excluded from the ordinary build. Do not register
# their encoder-dependent controls in the default repository CTest suite.
option(KH2_CLIP_OFFLINE_CONTROLS "Register the eight isolated owned-clip controls" OFF)
if(KH2_CLIP_OFFLINE_CONTROLS)
    if(NOT KH2_CLIP_TEST_ENCODER OR NOT KH2_CLIP_TEST_ENCODER_SHA256)
        message(FATAL_ERROR "Owned-clip controls require a pinned encoder path and SHA256")
    endif()
    # Root currently enables testing after entering this directory.
    enable_testing()
    foreach(clip_case clock hash quote exited cancel deadline descendant encode_decode)
        add_test(NAME kh2clip_${clip_case} COMMAND kh2ctl_clip_test --offline-control ${clip_case}
            "${CMAKE_CURRENT_BINARY_DIR}/clip-controls" "${KH2_CLIP_TEST_ENCODER}" "${KH2_CLIP_TEST_ENCODER_SHA256}")
        set_tests_properties(kh2clip_${clip_case} PROPERTIES TIMEOUT 55 RUN_SERIAL TRUE)
    endforeach()
endif()
