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
