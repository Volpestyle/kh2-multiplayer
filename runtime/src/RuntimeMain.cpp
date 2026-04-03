// WIN32_LEAN_AND_MEAN prevents <Windows.h> from pulling in winsock.h,
// avoiding conflicts with winsock2.h included by ENet.
// NOMINMAX prevents the min/max macros from conflicting with <algorithm>.
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#endif

#include "kh2coop/CameraController.hpp"
#include "kh2coop/GameBridgePC.hpp"
#include "kh2coop/NetworkClient.hpp"
#include "kh2coop/ReplicaController.hpp"
#include "kh2coop/Types.hpp"

#include <enet/enet.h>

// InputMailbox.hpp includes <Windows.h> — must come after WIN32_LEAN_AND_MEAN
// and after enet.h (which pulls in winsock2.h).
#ifdef _WIN32
#include "kh2coop/InputMailbox.hpp"
#endif

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <csignal>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#endif

namespace {

using namespace std::chrono_literals;

constexpr float kRuntimeSnapshotMaxSpeed = 6.0f;

std::atomic_bool g_running {true};

struct RuntimeConfig {
    kh2coop::RuntimeMode runtimeMode {kh2coop::RuntimeMode::CampaignCoop};
    kh2coop::SlotType ownedSlot {kh2coop::SlotType::Player};
    bool cameraOverrideEnabled {true};
    bool panicHotkeyEnabled {true};
    bool logOwnedActorState {false};
    std::uint32_t tickMs {16};

    // Networking
    bool networkingEnabled {false};
    std::string serverHost {"127.0.0.1"};
    std::uint16_t serverPort {7946};
    std::string peerId {"player-1"};
    std::string gameBuild {"1.0.0.10-steam-global"};
    std::string contentHash {"none"};
    std::string modHash;
    std::uint32_t heartbeatIntervalMs {1000};
    std::uint32_t snapshotIntervalMs {16};  // send owned actor state at tick rate
};

struct LaunchOptions {
    std::string configPath {"kh2coop_runtime.ini"};
    RuntimeConfig config {};
    std::uint32_t maxTicks {0};
    bool helpRequested {false};
    std::optional<kh2coop::RuntimeMode> runtimeModeOverride;
    std::optional<kh2coop::SlotType> ownedSlotOverride;
    std::optional<bool> cameraOverrideEnabledOverride;
    std::optional<bool> logOwnedActorStateOverride;
    std::optional<std::uint32_t> tickMsOverride;
    // Networking overrides
    std::optional<bool> networkingEnabledOverride;
    std::optional<std::string> serverHostOverride;
    std::optional<std::uint16_t> serverPortOverride;
    std::optional<std::string> peerIdOverride;
    std::optional<std::string> contentHashOverride;
};

void signalHandler(int) {
    g_running = false;
}

std::string trim(const std::string& value) {
    const auto begin = value.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) {
        return {};
    }

    const auto end = value.find_last_not_of(" \t\r\n");
    return value.substr(begin, end - begin + 1);
}

std::string toLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) {
                       return static_cast<char>(std::tolower(c));
                   });
    return value;
}

float clampUnit(float value) {
    return std::clamp(value, -1.0f, 1.0f);
}

float normalizeVelocityAxis(float value) {
    return clampUnit(value / kRuntimeSnapshotMaxSpeed);
}

bool parseBool(const std::string& value, bool& out) {
    const auto lower = toLower(trim(value));
    if (lower == "1" || lower == "true" || lower == "yes" || lower == "on") {
        out = true;
        return true;
    }

    if (lower == "0" || lower == "false" || lower == "no" || lower == "off") {
        out = false;
        return true;
    }

    return false;
}

bool parseSlot(const std::string& value, kh2coop::SlotType& out) {
    const auto lower = toLower(trim(value));
    if (lower == "0" || lower == "player" || lower == "sora") {
        out = kh2coop::SlotType::Player;
        return true;
    }
    if (lower == "1" || lower == "friend1" || lower == "friend_1" ||
        lower == "p2") {
        out = kh2coop::SlotType::Friend1;
        return true;
    }
    if (lower == "2" || lower == "friend2" || lower == "friend_2" ||
        lower == "p3") {
        out = kh2coop::SlotType::Friend2;
        return true;
    }

    return false;
}

