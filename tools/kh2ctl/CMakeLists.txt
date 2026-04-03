add_executable(kh2ctl
    src/main.cpp
)

target_link_libraries(kh2ctl PRIVATE
    kh2coop_runtime
    user32
)

target_compile_definitions(kh2ctl PRIVATE
    KH2COOP_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)
