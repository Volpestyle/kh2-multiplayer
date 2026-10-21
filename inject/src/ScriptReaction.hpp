#pragma once
// ScriptMirror's host-owned reaction windows. Native attack/effect handles stay
// in the adapter; only host-minted occurrence IDs and admitted seat IDs travel.
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

namespace kh2coop::inject::scriptmirror::rules {

struct OwnerCaptureSignature { std::uint64_t motion{},blend{}; };
inline bool OwnerRoleBeginAllowed(std::uint32_t resource,std::uint32_t active,std::uint32_t floor) noexcept {
    return resource && (resource==active || resource>floor);
}
// State refresh must not restart a still-installed source motion, or rewind a
// native owner command that consumed it. A new role/input or failed install retries.
inline bool CaptureSnapshotApplied(OwnerCaptureSignature previous,OwnerCaptureSignature incoming,
    bool havePrevious,bool nativeCurrent,bool commandConsumed,bool nativeOwnIdle=false) noexcept {
    return havePrevious && previous.motion==incoming.motion && previous.blend==incoming.blend &&
        (nativeCurrent || commandConsumed || nativeOwnIdle);
}

// A numeric motion ID does not identify its animation bank. Only the latest
// authored capture input proves a borrowed ID; native idle0 is an own-bank
// transition. Unknown post-capture IDs fail closed instead of guessing a bank.
enum class OwnerMotionBank : std::uint8_t { Refuse, Own, Source };
inline OwnerMotionBank OwnerMotion(std::uint32_t motion,bool haveCapture,std::uint32_t captureMotion,
                                  bool commandConsumed) noexcept {
    if(commandConsumed)return OwnerMotionBank::Refuse;
    if(haveCapture && motion==captureMotion)return OwnerMotionBank::Source;
    if(!haveCapture || motion==0)return OwnerMotionBank::Own;
    return OwnerMotionBank::Refuse;
}

// VUH-2610: the owner's motion heartbeat trails its capture record by a frame or
// so. When a capture record replaces the capture motion, a heartbeat still naming
// the replaced one is stale: the spectator's clone plays the newer capture and
// takes no time from it. The allowance ends at the first heartbeat that names
// anything else, so a later genuine return to that ID is judged as usual.
struct OwnerVisualHeartbeat {
    std::uint32_t replaced{UINT32_MAX};
    void Capture(bool hadCapture,std::uint32_t previous,std::uint32_t next) noexcept {
        if(hadCapture && previous!=next)replaced=previous;
    }
    void Heartbeat(std::uint32_t motion) noexcept { if(motion!=replaced)replaced=UINT32_MAX; }
    std::uint32_t Motion(bool haveMotion,std::uint32_t motion,bool haveCapture,std::uint32_t captureMotion) const noexcept {
        if(!haveMotion)return captureMotion;
        return haveCapture && motion==replaced && motion!=captureMotion?captureMotion:motion;
    }
    // The heartbeat's motion time applies only to the motion it names.
    static bool TimeApplies(bool haveMotion,std::uint32_t heartbeat,std::uint32_t played) noexcept {
        return haveMotion && heartbeat==played;
    }
};

// Wire descriptor semantics; both native helpers still receive native kind 1.
enum class OwnerCommandMode : std::uint32_t { General=1, CapturedMotion=2 };
inline bool ValidOwnerCommandMode(std::uint32_t mode) noexcept { return mode==1 || mode==2; }
inline bool OwnerCommandCaptureLive(std::uint32_t mode,bool captureCurrent) noexcept {
    return ValidOwnerCommandMode(mode) && (mode==static_cast<std::uint32_t>(OwnerCommandMode::General) || captureCurrent);
}
inline bool OwnerCommandContinues(std::uint32_t previousCommand,std::uint32_t previousMode,std::uint32_t previousFrame,
    std::uint32_t command,std::uint32_t mode,std::uint32_t frame) noexcept {
    return command && command==previousCommand && ValidOwnerCommandMode(mode) && mode==previousMode &&
        frame>=previousFrame && frame-previousFrame<=15;
}
enum class OwnerCommandEntry : std::uint8_t { General, Captured };
inline bool NativeCapturedCommandCaller(OwnerCommandEntry entry,std::uintptr_t callerRva) noexcept {
    return entry==OwnerCommandEntry::Captured ? callerRva==0x40DA4E || callerRva==0x40E581 :
        callerRva==0x40DA55 || callerRva==0x40E588;
}
inline bool OwnerCommandRootTarget(const std::array<std::uint32_t,5>& target) noexcept {
    // Native copies 20 bytes but initializes only this 16-byte logical record.
    return target[0] && !target[1] && !target[2] && target[3]==UINT32_MAX;
}
struct CapturedCommandRoleFacts {
    std::uint32_t sourceHandle{};
    bool current{},captured{},sourceLinked{},captureFlags{},livingFresh{};
};
inline int SelectCapturedCommandRole(std::uint32_t sourceHandle,
    const std::array<CapturedCommandRoleFacts,3>& roles) noexcept {
    if(!sourceHandle)return -1;
    int selected=-1;
    for(unsigned i=0;i<roles.size();++i) {
        const auto& role=roles[i];
        if(role.sourceHandle!=sourceHandle || !role.current || !role.captured ||
           !role.sourceLinked || !role.captureFlags || !role.livingFresh)continue;
        if(selected>=0)return -1;
        selected=static_cast<int>(i);
    }
    return selected;
}

// Selection only; the native adapter still validates scope, connection and live
// roots. A client's canonical actor belongs to its authenticated local seat;
// host candidate arrays are not a client-local liveness source.
struct ReactionActorCandidate { std::uintptr_t actor{};std::uint8_t slot{0xFF}; };
inline std::uintptr_t ReactionActorForSeat(std::uint8_t requested,std::uint8_t local,
    std::uintptr_t canonical,const std::array<ReactionActorCandidate,3>& hostCandidates={}) noexcept {
    if(requested>=3 || local>=3)return 0;
    if(requested==local)return canonical; // unused default slot0 must never replace Sora
    if(local!=0)return 0; // only the host resolves another seat from its candidate table
    for(const auto& candidate:hostCandidates)
        if(candidate.actor && candidate.slot==requested)return candidate.actor;
    return 0;
}

// A TARGET has no native allocation serial. A cache receipt owns the immutable
// resource/occurrence selected by one native refresh, never a later same-handle
// lookup. Native pointer/root/scope facts are read and verified by the adapter.
struct NativeReactionIdentity {
    std::array<std::uint32_t,4> target{}; // fifth native word is padding
    std::array<std::uint64_t,8> roots{}; // controller, player/source and their checked roots
    std::uint64_t occurrence{},incarnation{},connection{};
    std::uint32_t resource{},epoch{},load{},transition{},actorHandle{},objectId{},mode{};
    std::uint16_t source{},command{};
    std::uint8_t slot{0xFF},family{}; // 1 attack, 2 actor command
    bool operator==(const NativeReactionIdentity&) const noexcept = default;
};
inline bool NativeReactionIdentityValid(const NativeReactionIdentity& id) noexcept {
    if(!id.resource || !id.occurrence || !id.incarnation || !id.connection || !id.epoch ||
       !id.source || !id.command || id.slot>=3 || !id.actorHandle || !id.objectId ||
       !id.target[0] || id.target[1] || id.target[3]!=UINT32_MAX)return false;
    for(unsigned i=0;i<7;++i)if(!id.roots[i])return false;
    return id.family==1 ? id.target[2] && id.roots[7] && !id.mode :
        id.family==2 && !id.target[2] && ValidOwnerCommandMode(id.mode);
}
struct NativeReactionReceipt {
    NativeReactionIdentity identity{};
    std::uint64_t refresh{};
    std::uint32_t frame{};
    bool valid{};
};
class NativeReactionCache {
public:
    // Every refresh invalidates the previous publication, even an early return.
    // Nested refreshes poison the outer pass rather than restoring stale proof.
    bool Begin() noexcept {
        published_={};pending_={};
        if(active_){poisoned_=true;return false;}
        if(serial_==UINT64_MAX)return false;
        ++serial_;active_=true;poisoned_=false;return true;
    }
    void Winner(const NativeReactionIdentity* identity,std::uint32_t frame) noexcept {
        if(!active_)return;
        pending_={}; // unrelated/unknown last winner must replace an earlier owned one
        if(identity && NativeReactionIdentityValid(*identity))
            pending_={*identity,serial_,frame,true};
    }
    void Poison() noexcept { poisoned_=true;pending_={};published_={}; }
    void End(const NativeReactionIdentity* current,std::uint16_t command,
             const std::array<std::uint32_t,4>& target,std::uint32_t frame) noexcept {
        if(active_ && !poisoned_ && pending_.valid && current && pending_.identity==*current &&
           command==pending_.identity.command && target==pending_.identity.target && pending_.frame==frame)
            published_=pending_;
        else published_={};
        active_=false;pending_={};
    }
    void Reset() noexcept { published_={};pending_={};active_=false;poisoned_=false; }
    const NativeReactionReceipt& Published() const noexcept { return published_; }
    bool Current(const NativeReactionIdentity& current,std::uint32_t frame) const noexcept {
        return published_.valid && published_.identity==current && frame>=published_.frame &&
            frame-published_.frame<=1; // prompt may precede the next native controller update
    }
private:
    NativeReactionReceipt pending_{},published_{};
    std::uint64_t serial_{}; // never rewinds on room/session reset
    bool active_{},poisoned_{};
};

// Both pinned player-action paths pass the controller's inline active TARGET,
// including the tail-jump path whose return address belongs to its caller.
inline bool NativeReactionActive(const NativeReactionIdentity& accepted,std::uintptr_t targetAddress,
    std::uint16_t activeCommand,const std::array<std::uint32_t,4>& activeTarget) noexcept {
    return accepted.roots[0] && targetAddress==accepted.roots[0]+0x10 &&
        activeCommand==accepted.command && activeTarget==accepted.target;
}

// Native acceptance is not completion. Reserve one immutable selected identity
// before entering native code; only its later callback can take it once.
class NativeReactionAcceptance {
public:
    bool Reserve(const NativeReactionReceipt& selected,const NativeReactionIdentity& current) noexcept {
        if(valid_ || !selected.valid || !NativeReactionIdentityValid(current) || selected.identity!=current)return false;
        identity_=current;valid_=true;return true;
    }
    void Finish(bool accepted) noexcept { if(!accepted)Reset(); }
    bool Pending() const noexcept { return valid_; }
    bool Current(const NativeReactionIdentity& current) const noexcept { return valid_ && identity_==current; }
    bool Take(const NativeReactionIdentity& current) noexcept {
        if(!Current(current))return false;
        Reset();return true;
    }
    void Reset() noexcept { identity_={};valid_=false; }
private:
    NativeReactionIdentity identity_{};
    bool valid_{};
};
inline std::uint16_t NativeReactionCommand(std::uint16_t raw,bool knownMirror,bool admitted) noexcept {
    return knownMirror && !admitted?std::uint16_t{0}:raw;
}

struct ReactionPoint { float x{}, y{}, z{}; };
struct ReactionSeat {
    std::uint64_t connection{};
    ReactionPoint position{};
    bool living{}, fresh{}, capable{};
};
inline bool Finite(ReactionPoint p) noexcept {
    return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
}
// No hysteresis for a new RC trigger. Equal distance goes to the stable seat
// index, independent of packet arrival/claim order. Downed seats never win.
inline int ReactionWinner(ReactionPoint trigger, float range,
                          const std::array<ReactionSeat, 3>& seats) noexcept {
    if (!Finite(trigger) || !std::isfinite(range) || range < 0) return -1;
    int winner = -1;
    double nearest = static_cast<double>(range) * range;
    for (unsigned i = 0; i < seats.size(); ++i) {
        const auto& s = seats[i];
        if (!s.connection || !s.living || !s.fresh || !s.capable || !Finite(s.position)) continue;
        const double x = static_cast<double>(s.position.x) - trigger.x;
        const double y = static_cast<double>(s.position.y) - trigger.y;
        const double z = static_cast<double>(s.position.z) - trigger.z;
        const double distance = x*x + y*y + z*z;
        if (distance <= nearest && (winner < 0 || distance < nearest)) {
            winner = static_cast<int>(i);
            nearest = distance;
        }
    }
    return winner;
}

struct ReactionWindow {
    std::uint64_t occurrence{}, connection{};
    std::uint32_t epoch{}, openedFrame{}, duration{};
    std::uint16_t source{}, command{};
    std::uint8_t slot{0xFF};
    bool consumed{};
};
struct ReactionCompletion {
    std::uint64_t occurrence{}, connection{};
    std::uint32_t epoch{};
    std::uint16_t source{}, command{};
    std::uint8_t slot{0xFF};
};
struct ActorRoleFacts {
    std::uintptr_t source{},thread{};
    bool current{},captured{};
};
// Prefer the executing coroutine, then its immediate native parent. A root
// callback may use one explicit captured role; multiple candidates refuse.
inline int SelectActorRole(std::uintptr_t source,std::uintptr_t executing,std::uintptr_t parent,
                           const std::array<ActorRoleFacts,3>& roles) noexcept {
    if(!source)return -1;
    for(unsigned pass=0;pass<3;++pass) {
        int selected=-1;
        for(unsigned i=0;i<roles.size();++i) {
            const auto& role=roles[i];
            if(!role.current || role.source!=source)continue;
            const bool match=pass==0?executing && role.thread==executing:
                pass==1?parent && role.thread==parent:role.captured;
            if(!match)continue;
            if(selected>=0)return -1;
            selected=static_cast<int>(i);
        }
        if(selected>=0)return selected;
    }
    return -1;
}
inline bool ReactionLive(const ReactionWindow& w, std::uint32_t epoch,
                         std::uint32_t frame) noexcept {
    return w.occurrence && w.source && w.command && w.connection && w.slot < 3 &&
        w.epoch == epoch && !w.consumed && w.duration &&
        frame >= w.openedFrame && frame - w.openedFrame < w.duration;
}
inline bool RefreshReaction(ReactionWindow& w,std::uint32_t epoch,std::uint32_t frame,
                            const std::array<ReactionSeat,3>& seats) noexcept {
    if(!ReactionLive(w,epoch,frame))return false;
    const auto& seat=seats[w.slot];
    if(!seat.living || !seat.fresh || !seat.capable || seat.connection!=w.connection)w.consumed=true;
    return !w.consumed;
}
// Reserve before the native callback: a fault after entry has unknown effects
// and must not permit a retry. A roster replacement or downing revokes the role.
inline bool ConsumeReaction(ReactionWindow& w, const ReactionCompletion& c,
                            std::uint32_t epoch, std::uint32_t frame,
                            const std::array<ReactionSeat, 3>& seats) noexcept {
    if (!ReactionLive(w, epoch, frame) || c.epoch != w.epoch || c.occurrence != w.occurrence ||
        c.source != w.source || c.command != w.command || c.slot != w.slot ||
        c.connection != w.connection) return false;
    const auto& s = seats[w.slot];
    if (!s.living || !s.fresh || !s.capable || s.connection != w.connection) {
        w.consumed=true; // observed revocation is permanent for this occurrence
        return false;
    }
    w.consumed = true;
    return true;
}

// Shared by attack-created and command-override windows. The caller retires a
// native attack at attack_free and on room/load/session changes. Exhaustion is
// refusal, never reuse of an occurrence that an old completion could name.
class ReactionOccurrences {
public:
    static constexpr std::size_t Capacity = 256;
    struct Entry { std::uint64_t native{}, incarnation{}; ReactionWindow window{}; };
    ReactionWindow* Open(std::uint64_t native, std::uint64_t incarnation,
                         std::uint16_t source, std::uint16_t command,
                         std::uint32_t epoch, std::uint32_t frame, std::uint32_t duration,
                         ReactionPoint trigger, float range,
                         const std::array<ReactionSeat, 3>& seats) noexcept {
        if (!native || !incarnation || !source || !command || !epoch || !duration) return nullptr;
        for (auto& e : entries_) {
            if (e.native != native || e.incarnation != incarnation) continue;
            // Repeated per-frame offers are the same trigger, even once consumed.
            return e.window.epoch == epoch && e.window.command == command && e.window.source == source
                ? &e.window : nullptr;
        }
        const int winner = ReactionWinner(trigger, range, seats);
        if (winner < 0 || next_ == std::numeric_limits<std::uint64_t>::max()) return nullptr;
        for (auto& e : entries_) {
            if (e.native) continue;
            e.native = native; e.incarnation = incarnation;
            e.window = {++next_, seats[static_cast<unsigned>(winner)].connection, epoch,
                        frame, duration, source, command, static_cast<std::uint8_t>(winner), false};
            return &e.window;
        }
        return nullptr;
    }
    ReactionWindow* Find(std::uint64_t occurrence) noexcept {
        for (auto& e : entries_) if (e.native && e.window.occurrence == occurrence) return &e.window;
        return nullptr;
    }
    void Retire(std::uint64_t native, std::uint64_t incarnation) noexcept {
        for (auto& e : entries_) if (e.native == native && e.incarnation == incarnation) e = {};
    }
    void Reset() noexcept { entries_ = {}; } // sequence never rewinds across resets
private:
    std::array<Entry, Capacity> entries_{};
    std::uint64_t next_{};
};

} // namespace kh2coop::inject::scriptmirror::rules