bool parseMode(const std::string& value, kh2coop::RuntimeMode& out) {
    const auto lower = toLower(trim(value));
    if (lower == "campaign_coop" || lower == "campaigncoop" || lower == "coop" ||
        lower == "0") {
        out = kh2coop::RuntimeMode::CampaignCoop;
        return true;
    }
    if (lower == "public_realm" || lower == "publicrealm" || lower == "realm" ||
        lower == "1") {
        out = kh2coop::RuntimeMode::PublicRealm;
        return true;
    }
    return false;
}

std::string modeToString(kh2coop::RuntimeMode mode) {
    switch (mode) {
        case kh2coop::RuntimeMode::CampaignCoop:
            return "CampaignCoop";
        case kh2coop::RuntimeMode::PublicRealm:
            return "PublicRealm";
    }
    return "UNKNOWN";
}

std::string slotToString(kh2coop::SlotType slot) {
    switch (slot) {
        case kh2coop::SlotType::Player:
            return "PLAYER";
        case kh2coop::SlotType::Friend1:
            return "FRIEND_1";
        case kh2coop::SlotType::Friend2:
            return "FRIEND_2";
    }

    return "UNKNOWN";
}

void printUsage() {
    std::cout
        << "Usage: kh2coop_runtime_scaffold [options]\n"
        << "  --config <path>       Runtime config file (default kh2coop_runtime.ini)\n"
        << "  --mode <mode>         Runtime mode: campaign_coop (default) or public_realm\n"
        << "  --role <slot>         Override client role: 0|1|2 or player|friend1|friend2\n"
        << "  --tick-ms <ms>        Loop delay in milliseconds (default 16)\n"
        << "  --max-ticks <count>   Exit after N ticks (default 0 = run until Ctrl+C)\n"
        << "  --no-camera           Disable camera override on boot\n"
        << "  --log-actor-state     Log the owned actor state once per second\n"
        << "\n"
        << "  Networking:\n"
        << "  --server <host>       Server host address (default 127.0.0.1)\n"
        << "  --port <port>         Server port (default 7946)\n"
        << "  --peer-id <id>        Peer identifier (default player-1)\n"
        << "  --content <hash>      Content hash (default none)\n"
        << "  --network             Enable networking (connect to server)\n"
        << "  --no-network          Disable networking (offline mode, default)\n"
        << "\n"
        << "  --help                Show this message\n";
}

