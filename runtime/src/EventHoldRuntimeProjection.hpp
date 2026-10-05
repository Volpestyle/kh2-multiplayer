#pragma once
// Separate input control projection after authenticated normal world admission.
#include "kh2coop/EventHoldControl.hpp"
#include "kh2coop/Codec.hpp"
#include <string_view>

namespace kh2coop::eventhold {
enum class Admission { Ignored, Excluded, Published, Aborted };
class RuntimeProjection {
public:
    explicit RuntimeProjection(Channel& channel) : channel_(channel) {}
    void Reject(Abort reason) noexcept { blocked_ = true; channel_.Invalidate(reason); }
    void Retire(Abort reason) noexcept {
        Scope bound {};
        if (channel_.BoundScope(bound)) {
            blocked_ = true;
            channel_.Reset(reason);
        } else {
            // Initial roster/reset may precede attachment. Nothing was armed.
            excludedCount_ = 0; baselineEpoch_ = 0;
        }
    }
    bool Tick(const Scope& current, bool eligible, std::uint64_t now) noexcept {
        if (blocked_) return false;
        Scope bound {};
        const bool hasBound = channel_.BoundScope(bound);
        if (!eligible || !ValidScope(current)) {
            if (hasBound) Retire(Abort::Unsupported);
            return false;
        }
        if (hasBound && bound != current) { Retire(Abort::BindingReset); return false; }
        if (!hasBound) {
            if (!channel_.Bind(current, now)) { blocked_ = true; return false; }
            for (std::size_t i = 0; i < excludedCount_; ++i)
                if (!channel_.ExcludeSource(excluded_[i])) { blocked_ = true; return false; }
        }
        if (!channel_.Heartbeat(now)) { blocked_ = true; return false; }
        Ack ack {};
        if (!channel_.Armed() && baselineEpoch_ && channel_.ReadAck(ack) && ack.epoch == baselineEpoch_)
            channel_.Arm(now);
        return true;
    }
    Admission Admit(const WorldEnvelope& envelope, const Scope& current,
                    bool eligible, std::uint64_t now) noexcept {
        if (envelope.packet.empty()) return Admission::Ignored;
        const auto type = static_cast<PacketType>(envelope.packet.front());
        if (type != PacketType::EventHold && type != PacketType::RoomTransition) return Admission::Ignored;
        if (blocked_) return Admission::Aborted;
        const auto fail = [&](Abort why) {
            blocked_ = true; channel_.Invalidate(why); return Admission::Aborted;
        };
        // Original admitted envelope must match the complete runtime binding,
        // including the current host delivery floor, never just target/epoch.
        const auto& s = envelope.scope;
        if (!ValidIdentity(current) || s.kind != WorldSourceKind::Native ||
            s.sessionId != std::string_view(current.session, sizeof(current.session)) ||
            s.sourceConnectionId != current.hostConnection || s.sourceDeliverySerial != current.hostDelivery ||
            s.targetConnectionId != current.selfConnection || s.targetDeliverySerial != current.targetDelivery ||
            !s.hostSourceSerial) return fail(Abort::WrongScope);
        if (!eligible) return fail(Abort::Unsupported);
        Command command {}; command.scope = current; command.hostSourceSerial = s.hostSourceSerial;
        try {
            const std::uint8_t* payload = nullptr; std::size_t bytes = 0;
            if (decodePacketHeader(envelope.packet.data(), envelope.packet.size(), payload, bytes) != type ||
                bytes+3 != envelope.packet.size()) return fail(Abort::InvalidOrder);
            ByteReader reader(payload, bytes);
            if (type == PacketType::EventHold) {
                EventHold hold {}; read(reader, hold);
                command.kind = hold.active ? Kind::Acquire : Kind::Release;
                command.epoch = hold.epoch; command.eventProgram = hold.eventProgram;
            } else {
                RoomTransition room {}; read(reader, room);
                command.kind = Kind::Transition; command.epoch = room.epoch;
                command.world = room.worldId; command.room = room.roomId; command.door = room.door;
                command.map = room.mapProgram; command.battle = room.battleProgram; command.eventProgram = room.eventProgram;
            }
            if (!reader.atEnd() || !command.epoch) return fail(Abort::InvalidOrder);
        } catch (...) { return fail(Abort::InvalidOrder); }
        Scope bound {};
        if (channel_.BoundScope(bound) && bound != current) return fail(Abort::WrongScope);
        if (!channel_.Armed()) {
            // Active bootstrap cannot safely be adopted. Initial inactive/cache
            // data remains solely in WorldInbox and is not an input command.
            if (command.kind == Kind::Acquire) return fail(Abort::Unsupported);
            if (command.kind == Kind::Transition) baselineEpoch_ = command.epoch;
            bool seen = false;
            for (std::size_t i = 0; i < excludedCount_; ++i) seen |= excluded_[i] == s.hostSourceSerial;
            if (!seen) {
                if (excludedCount_ == excluded_.size()) return fail(Abort::SourceLimit);
                excluded_[excludedCount_++] = s.hostSourceSerial;
            }
            if (channel_.BoundScope(bound) && !channel_.ExcludeSource(s.hostSourceSerial)) return fail(channel_.Reason());
            return Admission::Excluded;
        }
        return channel_.Publish(command, now) ? Admission::Published : fail(channel_.Reason());
    }
private:
    Channel& channel_;
    bool blocked_ {};
    std::uint32_t baselineEpoch_ {};
    std::array<std::uint64_t, SourceCapacity> excluded_ {};
    std::size_t excludedCount_ {};
};
} // namespace kh2coop::eventhold
