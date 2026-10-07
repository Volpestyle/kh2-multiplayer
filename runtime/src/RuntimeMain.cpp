// WIN32_LEAN_AND_MEAN prevents <Windows.h> from pulling in winsock.h,
// avoiding conflicts with winsock2.h included by ENet.
// NOMINMAX prevents the min/max macros from conflicting with <algorithm>.
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#endif

#include "kh2coop/AutomaticResyncNotice.hpp"
#include "kh2coop/AvatarSync.hpp"
#include "kh2coop/CameraController.hpp"
#include "kh2coop/ClientRecovery.hpp"
#include "kh2coop/DesyncCollector.hpp"
#include "kh2coop/DesyncUpload.hpp"
#include "kh2coop/GameBridgePC.hpp"
#include "kh2coop/NetworkClient.hpp"
#include "kh2coop/SteamTransport.hpp"
#include "kh2coop/SteamBroker.hpp"
#include "kh2coop/SessionHost.hpp"
#include "kh2coop/ReplicaController.hpp"
#include "kh2coop/ResyncEvidence.hpp"
#include "kh2coop/Types.hpp"

#include <enet/enet.h>

// InputMailbox.hpp includes <Windows.h> — must come after WIN32_LEAN_AND_MEAN
// and after enet.h (which pulls in winsock2.h).
#ifdef _WIN32
#include "kh2coop/AvatarBridge.hpp"
#include "kh2coop/InputMailbox.hpp"
#include "kh2coop/WorldPump.hpp"
#include "EventHoldRuntimeProjection.hpp"
#include <timeapi.h> // timeBeginPeriod (winmm)
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cctype>
#include <csignal>
#include <cstdlib>
#include <cstdint>
#include <deque>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <streambuf>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#endif

namespace {

using namespace std::chrono_literals;

constexpr float kRuntimeSnapshotMaxSpeed = 6.0f;

bool exactEnvironmentOne(const char* name) {
#ifdef _WIN32
    char* value = nullptr; std::size_t length = 0;
    if (_dupenv_s(&value, &length, name) != 0) return false;
    const bool enabled = value && length == 2 && value[0] == '1';
    std::free(value);
    return enabled;
#else
    const char* value = std::getenv(name);
    return value && value[0] == '1' && value[1] == '\0';
#endif
}

// VUH-1787: KH2COOP_AVATAR_HOLD_MS sets how long a stalled avatar stream keeps
// its puppet held before release. 1000..10000; 1000 restores the old 1 s release.
kh2coop::AvatarSync::Config avatarSyncConfigFromEnvironment() {
    kh2coop::AvatarSync::Config config;
    std::string text;
#ifdef _WIN32
    char* value = nullptr; std::size_t length = 0;
    if (_dupenv_s(&value, &length, "KH2COOP_AVATAR_HOLD_MS") == 0 && value) text = value;
    std::free(value);
#else
    if (const char* value = std::getenv("KH2COOP_AVATAR_HOLD_MS")) text = value;
#endif
    if (text.empty()) return config;
    if (const auto parsed = kh2coop::parseAvatarHoldMs(text)) {
        config.releaseAfterMs = *parsed;
        std::cout << "[avatar] KH2COOP_AVATAR_HOLD_MS=" << *parsed << " (release after " << *parsed << " ms)\n";
    } else {
        std::cout << "[avatar] KH2COOP_AVATAR_HOLD_MS ignored (want 1000..10000); release after "
                  << config.releaseAfterMs << " ms\n";
    }
    return config;
}

std::atomic_bool g_running {true};

// Retain actual runtime output independently of stdout redirection. Only a
// bounded raw tail is handed to the diagnostic worker; no disk I/O here.
class RuntimeLogTail {
public:
    struct Snapshot { std::string bytes; std::uint64_t totalBytes {}; };
    RuntimeLogTail() : out_(*this, std::cout.rdbuf()), err_(*this, std::cerr.rdbuf()) {
        std::cout.rdbuf(&out_); std::cerr.rdbuf(&err_);
    }
    ~RuntimeLogTail() { std::cout.rdbuf(out_.original); std::cerr.rdbuf(err_.original); }
    Snapshot Take() { std::lock_guard lock(mutex_); return {tail_, total_}; }
private:
    class Buffer : public std::streambuf {
    public:
        Buffer(RuntimeLogTail& owner, std::streambuf* destination) : original(destination), owner_(owner) {}
        std::streambuf* original;
    private:
        std::streamsize xsputn(const char* data, std::streamsize size) override {
            const auto written = original->sputn(data, size);
            if (written > 0) owner_.Append(data, static_cast<std::size_t>(written));
            return written;
        }
        int_type overflow(int_type c) override {
            if (traits_type::eq_int_type(c, traits_type::eof())) return traits_type::not_eof(c);
            const auto result = original->sputc(traits_type::to_char_type(c));
            if (!traits_type::eq_int_type(result, traits_type::eof())) {
                const char value = traits_type::to_char_type(c); owner_.Append(&value, 1);
            }
            return result;
        }
        int sync() override { return original->pubsync(); }
        RuntimeLogTail& owner_;
    };
    void Append(const char* data, std::size_t size) {
        std::lock_guard lock(mutex_);
        total_ += size;
        constexpr std::size_t cap = 128 * 1024;
        if (size >= cap) tail_.assign(data + size - cap, cap);
        else {
            if (tail_.size() + size > cap) tail_.erase(0, tail_.size() + size - cap);
            tail_.append(data, size);
        }
    }
    std::mutex mutex_;
    std::string tail_;
    std::uint64_t total_ {};
    Buffer out_, err_;
};

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
    std::string desyncDir {"build/rig/desync-local"};
    std::string injectLogPath; // explicitly registered launch log; never guessed from PID
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
    std::optional<std::string> desyncDirOverride, injectLogPathOverride;
    // Bind to one KH2 instance when several run (VUH-1492).
    std::optional<std::uint32_t> pid;
    // Pre-D2 replica path: apply relay actor snapshots to the game, write
    // them to the input mailbox and send InputFrames. Off by default; the
    // avatar path (local-primary) replaces it.
    bool legacyReplica {false};
    bool steamHost {false};
    bool steamListenerProbe {false}; // VUH-1493 probe 03: host listener with an empty allowlist
    std::optional<std::string> steamJoin;
    std::vector<std::uint64_t> steamAllow;
    // Applied to both directions when any field is set.
    kh2coop::LinkConditions link {};
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
        << "  --desync-dir <path>   Local diagnostic spool (default build/rig/desync-local)\n"
        << "  --inject-log <path>   Exact log registered by the launcher, if available\n"
        << "  --network             Enable networking (connect to server)\n"
        << "  --pid <pid>           Attach to this KH2 process (several instances)\n"
        << "  --steam-host         Opt-in broker host; requires --pid and Player role\n"
        << "  --steam-allow <ID64>  Explicitly admit a Steam friend (up to two)\n"
        << "  --steam-join <ID64>   Paste host SteamID; requires --pid and Friend role\n"
        << "  --steam-listener-probe  Diagnostic host listener with NO allowlist (admits nobody)\n"
        << "  --legacy-replica      Also run the old actor-snapshot replica path\n"
        << "  --link-latency-ms <n> Add one-way latency, both directions\n"
        << "  --link-jitter-ms <n>  Add uniform jitter in [0, n] ms\n"
        << "  --link-loss <pct>     Drop this percent of unreliable packets\n"
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
        if (key == "desync_dir") { config.desyncDir = value; continue; }
        if (key == "inject_log") { config.injectLogPath = value; continue; }
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
        if (arg == "--steam-host") { options.steamHost = true; continue; }
        if (arg == "--steam-listener-probe") { options.steamListenerProbe = true; continue; }
        if ((arg == "--steam-join" || arg == "--steam-allow") && i + 1 < argc) {
            const std::string value = argv[++i]; std::uint64_t id = 0;
            if (!kh2coop::steam::parseId(value, id)) { error = "Expected public individual SteamID64"; return false; }
            if (arg == "--steam-join") { if (options.steamJoin) { error="Duplicate Steam target"; return false; } options.steamJoin=value; }
            else { if (options.steamAllow.size() >= 2 || std::find(options.steamAllow.begin(),options.steamAllow.end(),id)!=options.steamAllow.end()) { error="At most two distinct admitted SteamIDs";return false; } options.steamAllow.push_back(id); }
            continue;
        }
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

        if (arg == "--desync-dir" && i + 1 < argc) {
            options.desyncDirOverride = argv[++i]; continue;
        }
        if (arg == "--inject-log" && i + 1 < argc) {
            options.injectLogPathOverride = argv[++i]; continue;
        }

        if (arg == "--legacy-replica") {
            options.legacyReplica = true;
            continue;
        }

