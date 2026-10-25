#pragma once
// One validated TT34 end occurrence owns the prelude and its native315.
// Owner-thread state only; the adapter supplies fresh scope/native gates.
#include <array>
#include <cstdint>

namespace kh2coop::inject::thorndefeat {
struct Key {
    std::array<std::uint8_t,32> session {};
    std::uint64_t host {}, hostDelivery {}, self {}, delivery {};
    std::uintptr_t mission {};
    std::uint32_t generation {}, epoch {}, load {}, transition {};
    std::uint8_t slot = 255;
    bool operator==(const Key&) const = default;
    bool Valid() const noexcept {
        bool sessionSet = false; for (auto c : session) sessionSet |= c != 0;
        return sessionSet && host && hostDelivery && self && delivery && mission &&
            generation && epoch && load && slot > 0 && slot < 3;
    }
};
struct Gates {
    bool scope = false, healthy = false, converged = false, nativeSafe = false,
         mission = false, pinned = false;
    bool Ready() const noexcept { return scope && healthy && converged && nativeSafe && mission && pinned; }
};
class State {
public:
    enum class Phase { Empty, Admitted, Starting, Prelude, Scene, Failed };
    void Reset() noexcept { *this = {}; }
    // Called for every accepted mission snapshot. An inactive latest snapshot
    // never erases an admitted occurrence in this same native mission/load.
    bool Observe(const Key& key, bool end16, std::uint64_t source) noexcept {
        if (!key.Valid()) { Reset(); return false; }
        if (phase_ != Phase::Empty && key != key_) Reset();
        if (!end16 || !source || source_) return false;
        // A qualified native315 may beat the host end snapshot. Its observed
        // scene owns deduplication, but never grants permission to call16.
        if (phase_ == Phase::Scene) { source_ = source; return true; }
        if (phase_ != Phase::Empty) return false;
        key_ = key; source_ = source; phase_ = Phase::Admitted;
        return true;
    }
    bool Matches(const Key& key) const noexcept { return key.Valid() && key == key_ && phase_ != Phase::Empty; }
    bool Seal(const Key& key) noexcept {
        if (!Matches(key) || sealed_) return false;
        sealed_ = true; return true;
    }
    bool Begin(const Key& key, const Gates& gates) noexcept {
        if (!Matches(key) || phase_ != Phase::Admitted || !sealed_ || !gates.Ready()) return false;
        phase_ = Phase::Starting; // before calling native code: reentrancy cannot start it twice
        return true;
    }
    void CompletePrelude(bool ok) noexcept {
        if (!ok) phase_ = Phase::Failed;
        else if (phase_ == Phase::Starting) phase_ = Phase::Prelude;
    }
    bool Owns315(const Key& key) const noexcept {
        return Matches(key) && (sealed_ || phase_ == Phase::Scene) && phase_ != Phase::Failed;
    }
    // A matching native315 can arrive before the queued consumer is serviced.
    // Adopt that first start; every later attempt in this occurrence is refused.
    bool Native315(const Key& key) noexcept {
        if (!Owns315(key) || started315_) return false;
        started315_ = true; phase_ = Phase::Scene; return true;
    }
    bool AdoptNative315(const Key& key) noexcept {
        if (!key.Valid() || phase_ != Phase::Empty) return false;
        key_ = key; phase_ = Phase::Scene; started315_ = true; return true;
    }
    void CompleteScene(bool ok) noexcept { if (!ok) phase_ = Phase::Failed; }
    Phase Current() const noexcept { return phase_; }
    const Key& Scope() const noexcept { return key_; }
    std::uint64_t Source() const noexcept { return source_; }
private:
    Key key_ {};
    std::uint64_t source_ = 0;
    Phase phase_ = Phase::Empty;
    bool sealed_ = false, started315_ = false;
};
} // namespace kh2coop::inject::thorndefeat
