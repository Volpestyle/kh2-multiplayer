#pragma once

#include <algorithm>
#include <cstdint>
#include <tuple>
#include <utility>
#include <vector>

namespace kh2coop {

// An observation of a local native actor, never a received manifest/HP cache.
// Zero identifies an unmatched actor. Multiple identical records are retained:
// a second unmatched or multiply-bound actor must change the population hash.
// Step 1 runs independent AI, so positions are deliberately absent. maxHP is
// absent too: only current HP is mirrored by the native client implementation.
struct AppliedEnemyState {
    std::uint16_t netId = 0;
    std::uint32_t objectId = 0;
    std::int32_t hp = 0;
};

inline std::vector<AppliedEnemyState> canonicalAppliedEnemies(std::vector<AppliedEnemyState> records) {
    records.erase(std::remove_if(records.begin(), records.end(),
                                 [](const auto& r) { return r.hp <= 0; }), records.end());
    std::sort(records.begin(), records.end(), [](const auto& a, const auto& b) {
        return std::tie(a.netId, a.objectId, a.hp) < std::tie(b.netId, b.objectId, b.hp);
    });
    return records;
}

inline std::uint32_t hashAppliedEnemies(std::vector<AppliedEnemyState> observations) {
    const auto records = canonicalAppliedEnemies(std::move(observations));
    // FNV-1a over explicitly sized little-endian fields, independent of native
    // padding, address, enumeration order and compiler ABI. This is diagnostic,
    // not cryptographic. Include a schema tag and count to delimit the stream.
    std::uint32_t hash = 2166136261u;
    const auto add = [&hash](std::uint32_t value, unsigned bytes) {
        for (unsigned i = 0; i < bytes; ++i) {
            hash = (hash ^ (value & 0xFFu)) * 16777619u;
            value >>= 8;
        }
    };
    add(0x3145484Bu, 4);  // "KHE1"
    add(static_cast<std::uint32_t>(records.size()), 4);
    for (const auto& record : records) {
        add(record.netId, 2);
        add(record.objectId, 4);
        add(static_cast<std::uint32_t>(record.hp), 4);
    }
    return hash;
}

} // namespace kh2coop
