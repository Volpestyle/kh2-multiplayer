#pragma once
#include <cstdint>
#include <atomic>
namespace kh2coop::routespawn { struct MissingPartySite; }
namespace kh2coop::inject::privatestatus {
using LogFn = void (*)(const char*, ...);

// Always-on existing factory-hook notifications, not creation authority. All
// notices borrow their POD only until the callback returns. A foreign callback
// must use atomics only; it must never read actors or mutate owner containers.
enum class FactoryNativeOutcome : std::uint8_t { Returned, Unwound, NotInvoked };
enum class FactoryRoute : std::uint8_t { Scoped, ScopeFallback, MissingPartyRefusal };
struct FactoryReturnNotice {
    std::uint64_t serial{};
    uintptr_t actor{},caller{};
    std::uint32_t requestedObjectId{},entryThread{},returnThread{},registeredOwnerThread{};
    FactoryNativeOutcome outcome{};
    FactoryRoute route{};
    bool ownerAtEntry{},ownerAtExit{},ownerKnown{},nativeInvoked{},scopePushed{},notificationComplete{};
};
using FactoryReturnObserver=void(*)(const FactoryReturnNotice&) noexcept;
struct FactoryReturnCoverage {
    std::uint64_t serial{};
    std::uint32_t inFlight{};
    bool registered{},installed{},poisoned{};
};
// Register once on the established game owner, before any owned child receipt.
// Callback must be a no-yield, fixed-POD/atomic noexcept leaf: no native game
// calls, census, allocation, locks, logging, networking or factory reentry.
// Observer and this module remain retained; no unregister/rearm/reset exists.
bool RegisterFactoryReturnObserver(FactoryReturnObserver observer) noexcept;
FactoryReturnCoverage ReadFactoryReturnCoverage() noexcept;

namespace detail {
// Stable pure boundary shared by the native adapter and focused tests. This
// negative observation fence is neither creator exclusion nor an actor ledger.
class FactoryReturnCounters {
public:
    static constexpr std::uint64_t CountMask=0xFFFF,ArmedBit=1ull<<62,PoisonBit=1ull<<63;
    static constexpr unsigned AttemptCap=64;
    static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
    explicit FactoryReturnCounters(std::uint64_t initialSerial=0) noexcept:serial_(initialSerial){}
    bool Arm() noexcept {
        std::uint64_t expected=0;
        return activity_.compare_exchange_strong(expected,ArmedBit);
    }
    bool Begin(bool registeredOwner,bool callbackReentry) noexcept {
        auto old=activity_.load();
        for(unsigned i=0;i<AttemptCap;++i) {
            if((old&CountMask)==CountMask){Poison();return false;}
            auto next=old+1;
            if((old&ArmedBit) && (!registeredOwner || callbackReentry))next|=PoisonBit;
            if(activity_.compare_exchange_weak(old,next))return true;
        }
        Poison();return false;
    }
    void End(bool counted) noexcept {
        if(!counted)return;
        auto old=activity_.load();
        for(unsigned i=0;i<AttemptCap;++i) {
            if(!(old&CountMask)){Poison();return;}
            if(activity_.compare_exchange_weak(old,old-1))return;
        }
        // Lost accounting cannot become a future clean/available observation.
        Poison();
    }
    std::uint64_t Next() noexcept {
        auto old=serial_.load();
        for(unsigned i=0;i<AttemptCap;++i) {
            if(old==UINT64_MAX){Poison();return 0;}
            if(serial_.compare_exchange_weak(old,old+1))return old+1;
        }
        Poison();return 0;
    }
    void Poison() noexcept {activity_.fetch_or(PoisonBit);}
    void PoisonIfArmed() noexcept {
        if(activity_.load()&ArmedBit)Poison();
    }
    FactoryReturnCoverage Snapshot() const noexcept {
        const auto word=activity_.load();
        return {serial_.load(),static_cast<std::uint32_t>(word&CountMask),
            (word&ArmedBit)!=0,false,(word&PoisonBit)!=0};
    }
private:
    std::atomic<std::uint64_t> serial_{};
    std::atomic<std::uint64_t> activity_{};
};
} // namespace detail

// Exact Steam 9002b2de private status. Explicit legacy experiment, or automatic
// two-player route dependency under shipped TARGET_ALL + MIRROR. Bailey uses an
// explicit retained-record scope; it never claims or writes a party leaf.
// VUH-1515 (KH2COOP_ENEMY_TARGET_REMOTE): scope also verified rooms (BC 05/06) with their exact leaf row 00/00/02/12.
void EnableEnemyTargetScope();
bool Initialize(std::uintptr_t base);
void Drain(LogFn log);
void StopNewAllocations(); // Existing ownership/veto remains until native release.
bool RetainsMinHookResources();
// VUH-1519: true while every hook is installed and no fault has disarmed selection.
// PartyNative refuses to spawn clones otherwise (they would share Sora's SAVE-bound record).
bool Ready();
using NativeWrapper = void* (__fastcall*)(const void*,void*);
// Caller qualified exact MissingPartySite ingress (Bailey; VUH-2464 prologue sites). Normal paths call
// nativeRecord once and return its result; an added direct construction of `cloneObject` (Sora 84, or the
// prologue remote's qualified profile) uses retained immutable storage and explicit FLS selection.
void* SpawnMissingParty(const void* nativeRecord,void* controller,NativeWrapper spawn,LogFn log,
                        const routespawn::MissingPartySite& site,std::uint16_t cloneObject,
                        bool* bound=nullptr,std::uintptr_t* clone=nullptr);
// Only actors created by that scope are filtered. Unbound/stale/disconnected actors stay quarantined.
bool MissingPartyPuppetAllowed(std::uintptr_t actor);
// VUH-2464: a prologue site admitted a non-Sora clone this process and NOW is in TT: qualified kit
// descriptors (Roxas) count as clones for neutral input and private status there.
bool MissingPartyKitScope();
// VUH-2911: an encounter site (Axel II) admitted its encounter body and NOW is still exactly that site:
// its encounter-only descriptor (dual-wield Roxas 0x323) counts like a qualified kit there, nowhere else.
bool MissingPartyEncounterScope();
// VUH-2464: `actor` is a bound missing-party clone that is still current (MissingPartyPuppetAllowed
// for a known entry). Unknown actors are false here, unlike MissingPartyPuppetAllowed.
bool MissingPartyClone(std::uintptr_t actor);
// VUH-2482: shows the owner's streamed HP on a bound missing-party clone by writing its own private
// status record only (game thread, hp 1..maxHp). 1 written, 0 already equal, negative refused.
int MirrorMissingPartyHp(std::uintptr_t actor, std::int32_t hp, std::int32_t maxHp);

}
