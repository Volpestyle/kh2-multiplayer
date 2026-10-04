#include "kh2coop/WorldBridge.hpp"
#include <sstream>
#include <stdexcept>

namespace kh2coop {
// Kept in its own translation unit: HitChannel's local diagnostic HitClaim
// layout is independent of the network claim record used by Codec.hpp.
std::string queueWorldResyncCommand(std::uint32_t pid, std::uint8_t targetMask) {
    if (!pid || (targetMask != 2 && targetMask != 4 && targetMask != 6))
        throw std::runtime_error("Invalid explicit world-resync PID or target mask");
    WorldBridge bridge;
    if (!bridge.OpenExisting(pid)) throw std::runtime_error("No compatible existing WorldBridge mapping");
    const ProducerWorldContext captured {bridge.SessionGeneration(), bridge.DeliverySerial(), 0};
    const auto host = bridge.ConnectionId(0);
    if (!captured.generation || !captured.deliverySerial || !host || bridge.LocalSlot() != 0 ||
        bridge.GetPuppetAuthorityMode() != PuppetAuthorityMode::Network)
        throw std::runtime_error("WorldBridge has no admitted host authority");
    if (bridge.SessionGeneration() != captured.generation || bridge.DeliverySerial() != captured.deliverySerial ||
        bridge.ConnectionId(0) != host || bridge.LocalSlot() != 0 ||
        bridge.GetPuppetAuthorityMode() != PuppetAuthorityMode::Network ||
        !bridge.QueueResyncCommand(targetMask, captured, host))
        throw std::runtime_error("Host context changed or WorldBridge operator mailbox is busy");
    std::ostringstream json;
    json << "{\"ok\":true,\"queued\":true,\"nativeConvergence\":false,\"processId\":" << pid
         << ",\"targetMask\":" << static_cast<unsigned>(targetMask)
         << ",\"generation\":" << captured.generation
         << ",\"deliverySerial\":" << captured.deliverySerial << '}';
    return json.str();
}
} // namespace kh2coop