bool loadConfigFile(const std::string& path, RuntimeConfig& config,
                    std::string& error) {
    std::ifstream input(path);
    if (!input.is_open()) {
        return true;
    }

    std::string line;
    int lineNumber = 0;
    while (std::getline(input, line)) {
        ++lineNumber;
        const auto commentPos = line.find_first_of("#;");
        if (commentPos != std::string::npos) {
            line.erase(commentPos);
        }

        line = trim(line);
        if (line.empty()) {
            continue;
        }

        const auto equalsPos = line.find('=');
        if (equalsPos == std::string::npos) {
            error = "Config parse error on line " + std::to_string(lineNumber) +
                    ": expected key=value";
            return false;
        }

        const auto key = toLower(trim(line.substr(0, equalsPos)));
        const auto value = trim(line.substr(equalsPos + 1));

        if (key == "runtime_mode") {
            if (!parseMode(value, config.runtimeMode)) {
                error = "Invalid runtime_mode on line " +
                        std::to_string(lineNumber) + ": " + value;
                return false;
            }
            continue;
        }

        if (key == "client_role") {
            if (!parseSlot(value, config.ownedSlot)) {
                error = "Invalid client_role on line " +
                        std::to_string(lineNumber) + ": " + value;
                return false;
            }
            continue;
        }

        if (key == "camera_override") {
            if (!parseBool(value, config.cameraOverrideEnabled)) {
                error = "Invalid camera_override on line " +
                        std::to_string(lineNumber) + ": " + value;
                return false;
            }
            continue;
        }

        if (key == "panic_hotkey") {
            if (!parseBool(value, config.panicHotkeyEnabled)) {
                error = "Invalid panic_hotkey on line " +
                        std::to_string(lineNumber) + ": " + value;
                return false;
            }
            continue;
        }

        if (key == "log_owned_actor_state") {
            if (!parseBool(value, config.logOwnedActorState)) {
                error = "Invalid log_owned_actor_state on line " +
                        std::to_string(lineNumber) + ": " + value;
                return false;
            }
            continue;
        }

        if (key == "tick_ms") {
            try {
                config.tickMs = static_cast<std::uint32_t>(std::stoul(value));
            } catch (const std::exception&) {
                error = "Invalid tick_ms on line " + std::to_string(lineNumber) +
                        ": " + value;
                return false;
            }
            continue;
        }

        // Networking config keys
        if (key == "networking" || key == "networking_enabled") {
            if (!parseBool(value, config.networkingEnabled)) {
                error = "Invalid networking on line " +
                        std::to_string(lineNumber) + ": " + value;
                return false;
            }
            continue;
        }

        if (key == "server_host" || key == "server") {
            config.serverHost = value;
            continue;
        }

        if (key == "server_port" || key == "port") {
            try {
                config.serverPort =
                    static_cast<std::uint16_t>(std::stoul(value));
            } catch (const std::exception&) {
                error = "Invalid server_port on line " +
                        std::to_string(lineNumber) + ": " + value;
                return false;
            }
            continue;
        }

        if (key == "peer_id") {
            config.peerId = value;
            continue;
        }

        if (key == "game_build") {
            config.gameBuild = value;
            continue;
        }

        if (key == "content_hash") {
            config.contentHash = value;
            continue;
        }

        if (key == "mod_hash") {
            config.modHash = value;
            continue;
        }

        if (key == "heartbeat_interval_ms") {
            try {
                config.heartbeatIntervalMs =
                    static_cast<std::uint32_t>(std::stoul(value));
            } catch (const std::exception&) {
                error = "Invalid heartbeat_interval_ms on line " +
                        std::to_string(lineNumber) + ": " + value;
                return false;
            }
            continue;
        }

        if (key == "snapshot_interval_ms") {
            try {
                config.snapshotIntervalMs =
                    static_cast<std::uint32_t>(std::stoul(value));
            } catch (const std::exception&) {
                error = "Invalid snapshot_interval_ms on line " +
                        std::to_string(lineNumber) + ": " + value;
                return false;
            }
            continue;
        }

        error = "Unknown config key on line " + std::to_string(lineNumber) +
                ": " + key;
        return false;
    }

    return true;
}

