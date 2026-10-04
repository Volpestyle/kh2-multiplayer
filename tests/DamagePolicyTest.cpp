// Offline controls for the actual DamagePolicy core and checked zero leaf.
// Link inject/src/DamagePolicy.cpp; no KH2 process, native game memory or log
// fixture is used. Owned VirtualAlloc pages exercise actual read/write faults.
// The synthetic original below models consumption and preserves an opaque ABI
// result. It does NOT execute EntityHook's detour, authenticate puppet membership,
// prove generation/connection-width checks, or establish live damage acceptance.
// Facts deliberately contain only copied classifications/context availability;
// membership capture, session revalidation and hook ordering need their separate
// production-adapter review. Root owns compilation, W4/WX and sanitizer receipts.
#include "DamagePolicy.hpp"
#include <Windows.h>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <limits>

namespace dp = kh2coop::inject::damagepolicy;
namespace {
unsigned g_checks = 0;
unsigned g_failed = 0;

void Check(bool condition, const char* label) {
    ++g_checks;
    if (!condition) {
        ++g_failed;
        std::printf("FAIL %s\n", label);
    }
}

dp::Facts Active(dp::Role role = dp::Role::Client) {
    dp::Facts f {};
    f.role = role;
    f.ownerThread = true;
    f.contextAvailable = true;
    f.victim = dp::ActorClass::LocalAvatar;
    f.source = dp::ActorClass::Enemy;
    f.hit.readMask = dp::RequiredHitReads | dp::KindAvailable;
    f.hit.amount = 8;
    f.hit.kind = 1;
    return f;
}

void Expect(const dp::Facts& f, dp::Action action, dp::Reason reason,
            bool supported, const char* label) {
    const auto d = dp::Evaluate(f);
    Check(d.action == action && d.reason == reason && d.supported == supported, label);
}

void Matrix() {
    using A = dp::Action;
    using C = dp::ActorClass;
    constexpr std::array<C, 7> kinds {C::Unknown, C::LocalAvatar,
        C::RemoteRepresentation, C::NonLocalPlayer, C::NativeCompanion,
        C::Enemy, C::OtherNative};
    constexpr A z = A::ZeroHp, n = A::Native, c = A::ClaimThenZeroHp;
    // Independent specification tables: rows victim, columns source.
    constexpr A host[7][7] = {
        {z,z,z,z,z,z,z}, {z,n,z,z,n,n,n}, {z,z,z,z,z,z,z},
        {z,z,z,z,z,z,z}, {z,z,z,z,z,z,z}, {z,n,z,z,n,z,z}, {z,z,z,z,z,z,z}};
    constexpr A client[7][7] = {
        {z,z,z,z,z,z,z}, {z,n,z,z,n,n,n}, {z,z,z,z,z,z,z},
        {z,z,z,z,z,z,z}, {z,z,z,z,z,z,z}, {z,c,z,z,z,z,z}, {z,z,z,z,z,z,z}};
    for (const auto role : {dp::Role::Host, dp::Role::Client}) {
        for (std::size_t v = 0; v < kinds.size(); ++v) {
            for (std::size_t s = 0; s < kinds.size(); ++s) {
                auto f = Active(role);
                f.victim = kinds[v]; f.source = kinds[s];
                const auto decision = dp::Evaluate(f);
                char label[100] {};
                std::snprintf(label, sizeof(label), "matrix role%u victim%zu source%zu",
                    static_cast<unsigned>(role), v, s);
                Check(decision.supported && decision.action ==
                    (role == dp::Role::Host ? host[v][s] : client[v][s]), label);
            }
        }
    }
    auto f = Active();
    f.source = C::RemoteRepresentation;
    Expect(f, z, dp::Reason::RemoteSource, true, "remote source veto precedes canonical local victim");
    f.victim = C::RemoteRepresentation; f.source = C::Unknown;
    Expect(f, z, dp::Reason::RemoteVictim, true, "remote victim veto precedes unknown source");
    f = Active(); f.source = C::Unknown;
    Expect(f, z, dp::Reason::UnknownSource, true, "unknown source is not native NPC");
    f = Active(dp::Role::Host); f.victim = C::Enemy; f.source = C::NativeCompanion;
    Expect(f, n, dp::Reason::HostCompanionAttack, true, "positively classified native companion can damage host enemy");
    f.source = C::RemoteRepresentation;
    Expect(f, z, dp::Reason::RemoteSource, true, "same host enemy with remote source is vetoed");
    f.source = C::Unknown;
    Expect(f, z, dp::Reason::UnknownSource, true, "expired driver does not grant NPC authority");
}

void ScopeAndAmounts() {
    using A = dp::Action;
    auto f = Active();
    f.role = dp::Role::Off;
    Expect(f, A::Native, dp::Reason::Off, false, "verified Off preserves existing path");
    f = Active(); f.ownerThread = false;
    Expect(f, A::Native, dp::Reason::ForeignThread, false, "foreign thread unsupported");
    f = Active(); f.contextAvailable = false;
    Expect(f, A::Native, dp::Reason::ContextUnavailable, false, "unavailable session preserves existing path");
    f = Active(); f.role = static_cast<dp::Role>(255);
    Expect(f, A::Native, dp::Reason::ContextUnavailable, false, "unknown role unsupported");
    for (const auto mask : {dp::FlagsAvailable, dp::StatAvailable, dp::AmountAvailable}) {
        f = Active(); f.hit.readMask &= ~mask;
        Expect(f, A::Native, dp::Reason::HitUnavailable, false, "missing required hit read preserves existing path");
    }
    f = Active(); f.source = dp::ActorClass::RemoteRepresentation; f.hit.flags = 2;
    Expect(f, A::Native, dp::Reason::AlreadyApplied, false, "bit2 applied consumes no new claim or veto");
    f.hit.flags = 1;
    Expect(f, A::ZeroHp, dp::Reason::RemoteSource, true, "bit1 numeric is not native applied mask2");
    f.hit.stat = 1;
    Expect(f, A::Native, dp::Reason::NonHp, false, "nonHP untouched even for remote source");
    f.hit.stat = 0; f.hit.amount = 0;
    Expect(f, A::Native, dp::Reason::ZeroAmount, false, "zero amount untouched");

    for (const std::uint8_t kind : {std::uint8_t{5}, std::uint8_t{6}}) {
        f = Active(); f.hit.kind = kind; f.source = dp::ActorClass::NativeCompanion;
        Expect(f, A::Native, dp::Reason::LocalVictim, true, "legitimate native healing canonical victim allowed");
        f.source = dp::ActorClass::RemoteRepresentation;
        Expect(f, A::ZeroHp, dp::Reason::RemoteSource, true, "remote healing canonical victim vetoed");
        f.source = dp::ActorClass::NativeCompanion; f.victim = dp::ActorClass::RemoteRepresentation;
        Expect(f, A::ZeroHp, dp::Reason::RemoteVictim, true, "healing remote victim vetoed");
        f.source = dp::ActorClass::LocalAvatar; f.victim = dp::ActorClass::Enemy;
        Expect(f, A::ZeroHp, dp::Reason::OtherActiveHit, true, "client canonical healing enemy never becomes damage claim");
    }
    f = Active(); f.source = dp::ActorClass::LocalAvatar; f.victim = dp::ActorClass::Enemy;
    Expect(f, A::ClaimThenZeroHp, dp::Reason::ClientClaim, true, "ordinary canonical client enemy claim");
    f.hit.readMask &= ~dp::KindAvailable;
    Expect(f, A::ZeroHp, dp::Reason::OtherActiveHit, true, "unknown attack kind cannot claim but stays suppressed");
    f.hit.readMask |= dp::KindAvailable; f.hit.amount = -8;
    Expect(f, A::ZeroHp, dp::Reason::OtherActiveHit, true, "negative client amount never becomes positive damage claim");
    f.victim = dp::ActorClass::LocalAvatar; f.source = dp::ActorClass::Enemy;
    Expect(f, A::Native, dp::Reason::LocalVictim, true, "nonzero signed amount canonical native path retained");
    f.source = dp::ActorClass::RemoteRepresentation;
    Expect(f, A::ZeroHp, dp::Reason::RemoteSource, true, "negative remote HP amount vetoed too");
}

class OwnedPages {
public:
    OwnedPages() {
        SYSTEM_INFO info {};
        GetSystemInfo(&info);
        pageSize = info.dwPageSize;
        data = static_cast<unsigned char*>(VirtualAlloc(nullptr, pageSize * 2,
            MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    }
    ~OwnedPages() { if (data) VirtualFree(data, 0, MEM_RELEASE); }
    OwnedPages(const OwnedPages&) = delete;
    OwnedPages& operator=(const OwnedPages&) = delete;
    bool Protect(std::size_t offset, DWORD protection) const {
        DWORD previous = 0;
        return VirtualProtect(data + offset, pageSize, protection, &previous) != 0;
    }
    uintptr_t Address() const { return reinterpret_cast<uintptr_t>(data); }
    unsigned char* data = nullptr;
    std::size_t pageSize = 0;
};

template <typename T> void Put(unsigned char* record, std::size_t offset, T value) {
    std::memcpy(record + offset, &value, sizeof(value));
}
template <typename T> T Get(const unsigned char* record, std::size_t offset) {
    T value {};
    std::memcpy(&value, record + offset, sizeof(value));
    return value;
}
void SetRecord(unsigned char* record, const dp::Hit& h) {
    std::memset(record, 0xA5, 64);
    Put(record, 0x18, h.flags);
    Put(record, 0x25, h.stat);
    Put(record, 0x28, h.amount);
}

void CheckedZeroLeaf() {
    OwnedPages pages;
    Check(pages.data != nullptr, "VirtualAlloc owned records");
    if (!pages.data) return;
    auto h = Active().hit;
    h.flags = 0x10;
    SetRecord(pages.data, h);
    std::array<unsigned char, 64> expected {};
    std::memcpy(expected.data(), pages.data, expected.size());
    Put(expected.data(), 0x28, std::int32_t{0});
    Check(dp::TryZeroHp(pages.Address(), h) == dp::ZeroResult::Zeroed, "actual production zero leaf succeeds");
    Check(std::memcmp(expected.data(), pages.data, expected.size()) == 0,
        "zero changes only four HP amount bytes, leaves consumption/effect bytes alone");
    Check(dp::TryZeroHp(pages.Address(), h) == dp::ZeroResult::Changed,
        "same expected record cannot zero twice after amount changes");

    for (const auto amount : {std::int32_t{-9}, (std::numeric_limits<std::int32_t>::min)(),
                              (std::numeric_limits<std::int32_t>::max)()}) {
        h = Active().hit; h.amount = amount; SetRecord(pages.data, h);
        Check(dp::TryZeroHp(pages.Address(), h) == dp::ZeroResult::Zeroed &&
              Get<std::int32_t>(pages.data, 0x28) == 0, "signed-width HP amount zeroed without arithmetic");
    }
    h = Active().hit;
    SetRecord(pages.data, h); Put(pages.data, 0x18, std::uint32_t{2});
    Check(dp::TryZeroHp(pages.Address(), h) == dp::ZeroResult::AlreadyApplied &&
          Get<std::int32_t>(pages.data, 0x28) == 8, "native consumption between capture and write prevents zero");
    SetRecord(pages.data, h); Put(pages.data, 0x18, std::uint32_t{1});
    Check(dp::TryZeroHp(pages.Address(), h) == dp::ZeroResult::Changed, "non-applied flag change invalidates copied decision");
    SetRecord(pages.data, h); Put(pages.data, 0x25, std::uint8_t{1});
    Check(dp::TryZeroHp(pages.Address(), h) == dp::ZeroResult::Changed &&
          Get<std::int32_t>(pages.data, 0x28) == 8, "stat changed after capture cannot write");
    SetRecord(pages.data, h); Put(pages.data, 0x28, std::int32_t{9});
    Check(dp::TryZeroHp(pages.Address(), h) == dp::ZeroResult::Changed &&
          Get<std::int32_t>(pages.data, 0x28) == 9, "changed or reused record amount cannot use stale expectation");
    auto replacement = h; replacement.amount = 9;
    Check(dp::TryZeroHp(pages.Address(), replacement) == dp::ZeroResult::Zeroed,
        "fresh expectation at reused address is not mistaken for lifetime duplicate");

    auto invalid = h; invalid.readMask = 0;
    Check(dp::TryZeroHp(0, invalid) == dp::ZeroResult::InvalidExpected, "invalid capture rejected before pointer read");
    invalid = h; invalid.flags = 2;
    Check(dp::TryZeroHp(0, invalid) == dp::ZeroResult::InvalidExpected, "already applied expectation rejected before read");
    invalid = h; invalid.stat = 1;
    Check(dp::TryZeroHp(0, invalid) == dp::ZeroResult::InvalidExpected, "nonHP expectation rejected before read");
    invalid = h; invalid.amount = 0;
    Check(dp::TryZeroHp(0, invalid) == dp::ZeroResult::InvalidExpected, "zero expectation rejected before read");
    Check(dp::TryZeroHp(0, h) == dp::ZeroResult::Fault, "null record bounded fault");
    Check(dp::TryZeroHp((std::numeric_limits<uintptr_t>::max)(), h) == dp::ZeroResult::Fault,
        "overflowing pointer range bounded fault");
    SetRecord(pages.data, h);
    Check(pages.Protect(0, PAGE_READONLY), "protect owned record readonly");
    Check(dp::TryZeroHp(pages.Address(), h) == dp::ZeroResult::Fault,
        "actual write protection fault contained by production leaf");
    Check(Get<std::int32_t>(pages.data, 0x28) == 8, "failed write leaves amount unchanged");
    Check(pages.Protect(0, PAGE_NOACCESS), "protect owned record inaccessible");
    Check(dp::TryZeroHp(pages.Address(), h) == dp::ZeroResult::Fault,
        "actual read protection fault contained by production leaf");
    Check(pages.Protect(0, PAGE_READWRITE), "restore owned record access");
    Check(pages.Protect(pages.pageSize, PAGE_NOACCESS), "guard second owned page");
    auto* boundary = pages.data + pages.pageSize - 0x28;
    Put(boundary, 0x18, h.flags); Put(boundary, 0x25, h.stat);
    Check(dp::TryZeroHp(reinterpret_cast<uintptr_t>(boundary), h) == dp::ZeroResult::Fault,
        "partial readable record cannot cross inaccessible amount page");
    Check(Get<std::uint32_t>(boundary, 0x18) == h.flags, "partial-read fault does not write applied flags");
}

// A useful integration boundary control, not a replacement EntityHook adapter.
// Evaluate and TryZeroHp are production code; the original and claim sink are
// synthetic. The observer, native dispatcher and session capture are not linked.
struct Execution {
    unsigned originals = 0, claimAttempts = 0, acceptedClaims = 0;
    std::int32_t hp = 24, capturedClaimAmount = 0;
    bool acceptClaim = true;
    dp::ZeroResult zero = dp::ZeroResult::InvalidExpected;
};
constexpr uintptr_t RawReturn = 0xFEDCBA9876543210ULL;
uintptr_t SyntheticOriginal(unsigned char* record, Execution& x, bool healing) {
    ++x.originals;
    const auto flags = Get<std::uint32_t>(record, 0x18);
    if (!(flags & 2U)) {
        Put(record, 0x18, flags | 2U);
        const auto amount = Get<std::int32_t>(record, 0x28);
        if (Get<std::uint8_t>(record, 0x25) == 0) x.hp += healing ? amount : -amount;
    }
    return RawReturn;
}
uintptr_t Exercise(dp::Facts facts, unsigned char* record, Execution& x) {
    facts.hit.flags = Get<std::uint32_t>(record, 0x18);
    facts.hit.stat = Get<std::uint8_t>(record, 0x25);
    facts.hit.amount = Get<std::int32_t>(record, 0x28);
    const auto d = dp::Evaluate(facts);
    if (d.action == dp::Action::ClaimThenZeroHp) {
        ++x.claimAttempts;
        x.capturedClaimAmount = facts.hit.amount;
        if (x.acceptClaim) ++x.acceptedClaims;
    }
    if (d.action != dp::Action::Native)
        x.zero = dp::TryZeroHp(reinterpret_cast<uintptr_t>(record), facts.hit);
    return SyntheticOriginal(record, x, facts.hit.kind == 5 || facts.hit.kind == 6);
}

void ExecutionBoundary() {
    OwnedPages pages;
    Check(pages.data != nullptr, "allocate execution record");
    if (!pages.data) return;
    auto f = Active();
    SetRecord(pages.data, f.hit); Execution x {};
    Check(Exercise(f, pages.data, x) == RawReturn && x.originals == 1 && x.hp == 16,
        "canonical incoming native original once and opaque fullwidth ABI return");
    Check((Get<std::uint32_t>(pages.data, 0x18) & 2U) != 0 && x.claimAttempts == 0,
        "native original consumes local incoming record without outgoing claim");

    f.source = dp::ActorClass::RemoteRepresentation;
    SetRecord(pages.data, f.hit); x = {};
    Check(Exercise(f, pages.data, x) == RawReturn && x.originals == 1 && x.hp == 24 &&
          x.zero == dp::ZeroResult::Zeroed, "remote veto retains original and consumption with no HP change");
    Check((Get<std::uint32_t>(pages.data, 0x18) & 2U) != 0, "zero veto does not skip native applied bit");

    f = Active(); f.source = dp::ActorClass::LocalAvatar; f.victim = dp::ActorClass::Enemy;
    SetRecord(pages.data, f.hit); x = {};
    Check(Exercise(f, pages.data, x) == RawReturn && x.hp == 24 && x.claimAttempts == 1 &&
          x.acceptedClaims == 1 && x.capturedClaimAmount == 8, "claim captures original amount before production zero");
    Check(Exercise(f, pages.data, x) == RawReturn && x.originals == 2 && x.claimAttempts == 1 && x.hp == 24,
        "already consumed record invokes original once more without duplicate claim or HP delta");
    SetRecord(pages.data, f.hit);
    Check(Exercise(f, pages.data, x) == RawReturn && x.claimAttempts == 2 && x.originals == 3,
        "new unapplied record at reused pointer is a new execution not a pointer identity");
    SetRecord(pages.data, f.hit); x = {}; x.acceptClaim = false;
    Check(Exercise(f, pages.data, x) == RawReturn && x.claimAttempts == 1 && x.acceptedClaims == 0 &&
          x.hp == 24 && x.zero == dp::ZeroResult::Zeroed, "rejected or full claim sink does not restore local enemy HP authority");

    f = Active(); f.hit.kind = 5; f.source = dp::ActorClass::NativeCompanion;
    SetRecord(pages.data, f.hit); x = {};
    Check(Exercise(f, pages.data, x) == RawReturn && x.hp == 32 && x.originals == 1,
        "legitimate canonical healing retains positive native HP effect");
    f.source = dp::ActorClass::RemoteRepresentation;
    SetRecord(pages.data, f.hit); x = {};
    Check(Exercise(f, pages.data, x) == RawReturn && x.hp == 24 && x.originals == 1,
        "remote healing veto does not turn into an outgoing damage claim");
}
}

int main() {
    Matrix();
    ScopeAndAmounts();
    CheckedZeroLeaf();
    ExecutionBoundary();
    std::printf("DamagePolicyTest: %u checks, %u failed\n", g_checks, g_failed);
    std::printf("Scope: production copied-facts policy and owned-record zero leaf; synthetic original/claim control, no live KH2 or membership proof\n");
    return g_failed ? 1 : 0;
}
