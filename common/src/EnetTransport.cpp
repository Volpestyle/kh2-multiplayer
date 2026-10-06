#include "kh2coop/Transport.hpp"
#include <enet/enet.h>
#include <sstream>

namespace kh2coop {
namespace {
ENetPeer* native(TransportPeer* peer) { return reinterpret_cast<ENetPeer*>(peer); }
class EnetTransport final : public Transport {
public:
    ~EnetTransport() override { close(); }
    bool createClient(std::size_t peers, std::size_t channels) override {
        host_ = enet_host_create(nullptr, peers, channels, 0, 0);
        return host_ != nullptr;
    }
    TransportOpenResult listen(const std::string& bind, std::uint16_t port,
                               std::size_t peers, std::size_t channels) override {
        ENetAddress address{}; address.host = ENET_HOST_ANY; address.port = port;
        if (!bind.empty() && enet_address_set_host_ip(&address, bind.c_str()) != 0)
            return TransportOpenResult::InvalidAddress;
        host_ = enet_host_create(&address, peers, channels, 0, 0);
        return host_ ? TransportOpenResult::Ok : TransportOpenResult::CreateFailed;
    }
    TransportPeer* connect(const std::string& host, std::uint16_t port,
                           std::size_t channels, bool& resolved) override {
        ENetAddress address{};
        resolved = enet_address_set_host(&address, host.c_str()) == 0;
        if (!resolved) return nullptr;
        address.port = port;
        return reinterpret_cast<TransportPeer*>(enet_host_connect(host_, &address, channels, 0));
    }
    bool isOpen() const override { return host_ != nullptr; }
    void close() override { if (host_) enet_host_destroy(host_); host_ = nullptr; }
    int service(TransportEvent& result, std::uint32_t timeoutMs) override {
        result = {};
        ENetEvent event{};
        const int n = enet_host_service(host_, &event, timeoutMs);
        if (n <= 0) return n;
        result.peer = reinterpret_cast<TransportPeer*>(event.peer); result.data = event.data;
        switch (event.type) {
        case ENET_EVENT_TYPE_CONNECT: result.type = TransportEventType::Connect; break;
        case ENET_EVENT_TYPE_DISCONNECT: result.type = TransportEventType::Disconnect; break;
        case ENET_EVENT_TYPE_RECEIVE:
            result.type = TransportEventType::Receive;
            result.packet = TransportPacket(event.packet, [](void* p) { enet_packet_destroy(static_cast<ENetPacket*>(p)); },
                event.packet->data, event.packet->dataLength, (event.packet->flags & ENET_PACKET_FLAG_RELIABLE) != 0);
            break;
        case ENET_EVENT_TYPE_NONE: break;
        }
        return n;
    }
    bool send(TransportPeer* peer, const std::uint8_t* data, std::size_t size,
              std::uint8_t channel, bool reliable) override {
        auto* packet = enet_packet_create(data, size, reliable ? ENET_PACKET_FLAG_RELIABLE : 0);
        if (!packet) return false;
        if (enet_peer_send(native(peer), channel, packet) < 0) {
            enet_packet_destroy(packet); return false;
        }
        return true;
    }
    void disconnect(TransportPeer* peer, std::uint32_t reason) override { enet_peer_disconnect(native(peer), reason); }
    void disconnectLater(TransportPeer* peer, std::uint32_t reason) override { enet_peer_disconnect_later(native(peer), reason); }
    TransportStats stats(TransportPeer* peer) const override {
        const auto* p = native(peer);
        return {p->roundTripTime, p->roundTripTimeVariance,
            static_cast<std::uint32_t>((static_cast<std::uint64_t>(p->packetLoss) * 1000u) / ENET_PEER_PACKET_LOSS_SCALE), p->channelCount};
    }
    std::string pendingPeerLabel(TransportPeer* peer) const override {
        std::ostringstream label;
        label << "peer_" << native(peer)->address.host << ":" << native(peer)->address.port;
        return label.str();
    }
private:
    ENetHost* host_ = nullptr;
};
} // namespace
std::unique_ptr<Transport> makeEnetTransport() { return std::make_unique<EnetTransport>(); }
} // namespace kh2coop
