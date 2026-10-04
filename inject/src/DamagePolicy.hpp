#pragma once
#include <cstdint>

namespace kh2coop::inject::damagepolicy {
// Production policy, independent of opt-in diagnostics. Classifications are
// valid only for the checked current context and actor metadata supplied by
// EntityHook. NonLocalPlayer does NOT authenticate a remote incarnation.
enum class Role : std::uint8_t { Off, Host, Client };
enum class ActorClass : std::uint8_t {
    Unknown, LocalAvatar, RemoteRepresentation, NonLocalPlayer,
    NativeCompanion, Enemy, OtherNative
};
enum class Action : std::uint8_t { Native, ZeroHp, ClaimThenZeroHp };
enum class Reason : std::uint8_t {
    Off, ForeignThread, ContextUnavailable, HitUnavailable, AlreadyApplied,
    NonHp, ZeroAmount, RemoteVictim, RemoteSource, UnknownSource, LocalVictim,
    HostLocalAttack, HostCompanionAttack, ClientClaim, OtherActiveHit
};
constexpr std::uint32_t FlagsAvailable = 1, StatAvailable = 2,
    AmountAvailable = 4, KindAvailable = 8, RequiredHitReads = 7;
struct Hit {
    std::uint32_t flags = 0, readMask = 0;
    std::int32_t amount = 0;
    std::uint8_t stat = 0, kind = 0;
};
struct Facts {
    Role role = Role::Off;
    bool ownerThread = false, contextAvailable = false;
    ActorClass victim = ActorClass::Unknown, source = ActorClass::Unknown;
    Hit hit {};
};
struct Decision {
    Action action = Action::Native;
    Reason reason = Reason::ContextUnavailable;
    // True only for the active, readable, unapplied, nonzero HP boundary.
    // False preserves the existing native/manual/client-sync path.
    bool supported = false;
};
// Pure copied-facts decision. No memory reads/writes, callbacks, network,
// allocation or replay. All actions still require one genuine original call.
Decision Evaluate(const Facts& facts) noexcept;
enum class ZeroResult : std::uint8_t { Zeroed, InvalidExpected, Changed, AlreadyApplied, Fault };
// SEH leaf used by the production adapter and owned-memory tests. Revalidates
// exact flags/stat/amount against the copied decision input, then changes only
// hit+0x28. No ownership inference, native helper or original callback here.
ZeroResult TryZeroHp(uintptr_t hit, const Hit& expected) noexcept;
}
