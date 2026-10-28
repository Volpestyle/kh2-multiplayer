#pragma once
// ScriptMirror's host-owned reaction windows. Native attack/effect handles stay
// in the adapter; only host-minted occurrence IDs and admitted seat IDs travel.
#include "kh2coop/EnemyClass.hpp"
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
    std::uint8_t slot{0xFF},family{}; // 1 attack, 2 actor command, 3 Axel II window (AxelReaction.hpp)
    bool operator==(const NativeReactionIdentity&) const noexcept = default;
};
inline bool NativeReactionIdentityValid(const NativeReactionIdentity& id) noexcept {
    if(!id.resource || !id.occurrence || !id.incarnation || !id.connection || !id.epoch ||
       !id.source || !id.command || id.slot>=3 || !id.actorHandle || !id.objectId ||
       !id.target[0] || id.target[1] || id.target[3]!=UINT32_MAX)return false;
    for(unsigned i=0;i<7;++i)if(!id.roots[i])return false;
    return id.family==1 || id.family==3 ? id.target[2] && id.roots[7] && !id.mode :
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
// VUH-2620: whose native command waits for a host occurrence. An actor the host routes reactions for
// (a reaction binding, a script-owner role or its cleanup) always does; a mirrored copy only when its
// class's reactions are host-routed: Twilight Thorn's script, and Axel II's collision-offered commands
// (VUH-2913, AxelReaction.hpp). Any other mirrored enemy's command (a Dusk's Reversal 69, command 32)
// belongs to the seat that sees it and runs natively there, as it did before the Thorn routing.
inline bool HostRoutedReactionClass(std::uint32_t objectId) noexcept {
    return objectId==kh2coop::enemyclass::TwilightThorn || objectId==kh2coop::enemyclass::AxelII;
}
inline bool KnownReactionSource(bool hostRouted,bool mirroredCopy,std::uint32_t objectId) noexcept {
    return hostRouted || (mirroredCopy && HostRoutedReactionClass(objectId));
}
// Whether a client keeps a mirrored receiver's reaction callback (0x3B5FA0, its BDX key 11) to itself:
// unpaired, it is refused, and its warmup or held state never reopens it. "Mirrored" includes
// host-owned field and puzzle actors as well as enemies, so it stays refused for every class except
// one qualified to run natively: the Dusk (objectId 318), whose Reversal shout is what starts the
// player's own Reversal, as before 5dd231f. A receiver whose class cannot be read stays refused.
inline constexpr std::uint32_t kNativeCallbackDusk=318;
inline bool NativeCallbackClass(std::uint32_t objectId) noexcept { return objectId==kNativeCallbackDusk; }
inline bool ClientOwnedCallbackReceiver(bool client,bool mirroredCopy,bool objectIdRead,std::uint32_t objectId) noexcept {
    return client && mirroredCopy && !(objectIdRead && NativeCallbackClass(objectId));
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
// Native update 3B2340 clears the script command slots (3B20B0) after every
// reaction refresh, so a prompt exists only on updates that re-registered it.
// A client's own native per-frame registration may pass only through the
// captured entry (its own player's captured motion; another actor's motion on
// the general entry stays refused) and only for the window the host offered
// to that seat: one role names the source, the role and its
// window are this seat's on its current connection, the window is live in this
// epoch and unconsumed, the command matches, and the seat is not logically
// down. Ownership, liveness and minting a window stay with the host (VUH-2617).
struct ClientReactionRegistration {
    std::uint32_t sourceHandle{};
    std::uint8_t roleSlot{0xFF};
    bool current{},captureCurrent{};
    ReactionWindow reaction{};
};
inline int SelectClientReactionRegistration(OwnerCommandEntry entry,std::uint32_t sourceHandle,std::uint16_t command,std::uint8_t localSlot,
    std::uint64_t connection,std::uint32_t epoch,std::uint32_t frame,bool locallyDowned,
    const std::array<ClientReactionRegistration,3>& roles) noexcept {
    if(entry!=OwnerCommandEntry::Captured || !sourceHandle || !command || localSlot==0 || localSlot>=3 ||
       !connection || locallyDowned)return -1;
    int selected=-1;
    for(unsigned i=0;i<roles.size();++i) {
        if(roles[i].sourceHandle!=sourceHandle)continue;
        if(selected>=0)return -1; // two roles naming one source leave ownership ambiguous
        selected=static_cast<int>(i);
    }
    if(selected<0)return -1;
    const auto& role=roles[static_cast<unsigned>(selected)];
    const auto& w=role.reaction;
    return role.current && role.captureCurrent && role.roleSlot==localSlot && w.slot==localSlot &&
        w.connection==connection && w.command==command && ReactionLive(w,epoch,frame) ? selected : -1;
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

// The owner-release cleanup's existing decision (SmOwnerCleanupPlayer), unchanged, so a release can say which
// gate it stopped at (VUH-2617: the friend left held after bots05 attempt02's seat-1 release; which gate held is
// what the read-only release line records). Cleaned is the only outcome that runs the native detach/idle.
struct ReleaseCleanupFacts {
    bool current{},field{},local{},clone{};
    bool threadRead{},threadZero{};
    bool threadOwned{};   // +D48 is the released role's coroutine, run by the released source
    bool captureOwned{};  // +2B0 is the released source
    bool attachment{};    // the released role's qualified attachment
    bool parentRead{},parentZero{};
};
enum class ReleaseCleanup : std::uint8_t { NotCurrent, NotField, NotLocalOrClone, Guards, ReplacementParent, Cleaned };
inline ReleaseCleanup DecideReleaseCleanup(const ReleaseCleanupFacts& f) noexcept {
    if(!f.current)return ReleaseCleanup::NotCurrent;
    if(!f.field)return ReleaseCleanup::NotField;
    if(!f.local && !f.clone)return ReleaseCleanup::NotLocalOrClone;
    const bool cleanup=f.threadRead && (f.threadOwned || (f.threadZero && (f.captureOwned || f.attachment)));
    if(!cleanup)return ReleaseCleanup::Guards;
    // A replacement's attachment/state is not ours to detach or idle.
    if(!f.parentRead || !(f.parentZero || f.attachment))return ReleaseCleanup::ReplacementParent;
    return ReleaseCleanup::Cleaned;
}
// The release line's read-only snapshot of the friend's own actor. Every read goes through `read(address, out, size)`
// and only for a root proven by the caller: none at all for a refused actor; the source's bank (+0x158 of the
// source) only for a source proven on its own (driven binding and list handle), never inferred from the actor.
// `decode` resolves the +0xC action handle; its object is read only when decode returns it.
struct LocalHold {
    std::uintptr_t thread{},capture{},bank{},sourceBank{},anchor{},actionVtable{};
    std::uint32_t flags6C8{},flags18C{},parent{},motion{0xFFFFFFFFu},actionHandle{};
    bool actorRead{},sourceRead{};
};
template<class Read,class Decode>
LocalHold ReadLocalHold(bool actorProven,std::uintptr_t actor,bool sourceProven,std::uintptr_t source,
                        Read&& read,Decode&& decode) {
    LocalHold h{};
    if(!actorProven || !actor)return h;  // a refused root: metadata only
    h.actorRead=read(actor+0xD48,&h.thread,sizeof(h.thread)) && read(actor+0x2B0,&h.capture,sizeof(h.capture)) &&
        read(actor+0x2B8,&h.bank,sizeof(h.bank)) && read(actor+0x6C8,&h.flags6C8,sizeof(h.flags6C8)) &&
        read(actor+0x18C,&h.flags18C,sizeof(h.flags18C)) && read(actor+0x6A0,&h.parent,sizeof(h.parent)) &&
        read(actor+0x2A0,&h.anchor,sizeof(h.anchor)) && read(actor+0x180,&h.motion,sizeof(h.motion));
    if(h.actorRead && read(actor+0xC,&h.actionHandle,sizeof(h.actionHandle)) && h.actionHandle) {
        if(const std::uintptr_t action=decode(h.actionHandle))(void)read(action,&h.actionVtable,sizeof(h.actionVtable));
    }
    h.sourceRead=sourceProven && source && read(source+0x158,&h.sourceBank,sizeof(h.sourceBank));
    return h;
}
// Which roots the release line may read (Quill r2): none after the native coroutine cancel (it may free or replace
// either root) or when the release refused its root; otherwise the exact admitted actor through the entity list's
// handle (`listed(pointer)` walks the list and compares pointers, reading neither root), then the source the same
// way, and only then the mirror's driven entry for the source (`driven()`: the driven table, no reads).
struct LocalHoldRoots { bool actor{},source{}; };
// The list handle that proves a root, for `listed` above. The entity list is walked through each node's link to the
// next (`walk`), so its head has no incoming link to match: the canonical player usually is the head (bots05
// attempt03: every friend release line had actorRead=0). At the head, the proof is the head global equal to the root
// and the admitted handle still resolving to that same pointer through the handle table (`resolve`); neither reads
// the root. Elsewhere the walk decides, as before.
template<class Head,class Resolve,class Walk>
std::uint32_t ListedHandle(std::uintptr_t at,std::uint32_t admitted,Head&& head,Resolve&& resolve,Walk&& walk) {
    if(at && head()==at)return admitted && resolve(admitted)==at?admitted:0u;
    return walk(at);
}
template<class Listed,class Driven>
LocalHoldRoots ProveLocalHoldRoots(bool decisionCurrent,bool cancelled,std::uintptr_t actor,std::uint32_t actorHandle,
                                   std::uintptr_t source,std::uint32_t sourceHandle,Listed&& listed,Driven&& driven) {
    LocalHoldRoots roots{};
    if(!decisionCurrent || cancelled)return roots;
    if(!actor || !actorHandle || listed(actor)!=actorHandle)return roots;
    roots.actor=true;
    roots.source=source && sourceHandle && listed(source)==sourceHandle && driven()==source;
    return roots;
}
// VUH-2617: a friend local-seat release that stops at `guards` leaves the friend's own actor held by nothing anyone
// releases. bots05 attempt04 (frame 17301): own-bank 263 left in ACTION_IDLE; attempt06 (run 20261022-133757, frame
// 18186): the role had installed 263, the actor was in its own motion 10, and it stayed there ~100 s while its pilot
// held direction keys. Both lines read capture=0 bank=0 parent=0 d48=0. A cleaned release runs the native pair detach
// (0x42C5F0 -> 0x3DA250, which acts only with a +6A0 parent) and idle (0x42D0E0 -> 0x3B4DA0(actor, 0)); a guards
// release runs neither. The release now runs that same pair once when this role holds a positive receipt that it held
// this actor (an own-bank install read back by OwnInstallQualified, or a borrowed capture it installed), the actor is
// held by nothing else, and the role's admitted session is current. It replaces the earlier own-motion reset.
struct HeldReleaseFacts {
    bool rootsRefused{};  // a held rebuild whose saved roots failed their list/handle proof (RebuildHeld)
    bool friendLocal{},releaseStage{},sessionCurrent{},field{},actorProven{},actorRead{},roleFree{},heldReceipt{};
    bool sourceProven{};
    ReleaseCleanup outcome{ReleaseCleanup::NotCurrent};
    std::uint32_t parent{};
    std::uintptr_t thread{},capture{},bank{},source{},sourceBank{};
};
inline bool DecideHeldRelease(const HeldReleaseFacts& f,const char*& why) noexcept {
    if(f.rootsRefused){why="roots-refused";return false;}
    if(!f.friendLocal){why="not-friend-local";return false;}
    if(f.outcome!=ReleaseCleanup::Guards){why="not-guards";return false;}  // cleaned ran it; others are not ours
    if(!f.releaseStage){why="not-release-stage";return false;}
    if(!f.heldReceipt){why="no-receipt";return false;}
    if(!f.sessionCurrent){why="session";return false;}
    if(!f.field){why="not-field";return false;}
    if(!f.actorProven || !f.actorRead){why="unproven";return false;}
    if(!f.roleFree){why="role-current";return false;}
    if(f.thread || f.parent){why="held";return false;}
    if(f.capture && f.capture!=f.source){why="foreign-capture";return false;}
    if(f.bank && (!f.sourceProven || f.bank!=f.sourceBank)){why="foreign-bank";return false;}
    why="held-guards";return true;
}
// Quill r1 on 18b15fa: idle (a gameplay mutation) follows detach only when detach itself succeeded and a fresh decision
// from facts rebuilt after detach (session, epoch, connection, admitted scope, canonical friend, list-proven root,
// field, a fresh hold read, the seat's role and cleanup slots) still admits it. Nothing decided before detach is reused.
inline bool HeldReleaseIdleAllowed(bool detached,const HeldReleaseFacts& fresh,const char*& why) noexcept {
    if(!detached){why="detach-failed";return false;}
    return DecideHeldRelease(fresh,why);
}
// The pair in order: detach; on success only, rebuild every fact (the only reads between the calls); idle only on that
// fresh decision; then, only if idle returned, rebuild every fact again and stand only on that second decision.
// Quill r1 on 62ce05f: idle (0x42D0E0 -> 0x3B4DA0) runs the old action's virtual exit before it enters action 0, and
// the guards admission does not constrain that old action, so nothing decided before idle authorizes the stand.
// Nothing reads the actor after the stand.
// bots05 attempt07 (run 20261022-150823, friend frame 15074): the pair ran (detach=1 idle=1) on a guards release whose
// actor stood in its own motion 262 (+18C 0x4002) and the friend stayed in 262 at one spot ~1170 frames while its pilot
// drove, until a Thorn hit set a hit-reaction motion; it then moved normally. Idle switches the action only (ACTION_IDLE
// enter/exit are empty), so the motion that holds the actor stays. The held release now also sets the actor's own
// stand motion (kOwnIdleMotion, 0, the motion the freed friend stood in) through the ordinary own-bank motion set, in
// its own fresh admission after idle. A cleaned release never comes here.
// Quill r2 on 96d77cc: the held rebuild (after detach and after idle) first proves both saved roots listed under their
// saved handles (listed: ListedHandle, through the head global, the handle resolver and the list walk, none of which
// reads either root). Only then does follow run: SmOwnerReleaseRootCurrent reads each root's +4, then the field, the
// hold read (its own ProveLocalHoldRoots) and the scope facts, as before. A failed proof refuses with no root read.
template<class Listed,class Follow>
HeldReleaseFacts RebuildHeld(std::uintptr_t actor,std::uint32_t actorHandle,std::uintptr_t source,
                             std::uint32_t sourceHandle,Listed&& listed,Follow&& follow) {
    if(!actor || !actorHandle || listed(actor)!=actorHandle || !source || !sourceHandle || listed(source)!=sourceHandle)
        {HeldReleaseFacts refused{};refused.rootsRefused=true;return refused;}
    return follow();
}
constexpr std::uint32_t kOwnIdleMotion=0;  // MotionSet IDLE
struct HeldPairResult {
    bool detached{},idled{},stood{};
    const char* idleWhy{"-"};
    const char* standWhy{"-"};
};
template<class Detach,class Rebuild,class Idle,class Stand>
HeldPairResult RunHeldPair(bool release,Detach&& detach,Rebuild&& rebuild,Idle&& idle,Stand&& stand) {
    HeldPairResult out{};
    if(!release)return out;
    out.detached=detach();
    HeldReleaseFacts fresh{};
    if(out.detached)fresh=rebuild();
    if(!HeldReleaseIdleAllowed(out.detached,fresh,out.idleWhy))return out;
    out.idled=idle();
    if(!out.idled){out.standWhy="idle-failed";return out;}
    const HeldReleaseFacts after=rebuild();
    if(!DecideHeldRelease(after,out.standWhy))return out;
    out.stood=stand(kOwnIdleMotion);
    return out;
}
// Quill r1 on 06ff413: the receipt is minted from a readback of the install just made. The general own-bank
// classifier (SmOwnerMotionState) also requires +18C without 0x4000, but +18C carries the current motion's own flags
// (no constant writer; 252/261 read 0x6040xx, 263 reads 0x4000), so an own-bank 263 reads Foreign there and the
// receipt was never minted. This qualification is for the local actor's own install only: the controller is the
// actor's own, the anchor its own, no +2B0 source and no +2B8 bank, its own bank present, no parent but its own
// attachment, and the current motion exactly the one just installed. +18C is not consulted. Visual clones and
// borrowed captures keep the general classifier.
struct OwnInstallFacts {
    bool rootsRead{},controllerSelf{},anchorOwn{},ownBankPresent{},parentOk{};
    std::uintptr_t capture{},bank{};
    std::uint32_t installed{0xFFFFFFFFu},motionNow{0xFFFFFFFFu};
};
inline bool OwnInstallQualified(const OwnInstallFacts& f) noexcept {
    return f.rootsRead && f.controllerSelf && f.anchorOwn && f.ownBankPresent && f.parentOk && !f.capture && !f.bank &&
        f.installed!=0xFFFFFFFFu && f.motionNow==f.installed;
}
// What the native input path consults for a player actor (docs/CLONE_NEUTRAL_INPUT.md, docs/NATIVE_GUEST_MAGIC.md,
// DownedSpike): its pad pointer +0xDB8 (processed entry 0), the shared FIELD_COMMAND at +0xDC0/+0xDD0 and its
// +0x24 bit 0 (controller off), the action +0xC with its time +0xBA8, the actor flags +0x9B8, the actor update flags
// +0x120 and the motion id/time. Read only from a proven actor root, through one callback, like ReadLocalHold.
// `fieldCommand` is the FIELD_COMMAND address (exe+0x2A10620); +0x24 is read only through that exact pointer.
struct InputGate {
    std::uintptr_t pad{},command{},commandAux{},actionVtable{};
    std::uint32_t command24{},flags9B8{},flags120{},motion{0xFFFFFFFFu},actionHandle{};
    float actionTime{},motionTime{};
    bool read{},commandRead{};
};
template<class Read,class Decode>
InputGate ReadInputGate(bool actorProven,std::uintptr_t actor,std::uintptr_t fieldCommand,Read&& read,Decode&& decode) {
    InputGate g{};
    if(!actorProven || !actor)return g;  // a refused root: metadata only
    g.read=read(actor+0xDB8,&g.pad,sizeof(g.pad)) && read(actor+0xDC0,&g.command,sizeof(g.command)) &&
        read(actor+0xDD0,&g.commandAux,sizeof(g.commandAux)) && read(actor+0x9B8,&g.flags9B8,sizeof(g.flags9B8)) &&
        read(actor+0x120,&g.flags120,sizeof(g.flags120)) && read(actor+0x180,&g.motion,sizeof(g.motion)) &&
        read(actor+0x19C,&g.motionTime,sizeof(g.motionTime)) && read(actor+0xBA8,&g.actionTime,sizeof(g.actionTime)) &&
        read(actor+0xC,&g.actionHandle,sizeof(g.actionHandle));
    if(g.read && g.actionHandle) {
        if(const std::uintptr_t action=decode(g.actionHandle))(void)read(action,&g.actionVtable,sizeof(g.actionVtable));
    }
    // Only the known shared FIELD_COMMAND is followed, never another pointer found in the actor.
    g.commandRead=g.read && g.command && g.command==fieldCommand && read(g.command+0x24,&g.command24,sizeof(g.command24));
    return g;
}
inline const char* ReleaseCleanupName(ReleaseCleanup c) noexcept {
    switch(c) {
    case ReleaseCleanup::NotCurrent: return "not-current";
    case ReleaseCleanup::NotField: return "not-field";
    case ReleaseCleanup::NotLocalOrClone: return "not-local-or-clone";
    case ReleaseCleanup::Guards: return "guards";
    case ReleaseCleanup::ReplacementParent: return "replacement-parent";
    case ReleaseCleanup::Cleaned: return "cleaned";
    }
    return "?";
}

} // namespace kh2coop::inject::scriptmirror::rules
