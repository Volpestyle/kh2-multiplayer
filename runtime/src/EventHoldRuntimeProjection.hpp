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
    void Reject(Abort reason) noexcept { RememberLifetime(); blocked_ = true; channel_.Invalidate(reason); }
    void Retire(Abort reason) noexcept {
        RememberLifetime();
        Scope bound {};
        if (channel_.BoundScope(bound)) {
            blocked_ = true;
            channel_.Reset(reason);
        } else {
            // Initial roster/reset may precede attachment. Nothing was armed.
            if (haveLifetime_) blocked_ = true;
            else { excludedCount_ = 0; baselineEpoch_ = 0; }
        }
    }
    // Setup/pump thread only, after the old producer retired and Channel was
    // rebound to the native-created fresh scoped mapping. The caller must prove
    // current native scope/epoch, matching authenticated begin+snapshot+result,
    // cut/fingerprint, and two-frame Converged/result consumption. A cached
    // RoomTransition is never that proof. This method cannot clear an old abort.
    bool AdoptConvergedBaseline(const Scope& current,std::uint32_t epoch,
                                const ResyncResult& result,std::uint64_t now) noexcept {
        Scope bound {};
        if (!blocked_ || !haveLifetime_ || !ValidScope(current) || !epoch ||
            std::memcmp(current.session,lifetimeScope_.session,sizeof(current.session))!=0 ||
            current.hostConnection!=lifetimeScope_.hostConnection || current.slot!=lifetimeScope_.slot ||
            current.generation<=lifetimeScope_.generation || current.selfConnection==lifetimeScope_.selfConnection ||
            !channel_.IsScopedFor(current) || !channel_.BoundScope(bound) || bound!=current ||
            channel_.PublishedOrdinal()!=1 || channel_.Armed() ||
            result.reason!=ResyncResultReason::Converged || !result.key.requestId ||
            result.key.requestId<=baselineRequest_ || result.key.hostConnectionId!=current.hostConnection ||
            result.key.sessionId!=std::string_view(current.session,sizeof(current.session)) ||
            !result.targetCount || result.targetCount>result.targets.size()) return false;
        bool matched=false;
        std::array<bool,3> seen {};
        for (std::size_t i=0;i<result.targetCount;++i) {
            const auto& target=result.targets[i];
            if (target.target.slot<1 || target.target.slot>2 || seen[target.target.slot] ||
                !target.target.connectionId || !target.target.deliverySerial || !target.appliedCut ||
                target.status!=ResyncAckStatus::Converged) return false;
            seen[target.target.slot]=true;
            if (target.target.slot==current.slot) {
                if (target.target.connectionId!=current.selfConnection ||
                    target.target.deliverySerial!=current.targetDelivery) return false;
                matched=true;
            }
        }
        // Heartbeat proves this Channel is the new producer, not an owner view.
        if (!matched || !channel_.Heartbeat(now)) return false;
        blocked_=false;baselineEpoch_=epoch;explicitBaseline_=true;
        excludedCount_=0;excluded_.fill(0);
        lifetimeScope_=current;baselineRequest_=result.key.requestId;
        return true; // Arm still requires the new ordinal1 native ACK on Tick.
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
        RememberLifetime();
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
                if (hold.start) {
                    command.startWorld = hold.start->worldId; command.startRoom = hold.start->roomId;
                    command.startId = hold.start->id; command.startFlags = hold.start->flags;
                    command.startParams = hold.start->params;
                }
            } else {
                RoomTransition room {}; readRoomTransitionPacket(reader, room);
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
            if (explicitBaseline_ && command.epoch!=baselineEpoch_) return fail(Abort::InvalidOrder);
            if (!explicitBaseline_ && command.kind == Kind::Transition) baselineEpoch_ = command.epoch;
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
    void RememberLifetime() noexcept {
        if (!haveLifetime_ && channel_.BoundScope(lifetimeScope_)) haveLifetime_=true;
    }
    Channel& channel_;
    bool blocked_ {}, haveLifetime_ {}, explicitBaseline_ {};
    Scope lifetimeScope_ {};
    std::uint64_t baselineRequest_ {};
    std::uint32_t baselineEpoch_ {};
    std::array<std::uint64_t, SourceCapacity> excluded_ {};
    std::size_t excludedCount_ {};
};
} // namespace kh2coop::eventhold
