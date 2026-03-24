#include "kh2coop/SessionHost.hpp"

#include <csignal>
#include <enet/enet.h>
#include <iostream>
#include <string>

static volatile bool g_running = true;

static void signalHandler(int) { g_running = false; }

static void printUsage() {
    std::cout << "Usage: kh2coop_server [options]\n"
              << "  --port <port>       Listen port (default 7782)\n"
              << "  --build <hash>      Required game build hash\n"
              << "  --mod <hash>        Required mod hash\n"
              << "  --session <id>      Session identifier\n"
              << "  --max-peers <n>     Max peers (default 3)\n";
}

int main(int argc, char* argv[]) {
    // --- Parse args ---
    kh2coop::SessionConfig config;
    config.port = 7782;
    config.maxPeers = 3;
    config.gameBuild = "dev";
    config.modHash = "none";
    config.sessionId = "local-test";

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            printUsage();
            return 0;
        }
        if (arg == "--port" && i + 1 < argc) {
            config.port = static_cast<std::uint16_t>(std::stoi(argv[++i]));
        } else if (arg == "--build" && i + 1 < argc) {
            config.gameBuild = argv[++i];
        } else if (arg == "--mod" && i + 1 < argc) {
            config.modHash = argv[++i];
        } else if (arg == "--session" && i + 1 < argc) {
            config.sessionId = argv[++i];
        } else if (arg == "--max-peers" && i + 1 < argc) {
            config.maxPeers = static_cast<std::uint32_t>(std::stoi(argv[++i]));
        }
    }

    // --- Init ENet ---
    if (enet_initialize() != 0) {
        std::cerr << "Failed to initialize ENet.\n";
        return 1;
    }

    // --- Callbacks ---
    kh2coop::SessionCallbacks callbacks;
    callbacks.onLog = [](const std::string& msg) {
        std::cout << msg << "\n";
    };
    callbacks.onPeerJoined = [](const std::string& peerId,
                                kh2coop::SlotType slot) {
        std::cout << "[Server] Peer joined: " << peerId
                  << " as slot " << static_cast<int>(slot) << "\n";
    };
    callbacks.onPeerLeft = [](const std::string& peerId) {
        std::cout << "[Server] Peer left: " << peerId << "\n";
    };
    callbacks.onPeerRejected = [](const std::string& peerId,
                                  const std::string& reason) {
        std::cout << "[Server] Peer rejected: " << peerId
                  << " (" << reason << ")\n";
    };
    callbacks.onInputReceived = [](const std::string& peerId,
                                   const kh2coop::InputFrame& input) {
        // In a real implementation this would feed into the authoritative sim.
        // For now, just log at high verbosity if desired.
    };

    // --- Start ---
    kh2coop::SessionHost host(config, std::move(callbacks));
    if (!host.start()) {
        std::cerr << "Failed to start session host.\n";
        enet_deinitialize();
        return 1;
    }

    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    std::cout << "[Server] Running. Press Ctrl+C to stop.\n";

    // --- Main loop ---
    while (g_running) {
        host.tick(16 /* ~60 Hz */);

        // TODO: In the future, run the authoritative simulation here and
        // broadcast actor/enemy snapshots every tick.
    }

    host.stop();
    enet_deinitialize();
    std::cout << "[Server] Shutdown complete.\n";
    return 0;
}
