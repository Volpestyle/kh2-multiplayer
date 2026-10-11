#pragma once
#include "kh2coop/Transport.hpp"
#include <enet/enet.h>
#include <iostream>

// Test-only ENet adapter. The loss fixture already supplies its own seeded
// LinkConditioner. Prevent ENet's additional scheduler-sensitive drop policy from changing the seeded loss model.
class AvatarConditionedTransport final : public kh2coop::Transport {
public:
    ~AvatarConditionedTransport() override {
        std::cout << "  ENet fixture throttle: min=" << minimum_
                  << "/32 sendsBelowFull=" << reduced_ << '\n';
    }
    bool createClient(std::size_t p, std::size_t c) override { return inner_->createClient(p,c); }
    kh2coop::TransportOpenResult listen(const std::string& a, std::uint16_t p, std::size_t n, std::size_t c) override { return inner_->listen(a,p,n,c); }
    kh2coop::TransportPeer* connect(const std::string& a, std::uint16_t p, std::size_t c, bool& resolved) override {
        auto* peer = inner_->connect(a,p,c,resolved); configure(peer); return peer;
    }
    bool isOpen() const override { return inner_->isOpen(); }
    void close() override { inner_->close(); }
    int service(kh2coop::TransportEvent& e, std::uint32_t ms) override {
        const auto n=inner_->service(e,ms);
        if (n>0 && e.type==kh2coop::TransportEventType::Connect) configure(e.peer);
        return n;
    }
    bool send(kh2coop::TransportPeer* peer, const std::uint8_t* data, std::size_t size, std::uint8_t channel, bool reliable) override {
        const auto value=reinterpret_cast<ENetPeer*>(peer)->packetThrottle;
        if (value<minimum_) minimum_=value;
        if (!reliable && value<ENET_PEER_PACKET_THROTTLE_SCALE) ++reduced_;
        return inner_->send(peer,data,size,channel,reliable);
    }
    void disconnect(kh2coop::TransportPeer* p,std::uint32_t r) override { inner_->disconnect(p,r); }
    void disconnectLater(kh2coop::TransportPeer* p,std::uint32_t r) override { inner_->disconnectLater(p,r); }
    kh2coop::TransportStats stats(kh2coop::TransportPeer* p) const override { return inner_->stats(p); }
    std::string pendingPeerLabel(kh2coop::TransportPeer* p) const override { return inner_->pendingPeerLabel(p); }
private:
    void configure(kh2coop::TransportPeer* peer) {
        if (peer) {
            auto* p=reinterpret_cast<ENetPeer*>(peer);
            // Keep the full initial throttle; zero acceleration/deceleration
            // prevents RTT jitter adding a second loss model to this fixture.
            p->packetThrottle=ENET_PEER_PACKET_THROTTLE_SCALE;
            enet_peer_throttle_configure(p,ENET_PEER_PACKET_THROTTLE_INTERVAL,0,0);
        }
    }
    std::unique_ptr<kh2coop::Transport> inner_=kh2coop::makeEnetTransport();
    enet_uint32 minimum_=ENET_PEER_PACKET_THROTTLE_SCALE;
    std::uint64_t reduced_=0;
};
