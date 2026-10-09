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
