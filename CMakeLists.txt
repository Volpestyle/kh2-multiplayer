cmake_minimum_required(VERSION 3.20)
project(kh2coop LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(kh2coop_latency_config_test tests/LatencyConfigTest.cpp)
target_include_directories(kh2coop_latency_config_test PRIVATE common/include inject/src)
if(MSVC)
    target_compile_options(kh2coop_latency_config_test PRIVATE /W4 /WX)
else()
    target_compile_options(kh2coop_latency_config_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

add_executable(kh2coop_avatar_position_test tests/AvatarPositionDiagnosticTest.cpp)
target_include_directories(kh2coop_avatar_position_test PRIVATE common/include inject/src)
if(MSVC)
    target_compile_options(kh2coop_avatar_position_test PRIVATE /W4 /WX)
else()
    target_compile_options(kh2coop_avatar_position_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

if(WIN32)
    add_executable(kh2coop_avatar_position_fault_test tests/AvatarPositionFaultTest.cpp)
    target_include_directories(kh2coop_avatar_position_fault_test PRIVATE common/include inject/src)
    target_compile_definitions(kh2coop_avatar_position_fault_test PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    if(MSVC)
        target_compile_options(kh2coop_avatar_position_fault_test PRIVATE /W4 /WX)
    endif()
endif()

find_package(Threads REQUIRED)
add_executable(kh2coop_downed_spectate_adapter_test tests/DownedSpectateAdapterTest.cpp)
target_include_directories(kh2coop_downed_spectate_adapter_test PRIVATE inject/src)
target_link_libraries(kh2coop_downed_spectate_adapter_test PRIVATE Threads::Threads)
if(MSVC)
    target_compile_options(kh2coop_downed_spectate_adapter_test PRIVATE /W4 /WX)
else()
    target_compile_options(kh2coop_downed_spectate_adapter_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()


# ---------------------------------------------------------------------------
# ENet — lightweight UDP networking library for game traffic.
# ---------------------------------------------------------------------------
include(FetchContent)
# ENet v1.3.17 declares cmake_minimum_required < 3.5, which CMake 4 rejects.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
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
    common/src/EnetTransport.cpp
    common/src/SteamBroker.cpp
    common/src/SteamPipe.cpp
    common/src/SteamTransport.cpp
)

add_executable(kh2coop_transport_test tests/TransportTest.cpp)
target_link_libraries(kh2coop_transport_test PRIVATE kh2coop_common)
if(MSVC)
    target_compile_options(kh2coop_transport_test PRIVATE /W4 /WX)
    set_source_files_properties(common/src/EnetTransport.cpp PROPERTIES COMPILE_OPTIONS "/W4;/WX")
else()
    target_compile_options(kh2coop_transport_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
    set_source_files_properties(common/src/EnetTransport.cpp PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra;-Wpedantic;-Werror")
endif()

target_include_directories(kh2coop_common PUBLIC
    common/include
    ${enet_SOURCE_DIR}/include
)

target_link_libraries(kh2coop_common PUBLIC enet)

# ENet requires Winsock2 and winmm on Windows.
if(WIN32)
    target_link_libraries(kh2coop_common PUBLIC ws2_32 winmm advapi32)
endif()

# ---------------------------------------------------------------------------
# kh2coop_server_core — session host logic (library so tests can link it).
# ---------------------------------------------------------------------------
add_library(kh2coop_server_core
    server/src/SessionHost.cpp
    server/src/SimulationState.cpp
    server/src/DesyncCapture.cpp
)

target_include_directories(kh2coop_server_core PUBLIC
    server/include
)

target_link_libraries(kh2coop_server_core PUBLIC kh2coop_common Threads::Threads)

add_executable(kh2coop_party_test tests/PartyLayoutTest.cpp)
target_link_libraries(kh2coop_party_test PRIVATE kh2coop_server_core)
if(MSVC)
    target_compile_options(kh2coop_party_test PRIVATE /W4 /WX)
else()
    target_compile_options(kh2coop_party_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

add_executable(kh2coop_party_intent_test tests/PartyIntentTest.cpp)
target_link_libraries(kh2coop_party_intent_test PRIVATE kh2coop_server_core)
if(MSVC)
    target_compile_options(kh2coop_party_intent_test PRIVATE /W4 /WX)
else()
    target_compile_options(kh2coop_party_intent_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

add_executable(kh2coop_prologue_seats_test tests/PrologueSeatsTest.cpp)
target_link_libraries(kh2coop_prologue_seats_test PRIVATE kh2coop_server_core)
if(MSVC)
    target_compile_options(kh2coop_prologue_seats_test PRIVATE /W4 /WX)
else()
    target_compile_options(kh2coop_prologue_seats_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

if(WIN32)
    add_executable(kh2coop_native_party_hud_test tests/NativePartyHudTest.cpp)
    target_include_directories(kh2coop_native_party_hud_test PRIVATE common/include runtime/include inject/src)
    target_link_libraries(kh2coop_native_party_hud_test PRIVATE kh2coop_common minhook_lib)
    target_compile_definitions(kh2coop_native_party_hud_test PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    if(MSVC)
        target_compile_options(kh2coop_native_party_hud_test PRIVATE /W4 /WX /wd4324)
    endif()
endif()

add_executable(kh2coop_revive_test tests/ReviveTest.cpp)
target_link_libraries(kh2coop_revive_test PRIVATE kh2coop_server_core)
if(MSVC)
    target_compile_options(kh2coop_revive_test PRIVATE /W4 /WX /wd4324) # existing cache-line padding in AvatarBridge
else()
    target_compile_options(kh2coop_revive_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

add_executable(kh2coop_steam_broker_test tests/SteamBrokerTest.cpp)
target_link_libraries(kh2coop_steam_broker_test PRIVATE kh2coop_server_core)
if(WIN32)
    add_executable(kh2coop_steam_pipe_test tests/SteamPipeTest.cpp)
    target_link_libraries(kh2coop_steam_pipe_test PRIVATE kh2coop_common)
    add_executable(kh2coop_steam_native_test tests/SteamNativeAdapterTest.cpp)
    target_link_libraries(kh2coop_steam_native_test PRIVATE kh2coop_common)
    target_compile_options(kh2coop_steam_pipe_test PRIVATE /W4 /WX)
    target_compile_options(kh2coop_steam_native_test PRIVATE /W4 /WX)
    target_compile_definitions(kh2coop_steam_native_test PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS)
endif()
if(MSVC)
    target_compile_options(kh2coop_steam_broker_test PRIVATE /W4 /WX)
    set_source_files_properties(common/src/SteamBroker.cpp common/src/SteamPipe.cpp common/src/SteamTransport.cpp PROPERTIES COMPILE_OPTIONS "/W4;/WX")
else()
    target_compile_options(kh2coop_steam_broker_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
    set_source_files_properties(common/src/SteamBroker.cpp common/src/SteamPipe.cpp common/src/SteamTransport.cpp PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra;-Wpedantic;-Werror")
endif()

# ---------------------------------------------------------------------------
# kh2coop_server — the host-authoritative relay executable.
# ---------------------------------------------------------------------------
add_executable(kh2coop_server
    server/src/ServerMain.cpp
)

target_link_libraries(kh2coop_server PRIVATE kh2coop_server_core)

# ---------------------------------------------------------------------------
# kh2coop_runtime — runtime library (GameBridgePC + controllers).
# Compiles on all platforms; Windows-only code is behind #ifdef _WIN32.
# ---------------------------------------------------------------------------
add_library(kh2coop_runtime
    runtime/src/GameBridgePC.cpp
    runtime/src/DesyncCollector.cpp
)

target_include_directories(kh2coop_runtime PUBLIC
    runtime/include
)

target_link_libraries(kh2coop_runtime PUBLIC kh2coop_common Threads::Threads)

# ---------------------------------------------------------------------------
# kh2coop_runtime_scaffold — placeholder entry point for the runtime.
# ---------------------------------------------------------------------------
add_executable(kh2coop_runtime_scaffold
    runtime/src/RuntimeMain.cpp
)

target_link_libraries(kh2coop_runtime_scaffold PRIVATE kh2coop_runtime kh2coop_server_core)

# ---------------------------------------------------------------------------
# kh2coop_fake_sim — end-to-end test: 3 fake clients + server, no KH2.
# ---------------------------------------------------------------------------
add_executable(kh2coop_fake_sim
    tests/FakeSimulation.cpp
)

target_link_libraries(kh2coop_fake_sim PRIVATE kh2coop_server_core kh2coop_runtime)

# ---------------------------------------------------------------------------
# kh2coop_avatar_test — avatar relay, clock sync, interpolation and simulated
# latency/loss end to end (VUH-1492), no KH2.
# ---------------------------------------------------------------------------
add_executable(kh2coop_avatar_test
    tests/AvatarRelayTest.cpp
)

target_link_libraries(kh2coop_avatar_test PRIVATE kh2coop_server_core kh2coop_runtime)

# kh2coop_avatar_hold_test — VUH-1787 stalled-stream hold and release rules
# plus a seeded stall soak. Pure AvatarSync: no sockets.
add_executable(kh2coop_avatar_hold_test tests/AvatarHoldTest.cpp)
target_link_libraries(kh2coop_avatar_hold_test PRIVATE kh2coop_common)
target_include_directories(kh2coop_avatar_hold_test PRIVATE inject/src)
if(MSVC)
    target_compile_options(kh2coop_avatar_hold_test PRIVATE /W4 /WX)
else()
    target_compile_options(kh2coop_avatar_hold_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

# ---------------------------------------------------------------------------
# kh2coop_world_test — host-authored transitions, cutscene holds, enemy
# manifest/HP/deaths, hit claims and late-joiner catch-up via the relay.
# ---------------------------------------------------------------------------
add_executable(kh2coop_world_test
    tests/WorldSyncTest.cpp
)

target_link_libraries(kh2coop_world_test PRIVATE kh2coop_server_core kh2coop_common)

# Same-room HP producer ordering through actual local ENet and cache replay.
add_executable(kh2coop_hp_order_test tests/EnemyHpOrderTest.cpp)
target_link_libraries(kh2coop_hp_order_test PRIVATE kh2coop_server_core kh2coop_common)
if(MSVC)
    target_compile_options(kh2coop_hp_order_test PRIVATE /W4 /WX)
else()
    target_compile_options(kh2coop_hp_order_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

# Default-off scoped record correspondence; no live native process.
add_executable(kh2coop_enemy_record_binding_test tests/EnemyRecordBindingTest.cpp)
target_link_libraries(kh2coop_enemy_record_binding_test PRIVATE kh2coop_common)
target_include_directories(kh2coop_enemy_record_binding_test PRIVATE inject/src)
if(MSVC)
    target_compile_options(kh2coop_enemy_record_binding_test PRIVATE /W4 /WX)
else()
    target_compile_options(kh2coop_enemy_record_binding_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

# VUH-1515 step 2: EnemyMotion codec, relay forwarding rules and the pure
# client stream state (inject/src/EnemyMirrorState.hpp). Loopback ENet, no KH2.
add_executable(kh2coop_enemy_mirror_test tests/EnemyMirrorTest.cpp)
target_link_libraries(kh2coop_enemy_mirror_test PRIVATE kh2coop_server_core kh2coop_common)
target_include_directories(kh2coop_enemy_mirror_test PRIVATE inject/src)
if(MSVC)
    target_compile_options(kh2coop_enemy_mirror_test PRIVATE /W4 /WX)
else()
    target_compile_options(kh2coop_enemy_mirror_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

# VUH-2252: ScriptCall codec, relay forwarding and the send-on-change rules
# (inject/src/ScriptMirror.hpp). Loopback ENet, no KH2.
add_executable(kh2coop_script_mirror_test tests/ScriptMirrorTest.cpp)
add_executable(kh2coop_spawn_emission_test tests/SpawnEmissionTest.cpp)

# VUH-2342: MissionState codec and the mission mirror's rules (inject/src/MissionMirror.hpp).
add_executable(kh2coop_mission_mirror_test tests/MissionMirrorTest.cpp)
add_executable(kh2coop_thorn_defeat_test tests/ThornDefeatTest.cpp)
target_include_directories(kh2coop_thorn_defeat_test PRIVATE inject/src)
if(MSVC)
    target_compile_options(kh2coop_thorn_defeat_test PRIVATE /W4 /WX)
else()
    target_compile_options(kh2coop_thorn_defeat_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
target_link_libraries(kh2coop_mission_mirror_test PRIVATE kh2coop_common)
target_include_directories(kh2coop_mission_mirror_test PRIVATE inject/src)
target_compile_definitions(kh2coop_mission_mirror_test PRIVATE
    KH2COOP_TERMINAL_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/mansion_terminal")
if(MSVC)
    target_compile_options(kh2coop_mission_mirror_test PRIVATE /W4 /WX)
else()
    target_compile_options(kh2coop_mission_mirror_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

# VUH-2276: complete field roster codec, host admission and script identity retirement.
add_executable(kh2coop_field_objects_test tests/FieldObjectsTest.cpp)
target_link_libraries(kh2coop_field_objects_test PRIVATE kh2coop_server_core)
if(MSVC)
    target_compile_options(kh2coop_field_objects_test PRIVATE /W4 /WX)
else()
    target_compile_options(kh2coop_field_objects_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

target_link_libraries(kh2coop_spawn_emission_test PRIVATE kh2coop_server_core)
if(MSVC)
    target_compile_options(kh2coop_spawn_emission_test PRIVATE /W4 /WX)
else()
    target_compile_options(kh2coop_spawn_emission_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
target_link_libraries(kh2coop_script_mirror_test PRIVATE kh2coop_server_core kh2coop_common)
target_include_directories(kh2coop_script_mirror_test PRIVATE inject/src)
if(MSVC)
    target_compile_options(kh2coop_script_mirror_test PRIVATE /W4 /WX)
else()
    target_compile_options(kh2coop_script_mirror_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

add_executable(kh2coop_resync_test tests/ForcedResyncTest.cpp)
target_link_libraries(kh2coop_resync_test PRIVATE kh2coop_server_core kh2coop_common)
if(MSVC)
    target_compile_options(kh2coop_resync_test PRIVATE /W4 /WX)
else()
    target_compile_options(kh2coop_resync_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

# Bounded automatic notice policy with the production request generator.
add_executable(kh2coop_automatic_notice_test tests/AutomaticResyncNoticeTest.cpp)
target_link_libraries(kh2coop_automatic_notice_test PRIVATE kh2coop_server_core)
if(MSVC)
    target_compile_options(kh2coop_automatic_notice_test PRIVATE /W4 /WX)
else()
    target_compile_options(kh2coop_automatic_notice_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

# Automatic diagnostic collection over real ENet peers and owned test files.
# Native capture is represented by an explicit synthetic provider, never KH2.
add_executable(kh2coop_desync_test tests/DesyncCaptureTest.cpp)
target_link_libraries(kh2coop_desync_test PRIVATE kh2coop_server_core kh2coop_runtime)
add_executable(kh2coop_runtime_desync_smoke tests/RuntimeDesyncSmoke.cpp)
target_link_libraries(kh2coop_runtime_desync_smoke PRIVATE kh2coop_server_core)
if(WIN32)
    target_compile_definitions(kh2coop_desync_test PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_compile_definitions(kh2coop_runtime_desync_smoke PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    add_executable(kh2coop_desync_collector_test tests/DesyncCollectorTest.cpp)
    target_link_libraries(kh2coop_desync_collector_test PRIVATE kh2coop_runtime)
    target_compile_definitions(kh2coop_desync_collector_test PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
endif()

# Production bounded friend-recovery coordinator and real loopback bootstrap.
add_executable(kh2coop_recovery_test tests/ClientRecoveryTest.cpp)
target_link_libraries(kh2coop_recovery_test PRIVATE kh2coop_server_core kh2coop_common)
if(MSVC)
    target_compile_options(kh2coop_recovery_test PRIVATE /W4 /WX)
else()
    target_compile_options(kh2coop_recovery_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

add_executable(kh2coop_activation_test tests/ActivationLeaseTest.cpp)
target_link_libraries(kh2coop_activation_test PRIVATE kh2coop_common)

add_executable(kh2coop_activation_replay_wire_test tests/ActivationReplayWireTest.cpp)
target_link_libraries(kh2coop_activation_replay_wire_test PRIVATE kh2coop_common)
if(MSVC)
    target_compile_options(kh2coop_activation_replay_wire_test PRIVATE /W4 /WX)
else()
    target_compile_options(kh2coop_activation_replay_wire_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

# Exact ordinary record-content comparison; no native creation or lifetime claim.
add_executable(kh2coop_record_content_test tests/NativeRecordContentTest.cpp)
target_link_libraries(kh2coop_record_content_test PRIVATE kh2coop_common)
if(MSVC)
    target_compile_options(kh2coop_record_content_test PRIVATE /W4 /WX)
else()
    target_compile_options(kh2coop_record_content_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

# Bounded surviving-pack preparation and sticky outcomes; never dispatches native code.
add_executable(kh2coop_pack_preparation_test tests/SurvivingPackPreparationTest.cpp)
target_link_libraries(kh2coop_pack_preparation_test PRIVATE kh2coop_common)
if(MSVC)
    target_compile_options(kh2coop_pack_preparation_test PRIVATE /W4 /WX)
else()
    target_compile_options(kh2coop_pack_preparation_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

# ---------------------------------------------------------------------------
# kh2coop_bridge_test — PacketRing + WorldBridge DLL<->runtime plumbing, with a
# fake DLL on each side of the relay.
# ---------------------------------------------------------------------------
add_executable(kh2coop_bridge_test
    tests/BridgeTest.cpp
)

target_link_libraries(kh2coop_bridge_test PRIVATE kh2coop_server_core kh2coop_common)

# ---------------------------------------------------------------------------
# avatarctl — record/replay/synthesize avatar streams through a KH2
# instance's AvatarBridge (puppet-driver testing without a network).
# ---------------------------------------------------------------------------
if(WIN32)
    add_executable(avatarctl tools/avatarctl/main.cpp)
    target_link_libraries(avatarctl PRIVATE kh2coop_common winmm)
endif()

# ---------------------------------------------------------------------------
# kh2coop_inject — in-process DLL for friend AI replacement (Strategy B).
# Loads into KH2's process and hooks PerEntityUpdate to replace friend AI
# with player input from XInput gamepads.
# Only builds on Windows (requires MinHook, XInput, PE headers).
# ---------------------------------------------------------------------------
if(WIN32)
    enable_language(ASM_MASM)
    add_subdirectory(tools/kh2ctl)
    add_subdirectory(inject)

    # Actual trace scope/queue code with synthetic native callbacks and deliberate
    # SEH. The test supplies unavailable MinHook stubs; it never installs hooks
    # or accesses a game process.
    add_executable(kh2coop_spawntrace_test tests/NativeSpawnTraceTest.cpp)
    target_sources(kh2coop_spawntrace_test PRIVATE inject/src/NativeResourceTrace.cpp)
    target_include_directories(kh2coop_spawntrace_test PRIVATE
        runtime/include
        common/include
        $<TARGET_PROPERTY:minhook_lib,INTERFACE_INCLUDE_DIRECTORIES>
    )
    target_link_libraries(kh2coop_spawntrace_test PRIVATE kh2coop_common)
    target_compile_definitions(kh2coop_spawntrace_test PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    if(MSVC)
        target_compile_options(kh2coop_spawntrace_test PRIVATE /W4 /WX)
    endif()

    # The concrete package callback observer over task-owned memory and native
    # Windows SEH/MinHook. No game is launched or accessed by this executable.
    add_executable(kh2coop_resourcetrace_test tests/NativeResourceTraceTest.cpp)
    target_include_directories(kh2coop_resourcetrace_test PRIVATE
        runtime/include
        common/include
        $<TARGET_PROPERTY:minhook_lib,INTERFACE_INCLUDE_DIRECTORIES>
    )
    target_link_libraries(kh2coop_resourcetrace_test PRIVATE minhook_lib kh2coop_common)
    target_compile_definitions(kh2coop_resourcetrace_test PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    if(MSVC)
        target_compile_options(kh2coop_resourcetrace_test PRIVATE /W4 /WX)
    endif()

    add_executable(kh2coop_lifecycletrace_test tests/NativeLifecycleTraceTest.cpp)
    target_include_directories(kh2coop_lifecycletrace_test PRIVATE
        runtime/include
        common/include
        $<TARGET_PROPERTY:minhook_lib,INTERFACE_INCLUDE_DIRECTORIES>
    )
    target_compile_definitions(kh2coop_lifecycletrace_test PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    if(MSVC)
        target_compile_options(kh2coop_lifecycletrace_test PRIVATE /W4 /WX)
    endif()

    # VUH-2897: actual observational adapters over a read-only owned image.
    add_executable(kh2coop_axel_boundary_trace_test tests/AxelBoundaryTraceTest.cpp)
    target_include_directories(kh2coop_axel_boundary_trace_test PRIVATE runtime/include
        $<TARGET_PROPERTY:minhook_lib,INTERFACE_INCLUDE_DIRECTORIES>)
    target_compile_definitions(kh2coop_axel_boundary_trace_test PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    if(MSVC)
        target_compile_options(kh2coop_axel_boundary_trace_test PRIVATE /W4 /WX)
    endif()

    # VUH-2897: combat-admission adapter over an owned executable image; real teardown
    # call-site and 3BF2C0 bytes run, MinHook is stubbed.
    add_executable(kh2coop_combat_admission_native_test tests/CombatAdmissionNativeTest.cpp)
    target_include_directories(kh2coop_combat_admission_native_test PRIVATE runtime/include
        $<TARGET_PROPERTY:minhook_lib,INTERFACE_INCLUDE_DIRECTORIES>)
    target_compile_definitions(kh2coop_combat_admission_native_test PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    if(MSVC)
        target_compile_options(kh2coop_combat_admission_native_test PRIVATE /W4 /WX)
    endif()

    # VUH-2881: actual orb detours and thread logging, no native hooks installed.
    add_executable(kh2coop_struggle_orbs_hook_test tests/StruggleOrbsHookTest.cpp)
    target_include_directories(kh2coop_struggle_orbs_hook_test PRIVATE common/include
        $<TARGET_PROPERTY:minhook_lib,INTERFACE_INCLUDE_DIRECTORIES>)
    target_link_libraries(kh2coop_struggle_orbs_hook_test PRIVATE minhook_lib kh2coop_common)
    target_compile_definitions(kh2coop_struggle_orbs_hook_test PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    if(MSVC)
        target_compile_options(kh2coop_struggle_orbs_hook_test PRIVATE /W4 /WX)
    endif()

    # Real hit consumer/publisher and census over owned synthetic native memory;
    # only transport, lifecycle and unrelated diagnostic services are stubbed.
    add_executable(kh2coop_nativehit_test tests/NativeHitClaimTest.cpp inject/src/NativeHitTrace.cpp
        inject/src/NativeSpawnController.cpp inject/src/NativeResourceTrace.cpp inject/src/EventHoldNative.cpp)
    target_include_directories(kh2coop_nativehit_test PRIVATE runtime/include common/include
        $<TARGET_PROPERTY:minhook_lib,INTERFACE_INCLUDE_DIRECTORIES>)
    target_link_libraries(kh2coop_nativehit_test PRIVATE kh2coop_common)
    target_compile_definitions(kh2coop_nativehit_test PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    if(MSVC)
        target_compile_options(kh2coop_nativehit_test PRIVATE /W4 /WX)
    endif()

    # Generic generation claim hold and complete native readback release.
    add_executable(kh2coop_claim_hold_test tests/ClientClaimHoldTest.cpp inject/src/NativeHitTrace.cpp
        inject/src/NativeSpawnController.cpp inject/src/NativeResourceTrace.cpp inject/src/EventHoldNative.cpp)
    target_include_directories(kh2coop_claim_hold_test PRIVATE runtime/include common/include
        $<TARGET_PROPERTY:minhook_lib,INTERFACE_INCLUDE_DIRECTORIES>)
    target_link_libraries(kh2coop_claim_hold_test PRIVATE kh2coop_common)
    target_compile_definitions(kh2coop_claim_hold_test PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    if(MSVC)
        target_compile_options(kh2coop_claim_hold_test PRIVATE /W4 /WX)
    endif()

    # Native load attribution and interval seals over owned memory.
    add_executable(kh2coop_load_cause_test tests/NativeLoadCauseTest.cpp inject/src/NativeHitTrace.cpp
        inject/src/NativeSpawnController.cpp inject/src/NativeResourceTrace.cpp inject/src/EventHoldNative.cpp)
    target_include_directories(kh2coop_load_cause_test PRIVATE runtime/include common/include
        $<TARGET_PROPERTY:minhook_lib,INTERFACE_INCLUDE_DIRECTORIES>)
    target_link_libraries(kh2coop_load_cause_test PRIVATE kh2coop_common)
    target_compile_definitions(kh2coop_load_cause_test PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    if(MSVC)
        target_compile_options(kh2coop_load_cause_test PRIVATE /W4 /WX)
    endif()

    # Actual bounded hit/policy serializer and owned-record zero leaf over
    # synthetic originals; no game process or MinHook installation.
    add_executable(kh2coop_hittrace_test tests/NativeHitTraceTest.cpp inject/src/NativeHitTrace.cpp inject/src/DamagePolicy.cpp)
    target_include_directories(kh2coop_hittrace_test PRIVATE inject/src runtime/include common/include)
    target_compile_definitions(kh2coop_hittrace_test PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    if(MSVC)
        target_compile_options(kh2coop_hittrace_test PRIVATE /W4 /WX)
    endif()

    add_executable(kh2coop_combat_causal_test tests/CombatCausalTraceTest.cpp inject/src/NativeHitTrace.cpp)
    target_include_directories(kh2coop_combat_causal_test PRIVATE inject/src common/include)
    target_compile_definitions(kh2coop_combat_causal_test PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(kh2coop_combat_causal_test PRIVATE kh2coop_common)
    if(MSVC)
        target_compile_options(kh2coop_combat_causal_test PRIVATE /W4 /WX)
    endif()

    # Production HP permission policy and checked zero-write leaf; only owned
    # record pages and synthetic native callbacks, never a game process.
    add_executable(kh2coop_damagepolicy_test tests/DamagePolicyTest.cpp inject/src/DamagePolicy.cpp)
    target_include_directories(kh2coop_damagepolicy_test PRIVATE inject/src)
    target_compile_definitions(kh2coop_damagepolicy_test PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    if(MSVC)
        target_compile_options(kh2coop_damagepolicy_test PRIVATE /W4 /WX)
    endif()

    # VUH-1504 downed/revive pure rules (gate, revive HP, episodes, channel layout).
    add_executable(kh2coop_downed_state_test tests/DownedSpikeStateTest.cpp)
    target_include_directories(kh2coop_downed_state_test PRIVATE inject/src)
    if(MSVC)
        target_compile_options(kh2coop_downed_state_test PRIVATE /W4 /WX)
    endif()

    # VUH-1489 clone neutral input: Gate (+0xDB8) and the 0x3A89A0 detour over fake game
    # memory with MinHook/warp/playerkit stand-ins; no game process, no installed hooks.
    # VUH-1515 step 2: the native mirrored-enemy driver (EnemyMirror.inl) over fake
    # game memory with MinHook/resolver/setter/MirrorPose stand-ins; no game, no hooks.
    add_executable(kh2coop_enemy_population_test tests/EnemyPopulationTest.cpp)
    target_include_directories(kh2coop_enemy_population_test PRIVATE inject/src common/include)
    if(MSVC)
        target_compile_options(kh2coop_enemy_population_test PRIVATE /W4 /WX)
    endif()

    add_executable(kh2coop_enemy_mirror_driver_test tests/EnemyMirrorDriverTest.cpp)
    target_include_directories(kh2coop_enemy_mirror_driver_test PRIVATE inject/src runtime/include common/include)
    target_compile_definitions(kh2coop_enemy_mirror_driver_test PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    if(MSVC)
        target_compile_options(kh2coop_enemy_mirror_driver_test PRIVATE /W4 /WX /wd4505)
    endif()

    add_executable(kh2coop_clone_neutral_test tests/CloneNeutralInputTest.cpp)
    target_include_directories(kh2coop_clone_neutral_test PRIVATE inject/src runtime/include common/include) # kit table
    target_compile_definitions(kh2coop_clone_neutral_test PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    if(MSVC)
        target_compile_options(kh2coop_clone_neutral_test PRIVATE /W4 /WX /wd4505)
    endif()
endif()

# Causal receipts: actual codec/ENet and owned-memory native boundary controls.
add_executable(kh2coop_causal_diagnostic_test tests/CausalDiagnosticTest.cpp)
add_executable(kh2coop_causal_exception_test tests/CausalExceptionTest.cpp)
add_executable(kh2coop_hash_notice_receipt_test tests/HashNoticeReceiptTest.cpp)
add_executable(kh2coop_statehash_census_test tests/StateHashCensusTest.cpp)
foreach(target kh2coop_causal_diagnostic_test kh2coop_causal_exception_test kh2coop_hash_notice_receipt_test kh2coop_statehash_census_test)
    target_link_libraries(${target} PRIVATE kh2coop_server_core)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /WX)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
endforeach()
if(WIN32)
    add_executable(kh2coop_native_hash_receipt_test tests/NativeHashReceiptTest.cpp
        inject/src/NativeHitTrace.cpp inject/src/NativeSpawnController.cpp inject/src/NativeResourceTrace.cpp inject/src/EventHoldNative.cpp)
    target_include_directories(kh2coop_native_hash_receipt_test PRIVATE inject/src runtime/include
        $<TARGET_PROPERTY:minhook_lib,INTERFACE_INCLUDE_DIRECTORIES>)
    target_compile_definitions(kh2coop_native_hash_receipt_test PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
    target_link_libraries(kh2coop_native_hash_receipt_test PRIVATE kh2coop_common)
    if(MSVC)
        target_compile_options(kh2coop_native_hash_receipt_test PRIVATE /W4 /WX)
    endif()
    # Headless controller users have no installed player/private-status hooks.
    foreach(target kh2coop_spawntrace_test kh2coop_nativehit_test kh2coop_claim_hold_test
                   kh2coop_load_cause_test kh2coop_native_hash_receipt_test)
        target_sources(${target} PRIVATE tests/NativeSpawnCreationStubs.cpp)
    endforeach()
endif()

add_executable(kh2coop_causal_stream_boundary_test tests/CausalStreamBoundaryTest.cpp)
add_executable(kh2coop_world_admission_receipt_test tests/WorldAdmissionReceiptTest.cpp)
# The receiver boundary test includes the actual implementation under the
# existing test-only visibility convention; do not compile a second copy.
foreach(target kh2coop_causal_stream_boundary_test kh2coop_world_admission_receipt_test)
    target_link_libraries(${target} PRIVATE kh2coop_common)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /WX)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
endforeach()

if(WIN32)
    add_executable(kh2coop_event_hold_control_test tests/EventHoldControlTest.cpp)
    target_include_directories(kh2coop_event_hold_control_test PRIVATE runtime/src)
    target_link_libraries(kh2coop_event_hold_control_test PRIVATE kh2coop_server_core kh2coop_common)
    add_executable(kh2coop_event_hold_input_test tests/EventHoldNativeInputTest.cpp inject/src/EventHoldNative.cpp)
    target_include_directories(kh2coop_event_hold_input_test PRIVATE inject/src runtime/include common/include)
    add_executable(kh2coop_owned_registry_test tests/OwnedRegistryTest.cpp)
    target_include_directories(kh2coop_owned_registry_test PRIVATE tools/kh2ctl/src)
    foreach(target kh2coop_event_hold_control_test kh2coop_event_hold_input_test kh2coop_owned_registry_test)
        target_compile_definitions(${target} PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
        if(MSVC)
            target_compile_options(${target} PRIVATE /W4 /WX)
        endif()
    endforeach()
endif()

# Owner-slot/HP projection and nonblocking render telemetry mailbox. No game.
if(WIN32)
    # Admitted peer labels, expiry and bounded shared-slot concurrency.
    add_executable(kh2coop_hud_names_test tests/HudNamesTest.cpp)
    target_include_directories(kh2coop_hud_names_test PRIVATE inject/src common/include)
    target_compile_definitions(kh2coop_hud_names_test PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    if(MSVC)
        # AvatarBridge's existing cache-line padding is intentional (C4324).
        target_compile_options(kh2coop_hud_names_test PRIVATE /W4 /WX /wd4324)
    endif()

    add_executable(kh2coop_hud_cpu_test tests/CoopHudStateTest.cpp)
    target_include_directories(kh2coop_hud_cpu_test PRIVATE inject/src common/include)
    target_compile_definitions(kh2coop_hud_cpu_test PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    if(MSVC)
        target_compile_options(kh2coop_hud_cpu_test PRIVATE /W4 /WX)
    endif()

    # VUH-2415 host crown: camera projection, clipped crown rectangles and the snapshot gate.
    add_executable(kh2coop_host_crown_test tests/HostCrownTest.cpp)
    target_include_directories(kh2coop_host_crown_test PRIVATE inject/src common/include runtime/include)
    target_compile_definitions(kh2coop_host_crown_test PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    if(MSVC)
        target_compile_options(kh2coop_host_crown_test PRIVATE /W4 /WX)
    endif()

    # VUH-2480 crown prop: inflate, EGS package, model group, sprite, head read and clip triangles.
    add_executable(kh2coop_crown_prop_test tests/CrownPropTest.cpp)
    target_include_directories(kh2coop_crown_prop_test PRIVATE inject/src common/include runtime/include)
    target_compile_definitions(kh2coop_crown_prop_test PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    if(MSVC)
        target_compile_options(kh2coop_crown_prop_test PRIVATE /W4 /WX)
    endif()

    # Ally player-to-player hits: pure policy (AllyHitPolicy.hpp) and the 3D2060 hook body over owned memory.
    add_executable(kh2coop_allyhit_test tests/AllyHitPolicyTest.cpp inject/src/AllyHit.cpp)
    target_include_directories(kh2coop_allyhit_test PRIVATE inject/src)
    target_compile_definitions(kh2coop_allyhit_test PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN KH2COOP_ALLYHIT_TESTING)
    if(MSVC)
        target_compile_options(kh2coop_allyhit_test PRIVATE /W4 /WX)
    endif()
    # VUH-1513 player kits: pure PlayerKit policy (local kit, puppet guard, remote layout).
    add_executable(kh2coop_playerkit_test tests/PlayerKitPolicyTest.cpp)
    target_include_directories(kh2coop_playerkit_test PRIVATE inject/src common/include) # kit table
    target_compile_definitions(kh2coop_playerkit_test PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    if(MSVC)
        target_compile_options(kh2coop_playerkit_test PRIVATE /W4 /WX)
    endif()

    # VUH-1515 enemy targeting of remote clones: pure policy (EnemyTargetRemote.hpp).
    add_executable(kh2coop_enemy_target_test tests/EnemyTargetRemoteTest.cpp)
    target_include_directories(kh2coop_enemy_target_test PRIVATE inject/src common/include)
    target_compile_definitions(kh2coop_enemy_target_test PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    if(MSVC)
        target_compile_options(kh2coop_enemy_target_test PRIVATE /W4 /WX)
    endif()

    # VUH-1515 host-agreed random spawn picks (SpawnPick.hpp): pure rules, native-formula parity, shape bytes.
    add_executable(kh2coop_spawnpick_test tests/SpawnPickTest.cpp)
    target_include_directories(kh2coop_spawnpick_test PRIVATE inject/src common/include)
    target_compile_definitions(kh2coop_spawnpick_test PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    if(MSVC)
        target_compile_options(kh2coop_spawnpick_test PRIVATE /W4 /WX)
    endif()

    # VUH-1515 spawn-row identity on a recycled actor address (SpawnRowIdentity.hpp): pure rules.
    add_executable(kh2coop_spawnrow_identity_test tests/SpawnRowIdentityTest.cpp)
    target_include_directories(kh2coop_spawnrow_identity_test PRIVATE inject/src)
    if(MSVC)
        target_compile_options(kh2coop_spawnrow_identity_test PRIVATE /W4 /WX)
    endif()

    # VUH-1504 revive prompt: hold/latch/cancel rules and its HUD row.
    add_executable(kh2coop_revive_prompt_test tests/RevivePromptTest.cpp)
    target_include_directories(kh2coop_revive_prompt_test PRIVATE inject/src common/include)
    target_compile_definitions(kh2coop_revive_prompt_test PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    if(MSVC)
        target_compile_options(kh2coop_revive_prompt_test PRIVATE /W4 /WX)
    endif()

    # VUH-1493: the Steam broker log is create-new and readable by the rig while the game runs.
    add_executable(kh2coop_shared_log_test tests/SharedLogFileTest.cpp)
    target_include_directories(kh2coop_shared_log_test PRIVATE inject/src)
    target_compile_definitions(kh2coop_shared_log_test PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    if(MSVC)
        target_compile_options(kh2coop_shared_log_test PRIVATE /W4 /WX)
    endif()
endif()

# Register every headless kh2coop_*_test executable defined above with CTest
# (ctest -C Release). None launches or attaches to the game. Several bind
# fixed loopback ports, so run serially (no -j).
if(WIN32 AND TARGET minhook_lib)
    add_library(kh2coop_preinject_guard SHARED tests/PreinjectGuardDll.cpp inject/src/SaveGuard.cpp)
    target_include_directories(kh2coop_preinject_guard PRIVATE inject/src common/include)
    target_link_libraries(kh2coop_preinject_guard PRIVATE minhook_lib)
    target_compile_definitions(kh2coop_preinject_guard PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
    add_executable(kh2coop_preinject_guard_test tests/PreinjectGuardTest.cpp)
    target_include_directories(kh2coop_preinject_guard_test PRIVATE common/include)
    target_compile_definitions(kh2coop_preinject_guard_test PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_dependencies(kh2coop_preinject_guard_test kh2coop_preinject_guard)
    if(MSVC)
        target_compile_options(kh2coop_preinject_guard_test PRIVATE /W4 /WX)
        target_compile_options(kh2coop_preinject_guard PRIVATE /W4 /WX)
    endif()
endif()
# Actual party-menu detours against owned synthetic native memory. Each retained
# install/signature failure requires a fresh process; CTest must remain serial.
if(WIN32 AND TARGET minhook_lib)
    add_executable(kh2coop_party_menu_admission_test tests/PartyMenuAdmissionTest.cpp)
    target_include_directories(kh2coop_party_menu_admission_test PRIVATE inject/src common/include
        $<TARGET_PROPERTY:minhook_lib,INTERFACE_INCLUDE_DIRECTORIES>)
    target_compile_definitions(kh2coop_party_menu_admission_test PRIVATE _CRT_SECURE_NO_WARNINGS)
    if(MSVC)
        target_compile_options(kh2coop_party_menu_admission_test PRIVATE /W4 /WX)
    endif()
endif()

enable_testing()
add_executable(kh2coop_factory_return_counter_test tests/FactoryReturnCounterTest.cpp)
target_include_directories(kh2coop_factory_return_counter_test PRIVATE inject/src)
if(MSVC)
    target_compile_options(kh2coop_factory_return_counter_test PRIVATE /W4 /WX)
else()
    target_compile_options(kh2coop_factory_return_counter_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME factory_return_counter COMMAND kh2coop_factory_return_counter_test)
add_executable(kh2coop_script_birth_host_test tests/ScriptBirthHostTest.cpp)
target_include_directories(kh2coop_script_birth_host_test PRIVATE inject/src)
if(MSVC)
    target_compile_options(kh2coop_script_birth_host_test PRIVATE /W4 /WX)
else()
    target_compile_options(kh2coop_script_birth_host_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME script_birth_host COMMAND kh2coop_script_birth_host_test)
add_executable(kh2coop_script_camera_test tests/ScriptCameraTest.cpp)
target_include_directories(kh2coop_script_camera_test PRIVATE inject/src)
add_executable(kh2coop_script_owner_command_test tests/ScriptOwnerCommandTest.cpp)
target_include_directories(kh2coop_script_owner_command_test PRIVATE inject/src)
target_link_libraries(kh2coop_script_owner_command_test PRIVATE kh2coop_common)
if(MSVC)
    target_compile_options(kh2coop_script_owner_command_test PRIVATE /W4 /WX)
else()
    target_compile_options(kh2coop_script_owner_command_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

# Native RC receipt/acceptance rules (no native memory or game process required).
add_executable(kh2coop_script_reaction_cache_test tests/ScriptReactionCacheTest.cpp)
target_include_directories(kh2coop_script_reaction_cache_test PRIVATE inject/src common/include)
if(MSVC)
    target_compile_options(kh2coop_script_reaction_cache_test PRIVATE /W4 /WX)
else()
    target_compile_options(kh2coop_script_reaction_cache_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
# VUH-2913: Axel II's collision-offered RC owner windows, presses and executor routing.
add_executable(kh2coop_axel_reaction_test tests/AxelReactionTest.cpp)
target_include_directories(kh2coop_axel_reaction_test PRIVATE inject/src common/include)
if(MSVC)
    target_compile_options(kh2coop_axel_reaction_test PRIVATE /W4 /WX)
else()
    target_compile_options(kh2coop_axel_reaction_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
# The adapter (AxelReactions.inl) against simulated native memory: completion, 1/3, the safe abort at 1/18, 1/29 and
# 1/101, and the client hold of the Axel clone.
add_executable(kh2coop_axel_reaction_adapter_test tests/AxelReactionAdapterTest.cpp)
target_include_directories(kh2coop_axel_reaction_adapter_test PRIVATE inject/src common/include)
target_link_libraries(kh2coop_axel_reaction_adapter_test PRIVATE kh2coop_common) # the codec's validScriptCallEntry
if(MSVC)
    target_compile_options(kh2coop_axel_reaction_adapter_test PRIVATE /W4 /WX)
else()
    target_compile_options(kh2coop_axel_reaction_adapter_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

add_executable(kh2coop_coop_difficulty_test tests/CoopDifficultyTest.cpp)
target_link_libraries(kh2coop_coop_difficulty_test PRIVATE kh2coop_server_core kh2coop_common)
if(MSVC)
    target_compile_options(kh2coop_coop_difficulty_test PRIVATE /W4 /WX)
else()
    target_compile_options(kh2coop_coop_difficulty_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
add_executable(kh2coop_population_cut_test tests/PopulationCutTest.cpp)
target_link_libraries(kh2coop_population_cut_test PRIVATE kh2coop_common)
add_executable(kh2coop_population_repair_native_test tests/PopulationRepairNativeTest.cpp)
target_include_directories(kh2coop_population_repair_native_test PRIVATE inject/src)
target_link_libraries(kh2coop_population_repair_native_test PRIVATE kh2coop_common)
if(MSVC)
    target_compile_options(kh2coop_population_cut_test PRIVATE /W4 /WX)
    target_compile_options(kh2coop_population_repair_native_test PRIVATE /W4 /WX)
else()
    target_compile_options(kh2coop_population_cut_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
    target_compile_options(kh2coop_population_repair_native_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
# VUH-2612: held actor pointers are followed only while listed (no game process required; Windows-only:
# the page check uses VirtualQuery).
if(WIN32)
    add_executable(kh2coop_actor_lifetime_test tests/ActorLifetimeTest.cpp)
    target_include_directories(kh2coop_actor_lifetime_test PRIVATE inject/src runtime/include)
    if(MSVC)
        target_compile_options(kh2coop_actor_lifetime_test PRIVATE /W4 /WX)
    else()
        target_compile_options(kh2coop_actor_lifetime_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
endif()
add_executable(kh2coop_downed_spectate_test tests/DownedSpectateTest.cpp)
target_include_directories(kh2coop_downed_spectate_test PRIVATE inject/src)
if(MSVC)
    target_compile_options(kh2coop_downed_spectate_test PRIVATE /W4 /WX)
else()
    target_compile_options(kh2coop_downed_spectate_test PRIVATE -Wall -Wextra -Wpedantic)
endif()
# VUH-2632 (D17): revive availability and hold time by the session's native difficulty.
add_executable(kh2coop_downed_policy_test tests/DownedPolicyTest.cpp)
target_include_directories(kh2coop_downed_policy_test PRIVATE inject/src)
if(MSVC)
    target_compile_options(kh2coop_downed_policy_test PRIVATE /W4 /WX)
else()
    target_compile_options(kh2coop_downed_policy_test PRIVATE -Wall -Wextra -Wpedantic)
endif()
# VUH-2632 (D17): when a downed teammate who sits out comes back.
add_executable(kh2coop_downed_return_test tests/DownedReturnTest.cpp)
target_include_directories(kh2coop_downed_return_test PRIVATE inject/src common/include)
if(MSVC)
    target_compile_options(kh2coop_downed_return_test PRIVATE /W4 /WX)
else()
    target_compile_options(kh2coop_downed_return_test PRIVATE -Wall -Wextra -Wpedantic)
endif()
# VUH-2897: CombatAdmissionAck framing and relay admission over loopback ENet.
add_executable(kh2coop_combat_admission_net_test tests/CombatAdmissionNetTest.cpp)
target_link_libraries(kh2coop_combat_admission_net_test PRIVATE kh2coop_server_core kh2coop_common)
if(MSVC)
    target_compile_options(kh2coop_combat_admission_net_test PRIVATE /W4 /WX)
else()
    target_compile_options(kh2coop_combat_admission_net_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
# VUH-2897: combat-admission lease (inject/src/CombatAdmission.hpp), std-only.
add_executable(kh2coop_combat_admission_test tests/CombatAdmissionTest.cpp)
target_include_directories(kh2coop_combat_admission_test PRIVATE inject/src)
if(MSVC)
    target_compile_options(kh2coop_combat_admission_test PRIVATE /W4 /WX)
else()
    target_compile_options(kh2coop_combat_admission_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
# VUH-2617 (a): which updating actor the default-off Thorn hit trace may read.
add_executable(kh2coop_thorn_hit_trace_test tests/ThornHitTraceTest.cpp)
target_include_directories(kh2coop_thorn_hit_trace_test PRIVATE inject/src)
if(MSVC)
    target_compile_options(kh2coop_thorn_hit_trace_test PRIVATE /W4 /WX)
else()
    target_compile_options(kh2coop_thorn_hit_trace_test PRIVATE -Wall -Wextra -Wpedantic)
endif()
# VUH-2466: Struggle orb rows (protocol 34) and the host-book / friend-display rules.
add_executable(kh2coop_struggle_orbs_test tests/StruggleOrbsTest.cpp)
target_include_directories(kh2coop_struggle_orbs_test PRIVATE inject/src)
target_link_libraries(kh2coop_struggle_orbs_test PRIVATE kh2coop_common)
if(MSVC)
    target_compile_options(kh2coop_struggle_orbs_test PRIVATE /W4 /WX)
else()
    target_compile_options(kh2coop_struggle_orbs_test PRIVATE -Wall -Wextra -Wpedantic)
endif()
# VUH-2912 (lead #76): per-actor 0x3D6FC0 call counts for the hit witness, and the runner's stats-mark channel.
add_executable(kh2coop_reaction_calls_test tests/ReactionCallsTest.cpp)
target_include_directories(kh2coop_reaction_calls_test PRIVATE inject/src)
if(MSVC)
    target_compile_options(kh2coop_reaction_calls_test PRIVATE /W4 /WX)
else()
    target_compile_options(kh2coop_reaction_calls_test PRIVATE -Wall -Wextra -Wpedantic)
endif()
get_property(_kh2coop_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
foreach(_kh2coop_target IN LISTS _kh2coop_targets)
    if(_kh2coop_target MATCHES "^kh2coop_.*_test$")
        add_test(NAME ${_kh2coop_target} COMMAND ${_kh2coop_target})
    endif()
endforeach()
if(TARGET kh2coop_event_hold_input_test)
    foreach(_event_mode off abort unowned elsewhere thorn-owned thorn-owned-closing
                        thorn-late-owner thorn-missing-end thorn-no-end thorn-late-owner-closing
                        closing-315 closing-release closing-abort closing-scope
                        closing-partial closing-unconverged closing-frozen closing-blocked closing-event
                        closing-context closing-field closing-menu closing-foreign)
        add_test(NAME kh2coop_event_hold_input_test_${_event_mode}
                 COMMAND kh2coop_event_hold_input_test ${_event_mode})
    endforeach()
endif()
if(TARGET kh2coop_resourcetrace_test)
    # Each retained-install failure boundary runs in its own process.
    foreach(_kh2coop_mode identity allocate register pin enable)
        add_test(NAME kh2coop_resourcetrace_test_${_kh2coop_mode}
                 COMMAND kh2coop_resourcetrace_test ${_kh2coop_mode})
    endforeach()
endif()

if(TARGET kh2coop_party_menu_admission_test)
    foreach(_party_mode trace trace-nonexact paired-donald paired-goofy apply)
        add_test(NAME kh2coop_party_menu_admission_test_${_party_mode}
                 COMMAND kh2coop_party_menu_admission_test ${_party_mode})
    endforeach()
    foreach(_party_index RANGE 0 17)
        add_test(NAME kh2coop_party_menu_admission_test_signature-${_party_index}
                 COMMAND kh2coop_party_menu_admission_test signature-${_party_index})
    endforeach()
    foreach(_party_kind create enable)
        foreach(_party_index RANGE 0 6)
            add_test(NAME kh2coop_party_menu_admission_test_${_party_kind}-${_party_index}
                     COMMAND kh2coop_party_menu_admission_test ${_party_kind}-${_party_index})
        endforeach()
    endforeach()
    foreach(_party_index RANGE 0 15)
        add_test(NAME kh2coop_party_menu_admission_test_party-signature-${_party_index}
                 COMMAND kh2coop_party_menu_admission_test party-signature-${_party_index})
    endforeach()
    foreach(_status_index RANGE 0 9)
        add_test(NAME kh2coop_party_menu_admission_test_status-signature-${_status_index}
                 COMMAND kh2coop_party_menu_admission_test status-signature-${_status_index})
    endforeach()
endif()

if(TARGET kh2coop_party_menu_admission_test)
    foreach(_abilities_index RANGE 0 9)
        add_test(NAME kh2coop_party_menu_admission_test_abilities-signature-${_abilities_index}
                 COMMAND kh2coop_party_menu_admission_test abilities-signature-${_abilities_index})
    endforeach()
endif()

if(TARGET kh2coop_party_menu_admission_test)
    foreach(_equipment_index RANGE 0 17)
        add_test(NAME kh2coop_party_menu_admission_test_equipment-signature-${_equipment_index}
                 COMMAND kh2coop_party_menu_admission_test equipment-signature-${_equipment_index})
    endforeach()
endif()


if(WIN32)
    add_executable(kh2coop_limit_admission_test tests/LimitAdmissionTest.cpp)
    target_include_directories(kh2coop_limit_admission_test PRIVATE inject/src)
    target_link_libraries(kh2coop_limit_admission_test PRIVATE minhook_lib)
    target_compile_definitions(kh2coop_limit_admission_test PRIVATE _CRT_SECURE_NO_WARNINGS)
    foreach(_limit_mode success reentrant create0 create1 queue0 queue1 apply0 apply1 apply2 apply3
            pin foreign identity cleanup-fail cleanup-fallback lost-patch shutdown held owner-hold
            signature-0 signature-1 signature-2 signature-3 signature-4 signature-5 signature-6 signature-7 signature-8)
        add_test(NAME kh2coop_limit_admission_test_${_limit_mode} COMMAND kh2coop_limit_admission_test ${_limit_mode})
    endforeach()
endif()

# VUH-2343/2352: exact route spawn admission and fail-closed retained-record policy.
add_executable(kh2coop_route_spawn_policy_test tests/RouteSpawnPolicyTest.cpp)
target_include_directories(kh2coop_route_spawn_policy_test PRIVATE common/include)
if(MSVC)
    target_compile_options(kh2coop_route_spawn_policy_test PRIVATE /W4 /WX)
else()
    target_compile_options(kh2coop_route_spawn_policy_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME kh2coop_route_spawn_policy_test COMMAND kh2coop_route_spawn_policy_test)

add_executable(kh2coop_bailey_creeper_authority_test tests/BaileyCreeperAuthorityTest.cpp)
target_include_directories(kh2coop_bailey_creeper_authority_test PRIVATE common/include inject/src)
if(MSVC)
    target_compile_options(kh2coop_bailey_creeper_authority_test PRIVATE /W4 /WX)
else()
    target_compile_options(kh2coop_bailey_creeper_authority_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME kh2coop_bailey_creeper_authority_test COMMAND kh2coop_bailey_creeper_authority_test)

# The Python unit tests (tests/test_*.py) run under ctest, so a governed build catches a stale fixture. Five of them
# sat red for days because nothing ran them. test_runtime_desync.py is a harness that needs --bin-dir and
# --out-root, so it's left out.
find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    file(GLOB _kh2coop_python_tests CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_*.py")
    list(FILTER _kh2coop_python_tests EXCLUDE REGEX "/test_runtime_desync\\.py$")
    foreach(_kh2coop_python_test IN LISTS _kh2coop_python_tests)
        get_filename_component(_kh2coop_python_name "${_kh2coop_python_test}" NAME_WE)
        add_test(NAME kh2coop_py_${_kh2coop_python_name}
                 COMMAND Python3::Interpreter -B "${_kh2coop_python_test}"
                 WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
        set_tests_properties(kh2coop_py_${_kh2coop_python_name} PROPERTIES TIMEOUT 300 LABELS python)
    endforeach()
else()
    message(STATUS "Python 3 not found: the tests/test_*.py ctest entries are skipped")
endif()