        if ((arg == "--pid" || arg == "--link-latency-ms" ||
             arg == "--link-jitter-ms" || arg == "--link-loss") &&
            i + 1 < argc) {
            const std::string value = argv[++i];
            try {
                if (arg == "--pid") {
                    options.pid = static_cast<std::uint32_t>(std::stoul(value));
                } else if (arg == "--link-latency-ms") {
                    options.link.latencyMs = static_cast<std::uint32_t>(std::stoul(value));
                } else if (arg == "--link-jitter-ms") {
                    options.link.jitterMs = static_cast<std::uint32_t>(std::stoul(value));
                } else {
                    options.link.lossRate = std::stof(value) / 100.0f;
                }
            } catch (const std::exception&) {
                error = "Invalid " + arg + " value";
                return false;
            }
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
    RuntimeLogTail runtimeLog;
    // Unbuffered so redirected logs survive a kill (scenario runner).
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;
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
    if (options.desyncDirOverride) options.config.desyncDir = *options.desyncDirOverride;
    if (options.injectLogPathOverride) options.config.injectLogPath = *options.injectLogPathOverride;

    const bool steamEnabled = options.steamHost || options.steamJoin.has_value();
    kh2coop::SteamTransports steamTransports;
    std::unique_ptr<kh2coop::SessionHost> steamHost;
    if (steamEnabled || !options.steamAllow.empty() || options.steamListenerProbe) {
        if (!options.pid || !*options.pid || options.steamHost == options.steamJoin.has_value() ||
            (options.steamListenerProbe && (!options.steamHost || !options.steamAllow.empty())) ||
            (options.steamHost && !options.steamListenerProbe && options.steamAllow.empty()) ||
            (!options.steamHost && !options.steamAllow.empty()) ||
            (!options.steamHost && options.config.ownedSlot == kh2coop::SlotType::Player) ||
            (options.steamHost && options.config.ownedSlot != kh2coop::SlotType::Player)) {
            std::cerr << "[Runtime] Steam requires explicit --pid, host Player with --steam-allow, or join Friend slot\n"; return 1;
        }
        steamTransports = kh2coop::makeSteamTransports(*options.pid, options.steamHost, options.steamAllow,
                                                       options.steamListenerProbe);
        if (!steamTransports.client) { std::cerr << "[Runtime] Steam broker unavailable; ENet fallback not attempted\n";return 1; }
        options.config.peerId = "steam:" + std::to_string(steamTransports.identity);
        options.config.serverHost = options.steamHost ? "local" : *options.steamJoin;
        options.config.networkingEnabled = true;
        std::cout << "[Runtime] Steam identity=" << steamTransports.identity << " host=" << options.steamHost << " relayOnly=required\n";
        if (options.steamListenerProbe) std::cout << "[Runtime] Steam listener probe: empty allowlist, no remote admission\n";
        if (options.steamHost) {
            kh2coop::SessionConfig config;
            config.gameBuild=options.config.gameBuild;config.contentHash=options.config.contentHash;
            config.modHash=options.config.modHash;config.runtimeMode=options.config.runtimeMode;
            config.authenticatedHostIdentity=options.config.peerId;
            config.desyncOutputRoot=options.config.desyncDir;
            kh2coop::SessionCallbacks cb;
            cb.onLog=[](const std::string& s){std::cout << "[SteamHost] " << s << '\n';};
            steamHost=std::make_unique<kh2coop::SessionHost>(config,std::move(cb),std::move(steamTransports.server));
            if (!steamHost->start()) { std::cerr << "[Runtime] Steam host refused\n";return 1; }
        }
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

    // Avatar path (plan D2/D9): the DLL publishes the local avatar into the
    // bridge; we send it to the relay, feed received avatars to AvatarSync and
    // publish interpolated puppet poses back for the DLL to apply.
    kh2coop::AvatarSync avatarSync(options.config.ownedSlot, avatarSyncConfigFromEnvironment());
    std::string avatarSessionId;
    // Assigned after the existing diagnostic binding is available. Earlier
    // world-reset lambdas invoke it only at their actual state boundaries.
    std::function<void(const char*)> observeIdentity;
#ifdef _WIN32
    kh2coop::AvatarBridge avatarBridge;
    kh2coop::hudnames::Roster admittedHudNames;
    std::string admittedHudNameSession; // exact admitted session, never a native actor name
    const auto publishInactiveAvatars = [&avatarBridge]() {
        (void)avatarBridge.PublishRosterNames({});
        for (int index = 0; index < kh2coop::AVATAR_BRIDGE_PUPPETS; ++index)
            avatarBridge.PublishPuppet(index, kh2coop::PuppetPose {});
    };
    // Discrete world events (transitions, enemies, claims, progress) as
    // encoded packets: DLL -> relay and relay -> DLL.
    kh2coop::WorldBridge worldBridge;
    kh2coop::WorldPumpStats worldStats;
    kh2coop::WorldInbox worldInbox;
    const bool eventHoldControlEnabled = exactEnvironmentOne("KH2COOP_EVENT_HOLD_CONTROL");
    kh2coop::eventhold::Channel eventHoldControl;
    kh2coop::eventhold::RuntimeProjection eventHoldProjection(eventHoldControl);
    bool eventHoldOpenAttempted = false;
    kh2coop::eventhold::Abort eventHoldLoggedAbort = kh2coop::eventhold::Abort::None;
    bool eventHoldLoggedArm = false;
    std::uint64_t eventHoldLastPulseMs = 0, eventHoldLastTickOkMs = 0, eventHoldMaxPulseGapMs = 0;
    std::uint8_t worldSessionSlot = kh2coop::WORLD_SLOT_UNKNOWN;
    std::string worldSessionHost;
    std::string worldSessionId;
    std::array<std::uint64_t, 3> worldConnectionIds {};
    std::array<std::uint64_t, 3> worldPeerDeliverySerials {};
    std::uint64_t worldDeliverySerial = 0;
    bool worldQuarantined = true;
    std::optional<std::pair<kh2coop::ResyncBegin, kh2coop::ResyncSnapshot>> pendingNativeSnapshot;
    std::deque<std::vector<std::uint8_t>> pendingNativeContinuation;
    std::size_t pendingNativeContinuationBytes = 0;
    const auto clearNativeContinuation = [&]() {
        pendingNativeContinuation.clear();
        pendingNativeContinuationBytes = 0;
    };
    std::uint64_t worldHostConnectionId = 0, worldSelfConnectionId = 0;
    std::uint32_t worldSessionGeneration = 0;
    std::uint64_t worldCauseSequence = 0;
    const auto worldCauseReceipt = [&](const char* origin, std::uint32_t priorGeneration,
                                       bool markerQueued, const kh2coop::ResyncBegin* begin = nullptr,
                                       const kh2coop::ResyncKey* terminal = nullptr) {
        const auto now = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
        std::string sha = "-";
        if (begin) {
            constexpr char digits[] = "0123456789abcdef"; sha.clear();
            for (const auto byte : begin->sha256) { sha += digits[byte >> 4]; sha += digits[byte & 15]; }
        }
        const auto* key = begin ? &begin->key : terminal;
        std::cout << "[runtime-world-cause] schema=1 seq=" << ++worldCauseSequence
            << " observationMs=" << now << " origin=" << origin << " cachedRejoinProven=0"
            << " gamePid=" << (game.IsAttached() ? game.ProcessId() : 0)
            << " bridgeOpen=" << worldBridge.IsOpen() << " priorGeneration=" << priorGeneration
            << " generation=" << worldSessionGeneration << " delivery=" << worldDeliverySerial
            << " nativeDelivery=" << (worldBridge.IsOpen() ? worldBridge.DeliverySerial() : 0)
            << " slot=" << static_cast<unsigned>(worldSessionSlot)
            << " session=" << (worldSessionId.empty() ? "-" : worldSessionId)
            << " host=" << worldHostConnectionId << " self=" << worldSelfConnectionId
            << " roster0=" << worldConnectionIds[0] << " roster1=" << worldConnectionIds[1]
            << " roster2=" << worldConnectionIds[2] << " markerQueued=" << markerQueued
            << " keyAvailable=" << (key != nullptr)
            << " requestSession=" << (key ? key->sessionId : "-")
            << " requestHost=" << (key ? key->hostConnectionId : 0)
            << " request=" << (key ? key->requestId : 0)
            << " phase=" << (begin ? static_cast<int>(begin->phase) : -1)
            << " cut=" << (begin ? begin->snapshotCut : 0) << " snapshotSHA=" << sha
            << " roomAvailable=" << (begin != nullptr)
            << " epoch=" << (begin ? begin->room.epoch : 0)
            << " world=" << (begin ? begin->room.worldId : 0)
            << " room=" << (begin ? begin->room.roomId : 0)
            << " door=" << (begin ? static_cast<unsigned>(begin->room.door) : 0)
            << " map=" << (begin ? begin->room.mapProgram : 0)
            << " battle=" << (begin ? begin->room.battleProgram : 0)
            << " event=" << (begin ? begin->room.eventProgram : 0)
            << " targetCount=" << (begin ? static_cast<unsigned>(begin->targetCount) : 0);
        if (begin) for (std::size_t i = 0; i < begin->targetCount; ++i)
            std::cout << " target" << i << "Slot=" << static_cast<unsigned>(begin->targets[i].slot)
                << " target" << i << "Connection=" << begin->targets[i].connectionId
                << " target" << i << "Delivery=" << begin->targets[i].deliverySerial;
        std::cout << " dropped=0\n";
    };
    const auto resetWorldSession = [&](std::uint8_t slot, const char* origin) {
        if (eventHoldControlEnabled) eventHoldProjection.Retire(kh2coop::eventhold::Abort::BindingReset);
        const auto priorGeneration = worldSessionGeneration;
        worldBridge.SetPuppetAuthorityMode(kh2coop::PuppetAuthorityMode::Unavailable);
        avatarSync.clear();
        publishInactiveAvatars();
        worldInbox.Clear();
        worldBridge.SetDeliverySerial(worldDeliverySerial);
        worldBridge.SetPeerDeliverySerials(worldPeerDeliverySerials);
        worldSessionGeneration = worldBridge.AdvanceSessionGeneration();
        worldBridge.SetConnectionIds(worldConnectionIds);
        worldBridge.SetLocalSlot(slot);
        if (slot < 3 && worldConnectionIds[0] && worldConnectionIds[slot] &&
            worldDeliverySerial && !worldQuarantined)
            worldBridge.SetPuppetAuthorityMode(kh2coop::PuppetAuthorityMode::Network);
        // Bridge-local reset shares the FIFO with world packets, so it cannot
        // race with a DLL frame and discard the new session's first transition.
        const bool markerQueued = worldInbox.Receive(worldBridge,
            kh2coop::encodeWorldSessionReset(worldSessionGeneration, worldDeliverySerial), worldStats);
        worldCauseReceipt(origin, priorGeneration, markerQueued);
        if (!markerQueued) {
            std::cerr << "[Runtime] Could not queue world session reset\n";
            g_running = false;
        }
        if (observeIdentity) observeIdentity("world-reset");
    };
#endif
    std::unique_ptr<kh2coop::NetworkClient> netClient;
    std::atomic_bool netReady {false};
#ifdef _WIN32
    const auto eventHoldScope = [&]() {
        kh2coop::eventhold::Scope scope {};
        if (worldSessionId.size() == sizeof(scope.session))
            std::memcpy(scope.session, worldSessionId.data(), sizeof(scope.session));
        scope.generation = worldSessionGeneration;
        scope.slot = worldSessionSlot;
        scope.hostConnection = worldHostConnectionId;
        scope.selfConnection = worldSelfConnectionId;
        scope.hostDelivery = worldPeerDeliverySerials[0];
        scope.targetDelivery = worldDeliverySerial;
        return scope;
    };
    const auto eventHoldEligible = [&]() {
        if (!netReady || !netClient || !netClient->worldReady() || netClient->resyncBusy() ||
            netClient->pendingResync() || worldQuarantined || pendingNativeSnapshot ||
            worldSessionSlot < 1 || worldSessionSlot > 2) return false;
        const auto& binding = netClient->worldBinding();
        if (!binding || binding->sessionId != worldSessionId || binding->selfSlot != worldSessionSlot ||
            binding->hostConnectionId != worldHostConnectionId || binding->selfConnectionId != worldSelfConnectionId ||
            binding->deliverySerial != worldDeliverySerial) return false;
        return !worldBridge.IsOpen() || (worldSessionGeneration &&
            worldBridge.SessionGeneration() == worldSessionGeneration &&
            worldBridge.DeliverySerial() == worldDeliverySerial &&
            worldBridge.PeerDeliverySerial(0) == worldPeerDeliverySerials[0] &&
            worldBridge.GetPuppetAuthorityMode() == kh2coop::PuppetAuthorityMode::Network &&
            (worldBridge.ConnectionId(0) == worldConnectionIds[0] &&
             worldBridge.ConnectionId(worldSessionSlot) == worldConnectionIds[worldSessionSlot]));
    };
    const auto pulseEventHoldControl = [&]() {
        if (!eventHoldControlEnabled) return;
        // Runtime wall-time pump, independent of AvatarBridge frame freshness.
        if (!eventHoldOpenAttempted && game.IsAttached() && worldSessionSlot >= 1 && worldSessionSlot <= 2) {
            eventHoldOpenAttempted = true;
            if (!eventHoldControl.Open(static_cast<DWORD>(game.ProcessId()))) {
                eventHoldProjection.Reject(kh2coop::eventhold::Abort::Unsupported);
                std::cerr << "[eventhold-control] mapping unavailable; interval unqualified\n";
            }
        }
        if (eventHoldControl.IsOpen()) {
            const auto scope = eventHoldScope();
            const auto pulseMs = GetTickCount64();
            const auto gapMs = eventHoldLastPulseMs && pulseMs >= eventHoldLastPulseMs
                ? pulseMs - eventHoldLastPulseMs : 0;
            eventHoldLastPulseMs = pulseMs;
            if (gapMs > eventHoldMaxPulseGapMs) eventHoldMaxPulseGapMs = gapMs;
            // Tick success includes successful heartbeat renewal, even unarmed.
            const bool tickOk = eventHoldProjection.Tick(scope, eventHoldEligible(), pulseMs);
            if (tickOk) eventHoldLastTickOkMs = pulseMs;
            if (eventHoldControl.Armed() && !eventHoldLoggedArm) {
                eventHoldLoggedArm = true;
                std::cout << "[eventhold-control] armed generation=" << scope.generation
                    << " host=" << scope.hostConnection << " self=" << scope.selfConnection
                    << " hostDelivery=" << scope.hostDelivery << " targetDelivery=" << scope.targetDelivery << '\n';
            }
            const auto reason = eventHoldControl.Reason();
            if (reason != kh2coop::eventhold::Abort::None && reason != eventHoldLoggedAbort) {
                eventHoldLoggedAbort = reason;
                std::cerr << "[eventhold-control] aborted reason=" << static_cast<LONG>(reason)
                    << " observationMs=" << pulseMs << " pulseGapMs=" << gapMs
                    << " maxPulseGapMs=" << eventHoldMaxPulseGapMs
                    << " lastTickOkMs=" << eventHoldLastTickOkMs
                    << " tickOkAgeMs=" << (eventHoldLastTickOkMs && pulseMs >= eventHoldLastTickOkMs
                        ? pulseMs - eventHoldLastTickOkMs : 0)
                    << " tickOk=" << tickOk << " armed=" << eventHoldControl.Armed() << '\n';
            }
        }
    };
    const auto enqueueWorld = [&](const std::vector<std::uint8_t>& packet) {
        if (worldInbox.Receive(worldBridge, packet, worldStats)) return true;
        if (eventHoldControlEnabled) eventHoldProjection.Retire(kh2coop::eventhold::Abort::Overflow);
        if (netClient) netClient->failWorldResync(kh2coop::ResyncResultReason::Overflow,
                                                 "runtime world inbox overflow");
        std::cerr << "[Runtime] World inbox overflow; stopping with incomplete world state\n";
        g_running = false;
        return false;
    };
    const auto deliverNativeSnapshot = [&](const kh2coop::ResyncBegin& begin,
                                           const kh2coop::ResyncSnapshot& snapshot) {
        if (!worldBridge.IsOpen()) {
            pendingNativeSnapshot = std::make_pair(begin, snapshot);
            return;
        }
        if (!netClient || !netClient->pendingResync() ||
            netClient->pendingResync()->request.key != begin.key) return;
        const auto plan = *netClient->pendingResync();
        if (begin.phase == kh2coop::ResyncPhase::Bootstrap) {
            const auto priorGeneration = worldSessionGeneration;
            worldBridge.SetPuppetAuthorityMode(kh2coop::PuppetAuthorityMode::Unavailable);
            avatarSync.clear();
            publishInactiveAvatars();
            worldInbox.Clear();
            worldBridge.SetDeliverySerial(worldDeliverySerial);
            worldBridge.SetPeerDeliverySerials(worldPeerDeliverySerials);
            worldSessionGeneration = worldBridge.AdvanceSessionGeneration();
            worldBridge.SetConnectionIds(worldConnectionIds);
            worldBridge.SetLocalSlot(worldSessionSlot);
            if (observeIdentity) observeIdentity("bootstrap-generation");
            // Requeue the immutable fence before its new marker, including
            // lazy attachment/full old FIFO. Duplicate plans cannot renew it.
            const bool markerQueued = enqueueWorld(kh2coop::encode(plan)) &&
                enqueueWorld(kh2coop::encodeWorldSessionReset(worldSessionGeneration, worldDeliverySerial));
            worldCauseReceipt("bootstrap-reset", priorGeneration, markerQueued, &begin);
            if (!markerQueued) return;
        }
        if (!enqueueWorld(kh2coop::encodeNativeResyncSnapshot(begin, snapshot))) return;
        worldCauseReceipt("snapshot-delivered", worldSessionGeneration, false, &begin);
        // The client can receive the complete snapshot and newer ordered world
        // records before KH2 attaches. Keep those records separately from the
        // old bootstrap FIFO that the new generation deliberately retires.
        auto continuation = std::move(pendingNativeContinuation);
        clearNativeContinuation();
        for (const auto& packet : continuation) {
            if (!enqueueWorld(packet)) return;
        }
        worldQuarantined = false;
        worldBridge.SetPuppetAuthorityMode(kh2coop::PuppetAuthorityMode::Network);
        if (observeIdentity) observeIdentity("snapshot-delivered");
    };
#endif
    kh2coop::ClientRecovery recovery(options.config.ownedSlot);
    std::optional<kh2coop::SessionResumePin> resumePin;
    bool membershipInvalidated = false;
    kh2coop::DesyncCollectorOptions desyncOptions;
    desyncOptions.spoolRoot = options.config.desyncDir;
    desyncOptions.injectLogPath = options.config.injectLogPath;
    kh2coop::DesyncCollector desyncCollector(std::move(desyncOptions));
    kh2coop::DesyncUpload desyncUpload;
    std::string diagnosticSessionId;
    std::array<std::uint64_t, 3> diagnosticConnections {};
    std::uint8_t diagnosticSlot {0xFF};
    kh2coop::DesyncKey diagnosticKey;
    std::uint64_t diagnosticDeadlineMs {};
    std::optional<kh2coop::RoomState> diagnosticRoom; // existing loop observation, never a new native read
    const auto recoveryNow = []() {
        return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
    };
    const bool causalDiagnosticsEnabled = exactEnvironmentOne("KH2COOP_CAUSAL_DIAGNOSTICS");
    const bool automaticRecoveryEnabled = exactEnvironmentOne("KH2COOP_AUTOMATIC_RECOVERY") &&
        exactEnvironmentOne("KH2COOP_SURVIVING_PACK_PREPARE") && exactEnvironmentOne("KH2COOP_SPAWN_TRACE");
    std::uint64_t automaticReceiptSequence = 0, automaticSealSequence = 0;
    kh2coop::AutomaticResyncNotice automaticRecovery(automaticRecoveryEnabled,
        [&](const kh2coop::AutomaticResyncNotice::Event& event) {
            std::uint32_t generation = 0;
#ifdef _WIN32
            generation = worldSessionGeneration;
#endif
            std::cout << kh2coop::formatAutomaticResyncNotice(event, ++automaticReceiptSequence,
                recoveryNow(), generation) << '\n';
        });
    const auto submitAutomaticResync = [&](std::uint8_t mask, kh2coop::ResyncRequest* generated) {
        if (!netClient) return false;
        if (!kh2coop::automaticResyncRuntimeCurrent(g_running.load(), netReady.load(), membershipInvalidated, *netClient)) {
            netClient->recordResyncCallerRejection(kh2coop::ResyncRequestOrigin::AutomaticNotice, mask);
            return false;
        }
        return netClient->requestWorldResync(mask, generated, kh2coop::ResyncRequestOrigin::AutomaticNotice);
    };
    if (automaticRecoveryEnabled) std::cout << "[automatic-resync-seal] schema=1 action=begin seq=0 dropped=0\n";
    const auto diagnosticBinding = [&]() {
        kh2coop::DesyncBinding binding;
        binding.sessionId = diagnosticSessionId;
        binding.connections = diagnosticConnections;
        binding.localSlot = diagnosticSlot;
        binding.admitted = netReady && netClient && netClient->ready();
        binding.attachedPid = game.IsAttached() ? game.ProcessId() : 0;
#ifdef _WIN32
        binding.localGeneration = worldSessionGeneration;
        binding.generationValid = worldBridge.IsOpen() && worldSessionGeneration != 0 &&
            worldBridge.GetPuppetAuthorityMode() == kh2coop::PuppetAuthorityMode::Network;
#endif
        return binding;
    };
    constexpr std::uint32_t identityLogLimit = 512;
    std::uint32_t identityLogAttempts = 0, identityLogSuppressed = 0, identityLogErrors = 0;
    const auto incrementDiagnostic = [](std::uint32_t& value) noexcept {
        if (value != std::numeric_limits<std::uint32_t>::max()) ++value;
    };
    observeIdentity = [&](const char* stage) noexcept {
        if (!options.config.networkingEnabled) return;
        if (identityLogAttempts == identityLogLimit) {
            incrementDiagnostic(identityLogSuppressed);
            if (identityLogSuppressed == 1) {
                try {
                    std::cout << "[runtime-identity-gap] schema=1 reason=budget-exhausted limit="
                              << identityLogLimit << " suppressed=1 errors=" << identityLogErrors << '\n';
                } catch (...) { incrementDiagnostic(identityLogErrors); }
            }
            return;
        }
        ++identityLogAttempts;
        try {
            const auto binding = diagnosticBinding(); // cached attachment; no native read
            const auto validSession = [](const std::string& value) {
                return value.empty() || (value.size() == 32 && std::all_of(value.begin(), value.end(),
                    [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); }));
            };
            const auto peerHex = [](const std::string& value) {
                constexpr char digits[] = "0123456789abcdef";
                std::string encoded;
                const auto length = std::min<std::size_t>(value.size(), 128);
                encoded.reserve(length * 2);
                for (std::size_t index = 0; index < length; ++index) {
                    const auto byte = static_cast<unsigned char>(value[index]);
                    encoded.push_back(digits[byte >> 4]); encoded.push_back(digits[byte & 15]);
                }
                return encoded.empty() ? std::string("-") : encoded;
            };
            const bool sessionValid = validSession(binding.sessionId);
            const bool pinValid = !resumePin || validSession(resumePin->sessionId);
            std::ostringstream out;
            out << "[runtime-identity] schema=1 seq=" << identityLogAttempts << " stage=" << stage
                << " observationClock=runtime-steady-ms observationMs=" << recoveryNow()
                << " identitySource=runtime-admission-snapshot identityCurrent=" << binding.admitted
                << " session=" << (sessionValid && !binding.sessionId.empty() ? binding.sessionId : "-")
                << " peerHex=" << peerHex(options.config.peerId)
                << " peerBytes=" << options.config.peerId.size()
                << " stringsComplete=" << (sessionValid && pinValid && options.config.peerId.size() <= 128)
                << " slot=" << static_cast<unsigned>(binding.localSlot)
                << " selfConnection=" << (binding.localSlot < 3 ? binding.connections[binding.localSlot] : 0)
                << " hostConnection=" << binding.connections[0]
                << " roster0=" << binding.connections[0] << " roster1=" << binding.connections[1]
                << " roster2=" << binding.connections[2] << " attachedPid=" << binding.attachedPid
                << " generation=" << binding.localGeneration
                << " generationValid=" << binding.generationValid;
#ifdef _WIN32
            out << " generationSource=runtime-owned delivery=" << worldDeliverySerial << " deliverySource=runtime-owned"
                << " worldSlot=" << static_cast<unsigned>(worldSessionSlot)
                << " worldRoster0=" << worldConnectionIds[0] << " worldRoster1=" << worldConnectionIds[1]
                << " worldRoster2=" << worldConnectionIds[2] << " worldRosterSource=runtime-owned"
                << " peerFloor0=" << worldPeerDeliverySerials[0]
                << " peerFloor1=" << worldPeerDeliverySerials[1]
                << " peerFloor2=" << worldPeerDeliverySerials[2] << " peerFloorSource=runtime-owned"
                << " authority=" << static_cast<unsigned>(worldBridge.GetPuppetAuthorityMode())
                << " authoritySource=bridge-header bridgeOpen=" << worldBridge.IsOpen()
                << " quarantine=" << worldQuarantined;
#else
            out << " generationSource=unavailable delivery=0 deliverySource=unavailable worldSlot=255 worldRoster0=0 worldRoster1=0 worldRoster2=0"
                   " worldRosterSource=unavailable peerFloor0=0 peerFloor1=0 peerFloor2=0"
                   " peerFloorSource=unavailable authority=0 authoritySource=unavailable bridgeOpen=0 quarantine=1";
#endif
            out << " admitted=" << binding.admitted << " transportConnected=" << (netClient && netClient->isConnected())
                << " recoveryState=" << static_cast<unsigned>(recovery.state())
                << " recoveryAttempts=" << recovery.attempts()
                << " pinPresent=" << resumePin.has_value()
                << " pinSession=" << (resumePin && pinValid && !resumePin->sessionId.empty() ? resumePin->sessionId : "-")
                << " pinHostConnection=" << (resumePin ? resumePin->hostConnectionId : 0)
                << " pinSlot=" << (resumePin ? static_cast<unsigned>(resumePin->localSlot) : 255)
                << " nativeBootstrapReady=unverified atomicBinding=0 errors=" << identityLogErrors << '\n';
            std::cout << out.str();
            if (!std::cout) incrementDiagnostic(identityLogErrors);
        } catch (...) {
            // Observation failures never alter admission, recovery, or world state.
            incrementDiagnostic(identityLogErrors);
        }
    };
    const auto diagnosticMetadata = [&]() {
        const auto binding = diagnosticBinding();
        const auto ring = runtimeLog.Take();
        std::ostringstream out;
        out << "observationClock=runtime-steady-ms observationMs=" << recoveryNow()
            << " protocol=" << kh2coop::PROTOCOL_VERSION
            << " session=" << binding.sessionId
            << " slot=" << static_cast<unsigned>(binding.localSlot)
            << " admitted=" << binding.admitted
            << " recoveryState=" << static_cast<unsigned>(recovery.state())
            << " recoveryAttempts=" << recovery.attempts()
            << " attachedPid=" << binding.attachedPid
            << " localGeneration=" << binding.localGeneration
            << " generationValid=" << binding.generationValid
            << " connections=" << binding.connections[0] << ',' << binding.connections[1] << ',' << binding.connections[2]
            << "\nnativeBootstrapReadiness=unverified; metadata is a local observation, not an atomic cross-peer snapshot"
            << "\nruntimeRing totalBytes=" << ring.totalBytes
            << " rangeBegin=" << ring.totalBytes - ring.bytes.size()
            << " rangeEnd=" << ring.totalBytes
            << " truncated=" << (ring.totalBytes > ring.bytes.size()) << '\n';
        if (diagnosticRoom) out << "lastObservedRoom " << describeRoomState(*diagnosticRoom) << '\n';
        else out << "lastObservedRoom=unavailable\n";
        if (netClient) {
            const auto link = netClient->linkStats();
            out << "transport connected=" << netClient->isConnected() << " statsValid=" << link.valid
                << " rttMs=" << link.rttMs << " appRttMs=" << link.appRttMs
                << " lossPermille=" << link.lossPermille << " avatarLossPermille=" << link.avatarLossPermille << '\n';
        }
#ifdef _WIN32
        out << "bridges avatarVersion=" << kh2coop::AVATAR_BRIDGE_VERSION
            << " avatarOpen=" << avatarBridge.IsOpen()
            << " worldVersion=" << kh2coop::WORLD_BRIDGE_VERSION
            << " worldOpen=" << worldBridge.IsOpen()
            << " worldAuthority=" << static_cast<unsigned>(worldBridge.GetPuppetAuthorityMode())
            << " worldToNet=" << worldStats.toNet << " worldToDll=" << worldStats.toDll
            << " rejected=" << worldStats.rejected << " ringFull=" << worldStats.dllRingFull
            << " deferred=" << worldStats.deferred << " inboxOverflow=" << worldStats.inboxOverflow
            << " ephemeralDropped=" << worldStats.ephemeralDropped
            << " retiredOutgoing=" << worldStats.retiredOutgoing << '\n';
#else
        out << "bridges=unavailable-on-this-platform\n";
#endif
        return out.str();
    };
    const auto retireNetworkState = [&]() {
        observeIdentity("retire-before");
        netReady = false;
        desyncCollector.Cancel();
        desyncUpload.Cancel();
        diagnosticSessionId.clear();
        diagnosticConnections = {};
        diagnosticSlot = 0xFF;
        std::lock_guard<std::mutex> lock(replicaMtx);
        replica.Reset();
        avatarSessionId.clear();
        avatarSync.setRoster(static_cast<kh2coop::SlotType>(0xFF), {});
#ifdef _WIN32
        admittedHudNames = {};
        admittedHudNameSession.clear();
        closeMailbox();
        worldSessionSlot = kh2coop::WORLD_SLOT_UNKNOWN;
        worldSessionHost.clear();
        worldSessionId.clear();
        worldHostConnectionId = worldSelfConnectionId = 0;
        worldConnectionIds = {};
        worldPeerDeliverySerials = {};
        worldDeliverySerial = 0;
        worldQuarantined = true;
        pendingNativeSnapshot.reset();
        clearNativeContinuation();
        resetWorldSession(kh2coop::WORLD_SLOT_UNKNOWN, "network-retired");
        worldBridge.SetNetStats(kh2coop::WORLD_NET_UNKNOWN, kh2coop::WORLD_NET_UNKNOWN);
#endif
        observeIdentity("retire-after");
    };

    if (options.config.networkingEnabled) {
        kh2coop::ClientCallbacks callbacks;

        callbacks.onConnected = [&]() {
            recovery.connected(recoveryNow());
            std::cout << "[Runtime] Network: transport connected; awaiting verified roster\n";
            observeIdentity("transport-connected");
        };

        callbacks.onDisconnected = retireNetworkState;
        callbacks.onClosed = [&](const kh2coop::ClientCloseInfo& info) {
            recovery.closed(recoveryNow(), info);
            std::cout << "[Runtime] Network: closed code=" << info.rawCode
                      << " reason=" << static_cast<std::uint32_t>(info.reason) << "\n";
            observeIdentity("transport-closed");
        };

        callbacks.onSessionState = [&](const kh2coop::SessionState& ss) {
            if (!netClient || !netClient->isConnected()) return;
            if (!netClient->ready()) {
                if (resumePin) membershipInvalidated = true;
                retireNetworkState();
                return;
            }
            std::cout << "[Runtime] Network: SessionState session="
                      << ss.sessionId
                      << " actors=" << ss.actors.size()
                      << " room=" << describeRoomState(ss.room) << "\n";
            const auto host = std::find_if(ss.actors.begin(), ss.actors.end(),
                [](const auto& actor) { return actor.slot == kh2coop::SlotType::Player; });
            const auto self = std::find_if(ss.actors.begin(), ss.actors.end(),
                [&options](const auto& actor) {
                    return actor.ownerPeerId == options.config.peerId &&
                           actor.slot == options.config.ownedSlot;
                });
            const auto slot = host != ss.actors.end() && self != ss.actors.end() &&
                              host->connectionId != 0 && self->connectionId != 0
                ? static_cast<std::uint8_t>(self->slot) : std::uint8_t {0xFF};
            const auto hostPeer = host != ss.actors.end() ? host->ownerPeerId : std::string {};
            const auto hostId = host != ss.actors.end() ? host->connectionId : 0;
            const auto selfId = self != ss.actors.end() ? self->connectionId : 0;
            if (resumePin && (ss.sessionId != resumePin->sessionId ||
                hostPeer != resumePin->hostPeerId || hostId != resumePin->hostConnectionId ||
                slot != static_cast<std::uint8_t>(resumePin->localSlot))) {
                membershipInvalidated = true;
                retireNetworkState();
                return;
            }
            recovery.admitted(recoveryNow(), selfId);
            if (recovery.state() != kh2coop::ClientRecovery::State::Admitted) {
                retireNetworkState();
                return;
            }
            if (!resumePin) {
                resumePin = kh2coop::SessionResumePin {ss.sessionId, hostPeer, hostId,
                                                     options.config.peerId, options.config.ownedSlot};
            }
            netReady = true; // verified membership, not native bootstrap readiness
#ifdef _WIN32
            if (game.IsAttached() && !mailboxWriter.IsOpen() &&
                mailboxWriter.Create(static_cast<DWORD>(game.ProcessId()))) {
                std::cout << "[Runtime] Input mailbox created for PID=" << game.ProcessId() << "\n";
            }
#endif
            std::lock_guard<std::mutex> lock(replicaMtx);
            std::array<std::uint64_t, 3> avatarConnections {};
            if (slot < 3) {
                for (const auto& member : ss.actors) {
                    const auto memberSlot = static_cast<std::uint8_t>(member.slot);
                    if (memberSlot < avatarConnections.size())
                        avatarConnections[memberSlot] = member.connectionId;
                }
            }
            if (avatarSessionId != ss.sessionId) avatarSync.clear();
            avatarSessionId = ss.sessionId;
            avatarSync.setRoster(static_cast<kh2coop::SlotType>(slot), avatarConnections);
            diagnosticSessionId = ss.sessionId;
            diagnosticConnections = avatarConnections;
            diagnosticSlot = slot;
#ifdef _WIN32
            const bool rosterChanged = worldConnectionIds != avatarConnections;
            for (std::size_t index = 0; index < worldPeerDeliverySerials.size(); ++index) {
                if (!avatarConnections[index]) worldPeerDeliverySerials[index] = 0;
                else if (avatarConnections[index] != worldConnectionIds[index])
                    worldPeerDeliverySerials[index] = 1;
            }
            worldConnectionIds = avatarConnections;
            if (slot != worldSessionSlot || hostPeer != worldSessionHost || ss.sessionId != worldSessionId ||
                hostId != worldHostConnectionId || selfId != worldSelfConnectionId) {
                worldSessionSlot = slot;
                worldSessionHost = hostPeer;
                worldSessionId = ss.sessionId;
                worldHostConnectionId = hostId;
                worldSelfConnectionId = selfId;
                worldDeliverySerial = 0;
                worldQuarantined = true;
                pendingNativeSnapshot.reset();
                clearNativeContinuation();
                resetWorldSession(slot, "roster-admitted");
            } else {
                // Membership changes retire queued claims without resetting the
                // room or losing the host's already-announced world state.
                if (rosterChanged) {
                    worldBridge.SetPuppetAuthorityMode(kh2coop::PuppetAuthorityMode::Unavailable);
                    publishInactiveAvatars();
                    worldBridge.SetConnectionIds(worldConnectionIds);
                    worldBridge.SetPeerDeliverySerials(worldPeerDeliverySerials);
                    if (slot < 3 && worldConnectionIds[0] && worldConnectionIds[slot] &&
                        worldDeliverySerial && !worldQuarantined)
                        worldBridge.SetPuppetAuthorityMode(kh2coop::PuppetAuthorityMode::Network);
                }
            }
#endif
            // Display-only copy after the original network admission/reset path.
#ifdef _WIN32
            admittedHudNames = kh2coop::hudnames::FromSession(ss, slot);
            admittedHudNameSession = ss.sessionId;
            (void)avatarBridge.PublishRosterNames({}); // retire old labels immediately
#endif
            observeIdentity("roster-admitted");
        };

        callbacks.onRejected = [&](const kh2coop::HelloReject& reject) {
            retireNetworkState();
            recovery.rejected(recoveryNow(), reject.code == 0
                ? kh2coop::DisconnectReason::AdmissionRejected
                : static_cast<kh2coop::DisconnectReason>(reject.code));
            std::cout << "[Runtime] Network: refused by relay: " << reject.reason
                      << "\n";
            observeIdentity("admission-rejected");
        };

        callbacks.onActorSnapshot =
            [&replica, &replicaMtx, &netReady,
#ifdef _WIN32
             &mailboxWriter,
#endif
             legacyReplica = options.legacyReplica,
             ownedSlot = options.config.ownedSlot](
                const kh2coop::ActorSnapshot& snap) {
                // The relay's simulated actors would overwrite the local
                // Sora (VUH-1492); avatars drive puppets instead.
                if (!legacyReplica || !netReady) return;
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
            [&replica, &replicaMtx, &netReady](const kh2coop::EnemySnapshot& snap) {
                if (!netReady) return;
                std::lock_guard<std::mutex> lock(replicaMtx);
                replica.ApplyEnemySnapshot(snap);
            };

        callbacks.onAvatarState = [&avatarSync, &replicaMtx, &netReady](
                                      const kh2coop::AvatarRelay& avatar) {
            if (!netReady) return;
            std::lock_guard<std::mutex> lock(replicaMtx);
            avatarSync.onRemote(avatar);
        };

        callbacks.onDesyncNotice = [&](const kh2coop::DesyncNotice& notice) {
            std::cout << "[Runtime] Desync notice epoch=" << notice.epoch
                      << " slot=" << static_cast<unsigned>(notice.slot)
                      << " fields=" << static_cast<unsigned>(notice.fields) << '\n';
            if (automaticRecoveryEnabled && netClient) automaticRecovery.Notice(notice, netClient->hostResyncContext(),
                netClient->resyncBusy(), recoveryNow(), submitAutomaticResync);
        };
        callbacks.onDesyncCaptureRequest = [&](const kh2coop::DesyncCaptureRequest& request) {
            const auto now = recoveryNow();
            const auto binding = diagnosticBinding();
            if (desyncUpload.State() == kh2coop::DesyncUploadState::Sending ||
                !desyncCollector.Start(request, binding, diagnosticMetadata(), runtimeLog.Take().bytes)) {
                std::cerr << "[Runtime] Desync collection unavailable/busy report=" << request.key.reportId << '\n';
                return;
            }
            diagnosticKey = request.key;
            diagnosticDeadlineMs = now + request.remainingMs;
            std::cout << "[Runtime] Desync collection started report=" << request.key.reportId
                      << " remainingMs=" << request.remainingMs << '\n';
        };

#ifdef _WIN32
        callbacks.onWorldBinding = [&](const kh2coop::WorldBinding& binding) {
            if (binding.selfConnectionId != worldSelfConnectionId ||
                binding.hostConnectionId != worldHostConnectionId || binding.sessionId != worldSessionId) return;
            const bool initial = worldDeliverySerial == 0;
            if (eventHoldControlEnabled && !initial && worldDeliverySerial != binding.deliverySerial)
                eventHoldProjection.Retire(kh2coop::eventhold::Abort::BindingReset);
            if (worldDeliverySerial == binding.deliverySerial) return;
            worldDeliverySerial = binding.deliverySerial;
            worldPeerDeliverySerials[binding.selfSlot] = binding.deliverySerial;
            worldBridge.SetDeliverySerial(worldDeliverySerial);
            worldBridge.SetPeerDeliverySerials(worldPeerDeliverySerials);
            worldQuarantined = !initial;
            if (worldQuarantined) {
                worldBridge.SetPuppetAuthorityMode(kh2coop::PuppetAuthorityMode::Unavailable);
                avatarSync.clear();
                publishInactiveAvatars();
                worldInbox.Clear();
                pendingNativeSnapshot.reset();
                clearNativeContinuation();
            } else if (worldBridge.IsOpen()) {
                if (!worldInbox.BindSessionGeneration(worldSessionGeneration, worldStats,
                                                       worldDeliverySerial)) {
                    g_running = false;
                    observeIdentity("world-binding-failed");
                    return;
                }
                worldBridge.SetPuppetAuthorityMode(kh2coop::PuppetAuthorityMode::Network);
            }
            worldCauseReceipt(initial ? "initial-binding" : "resync-binding", worldSessionGeneration,
                initial && worldBridge.IsOpen());
            observeIdentity("world-binding");
        };
        callbacks.onWorldEnvelope = [&](const kh2coop::WorldEnvelope& envelope) {
            if (!netReady || worldSessionSlot == kh2coop::WORLD_SLOT_UNKNOWN ||
                (envelope.scope.kind != kh2coop::WorldSourceKind::Native &&
                 !(envelope.scope.kind == kh2coop::WorldSourceKind::Relay && !envelope.packet.empty() &&
                   envelope.packet.front() == static_cast<std::uint8_t>(kh2coop::PacketType::PartyReapply)))) return;
            if (worldQuarantined && (!netClient || !netClient->pendingResync())) return;
            if (pendingNativeSnapshot && !worldBridge.IsOpen()) {
                const auto& begin = pendingNativeSnapshot->first;
                if (envelope.scope.hostSourceSerial <= begin.snapshotCut) return;
                if (envelope.packet.empty()) {
                    netClient->failWorldResync(kh2coop::ResyncResultReason::InvalidSnapshot,
                                               "empty native continuation");
                    return;
                }
                const auto type = static_cast<kh2coop::PacketType>(envelope.packet.front());
                if (kh2coop::isEphemeralWorldPacket(type)) {
                    ++worldStats.ephemeralDropped;
                    return;
                }
                auto packet = kh2coop::encode(envelope);
                if (pendingNativeContinuation.size() >= kh2coop::RESYNC_MAX_CONTINUATION_RECORDS ||
                    packet.size() > kh2coop::RESYNC_MAX_CONTINUATION_BYTES - pendingNativeContinuationBytes) {
                    ++worldStats.inboxOverflow;
                    netClient->failWorldResync(kh2coop::ResyncResultReason::Overflow,
                                               "unattached native continuation overflow");
                    return;
                }
                pendingNativeContinuationBytes += packet.size();
                pendingNativeContinuation.push_back(std::move(packet));
                ++worldStats.deferred;
                return;
            }
            // Already admitted by NetworkClient. Publish the immutable control
            // receipt before world enqueue so normal native consumption can
            // correlate its source to an ordinal. Enqueue failure aborts it.
            if (eventHoldControlEnabled && worldSessionSlot != 0) {
                const auto admitted = eventHoldProjection.Admit(envelope, eventHoldScope(),
                    eventHoldEligible(), GetTickCount64());
                if (admitted == kh2coop::eventhold::Admission::Published ||
                    admitted == kh2coop::eventhold::Admission::Aborted)
                    std::cout << "[eventhold-control] admission=" << static_cast<int>(admitted)
                        << " ordinal=" << eventHoldControl.PublishedOrdinal()
                        << " source=" << envelope.scope.hostSourceSerial
                        << " reason=" << static_cast<LONG>(eventHoldControl.Reason()) << '\n';
            }
            enqueueWorld(kh2coop::encode(envelope)); // unchanged sole native world path
        };
        callbacks.onResyncPlan = [&](const kh2coop::ResyncPlan& plan) {
            if (eventHoldControlEnabled) {
                eventHoldProjection.Retire(kh2coop::eventhold::Abort::Unsupported);
                eventHoldProjection.Reject(kh2coop::eventhold::Abort::Unsupported);
            }
            for (std::size_t index = 0; index < plan.targetCount; ++index)
                worldPeerDeliverySerials[plan.targets[index].slot] = plan.targets[index].deliverySerial;
            // Atomic floors retire reverse traffic already past network admission.
            worldBridge.SetPeerDeliverySerials(worldPeerDeliverySerials);
            if (worldSessionSlot != 0 && plan.phase == kh2coop::ResyncPhase::Bootstrap) {
                worldQuarantined = true;
                worldBridge.SetPuppetAuthorityMode(kh2coop::PuppetAuthorityMode::Unavailable);
                avatarSync.clear();
                publishInactiveAvatars();
            }
            enqueueWorld(kh2coop::encode(plan));
            observeIdentity("resync-fenced");
        };
        callbacks.onResyncSnapshot = deliverNativeSnapshot;
#endif
        callbacks.onResyncResult = [&](const kh2coop::ResyncResult& result) {
#ifdef _WIN32
            if (eventHoldControlEnabled) {
                eventHoldProjection.Retire(kh2coop::eventhold::Abort::Unsupported);
                eventHoldProjection.Reject(kh2coop::eventhold::Abort::Unsupported);
            }
#endif
            std::cout << kh2coop::formatResyncResultEvidence(result, "runtime", diagnosticSlot,
                diagnosticSlot < diagnosticConnections.size() ? diagnosticConnections[diagnosticSlot] : 0)
                      << '\n';
#ifdef _WIN32
            pendingNativeSnapshot.reset();
            clearNativeContinuation();
            if (worldSessionSlot != 0 && result.reason != kh2coop::ResyncResultReason::Converged) {
                worldQuarantined = true;
                worldBridge.SetPuppetAuthorityMode(kh2coop::PuppetAuthorityMode::Unavailable);
                avatarSync.clear();
                publishInactiveAvatars();
                worldInbox.Clear();
                // Invalidate native authority immediately, before the game
                // consumes another frame of an already staged bootstrap.
                // Keep the network delivery floor for a later authorized
                // resync, while this native generation remains unarmed.
                worldBridge.SetDeliverySerial(0);
                const auto priorGeneration = worldSessionGeneration;
                worldSessionGeneration = worldBridge.AdvanceSessionGeneration();
                observeIdentity("resync-cancel-generation");
                const bool markerQueued = enqueueWorld(kh2coop::encodeWorldSessionReset(worldSessionGeneration, 0));
                worldCauseReceipt("resync-cancel", priorGeneration, markerQueued, nullptr, &result.key);
                if (!markerQueued) return;
            }
            enqueueWorld(kh2coop::encode(result));
#endif
            observeIdentity("resync-result");
            automaticRecovery.Terminal(result);
        };

        callbacks.onEvent = [](const kh2coop::EventMessage& evt) {
            std::cout << "[Runtime] Network: Event type="
                      << static_cast<int>(evt.type)
                      << " payload=" << evt.payloadJson << "\n";
        };

        if (causalDiagnosticsEnabled) callbacks.onCausalDiagnostic = [](const std::string& row) {
            std::cout << row << std::endl; return std::cout.good();
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
            options.config.contentHash,
            kh2coop::PROTOCOL_VERSION, std::string{}, std::move(steamTransports.client));
        if (causalDiagnosticsEnabled) {
            netClient->sealRequestDiagnostics("begin");
            netClient->sealWorldAdmissionDiagnostics("begin");
        }

        if (options.link.active()) {
            netClient->setLinkConditions(options.link, options.link);
            std::cout << "[Runtime] Link conditions: latency="
                      << options.link.latencyMs << "ms jitter="
                      << options.link.jitterMs << "ms loss="
                      << options.link.lossRate * 100.0f << "%\n";
        }

        if (recovery.start(recoveryNow()) == kh2coop::ClientRecovery::Action::Connect &&
            !netClient->connect()) {
            std::cerr << "[Runtime] Failed to initiate network connection\n";
            retireNetworkState();
            recovery.initiationFailed(recoveryNow());
        }
        observeIdentity("initial-connect-attempted");
    }

    bool cameraOverrideEnabled = options.config.cameraOverrideEnabled;
    bool waitingForAttachLogged = false;
    bool attachLogged = false;
    std::optional<bool> lastEntityDiscovered;
    std::optional<kh2coop::RoomState> lastRoomState;
    auto lastActorLogAt = std::chrono::steady_clock::now();
    auto lastNetLogAt = std::chrono::steady_clock::now();
    auto lastHeartbeatAt = std::chrono::steady_clock::now();
    auto lastCausalSealAt = std::chrono::steady_clock::now();
    auto lastSnapshotAt = std::chrono::steady_clock::now();
    std::uint32_t snapshotSeq = 0;
    auto lastRecoveryState = kh2coop::ClientRecovery::State::Idle;

#ifdef _WIN32
    // Avatar exchange: local avatar out, interpolated puppet poses in. It runs
    // every tick and on a ~1 ms cadence between ticks, so the DLL (which reads
    // once per 16.7 ms game frame) always finds a fresh pose. A 16 ms loop
    // sleep rounds to 16-31 ms on Windows and made puppets stop-go (VUH-1492).
    std::uint16_t pumpWorld = 0;
    std::uint16_t pumpRoom = 0;
    const auto pumpAvatars = [&](std::uint16_t worldId, std::uint16_t roomId) {
        pumpWorld = worldId;
        pumpRoom = roomId;
        if (!avatarBridge.IsOpen()) return;
        if (!netClient || !netReady || worldSessionSlot >= 3 || worldSessionGeneration == 0 ||
            !worldConnectionIds[0] || !worldConnectionIds[worldSessionSlot] ||
            worldQuarantined || worldBridge.GetPuppetAuthorityMode() != kh2coop::PuppetAuthorityMode::Network) {
            publishInactiveAvatars();
            return;
        }
        // Network callbacks and this pump run on the same runtime thread. Names
        // renew independently of a new local avatar; resets publish an empty slot.
        const kh2coop::PuppetAuthority hudAuthority {kh2coop::PuppetAuthorityMode::Network,
            worldSessionSlot, worldSessionGeneration, worldConnectionIds};
        (void)avatarBridge.PublishRosterNames(kh2coop::hudnames::Bind(
            admittedHudNames, hudAuthority, GetTickCount64(), admittedHudNameSession, worldSessionId));
        kh2coop::AvatarState local;
        if (avatarBridge.TryReadLocal(local)) {
            // Network seq is per send (sendAvatar restamps 0), so receivers can
            // count gaps as loss; the DLL's frame counter stays in recordings.
            kh2coop::LocalDownedState downed;
            (void)avatarBridge.ReadLocalDownedState(downed);
            kh2coop::projectLocalDowned(local, downed,
                {worldSessionGeneration, worldBridge.DeliverySerial(), 0}, GetTickCount64());
            local.seq = 0;
            local.serverTimeMs = 0; // stamped by sendAvatar
            netClient->sendAvatar(local);
        }
        std::array<kh2coop::PuppetTarget, 2> targets;
        {
            std::lock_guard<std::mutex> lock(replicaMtx);
            targets = avatarSync.sample(netClient->estimatedServerTimeMs(),
                                        worldId, roomId);
        }
        for (int i = 0; i < 2; ++i) {
            kh2coop::PuppetPose pose;
            pose.active = targets[i].active ? 1 : 0;
            pose.pose = targets[i].pose;
            pose.provenance.producer = kh2coop::PuppetProducer::Network;
            pose.provenance.localSlot = worldSessionSlot;
            pose.provenance.generation = worldSessionGeneration;
            pose.provenance.ownerConnectionId = targets[i].ownerConnectionId;
            pose.provenance.localConnectionId = worldConnectionIds[worldSessionSlot];
            pose.provenance.hostConnectionId = worldConnectionIds[0];
            avatarBridge.PublishPuppet(i, pose);
        }
    };
    // 1 ms sleep granularity for the pump; restored at shutdown.
    timeBeginPeriod(1);
#endif

    // Diagnostic only: locate a missed control renewal without keeping an
    // unserviced runtime alive or changing its wall-time expiry.
    const auto eventHoldTimed = [&](const char* stage, auto&& operation) -> decltype(auto) {
#ifdef _WIN32
        struct Timing {
            const char* stage;
            std::uint64_t started;
            ~Timing() {
                if (!started) return;
                const auto ended = GetTickCount64();
                if (ended >= started && ended - started >= 100)
                    std::cerr << "[eventhold-control] slow-stage stage=" << stage
                        << " observationMs=" << ended << " durationMs=" << ended - started << '\n';
            }
        } timing {stage, eventHoldControlEnabled ? GetTickCount64() : 0};
#else
        (void)stage;
#endif
        return operation();
    };

    for (std::uint32_t tick = 0;
         g_running && (options.maxTicks == 0 || tick < options.maxTicks);
         ++tick) {

        // Pump network events every tick, even before KH2 is attached.
        if (netClient) {
            if (steamHost) steamHost->tick(0);
            eventHoldTimed("network-outer", [&] { netClient->tick(0); });
            if (automaticRecoveryEnabled) automaticRecovery.Pump(netClient->hostResyncContext(),
                [&]() { return netClient->resyncBusy(); }, submitAutomaticResync);
            if (membershipInvalidated) {
                membershipInvalidated = false;
                retireNetworkState();
                netClient->disconnect();
                kh2coop::ClientCloseInfo info;
                info.reason = kh2coop::DisconnectReason::SessionChanged;
                info.rawCode = static_cast<std::uint32_t>(info.reason);
                info.local = true;
                recovery.closed(recoveryNow(), info);
            }
            const auto action = recovery.tick(recoveryNow());
            if (action == kh2coop::ClientRecovery::Action::Disconnect) {
                observeIdentity("recovery-disconnect");
                retireNetworkState();
                netClient->disconnect();
            } else if (action == kh2coop::ClientRecovery::Action::Connect) {
                retireNetworkState();
                if (!resumePin || !netClient->SetResumePin(resumePin)) {
                    recovery.shutdown();
                    std::cerr << "[Runtime] Rejoin stopped: original session pin unavailable\n";
                } else {
                    std::cout << "[Runtime] Rejoin attempt=" << recovery.attempts() << "\n";
                    if (!netClient->connect()) recovery.initiationFailed(recoveryNow());
                }
                observeIdentity("retry-attempted");
            }
            // Keep protocol activity independent of game attachment and bootstrap.
            const auto networkNow = std::chrono::steady_clock::now();
            if (causalDiagnosticsEnabled && networkNow - lastCausalSealAt >= 1s) {
                netClient->sealRequestDiagnostics();
                netClient->sealWorldAdmissionDiagnostics();
                lastCausalSealAt = networkNow;
            }
            if (netClient->isConnected() && networkNow - lastHeartbeatAt >=
                std::chrono::milliseconds(options.config.heartbeatIntervalMs)) {
                netClient->sendHeartbeat();
                lastHeartbeatAt = networkNow;
            }
#ifdef _WIN32
            if (worldBridge.IsOpen() && !netReady)
                kh2coop::pumpDllToNet(worldBridge, *netClient, worldStats);
#endif
            if (recovery.state() != lastRecoveryState) {
                lastRecoveryState = recovery.state();
                if (lastRecoveryState == kh2coop::ClientRecovery::State::Terminal)
                    std::cerr << "[Runtime] Networking stopped: " << recovery.terminalReason() << "\n";
                else if (lastRecoveryState == kh2coop::ClientRecovery::State::RetryWait)
                    std::cout << "[Runtime] Rejoin waiting; world authority unavailable\n";
                else if (lastRecoveryState == kh2coop::ClientRecovery::State::Admitted)
                    std::cout << "[Runtime] Verified membership; native bootstrap remains separate\n";
                observeIdentity("recovery-state");
            }
        }

#ifdef _WIN32
        pulseEventHoldControl();
#endif
        // Collection filesystem/capture work runs on one owned worker. Poll
        // and upload before the attachment early-return, just like heartbeats.
        const auto binding = diagnosticBinding();
        desyncCollector.Tick(binding, desyncCollector.Busy() ? diagnosticMetadata() : std::string {});
        if (auto collected = desyncCollector.TakeCompleted()) {
            std::cout << "[Runtime] Desync local evidence report=" << collected->done.key.reportId
                      << " directory=" << collected->localDirectory.string()
                      << " identityChanged=" << collected->identityChanged << '\n';
            if (collected->done.key == diagnosticKey)
                desyncUpload.Begin(std::move(collected->done), std::move(collected->bytes), diagnosticDeadlineMs);
        }
        const auto uploadBefore = desyncUpload.State();
        const auto uploadAfter = desyncUpload.Tick(recoveryNow(), binding.sessionId,
            binding.localSlot < 3 ? binding.connections[binding.localSlot] : 0, binding.admitted,
            [&](const auto& chunk) { return netClient && netClient->sendDesyncArtifactChunk(chunk); },
            [&](const auto& done) { return netClient && netClient->sendDesyncCaptureDone(done); });
        if (uploadBefore == kh2coop::DesyncUploadState::Sending && uploadAfter != uploadBefore)
            std::cout << "[Runtime] Desync upload finished report=" << diagnosticKey.reportId
                      << " state=" << static_cast<unsigned>(uploadAfter)
                      << " (complete means queued, relay manifest is authoritative)\n";

        if (!game.IsAttached()) {
            if (!waitingForAttachLogged) {
                std::cout << "[Runtime] Waiting for KH2 process...\n";
                waitingForAttachLogged = true;
            }

            if (options.pid ? game.Attach(*options.pid) : game.Attach()) {
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
                if (netReady && !mailboxWriter.IsOpen()) {
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

        eventHoldTimed("game-tick", [&] { game.Tick(); });

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

        const auto room = eventHoldTimed("room-read", [&] { return game.ReadRoomState(); });
        diagnosticRoom = room;

#ifdef _WIN32
        // ----- Avatars: local out, remote puppets in -----
        if (netClient) {
            if (!avatarBridge.IsOpen()) {
                avatarBridge.Open(static_cast<DWORD>(game.ProcessId()));
            }
            if (!worldBridge.IsOpen()) {
                if (worldBridge.Open(static_cast<DWORD>(game.ProcessId()))) {
                    const auto priorGeneration = worldSessionGeneration;
                    worldBridge.SetPuppetAuthorityMode(kh2coop::PuppetAuthorityMode::Unavailable);
                    worldSessionGeneration = worldBridge.AdvanceSessionGeneration();
                    worldBridge.SetConnectionIds(worldConnectionIds);
                    const auto nativeDelivery = worldQuarantined ? 0 : worldDeliverySerial;
                    worldBridge.SetDeliverySerial(nativeDelivery);
                    worldBridge.SetPeerDeliverySerials(worldPeerDeliverySerials);
                    const bool markerQueued = worldInbox.BindSessionGeneration(worldSessionGeneration, worldStats,
                        nativeDelivery);
                    if (!markerQueued) {
                        std::cerr << "[Runtime] Could not bind world session generation\n";
                        g_running = false;
                    }
                    worldBridge.SetLocalSlot(worldSessionSlot);
                    if (worldSessionSlot < 3 && worldConnectionIds[0] && worldConnectionIds[worldSessionSlot] &&
                        worldDeliverySerial && !worldQuarantined)
                        worldBridge.SetPuppetAuthorityMode(kh2coop::PuppetAuthorityMode::Network);
                    worldCauseReceipt("bridge-attach", priorGeneration, markerQueued);
                    observeIdentity("bridge-open-bound");
                    if (pendingNativeSnapshot) {
                        const auto staged = std::move(*pendingNativeSnapshot);
                        pendingNativeSnapshot.reset();
                        deliverNativeSnapshot(staged.first, staged.second);
                    }
                }
            }
            if (worldBridge.IsOpen()) {
                // Tell the DLL its session slot (0 = Player = host).
                const auto slot = worldSessionSlot;
                if (worldBridge.LocalSlot() != slot) worldBridge.SetLocalSlot(slot);
                worldInbox.Flush(worldBridge, worldStats);
                // Link quality for the overlay: app-level RTT (includes any
                // simulated latency) and the avatar-stream loss players see,
                // falling back to ENet's estimate until a loss window closes.
                const auto link = netClient->linkStats();
                if (netReady && link.valid) {
                    const auto loss =
                        link.avatarLossPermille != kh2coop::NetworkClient::kNoAvatarLoss
                            ? link.avatarLossPermille
                            : link.lossPermille;
                    worldBridge.SetNetStats(link.appRttMs ? link.appRttMs : link.rttMs, loss);
                } else {
                    worldBridge.SetNetStats(kh2coop::WORLD_NET_UNKNOWN,
                                            kh2coop::WORLD_NET_UNKNOWN);
                }
                kh2coop::pumpDllToNet(worldBridge, *netClient, worldStats);
            }
            pumpAvatars(room.worldId, room.roomId);
        }
#endif
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

        eventHoldTimed("camera-tick", [&] { camera.Tick(room); });

        const auto now = std::chrono::steady_clock::now();

        // ----- Networking: send owned actor snapshot at configured interval -----
        if (options.legacyReplica && netClient && netReady && entityDiscovered) {
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

        if (netClient && netReady && now - lastNetLogAt >= 5s) {
            const auto link = netClient->linkStats();
            if (link.valid) {
                std::cout << "[Runtime] Net: rtt=" << link.appRttMs
                          << "ms enet_rtt=" << link.rttMs << "ms var="
                          << link.rttVarMs << "ms enet_loss="
                          << link.lossPermille / 10 << "." << link.lossPermille % 10 << "%";
                if (link.avatarLossPermille != kh2coop::NetworkClient::kNoAvatarLoss) {
                    std::cout << " avatar_loss=" << link.avatarLossPermille / 10 << "."
                              << link.avatarLossPermille % 10 << "%";
                }
                std::cout << "\n";
            }
            lastNetLogAt = now;
        }

        if ((options.config.logOwnedActorState || automaticRecoveryEnabled) &&
            now - lastActorLogAt >= 1s) {
            if (options.config.logOwnedActorState) {
                // Log all 3 party slots for visibility during testing.
                for (auto s : {kh2coop::SlotType::Player,
                               kh2coop::SlotType::Friend1,
                               kh2coop::SlotType::Friend2}) {
                    logOwnedActorState(game, s);
                }
            }
            if (automaticRecoveryEnabled && netClient) {
                std::uint64_t causeHighWater = 0;
                std::uint32_t generation = 0;
#ifdef _WIN32
                causeHighWater = worldCauseSequence;
                generation = worldSessionGeneration;
#endif
                // Existing one-second observation cadence only: never pump the
                // notice policy, retry a request or extend any deadline here.
                std::cout << kh2coop::formatAutomaticResyncInterval(automaticRecovery, *netClient,
                    ++automaticSealSequence, recoveryNow(), automaticReceiptSequence,
                    causeHighWater, generation, diagnosticConnections) << std::endl;
            }
            lastActorLogAt = now;
        }

#ifdef _WIN32
        const auto tickEnd = std::chrono::steady_clock::now() +
                             std::chrono::milliseconds(options.config.tickMs);
        while (g_running && std::chrono::steady_clock::now() < tickEnd) {
            eventHoldTimed("pump-sleep", [&] { std::this_thread::sleep_for(std::chrono::milliseconds(1)); });
            if (netClient) eventHoldTimed("network-inner", [&] { if (steamHost) steamHost->tick(0); netClient->tick(0); });
            pulseEventHoldControl();
            eventHoldTimed("avatar-inner", [&] { pumpAvatars(pumpWorld, pumpRoom); });
        }
#else
        std::this_thread::sleep_for(
            std::chrono::milliseconds(options.config.tickMs));
#endif
    }

    // ----- Graceful shutdown -----
    automaticRecovery.Shutdown();
    if (automaticRecoveryEnabled) std::cout << "[automatic-resync-seal] schema=1 action=end seq="
        << automaticReceiptSequence << " pending=" << automaticRecovery.PendingCount() << " dropped=0\n";
    observeIdentity("shutdown-before");
    desyncCollector.Cancel();
    desyncUpload.Cancel();
    recovery.shutdown();
#ifdef _WIN32
    if (eventHoldControlEnabled) eventHoldProjection.Retire(kh2coop::eventhold::Abort::Shutdown);
    timeEndPeriod(1);
    if (mailboxWriter.IsOpen()) {
        closeMailbox();
        std::cout << "[Runtime] Input mailbox closed\n";
    }
    worldConnectionIds = {};
    resetWorldSession(kh2coop::WORLD_SLOT_UNKNOWN, "shutdown");
    worldBridge.SetNetStats(kh2coop::WORLD_NET_UNKNOWN, kh2coop::WORLD_NET_UNKNOWN);
#endif

    if (netClient) {
        std::cout << "[Runtime] Disconnecting from server...\n";
        netClient->disconnect();
        netClient.reset();
    }
    observeIdentity("shutdown-after");
#ifdef _WIN32
    std::cout << "[runtime-world-cause-seal] schema=1 seq=" << worldCauseSequence << " dropped=0\n";
#endif
    if (options.config.networkingEnabled) {
        try {
            std::cout << "[runtime-identity-summary] schema=1 limit=" << identityLogLimit
                      << " attempted=" << identityLogAttempts << " suppressed=" << identityLogSuppressed
                      << " errors=" << identityLogErrors << '\n';
        } catch (...) { /* shutdown diagnostics never prevent owned cleanup */ }
    }

    if (attachLogged) {
        game.RestoreVanillaCamera();
    }

    enet_deinitialize();
    std::cout << "[Runtime] Shutdown\n";
    return 0;
}
