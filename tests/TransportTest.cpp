#include "kh2coop/Transport.hpp"
#include <enet/enet.h>
#include <array>
#include <chrono>
#include <iostream>
#include <thread>

using namespace kh2coop;
int main() {
    int failures = 0;
    auto check = [&](bool ok, const char* text) {
        std::cout << (ok ? "PASS " : "FAIL ") << text << '\n'; if (!ok) ++failures;
    };
    if (enet_initialize() != 0) return 2;
    {
        auto server = makeEnetTransport(), client = makeEnetTransport();
        check(server->listen("not-an-ip", 17962, 1, 3) == TransportOpenResult::InvalidAddress && !server->isOpen(), "invalid bind stays closed");
        if (server->listen("127.0.0.1", 17962, 1, 3) != TransportOpenResult::Ok || !client->createClient(1, 3)) return 2;
        bool resolved = false;
        auto* outgoing = client->connect("127.0.0.1", 17962, 3, resolved);
        check(resolved && outgoing, "connect initiates");
        TransportPeer* incoming = nullptr;
        bool connected = false;
        std::array<bool, 3> seen{};
        const auto pump = [&](auto done) {
            const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(3);
            while (!done() && std::chrono::steady_clock::now() < end) {
                TransportEvent e;
                while (client->service(e, 0) > 0) if (e.type == TransportEventType::Connect) connected = true;
                while (server->service(e, 0) > 0) {
                    if (e.type == TransportEventType::Connect) incoming = e.peer;
                    if (e.type == TransportEventType::Receive) {
                        check(e.packet.size == 4 && e.packet.data[1] == 0 && e.packet.data[2] == 255 && e.packet.data[3] == 42,
                              "payload bytes unchanged");
                        const auto channel = e.packet.data[0];
                        check(channel < 3 && e.packet.reliable == (channel != 1), "reliable/unreliable receipt unchanged");
                        if (channel < 3) seen[channel] = true;
                    }
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        };
        pump([&] { return connected && incoming; });
        check(connected && incoming && server->stats(incoming).channelCount == 3, "both connections and three channels");
        if (!connected || !incoming) return 2;
        check(server->pendingPeerLabel(incoming).starts_with("peer_"), "temporary ENet label retained");
        for (std::uint8_t channel = 0; channel < 3; ++channel) {
            const std::array<std::uint8_t, 4> bytes{channel, 0, 255, 42};
            check(client->send(outgoing, bytes.data(), bytes.size(), channel, channel != 1), "submission");
        }
        pump([&] { return seen[0] && seen[1] && seen[2]; });
        check(seen[0] && seen[1] && seen[2], "all channel payloads received");
        const std::array<std::uint8_t, 2> bytes{37, 99};
        check(!client->send(outgoing, bytes.data(), bytes.size(), 3, true), "invalid channel refuses and frees packet");
        check(client->send(outgoing, bytes.data(), bytes.size(), 0, true), "post-refusal send");
        TransportEvent held;
        const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (held.type != TransportEventType::Receive && std::chrono::steady_clock::now() < end) {
            TransportEvent e; client->service(e, 0); server->service(held, 1);
        }
        server->close(); client->close();
        check(held.type == TransportEventType::Receive && held.packet.size == 2 && held.packet.data[0] == 37 && held.packet.data[1] == 99,
              "borrowed packet survives host close from callback");
        TransportPacket moved = std::move(held.packet);
        check(held.packet.data == nullptr && moved.size == 2, "packet move has one owner");
        moved.reset(); moved.reset();
        check(client->createClient(1, 3), "closed transport can reconnect");
        client->close();
    }
    enet_deinitialize();
    return failures ? 1 : 0;
}
