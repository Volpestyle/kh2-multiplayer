#include "DamagePolicy.hpp"
#include <Windows.h>

namespace kh2coop::inject::damagepolicy {
namespace {
bool Remote(ActorClass kind) noexcept {
    return kind == ActorClass::RemoteRepresentation || kind == ActorClass::NonLocalPlayer;
}
bool KnownNative(ActorClass kind) noexcept {
    return kind == ActorClass::LocalAvatar || kind == ActorClass::NativeCompanion ||
        kind == ActorClass::Enemy || kind == ActorClass::OtherNative;
}
}
Decision Evaluate(const Facts& f) noexcept {
    if (!f.ownerThread) return {Action::Native, Reason::ForeignThread, false};
    if (!f.contextAvailable) return {Action::Native, Reason::ContextUnavailable, false};
    if (f.role == Role::Off) return {Action::Native, Reason::Off, false};
    if (f.role != Role::Host && f.role != Role::Client) return {Action::Native, Reason::ContextUnavailable, false};
    if ((f.hit.readMask & RequiredHitReads) != RequiredHitReads) return {Action::Native, Reason::HitUnavailable, false};
    if (f.hit.flags & 2U) return {Action::Native, Reason::AlreadyApplied, false};
    if (f.hit.stat != 0) return {Action::Native, Reason::NonHp, false};
    if (f.hit.amount == 0) return {Action::Native, Reason::ZeroAmount, false};
    if (Remote(f.victim)) return {Action::ZeroHp, Reason::RemoteVictim, true};
    if (Remote(f.source)) return {Action::ZeroHp, Reason::RemoteSource, true};
    if (!KnownNative(f.source)) return {Action::ZeroHp, Reason::UnknownSource, true};
    if (f.victim == ActorClass::LocalAvatar) return {Action::Native, Reason::LocalVictim, true};
    if (f.victim == ActorClass::Enemy) {
        if (f.role == Role::Host && f.source == ActorClass::LocalAvatar)
            return {Action::Native, Reason::HostLocalAttack, true};
        if (f.role == Role::Host && f.source == ActorClass::NativeCompanion)
            return {Action::Native, Reason::HostCompanionAttack, true};
        if (f.role == Role::Client && f.source == ActorClass::LocalAvatar && f.hit.amount > 0 &&
            (f.hit.readMask & KindAvailable) && f.hit.kind != 5 && f.hit.kind != 6)
            return {Action::ClaimThenZeroHp, Reason::ClientClaim, true};
    }
    return {Action::ZeroHp, Reason::OtherActiveHit, true};
}
ZeroResult TryZeroHp(uintptr_t hit, const Hit& expected) noexcept {
    if ((expected.readMask & RequiredHitReads) != RequiredHitReads || expected.stat != 0 ||
        expected.amount == 0 || (expected.flags & 2U)) return ZeroResult::InvalidExpected;
    if (hit <= 0x10000 || hit >= 0x7FFFFFFFFFFFULL || 0x2C > 0x7FFFFFFFFFFFULL - hit)
        return ZeroResult::Fault;
    __try {
        const auto flags = *reinterpret_cast<const volatile std::uint32_t*>(hit + 0x18);
        const auto stat = *reinterpret_cast<const volatile std::uint8_t*>(hit + 0x25);
        const auto amount = *reinterpret_cast<const volatile std::int32_t*>(hit + 0x28);
        if (flags & 2U) return ZeroResult::AlreadyApplied;
        if (flags != expected.flags || stat != expected.stat || amount != expected.amount)
            return ZeroResult::Changed;
        *reinterpret_cast<volatile std::int32_t*>(hit + 0x28) = 0;
        return ZeroResult::Zeroed;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return ZeroResult::Fault; }
}
}
