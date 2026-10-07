#pragma once
// ============================================================================
// The reviewed player-kit table (party kits, VUH-1513/1519). The ONE place a kit is admitted:
// the common intent rule, PartyNative's plan codes and host kit latch, PlayerKit's local kit,
// NativePrivateStatus's clone/local descriptor keys and CloneNeutralInput's descriptor check all
// look kits up here. Facts per row: vanilla 00objentry (VUH-1513 offline parse,
// build/rig/vuh1513-player-class-kit-route-20261006-01/objentry-player-rows.tsv).
//
// A row's `qualified` flag changes ONLY in a commit whose evidence names that kit's own live
// fixture run. Unqualified rows are listed (their stream roster code is real) but refused by
// every admission gate. Standalone header: no protocol dependency (EntityHook cannot include
// Protocol.hpp alongside HitChannel.hpp).
// ============================================================================
#include <cstdint>
#include <cstring>

namespace kh2coop {

struct KitProfile {
    std::uint16_t member;     // objentry id; also the resolved member value and the actor's objectId
    std::uint8_t roster;      // AvatarState.character code and the 2-bit party plan code
    const char* name;         // objentry model name (descriptor +8)
    std::uint16_t statusKey;  // status key (descriptor +0x4C, status +0x260); = NeoStatus
    std::uint8_t neoMoveset;
    std::uint8_t form;        // objentry form (0 = none for Sora/Roxas; 10 RoxasDualWield; 11 = n/a)
    bool playerClass;         // PLAYER-type row: may be a clone or a local
    bool qualified;           // admitted by every gate; flipped only with its own live fixture
};

inline constexpr std::uint16_t KIT_SORA = 0x54;
inline constexpr KitProfile kKits[] = {
    {0x54, 0, "P_EX100", 1, 1, 0, true, true},            // Sora: VUH-1519/1786 live
    {0x5A, 1, "P_EX110", 14, 9, 0, true, true},           // Roxas: party kits live PASS 20261007-090953
    {0x323, 2, "P_EX110_BTLF", 14, 10, 10, true, false},  // Roxas dual-wield: form machinery (VUH-1509) first
    {0x5B, 3, "P_EX200", 4, 28, 11, true, false},         // Mickey: status key 4 unmeasured
};

// The qualified player-class profile of a member/objentry value, or nullptr.
inline const KitProfile* qualifiedKit(std::uint16_t member) {
    for (const auto& k : kKits) if (k.member == member && k.playerClass && k.qualified) return &k;
    return nullptr;
}
// The qualified player-class profile with this roster/plan code, or nullptr.
inline const KitProfile* qualifiedKitByRoster(std::uint8_t roster) {
    for (const auto& k : kKits) if (k.roster == roster && k.playerClass && k.qualified) return &k;
    return nullptr;
}
// Stream roster code of an actual descriptor's objectId (any listed row; 0 = Sora/unknown).
inline std::uint8_t kitRosterForObject(std::uint32_t objectId) {
    for (const auto& k : kKits) if (k.member == objectId) return k.roster;
    return 0;
}
inline bool kitNameIs(const KitProfile& k, const char* name, std::size_t available) {
    const std::size_t n = std::strlen(k.name) + 1; // including the NUL
    return n <= available && std::memcmp(name, k.name, n) == 0;
}
// Private-status admission (pure): the status key of a qualified player-class descriptor
// (type 0, form 0, id + key + name of one row), else 0. Every non-Sora row needs `nonSoraAllowed`
// (party kits or the VUH-1513 remote kit member active on this machine).
inline int kitDescriptorKey(std::uint32_t id, std::uint8_t type, std::uint16_t key, std::int8_t form,
                            const char* name, std::size_t available, bool nonSoraAllowed) {
    if (type != 0 || form != 0) return 0;
    for (const auto& k : kKits)
        if (k.playerClass && k.qualified && k.member == id && k.statusKey == key && kitNameIs(k, name, available) &&
            (k.member == KIT_SORA || nonSoraAllowed)) return key;
    return 0;
}
// Clone-neutral-input admission (pure): same rows, matched on id, type 0 and name (no key read).
inline bool kitNeutralDescriptor(std::uint32_t id, std::uint8_t type, const char* name, std::size_t available, bool nonSoraAllowed) {
    if (type != 0) return false;
    for (const auto& k : kKits)
        if (k.playerClass && k.qualified && k.member == id && kitNameIs(k, name, available) &&
            (k.member == KIT_SORA || nonSoraAllowed)) return true;
    return false;
}
// The status key of a qualified kit member, else 0.
inline int kitStatusKey(std::uint16_t member) {
    const auto* k = qualifiedKit(member);
    return k ? k->statusKey : 0;
}

} // namespace kh2coop
