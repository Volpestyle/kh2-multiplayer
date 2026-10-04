#pragma once
#include "kh2coop/ResyncProtocol.hpp"
#include <sstream>
#include <string_view>

namespace kh2coop {
// Log actual terminal facts only. Hex encoding keeps bounded peer errors on
// one line and prevents them from forging another structured evidence record.
inline std::string formatResyncResultEvidence(const ResyncResult& result,
                                             std::string_view component,
                                             std::uint8_t observerSlot = 0xff,
                                             std::uint64_t observerConnection = 0) {
    const auto hex = [](const auto& bytes) {
        constexpr char digits[] = "0123456789abcdef";
        std::string out;
        out.reserve(bytes.size() * 2);
        for (const auto byte : bytes) {
            const auto value = static_cast<unsigned char>(byte);
            out.push_back(digits[value >> 4]);
            out.push_back(digits[value & 15]);
        }
        return out;
    };
    std::ostringstream out;
    out << "[resync-result] component=" << component
        << " observerSlot=" << static_cast<unsigned>(observerSlot)
        << " observerConnection=" << observerConnection
        << " session=" << result.key.sessionId
        << " host=" << result.key.hostConnectionId
        << " request=" << result.key.requestId
        << " reason=" << static_cast<unsigned>(result.reason)
        << " targetCount=" << static_cast<unsigned>(result.targetCount);
    for (std::size_t index = 0; index < result.targetCount && index < result.targets.size(); ++index) {
        const auto& target = result.targets[index];
        out << " target" << index << '=' << static_cast<unsigned>(target.target.slot)
            << ',' << target.target.connectionId << ',' << target.target.deliverySerial
            << ',' << static_cast<unsigned>(target.status) << ',' << target.appliedCut
            << ',' << hex(target.fingerprint) << ','
            << (target.error.empty() ? "-" : hex(target.error.substr(0, 256)));
    }
    return out.str();
}
} // namespace kh2coop