bool parseArgs(int argc, char* argv[], LaunchOptions& options,
               std::string& error) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            options.helpRequested = true;
            return false;
        }

        if (arg == "--config" && i + 1 < argc) {
            options.configPath = argv[++i];
            continue;
        }

        if (arg == "--mode" && i + 1 < argc) {
            kh2coop::RuntimeMode mode = kh2coop::RuntimeMode::CampaignCoop;
            if (!parseMode(argv[++i], mode)) {
                error = "Invalid --mode value";
                return false;
            }
            options.runtimeModeOverride = mode;
            continue;
        }

        if (arg == "--role" && i + 1 < argc) {
            kh2coop::SlotType slot = kh2coop::SlotType::Player;
            if (!parseSlot(argv[++i], slot)) {
                error = "Invalid --role value";
                return false;
            }
            options.ownedSlotOverride = slot;
            continue;
        }

        if (arg == "--tick-ms" && i + 1 < argc) {
            try {
                options.tickMsOverride =
                    static_cast<std::uint32_t>(std::stoul(argv[++i]));
            } catch (const std::exception&) {
                error = "Invalid --tick-ms value";
                return false;
            }
            continue;
        }

        if (arg == "--max-ticks" && i + 1 < argc) {
            try {
                options.maxTicks = static_cast<std::uint32_t>(std::stoul(argv[++i]));
            } catch (const std::exception&) {
                error = "Invalid --max-ticks value";
                return false;
            }
            continue;
        }

        if (arg == "--no-camera") {
            options.cameraOverrideEnabledOverride = false;
            continue;
        }

        if (arg == "--log-actor-state") {
            options.logOwnedActorStateOverride = true;
            continue;
        }

        // Networking CLI args
        if (arg == "--network") {
            options.networkingEnabledOverride = true;
            continue;
        }

        if (arg == "--no-network") {
            options.networkingEnabledOverride = false;
            continue;
        }

        if (arg == "--server" && i + 1 < argc) {
            options.serverHostOverride = argv[++i];
            continue;
        }

        if (arg == "--port" && i + 1 < argc) {
            try {
                options.serverPortOverride =
                    static_cast<std::uint16_t>(std::stoul(argv[++i]));
            } catch (const std::exception&) {
                error = "Invalid --port value";
                return false;
            }
            continue;
        }

        if (arg == "--peer-id" && i + 1 < argc) {
            options.peerIdOverride = argv[++i];
            continue;
        }

        if (arg == "--content" && i + 1 < argc) {
            options.contentHashOverride = argv[++i];
            continue;
        }

        error = "Unknown argument: " + arg;
        return false;
    }

    return true;
}

bool roomStateChanged(const kh2coop::RoomState& lhs,
                      const kh2coop::RoomState& rhs) {
    return lhs.worldId != rhs.worldId || lhs.roomId != rhs.roomId ||
           lhs.mapProgram != rhs.mapProgram ||
           lhs.battleProgram != rhs.battleProgram ||
           lhs.eventProgram != rhs.eventProgram ||
           lhs.inTransition != rhs.inTransition ||
           lhs.inCutscene != rhs.inCutscene;
}

std::string describeRoomState(const kh2coop::RoomState& room) {
    std::ostringstream oss;
    oss << "world=" << room.worldId
        << " room=" << room.roomId
        << " map=" << room.mapProgram
        << " btl=" << room.battleProgram
        << " evt=" << room.eventProgram
        << " cutscene=" << (room.inCutscene ? "yes" : "no")
        << " transition=" << (room.inTransition ? "yes" : "no");
    return oss.str();
}

void logOwnedActorState(kh2coop::GameBridgePC& game,
                        kh2coop::SlotType slot) {
    const auto actor = game.ReadActorState(slot);
    if (!actor.has_value()) {
        std::cout << "[Runtime] Owned actor unavailable for slot "
                  << slotToString(slot) << "\n";
        return;
    }

    std::ostringstream oss;
    oss << "[Runtime] Owned actor " << slotToString(slot)
        << " pos=(" << actor->position.x << ", " << actor->position.y
        << ", " << actor->position.z << ")"
        << " rotY=" << actor->rotationY
        << " hp=" << actor->hp
        << " airborne=" << (actor->airborne ? "yes" : "no");
    std::cout << oss.str() << "\n";
}

#ifdef _WIN32
bool panicHotkeyPressed() {
    return (GetAsyncKeyState(VK_F8) & 0x1) != 0;
}
#else
bool panicHotkeyPressed() {
    return false;
}
#endif

} // namespace

