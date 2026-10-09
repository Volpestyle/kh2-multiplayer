#pragma once
#include "kh2coop/Types.hpp"
#include "kh2coop/PuppetProvenance.hpp"
#include <array>
#include <cmath>
#include <cstdint>

namespace kh2coop::inject::avatarposition {
// Observational only: caller retains all native admission and existing writes.
enum class Phase : unsigned { BetweenUpdates, NativeCorrection, Writeback };
inline const char* Name(Phase p) noexcept {
    return p == Phase::BetweenUpdates ? "between-updates" : p == Phase::NativeCorrection ? "native-correction" : "writeback";
}
struct Scope {
    std::uintptr_t actor {};
    std::uint32_t handle {}, transition {}, load {};
    PuppetProvenance provenance {};
    std::uint8_t owner {}, character {}, puppetIndex {};
    std::uint16_t world {}, room {};
    bool operator==(const Scope& b) const noexcept {
        const auto& a = provenance; const auto& t = b.provenance;
        return actor == b.actor && handle == b.handle && transition == b.transition && load == b.load &&
            owner == b.owner && character == b.character && puppetIndex == b.puppetIndex && world == b.world && room == b.room &&
            a.producer == t.producer && a.localSlot == t.localSlot && a.generation == t.generation &&
            a.ownerConnectionId == t.ownerConnectionId && a.localConnectionId == t.localConnectionId &&
            a.hostConnectionId == t.hostConnectionId;
    }
};
// Shared by the actual native adapter and producer/admission integration controls.
inline Scope PoseScope(const AvatarState& p, const PuppetProvenance& provenance, std::uintptr_t actor,
                       std::uint32_t handle, std::uint32_t transition, std::uint32_t load, std::uint8_t index) {
    return {actor,handle,transition,load,provenance,static_cast<std::uint8_t>(p.ownerSlot),p.character,index,p.worldId,p.roomId};
}
struct Receipt {
    Phase phase {}; bool recovered {}, finite {}, budgetExhausted {};
    std::uint32_t frame {}, seq {}, streak {};
    Scope scope {}; Vec3 target {}, observed {}; double error {};
};
class Monitor {
    struct Lane { unsigned streak {}; bool reported {}; std::uint32_t frame {}, emitted {}; bool sampled {}; };
    Scope scope_ {}; bool scoped_ {}, previous_ {};
    Vec3 target_ {}; std::uint32_t frame_ {}, seq_ {};
    std::array<Lane, 3> lanes_ {};
    unsigned receipts_ {}, corrections_ {}; // Lifetime budgets deliberately survive scope resets.
public:
    static constexpr double Threshold = 5.0;
    static constexpr unsigned Debounce = 3, RepeatFrames = 120, ReceiptLimit = 128, CorrectionLimit = 16;
    void Reset() noexcept { scoped_ = previous_ = false; lanes_ = {}; }
    void Bind(const Scope& scope, bool held) noexcept {
        if (held) { Reset(); return; }
        if (!scoped_ || !(scope_ == scope)) { Reset(); scope_ = scope; scoped_ = true; }
    }
    template<class Emit>
    void Sample(Phase phase, std::uint32_t frame, std::uint32_t seq, Vec3 target, Vec3 observed, Emit emit) {
        if (!scoped_) return;
        auto& lane = lanes_[static_cast<unsigned>(phase)];
        if (lane.sampled && lane.frame == frame) return; // Reentrant/same-frame calls cannot satisfy debounce.
        if (lane.sampled && frame - lane.frame != 1) lane.streak = 0;
        lane.frame = frame; lane.sampled = true;
        const double dx = double(observed.x) - target.x, dy = double(observed.y) - target.y, dz = double(observed.z) - target.z;
        const double error = std::sqrt(dx*dx + dy*dy + dz*dz);
        const bool finite = std::isfinite(error);
        const bool bad = !finite || error > Threshold;
        lane.streak = bad ? lane.streak + (lane.streak < Debounce ? 1 : 0) : 0;
        const bool recovered = !bad && lane.reported;
        const bool report = bad && lane.streak >= Debounce && (!lane.reported || frame - lane.emitted >= RepeatFrames);
        auto& count = phase == Phase::NativeCorrection ? corrections_ : receipts_;
        const unsigned limit = phase == Phase::NativeCorrection ? CorrectionLimit : ReceiptLimit;
        if ((recovered || report) && count < limit) {
            ++count;
            try { emit(Receipt{phase, recovered, finite, count == limit, frame, seq, lane.streak, scope_, target, observed, error}); }
            catch (...) { /* A failed diagnostic sink cannot suppress existing writes. */ }
            lane.emitted = frame;
        }
        if (report) lane.reported = true;
        if (!bad) lane.reported = false;
    }
    template<class Read, class Emit>
    void BeforeUpdate(std::uint32_t frame, Read read, Emit emit) {
        if (scoped_ && previous_ && frame - frame_ == 1)
            Sample(Phase::BetweenUpdates, frame, seq_, target_, read(), emit);
        else lanes_[0] = {};
    }
    // Production uses this exact owned-memory seam around its existing xyz stores.
    template<class Read, class Write, class Emit>
    void Apply(std::uint32_t frame, std::uint32_t seq, Vec3 target, Read read, Write write, Emit emit) {
        if (!scoped_) { write(target); return; }
        Sample(Phase::NativeCorrection, frame, seq, target, read(), emit);
        write(target);
        Sample(Phase::Writeback, frame, seq, target, read(), emit);
        if (scoped_) { target_ = target; frame_ = frame; seq_ = seq; previous_ = true; }
    }
};
} // namespace kh2coop::inject::avatarposition
