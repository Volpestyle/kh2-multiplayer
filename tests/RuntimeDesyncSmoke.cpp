// Companion to the offline Python process check. The third participant is the
// real runtime executable with --pid 0: it cannot attach or touch a game.
#include "kh2coop/SessionHost.hpp"
#include "kh2coop/NetworkClient.hpp"
#include "kh2coop/DesyncUpload.hpp"
#include <enet/enet.h>
#include <array>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <memory>
#include <thread>

using namespace kh2coop;
namespace {
std::uint64_t nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}
int run(const std::filesystem::path& output, std::uint16_t port) {
    SessionConfig config;
    config.bindAddress = "127.0.0.1"; config.port = port;
    config.gameBuild = "runtime-desync-smoke"; config.contentHash = "none"; config.modHash = "";
    config.desyncOutputRoot = output.string();
    SessionHost relay(config);
    if (!relay.start()) return 2;
    std::array<std::unique_ptr<NetworkClient>, 2> clients;
    std::array<DesyncUpload, 2> uploads;
    std::array<DesyncCaptureRequest, 2> requests;
    for (unsigned index = 0; index < 2; ++index) {
        const auto slot = index == 0 ? SlotType::Player : SlotType::Friend2;
        ClientCallbacks callbacks;
        callbacks.onDesyncCaptureRequest = [&, index, slot](const DesyncCaptureRequest& request) {
            requests[index] = request;
            DesyncCaptureDone done;
            done.key = request.key; done.connectionId = request.connections[static_cast<unsigned>(slot)];
            std::array<std::vector<std::uint8_t>, 4> bytes;
            const std::string metadata = "owned offline fixture; no native observations\n";
            const std::string log = "offline fixture runtime peer " + std::to_string(index) + "\n";
            bytes[0].assign(metadata.begin(), metadata.end()); bytes[1].assign(log.begin(), log.end());
            for (unsigned kind = 0; kind < 4; ++kind) {
                auto& d = done.artifacts[kind]; d.kind = static_cast<DesyncArtifactKind>(kind);
                d.status = kind < 2 ? DesyncArtifactStatus::Complete : DesyncArtifactStatus::Unavailable;
                d.bytes = static_cast<std::uint32_t>(bytes[kind].size());
                d.sourceBytes = d.rangeEnd = d.bytes; d.sha256 = desyncSha256(bytes[kind]);
                d.sourceLabel = "offline-fixture";
                if (kind >= 2) d.error = "fixture has no inject log or renderer";
            }
            uploads[index].Begin(std::move(done), std::move(bytes), nowMs() + request.remainingMs);
        };
        clients[index] = std::make_unique<NetworkClient>("127.0.0.1", port, config.gameBuild,
            config.modHash, "runtime-smoke-fixture-" + std::to_string(index), slot,
            std::move(callbacks), RuntimeMode::CampaignCoop, config.contentHash);
        if (!clients[index]->connect()) return 3;
    }
    std::cout << "READY " << port << '\n' << std::flush;
    const auto stopAt = nowMs() + 20000;
    unsigned phase = 0;
    while (nowMs() < stopAt && !relay.lastDesyncCapture()) {
        relay.tick(0);
        for (unsigned i = 0; i < 2; ++i) {
            clients[i]->tick(0); clients[i]->sendHeartbeat();
            const auto slot = i == 0 ? 0u : 2u;
            uploads[i].Tick(nowMs(), requests[i].key.sessionId, requests[i].connections[slot], clients[i]->ready(),
                [&](const auto& chunk) { return clients[i]->sendDesyncArtifactChunk(chunk); },
                [&](const auto& done) { return clients[i]->sendDesyncCaptureDone(done); });
        }
        if (!phase && relay.verifiedPeerCount() == 3 && clients[0]->worldReady() && clients[1]->worldReady()) {
            const auto* runtime = relay.peerBySlot(SlotType::Friend1);
            if (!runtime || runtime->peerId != "offline-runtime") return 4;
            clients[0]->sendStateHash(StateHash {9, 4, 26, 100, 200}); phase = 1;
        }
        const auto* host = relay.peerBySlot(SlotType::Player);
        const auto* friend2 = relay.peerBySlot(SlotType::Friend2);
        if (phase == 1 && host && host->hasHash) {
            clients[1]->sendStateHash(StateHash {9, 4, 26, 101, 200}); phase = 2;
        } else if (phase == 2 && friend2 && friend2->hashReceiptSeq == 1) {
            clients[1]->sendStateHash(StateHash {9, 4, 26, 101, 200}); phase = 3;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    int code = 5;
    if (const auto& result = relay.lastDesyncCapture()) {
        std::cout << "MANIFEST " << result->manifestPath << '\n';
        code = result->manifestWritten && result->status == DesyncCollectionStatus::Partial ? 0 : 6;
    }
    for (auto& client : clients) client->disconnect();
    relay.stop();
    return code;
}
} // namespace
int main(int argc, char** argv) {
    if (argc != 3 || enet_initialize()) return 1;
    const auto code = run(argv[1], static_cast<std::uint16_t>(std::stoul(argv[2])));
    enet_deinitialize(); return code;
}