int main(int argc, char* argv[]) {
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    // ENet must be initialized before any networking calls.
    if (enet_initialize() != 0) {
        std::cerr << "[Runtime] ENet initialization failed\n";
        return 1;
    }

    LaunchOptions options;
    std::string error;
    if (!parseArgs(argc, argv, options, error)) {
        if (options.helpRequested) {
            printUsage();
            return 0;
        }

        if (!error.empty()) {
            std::cerr << "[Runtime] " << error << "\n";
            printUsage();
            return 1;
        }
        return 1;
    }

    if (!loadConfigFile(options.configPath, options.config, error)) {
        std::cerr << "[Runtime] " << error << "\n";
        return 1;
    }

    if (options.runtimeModeOverride.has_value()) {
        options.config.runtimeMode = *options.runtimeModeOverride;
    }
    if (options.ownedSlotOverride.has_value()) {
        options.config.ownedSlot = *options.ownedSlotOverride;
    }
    if (options.cameraOverrideEnabledOverride.has_value()) {
        options.config.cameraOverrideEnabled =
            *options.cameraOverrideEnabledOverride;
    }
    if (options.logOwnedActorStateOverride.has_value()) {
        options.config.logOwnedActorState =
            *options.logOwnedActorStateOverride;
    }
    if (options.tickMsOverride.has_value()) {
        options.config.tickMs = *options.tickMsOverride;
    }

    if (options.config.tickMs == 0) {
        options.config.tickMs = 16;
    }

    // Apply networking overrides from CLI
    if (options.networkingEnabledOverride.has_value()) {
        options.config.networkingEnabled = *options.networkingEnabledOverride;
    }
    if (options.serverHostOverride.has_value()) {
        options.config.serverHost = *options.serverHostOverride;
    }
    if (options.serverPortOverride.has_value()) {
        options.config.serverPort = *options.serverPortOverride;
    }
    if (options.peerIdOverride.has_value()) {
        options.config.peerId = *options.peerIdOverride;
    }
    if (options.contentHashOverride.has_value()) {
        options.config.contentHash = *options.contentHashOverride;
    }

    std::cout << "[Runtime] Booting runtime scaffold\n";
    std::cout << "[Runtime] config=" << options.configPath
              << " mode=" << modeToString(options.config.runtimeMode)
              << " role=" << slotToString(options.config.ownedSlot)
              << " camera_override="
              << (options.config.cameraOverrideEnabled ? "on" : "off")
              << " panic_hotkey="
              << (options.config.panicHotkeyEnabled ? "F8" : "disabled")
              << " tick_ms=" << options.config.tickMs
              << " max_ticks=" << options.maxTicks
              << " networking="
              << (options.config.networkingEnabled ? "on" : "off")
              << "\n";

    if (options.config.networkingEnabled) {
        std::cout << "[Runtime] Network: server="
                  << options.config.serverHost << ":"
                  << options.config.serverPort
                  << " peer_id=" << options.config.peerId
                  << " content=" << options.config.contentHash
                  << " heartbeat_ms=" << options.config.heartbeatIntervalMs
                  << " snapshot_ms=" << options.config.snapshotIntervalMs
                  << "\n";
    }

    std::ifstream configProbe(options.configPath);
    if (!configProbe.is_open()) {
        std::cout << "[Runtime] No config file found at " << options.configPath
                  << "; using defaults and command-line overrides\n";
    }

    kh2coop::GameBridgePC game;
    kh2coop::CameraController camera(game);
    camera.SetOwnedSlot(options.config.ownedSlot);
    camera.SetOverrideEnabled(options.config.cameraOverrideEnabled);

    // Replica controller — applies incoming snapshots to non-owned slots.
    kh2coop::ReplicaController replica(game);

    // -----------------------------------------------------------------------
    // InputMailbox — shared memory bridge for runtime→inject InputFrame delivery.
    // Created after KH2 process attach (needs the KH2 PID).
    // The inject DLL's MailboxReader will auto-detect and start consuming.
    // -----------------------------------------------------------------------
#ifdef _WIN32
    kh2coop::MailboxWriter mailboxWriter;

    const auto clearMailbox = [&mailboxWriter]() {
        if (!mailboxWriter.IsOpen()) {
            return;
        }

        kh2coop::InputFrame empty {};
        mailboxWriter.WriteSlot(kh2coop::MAILBOX_SLOT_PLAYER, empty);
        mailboxWriter.WriteSlot(kh2coop::MAILBOX_SLOT_FRIEND1, empty);
        mailboxWriter.WriteSlot(kh2coop::MAILBOX_SLOT_FRIEND2, empty);
    };

    const auto closeMailbox = [&mailboxWriter, &clearMailbox]() {
        if (!mailboxWriter.IsOpen()) {
            return;
        }

        clearMailbox();
        mailboxWriter.Close();
    };
#endif

    // -----------------------------------------------------------------------
    // NetworkClient setup (optional, gated on config.networkingEnabled)
    // -----------------------------------------------------------------------
    // The mutex guards ReplicaController calls from the ENet receive path,
    // which runs inside tick() on the main thread. Currently single-threaded,
    // but the mutex is cheap insurance for future threading.
    std::mutex replicaMtx;
    std::unique_ptr<kh2coop::NetworkClient> netClient;
    std::atomic_bool netConnected {false};

    if (options.config.networkingEnabled) {
        kh2coop::ClientCallbacks callbacks;

        callbacks.onConnected = [&netConnected,
#ifdef _WIN32
                                 &game,
                                 &mailboxWriter,
#endif
                                 &options]() {
            netConnected = true;
            std::cout << "[Runtime] Network: connected to server\n";

#ifdef _WIN32
            if (options.config.networkingEnabled && game.IsAttached() &&
                !mailboxWriter.IsOpen() &&
                mailboxWriter.Create(static_cast<DWORD>(game.ProcessId()))) {
                std::cout << "[Runtime] Input mailbox created for PID="
                          << game.ProcessId() << "\n";
            }
#endif
        };

        callbacks.onDisconnected = [&netConnected, &replica, &replicaMtx
#ifdef _WIN32
                                    , &closeMailbox
#endif
                                    ]() {
            netConnected = false;
            std::cout << "[Runtime] Network: disconnected from server\n";
            std::lock_guard<std::mutex> lock(replicaMtx);
            replica.Reset();

#ifdef _WIN32
            closeMailbox();
#endif
        };

        callbacks.onSessionState = [](const kh2coop::SessionState& ss) {
            std::cout << "[Runtime] Network: SessionState session="
                      << ss.sessionId
                      << " actors=" << ss.actors.size()
                      << " room=" << describeRoomState(ss.room) << "\n";
        };

        callbacks.onActorSnapshot =
            [&replica, &replicaMtx,
#ifdef _WIN32
             &mailboxWriter,
#endif
             ownedSlot = options.config.ownedSlot](
                const kh2coop::ActorSnapshot& snap) {
                // Skip snapshots for our own slot — we are authoritative.
                if (snap.actor.slot == ownedSlot) return;

                std::lock_guard<std::mutex> lock(replicaMtx);
                replica.ApplyActorSnapshot(snap);

#ifdef _WIN32
                // Write to the input mailbox so the inject DLL can drive
                // the friend entity through native physics/animation.
                // Map SlotType → mailbox slot index (Player is reserved for
                // native slot-0 input automation).
                if (mailboxWriter.IsOpen()) {
                    int mbSlot = -1;
                    if (snap.actor.slot == kh2coop::SlotType::Friend1)
                        mbSlot = kh2coop::MAILBOX_SLOT_FRIEND1;
                    else if (snap.actor.slot == kh2coop::SlotType::Friend2)
                        mbSlot = kh2coop::MAILBOX_SLOT_FRIEND2;

                    if (mbSlot >= 0) {
                        // The inject DLL interprets mailbox axes as world-space
                        // velocity, not normalized stick input.
                        kh2coop::InputFrame synth {};
                        synth.seq = snap.snapshotId;
                        synth.clientTimeMs = snap.snapshotId;
                        synth.ownedActorId = snap.actor.actorId;
                        synth.leftStickX = snap.actor.velocity.x;
                        synth.leftStickY = snap.actor.velocity.z;
                        // Map action state to buttons
                        synth.buttons.attack =
                            (snap.actor.action == kh2coop::ActionState::Attack);
                        synth.buttons.jump =
                            (snap.actor.action == kh2coop::ActionState::Jump);
                        synth.buttons.guard =
                            (snap.actor.action == kh2coop::ActionState::Guard);
                        synth.buttons.dodge =
                            (snap.actor.action == kh2coop::ActionState::Dodge);
                        synth.requestedTargetId = snap.actor.targetId;
                        mailboxWriter.WriteSlot(mbSlot, synth);
                    }
                }
#endif
            };

        callbacks.onEnemySnapshot =
            [&replica, &replicaMtx](const kh2coop::EnemySnapshot& snap) {
                std::lock_guard<std::mutex> lock(replicaMtx);
                replica.ApplyEnemySnapshot(snap);
            };

        callbacks.onEvent = [](const kh2coop::EventMessage& evt) {
            std::cout << "[Runtime] Network: Event type="
                      << static_cast<int>(evt.type)
                      << " payload=" << evt.payloadJson << "\n";
        };

        callbacks.onLog = [](const std::string& msg) {
            std::cout << "[Runtime] " << msg << "\n";
        };

        netClient = std::make_unique<kh2coop::NetworkClient>(
            options.config.serverHost,
            options.config.serverPort,
            options.config.gameBuild,
            options.config.modHash,
            options.config.peerId,
            options.config.ownedSlot,
            std::move(callbacks),
            options.config.runtimeMode,
            options.config.contentHash);

        if (!netClient->connect()) {
            std::cerr << "[Runtime] Failed to initiate network connection\n";
            // Continue in offline mode rather than aborting.
            netClient.reset();
        }
    }

    bool cameraOverrideEnabled = options.config.cameraOverrideEnabled;
    bool waitingForAttachLogged = false;
    bool attachLogged = false;
    std::optional<bool> lastEntityDiscovered;
    std::optional<kh2coop::RoomState> lastRoomState;
    auto lastActorLogAt = std::chrono::steady_clock::now();
    auto lastHeartbeatAt = std::chrono::steady_clock::now();
    auto lastSnapshotAt = std::chrono::steady_clock::now();
    std::uint32_t snapshotSeq = 0;

    for (std::uint32_t tick = 0;
         g_running && (options.maxTicks == 0 || tick < options.maxTicks);
         ++tick) {

        // Pump network events every tick, even before KH2 is attached.
        if (netClient) {
            netClient->tick(0);
        }

        if (!game.IsAttached()) {
            if (!waitingForAttachLogged) {
                std::cout << "[Runtime] Waiting for KH2 process...\n";
                waitingForAttachLogged = true;
            }

            if (game.Attach()) {
                attachLogged = true;
                waitingForAttachLogged = false;
                std::cout << "[Runtime] Attached to KH2 process (PID="
                          << game.ProcessId() << ")\n";
                // Reset replica ordering guards on fresh attach.
                std::lock_guard<std::mutex> lock(replicaMtx);
                replica.Reset();

#ifdef _WIN32
                // Create the input mailbox shared memory for this KH2 process.
                // The inject DLL (inside KH2) will open it by its own PID.
                if (options.config.networkingEnabled && !mailboxWriter.IsOpen()) {
                    if (mailboxWriter.Create(
                            static_cast<DWORD>(game.ProcessId()))) {
                        std::cout << "[Runtime] Input mailbox created for PID="
                                  << game.ProcessId() << "\n";
                    } else {
                        std::cerr
                            << "[Runtime] Failed to create input mailbox\n";
                    }
                }
#endif
            } else {
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(options.config.tickMs));
                continue;
            }
        }

        game.Tick();

        if (options.config.panicHotkeyEnabled && panicHotkeyPressed()) {
            cameraOverrideEnabled = !cameraOverrideEnabled;
            camera.SetOverrideEnabled(cameraOverrideEnabled);
            if (!cameraOverrideEnabled) {
                game.RestoreVanillaCamera();
            }
            std::cout << "[Runtime] Camera override "
                      << (cameraOverrideEnabled ? "enabled" : "disabled")
                      << " via F8\n";
        }

        const auto room = game.ReadRoomState();
        const bool entityDiscovered = game.HasEntityAddresses();
        if (!lastRoomState.has_value() || roomStateChanged(room, *lastRoomState)) {
            std::cout << "[Runtime] Room state: " << describeRoomState(room)
                      << " entity_discovered="
                      << (entityDiscovered ? "yes" : "no") << "\n";
            lastRoomState = room;

            // Reset replica on room change — stale addresses would corrupt.
            std::lock_guard<std::mutex> lock(replicaMtx);
            replica.Reset();
        }
        if (!lastEntityDiscovered.has_value() ||
            entityDiscovered != *lastEntityDiscovered) {
            std::cout << "[Runtime] Entity discovery "
                      << (entityDiscovered ? "ready" : "missing") << "\n";
            lastEntityDiscovered = entityDiscovered;
        }

        camera.Tick(room);

        const auto now = std::chrono::steady_clock::now();

        // ----- Networking: send heartbeat at configured interval -----
        if (netClient && netConnected) {
            const auto heartbeatElapsed =
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - lastHeartbeatAt)
                    .count();
            if (heartbeatElapsed >=
                static_cast<long long>(options.config.heartbeatIntervalMs)) {
                netClient->sendHeartbeat();
                lastHeartbeatAt = now;
            }
        }

        // ----- Networking: send owned actor snapshot at configured interval -----
        if (netClient && netConnected && entityDiscovered) {
            const auto snapshotElapsed =
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - lastSnapshotAt)
                    .count();
            if (snapshotElapsed >=
                static_cast<long long>(options.config.snapshotIntervalMs)) {
                const auto ownedActor =
                    game.ReadActorState(options.config.ownedSlot);
                if (ownedActor.has_value()) {
                    // Build an InputFrame from the owned actor's current state.
                    // This is still a placeholder path: we derive coarse input
                    // intent from live actor velocity until native input capture
                    // is wired into the runtime.
                    kh2coop::InputFrame frame;
                    frame.seq = ++snapshotSeq;
                    frame.clientTimeMs = static_cast<std::uint64_t>(
                        std::chrono::duration_cast<std::chrono::milliseconds>(
                            now.time_since_epoch())
                            .count());
                    frame.ownedActorId = ownedActor->actorId;
                    frame.leftStickX = normalizeVelocityAxis(ownedActor->velocity.x);
                    frame.leftStickY = normalizeVelocityAxis(ownedActor->velocity.z);
                    frame.requestedTargetId = ownedActor->targetId;
                    frame.buttons.attack =
                        (ownedActor->action == kh2coop::ActionState::Attack);
                    frame.buttons.jump =
                        (ownedActor->action == kh2coop::ActionState::Jump);
                    frame.buttons.guard =
                        (ownedActor->action == kh2coop::ActionState::Guard);
                    frame.buttons.dodge =
                        (ownedActor->action == kh2coop::ActionState::Dodge);
                    netClient->sendInput(frame);
                }
                lastSnapshotAt = now;
            }
        }

        if (options.config.logOwnedActorState &&
            now - lastActorLogAt >= 1s) {
            // Log all 3 party slots for visibility during testing.
            for (auto s : {kh2coop::SlotType::Player,
                           kh2coop::SlotType::Friend1,
                           kh2coop::SlotType::Friend2}) {
                logOwnedActorState(game, s);
            }
            lastActorLogAt = now;
        }

        std::this_thread::sleep_for(
            std::chrono::milliseconds(options.config.tickMs));
    }

    // ----- Graceful shutdown -----
#ifdef _WIN32
    if (mailboxWriter.IsOpen()) {
        closeMailbox();
        std::cout << "[Runtime] Input mailbox closed\n";
    }
#endif

    if (netClient) {
        std::cout << "[Runtime] Disconnecting from server...\n";
        netClient->disconnect();
        netClient.reset();
    }

    if (attachLogged) {
        game.RestoreVanillaCamera();
    }

    enet_deinitialize();
    std::cout << "[Runtime] Shutdown\n";
    return 0;
}
