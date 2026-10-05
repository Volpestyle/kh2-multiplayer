#include "kh2coop/SessionHost.hpp"
#include <cstdlib>
#include "kh2coop/SimulationState.hpp"

#include <chrono>
#include <cstdlib>
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
              << "  --bind <ip>         Listen only on this address (e.g. your Tailscale 100.x IP);\n"
              << "                      default: all interfaces\n"
              << "  --heartbeat-timeout-ms <ms>\n"
              << "                      Drop idle verified peers after this long (default 5000)\n"
              << "  --pending-timeout-ms <ms>\n"
              << "                      Drop unverified peers after this long (default 2000)\n"
              << "  --build <hash>      Required game build hash\n"
              << "  --content <hash>    Required content hash\n"
              << "  --mod <hash>        Required mod hash\n"
              << "  --session <id>      Session identifier\n"
              << "  --desync-dir <path> Automatic report root (default build/rig/desync)\n"
              << "  --max-peers <n>     Max peers (default 3)\n";
    std::cout << "  --simulate         Enable legacy synthetic actor simulation\n";
}

int main(int argc, char* argv[]) {
    // Unbuffered so redirected logs survive a kill (scenario runner).
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;
    constexpr auto kTickSleep = std::chrono::milliseconds(16);
    constexpr float kTickDtSeconds = 1.0f / 60.0f;

    // --- Parse args ---
    kh2coop::SessionConfig config;
    bool simulate = false;
    config.port = 7782;
    config.maxPeers = 3;
    config.heartbeatTimeoutMs = 5000;
    config.pendingPeerTimeoutMs = 2000;
    config.gameBuild = "dev";
    config.contentHash = "none";
    config.modHash = "none";
    config.sessionId = "local-test";
    config.desyncOutputRoot = "build/rig/desync";

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            printUsage();
            return 0;
        }
        if (arg == "--port" && i + 1 < argc) {
            config.port = static_cast<std::uint16_t>(std::stoi(argv[++i]));
        } else if (arg == "--bind" && i + 1 < argc) {
            config.bindAddress = argv[++i];
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
        } else if (arg == "--desync-dir" && i + 1 < argc) {
            config.desyncOutputRoot = argv[++i];
        } else if (arg == "--max-peers" && i + 1 < argc) {
            config.maxPeers = static_cast<std::uint32_t>(std::stoi(argv[++i]));
        } else if (arg == "--simulate") {
            simulate = true;
        }
    }

    // --- Init ENet ---
    if (enet_initialize() != 0) {
        std::cerr << "Failed to initialize ENet.\n";
        return 1;
    }

    // --- Callbacks ---
    kh2coop::SessionCallbacks callbacks;
    const auto* causalSetting=std::getenv("KH2COOP_CAUSAL_DIAGNOSTICS");
    const bool causalDiagnosticsEnabled=causalSetting && std::string(causalSetting)=="1";
    if (causalDiagnosticsEnabled) callbacks.onCausalDiagnostic = [](const std::string& row) {
        std::cout << row << std::endl; return std::cout.good();
    };
    const auto* hashSetting = std::getenv("KH2COOP_CAUSAL_DIAGNOSTICS");
    if (hashSetting && std::string(hashSetting) == "1") callbacks.onHashDiagnostic = [](const std::string& row) {
        std::cout << row << '\n'; std::cout.flush(); return std::cout.good();
    };
    callbacks.onLog = [](const std::string& msg) {
        std::cout << msg << "\n";
    };
    callbacks.onDesyncCaptureFinalized = [](const kh2coop::DesyncCaptureResult& result) {
        std::cout << "[Server] Desync report=" << result.key.reportId
                  << " state=" << static_cast<unsigned>(result.status)
                  << " manifestWritten=" << result.manifestWritten
                  << " manifest=" << result.manifestPath
                  << " error=" << result.error << '\n';
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
    if (causalDiagnosticsEnabled) host.sealCacheDiagnostics("begin");
    auto lastCausalSeal=std::chrono::steady_clock::now();
    const bool hashDiagnosticsEnabled = hashSetting && std::string(hashSetting) == "1";
    auto lastHashSeal = std::chrono::steady_clock::now();
    while (g_running) {
        host.tick(0);
        if(causalDiagnosticsEnabled){
            const auto causalNow=std::chrono::steady_clock::now();
            if(causalNow-lastCausalSeal>=std::chrono::seconds(1)){
                host.sealCacheDiagnostics();lastCausalSeal=causalNow;
            }
        }
        if (hashDiagnosticsEnabled) {
            const auto hashNow = std::chrono::steady_clock::now();
            if (hashNow - lastHashSeal >= std::chrono::seconds(1)) {
                host.sealHashDiagnostics(); lastHashSeal = hashNow;
            }
        }

        if (simulate) {
            for (const auto& peer : host.peers()) {
                if (peer.status == kh2coop::PeerStatus::Verified)
                    sim.applyInput(peer.assignedSlot, peer.lastInput);
            }
            sim.tick(kTickDtSeconds);
            host.broadcastActorSnapshots(sim.generateSnapshots());
        }

        std::this_thread::sleep_for(kTickSleep);
    }

    host.stop();
    enet_deinitialize();
    std::cout << "[Server] Shutdown complete.\n";
    return 0;
}
