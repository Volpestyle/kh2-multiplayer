#include "kh2coop/CameraController.hpp"
#include "kh2coop/GameBridgePC.hpp"
#include "kh2coop/Types.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <csignal>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <thread>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

namespace {

using namespace std::chrono_literals;

std::atomic_bool g_running {true};

struct RuntimeConfig {
    kh2coop::RuntimeMode runtimeMode {kh2coop::RuntimeMode::CampaignCoop};
    kh2coop::SlotType ownedSlot {kh2coop::SlotType::Player};
    bool cameraOverrideEnabled {true};
    bool panicHotkeyEnabled {true};
    bool logOwnedActorState {false};
    std::uint32_t tickMs {16};
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

    std::cout << "[Runtime] Booting runtime scaffold\n";
    std::cout << "[Runtime] config=" << options.configPath
              << " mode=" << modeToString(options.config.runtimeMode)
              << " role=" << slotToString(options.config.ownedSlot)
              << " camera_override="
              << (options.config.cameraOverrideEnabled ? "on" : "off")
              << " panic_hotkey="
              << (options.config.panicHotkeyEnabled ? "F8" : "disabled")
              << " tick_ms=" << options.config.tickMs
              << " max_ticks=" << options.maxTicks << "\n";

    std::ifstream configProbe(options.configPath);
    if (!configProbe.is_open()) {
        std::cout << "[Runtime] No config file found at " << options.configPath
                  << "; using defaults and command-line overrides\n";
    }

    kh2coop::GameBridgePC game;
    kh2coop::CameraController camera(game);
    camera.SetOwnedSlot(options.config.ownedSlot);
    camera.SetOverrideEnabled(options.config.cameraOverrideEnabled);

    bool cameraOverrideEnabled = options.config.cameraOverrideEnabled;
    bool waitingForAttachLogged = false;
    bool attachLogged = false;
    std::optional<bool> lastEntityDiscovered;
    std::optional<kh2coop::RoomState> lastRoomState;
    auto lastActorLogAt = std::chrono::steady_clock::now();

    for (std::uint32_t tick = 0;
         g_running && (options.maxTicks == 0 || tick < options.maxTicks);
         ++tick) {
        if (!game.IsAttached()) {
            if (!waitingForAttachLogged) {
                std::cout << "[Runtime] Waiting for KH2 process...\n";
                waitingForAttachLogged = true;
            }

            if (game.Attach()) {
                attachLogged = true;
                waitingForAttachLogged = false;
                std::cout << "[Runtime] Attached to KH2 process\n";
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
        }
        if (!lastEntityDiscovered.has_value() ||
            entityDiscovered != *lastEntityDiscovered) {
            std::cout << "[Runtime] Entity discovery "
                      << (entityDiscovered ? "ready" : "missing") << "\n";
            lastEntityDiscovered = entityDiscovered;
        }

        camera.Tick(room);

        const auto now = std::chrono::steady_clock::now();
        if (options.config.logOwnedActorState &&
            now - lastActorLogAt >= 1s) {
            logOwnedActorState(game, options.config.ownedSlot);
            lastActorLogAt = now;
        }

        std::this_thread::sleep_for(
            std::chrono::milliseconds(options.config.tickMs));
    }

    if (attachLogged) {
        game.RestoreVanillaCamera();
    }

    std::cout << "[Runtime] Shutdown\n";
    return 0;
}
