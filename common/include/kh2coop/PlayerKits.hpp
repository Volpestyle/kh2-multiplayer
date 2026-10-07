#pragma once
// ============================================================================
// The reviewed player-kit table (party kits, VUH-1513/1519). The ONE place a kit is admitted:
// the common intent rule, PartyNative's plan codes and host kit latch, PlayerKit's local kit,
// NativePrivateStatus's clone/local descriptor keys and CloneNeutralInput's descriptor check all
// look kits up here. Facts per row: vanilla 00objentry (VUH-1513 offline parse,
// build/rig/vuh1513-player-class-kit-route-20261006-01/objentry-player-rows.tsv).
//
// A row's `qualified` / `soloQualified` flag changes ONLY in a commit whose evidence names that kit's own
// live fixture run on that path (`qualified`: party kits / any gate; `soloQualified`: the solo
// KH2COOP_PLAYER_KIT path without party kits). Unqualified rows are listed (their stream roster code is real) but refused by
// every admission gate. Standalone header: no protocol dependency (EntityHook cannot include
// Protocol.hpp alongside HitChannel.hpp).
// ============================================================================
#include <cstdint>
#include <cstring>

namespace kh2coop {

struct KitProfile {
    std::uint16_t member;     // objentry id; also the resolved member value and the actor's objectId
    std::uint8_t roster;      // AvatarState.character code and the 3-bit party plan code (7 = invalid; must stay < 7)
    const char* name;         // objentry model name (descriptor +8)
    std::uint16_t statusKey;  // status key (descriptor +0x4C, status +0x260); = NeoStatus
    std::uint8_t neoMoveset;
    std::uint8_t form;        // objentry Form (+0x57; OpenKH Objentry.Form): 0 SoraRoxasDefault, 10 RoxasDualWield,
                              // 11 Default (the non-Sora characters' base form, not a drive form)
    bool playerClass;         // PLAYER-type row: may be a clone or a local
    bool qualified;           // admitted by every gate; flipped only with its own live fixture
    bool soloQualified;       // also the solo KH2COOP_PLAYER_KIT path (no party kits); its own solo run
};

inline constexpr std::uint16_t KIT_SORA = 0x54;
inline constexpr KitProfile kKits[] = {
    {0x54, 0, "P_EX100", 1, 1, 0, true, true, true},             // Sora: VUH-1519/1786 live
    {0x5A, 1, "P_EX110", 14, 9, 0, true, true, true},            // Roxas: solo 20261006-233415, party kits 20261007-090953
    {0x323, 2, "P_EX110_BTLF", 14, 10, 10, true, false, false},  // Roxas dual-wield: form machinery (VUH-1509) first
    {0x5B, 3, "P_EX200", 4, 28, 11, true, true, false},          // Mickey: party kits fixture-03 PASS (20261007-115923); no solo run
};

// The party plan packs a roster code into 3 bits with 7 reserved for "invalid" (PartyNative KIT_CODE_INVALID):
// every listed roster code must stay below it, or a future kit would alias under `& 7`.
constexpr bool kitRostersBelowInvalidCode() { for (const auto& k : kKits) if (k.roster >= 7) return false; return true; }
static_assert(kitRostersBelowInvalidCode(), "a kit roster code collides with the plan's invalid code 7");

// The qualified player-class profile of a member/objentry value, or nullptr.
inline const KitProfile* qualifiedKit(std::uint16_t member) {
    for (const auto& k : kKits) if (k.member == member && k.playerClass && k.qualified) return &k;
    return nullptr;
}
// The qualified profile that is also qualified for the solo KH2COOP_PLAYER_KIT path (no party kits), or nullptr.
inline const KitProfile* soloQualifiedKit(std::uint16_t member) {
    const auto* k = qualifiedKit(member);
    return k && k->soloQualified ? k : nullptr;
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
// Private-status admission (pure): the status key of a qualified player-class descriptor (type 0;
// id + key + name + FORM of one row: the descriptor form byte must equal the row's base form), else 0.
// Every non-Sora row needs `nonSoraAllowed` (party kits or the VUH-1513 remote kit member active here).
// Core over any table, so checks can evaluate a candidate row before it is qualified.
template <std::size_t N>
inline int kitDescriptorKeyIn(const KitProfile (&table)[N], std::uint32_t id, std::uint8_t type, std::uint16_t key,
                              std::int8_t form, const char* name, std::size_t available, bool nonSoraAllowed) {
    if (type != 0) return 0;
    for (const auto& k : table)
        if (k.playerClass && k.qualified && k.member == id && k.statusKey == key &&
            static_cast<std::uint8_t>(form) == k.form && kitNameIs(k, name, available) &&
            (k.member == KIT_SORA || nonSoraAllowed)) return key;
    return 0;
}
inline int kitDescriptorKey(std::uint32_t id, std::uint8_t type, std::uint16_t key, std::int8_t form,
                            const char* name, std::size_t available, bool nonSoraAllowed) {
    return kitDescriptorKeyIn(kKits, id, type, key, form, name, available, nonSoraAllowed);
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
