#pragma once
// ============================================================================
// HitChannel — hit ownership controls from kh2ctl to the inject DLL (VUH-1501).
//
// Naming: "Local\kh2coop_hit_<KH2_PID>". The DLL creates the mapping at init.
//
// Drop filter: while dropEnabled != 0, hits whose attacker/victim match
// (0 = any) have their damage zeroed before ApplyHitDamage (0x3D3BA0) uses
// it, and each dropped hit is appended to the claim ring — what a client
// would send to the host. enemyVictimsOnly restricts dropping to enemies.
//
// Apply: kh2ctl fills op/victim/amount and bumps requestSeq; on the game
// thread, at the start of the next frame, the DLL runs the op and sets
// doneSeq = requestSeq with the victim's HP before/after.
// Fields the DLL writes are marked [dll]; the rest are [client].
// ============================================================================

#include <cstdint>

namespace kh2coop {

constexpr const wchar_t* HIT_NAME_PREFIX = L"Local\\kh2coop_hit_";
constexpr std::uint32_t HIT_MAGIC = 0x54484B48;  // "HKHT"
constexpr std::uint32_t HIT_VERSION = 1;
constexpr std::uint32_t HIT_CLAIM_CAPACITY = 64;

enum class HitOp : std::uint32_t {
    Damage = 1,  // TakeDamage 0x3D5E50(victim, -amount, 0, 1): host applies a claim
    Lethal = 2,  // ApplyStatDelta 0x3D2EB0(victim, -hp, 0, 0): synthetic killing blow
};

enum class HitStatus : std::int32_t {
    Ok = 0,
    UnknownVictim = 1,  // not an actor in this frame's entity list
    Unavailable = 2,    // game function not hooked/verified on this build
    BadOp = 3,
    NoStats = 4,        // actor has no status block (HP reads -1); nothing to damage
};

#pragma pack(push, 4)
struct HitClaim {
    std::uint32_t frame;
    std::uint32_t atkpId;
    std::uint64_t attacker;  // actor address (0 = unresolved)
    std::uint64_t victim;
    std::int32_t damage;     // as built, before the drop zeroed it
    std::int32_t victimHp;   // victim HP when dropped
};

struct HitChannel {
    std::uint32_t magic;            // [dll]
    std::uint32_t version;          // [dll]
    // Drop filter.
    std::uint32_t dropEnabled;      // [client]
    std::uint32_t enemyVictimsOnly; // [client]
    std::uint64_t dropAttacker;     // [client] actor address, 0 = any
    std::uint64_t dropVictim;       // [client] actor address, 0 = any
    volatile long claimCount;       // [dll] total claims; slot = n % capacity
    std::uint32_t pad0;
    HitClaim claims[HIT_CLAIM_CAPACITY];  // [dll]
    // Apply request.
    volatile long requestSeq;       // [client]
    volatile long doneSeq;          // [dll]
    std::uint32_t op;               // [client] HitOp
    std::int32_t amount;            // [client] damage (positive), Damage op only
    std::uint64_t victim;           // [client] actor address
    std::int32_t status;            // [dll] HitStatus
    std::int32_t hpBefore;          // [dll]
    std::int32_t hpAfter;           // [dll]
    std::uint32_t frame;            // [dll]
};
#pragma pack(pop)

} // namespace kh2coop
