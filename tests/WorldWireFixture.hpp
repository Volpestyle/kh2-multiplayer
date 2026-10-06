#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
// Test-only authenticated ENet injection. No production friend or bypass API.
// Preinclude all NetworkClient dependencies before exposing its transport handle.
#include "kh2coop/Codec.hpp"
#include "kh2coop/CausalDiagnostics.hpp"
#include "kh2coop/LinkConditioner.hpp"
#include "kh2coop/Types.hpp"
#include <functional>
#include <optional>
#include <string>
#include <vector>
#define private public
#include "kh2coop/NetworkClient.hpp"
#undef private
#include <enet/enet.h>
namespace worldfixture {
inline std::vector<std::uint8_t> uncheckedEnvelope(const kh2coop::WorldScope& s,const std::vector<std::uint8_t>& bytes){
    kh2coop::ByteWriter w;w.writeString(s.sessionId);w.writeU64(s.sourceConnectionId);w.writeU64(s.sourceDeliverySerial);w.writeU64(s.hostSourceSerial);w.writeU64(s.targetConnectionId);w.writeU64(s.targetDeliverySerial);w.writeU8(static_cast<std::uint8_t>(s.kind));w.writeU16(static_cast<std::uint16_t>(bytes.size()));for(auto b:bytes)w.writeU8(b);return kh2coop::encodePacket(kh2coop::PacketType::WorldEnvelope,w.data());
}
inline bool raw(kh2coop::NetworkClient& client,const std::vector<std::uint8_t>& bytes,bool reliable=true){
    if (!client.transportPeer_ || !client.transport_->isOpen()) {
        return false;
    }
    auto* packet = enet_packet_create(bytes.data(), bytes.size(), reliable ? ENET_PACKET_FLAG_RELIABLE : 0);
    if (!packet) {
        return false;
    }
    if (enet_peer_send(reinterpret_cast<ENetPeer*>(client.transportPeer_), reliable ? 0 : 1, packet) != 0) {
        enet_packet_destroy(packet);
        return false;
    }
    enet_host_flush(reinterpret_cast<ENetPeer*>(client.transportPeer_)->host);
    return true;
}
inline std::vector<std::uint8_t> capture(kh2coop::NetworkClient& client,const std::vector<std::uint8_t>& bytes){
    if (!client.worldBinding()) {
        return {};
    }
    const auto& binding = *client.worldBinding();
    const auto context = client.makeTestingWorldContext();
    return uncheckedEnvelope({binding.sessionId, binding.selfConnectionId, context.deliverySerial,
                              context.hostSourceSerial, 0, 0}, bytes);
}
inline bool send(kh2coop::NetworkClient& client,const std::vector<std::uint8_t>& bytes,bool reliable=true){const auto packet=capture(client,bytes);return !packet.empty()&&raw(client,packet,reliable);}
} // namespace worldfixture
