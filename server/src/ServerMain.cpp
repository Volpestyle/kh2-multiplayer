#include "kh2coop/SessionHost.hpp"
#include "kh2coop/SimulationState.hpp"

#include <chrono>
#include <csignal>
#include <enet/enet.h>
#include <iostream>
#include <string>
#include <thread>

static volatile bool g_running = true;

static void signalHandler(int) { g_running = false; }

static void printUsage() {
    std::cout << "Usage: kh2coop_server [options]\n"
              << "  --port <port>       Listen port (default 7782)\n"
              << "  --heartbeat-timeout-ms <ms>\n"
              << "                      Drop idle verified peers after this long (default 5000)\n"
              << "  --pending-timeout-ms <ms>\n"
              << "                      Drop unverified peers after this long (default 2000)\n"
              << "  --build <hash>      Required game build hash\n"
              << "  --content <hash>    Required content hash\n"
              << "  --mod <hash>        Required mod hash\n"
              << "  --session <id>      Session identifier\n"
              << "  --max-peers <n>     Max peers (default 3)\n";
}

int main(int argc, char* argv[]) {
    constexpr auto kTickSleep = std::chrono::milliseconds(16);
    constexpr float kTickDtSeconds = 1.0f / 60.0f;

    // --- Parse args ---
    kh2coop::SessionConfig config;
    config.port = 7782;
    config.maxPeers = 3;
    config.heartbeatTimeoutMs = 5000;
    config.pendingPeerTimeoutMs = 2000;
    config.gameBuild = "dev";
    config.contentHash = "none";
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
        } else if (arg == "--heartbeat-timeout-ms" && i + 1 < argc) {
            config.heartbeatTimeoutMs = static_cast<std::uint32_t>(std::stoul(argv[++i]));
        } else if (arg == "--pending-timeout-ms" && i + 1 < argc) {
            config.pendingPeerTimeoutMs = static_cast<std::uint32_t>(std::stoul(argv[++i]));
        } else if (arg == "--build" && i + 1 < argc) {
            config.gameBuild = argv[++i];
        } else if (arg == "--content" && i + 1 < argc) {
            config.contentHash = argv[++i];
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
        (void)peerId;
        (void)input;
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
    kh2coop::SimulationState sim;
    while (g_running) {
        host.tick(0);

        for (const auto& peer : host.peers()) {
            if (peer.status == kh2coop::PeerStatus::Verified) {
                sim.applyInput(peer.assignedSlot, peer.lastInput);
            }
        }

        sim.tick(kTickDtSeconds);
        host.broadcastActorSnapshots(sim.generateSnapshots());

        std::this_thread::sleep_for(kTickSleep);
    }

    host.stop();
    enet_deinitialize();
    std::cout << "[Server] Shutdown complete.\n";
    return 0;
}
