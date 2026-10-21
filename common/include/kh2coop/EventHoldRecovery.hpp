#pragma once
#include "kh2coop/EventHoldControl.hpp"
#include "kh2coop/ResyncProtocol.hpp"
#include <algorithm>
#include <cstring>
#include <string_view>

namespace kh2coop::eventhold {
// Recovery deliberately excludes boss/script reconstruction. Native admission
// must additionally prove the strict, unpaused field and fresh record census.
inline bool RecoverySnapshotSupported(const ResyncSnapshot& snapshot) {
    return snapshot.coverageMask == ResyncNativeComplete && snapshot.room.epoch &&
        snapshot.progress.version && !snapshot.hold.active && !snapshot.deadCount &&
        snapshot.livingCount == snapshot.enemies.size() &&
        std::all_of(snapshot.enemies.begin(), snapshot.enemies.end(), [](const auto& row) {
            return (row.objectType == 3 || row.objectType == 4) && row.identity.objectId != 307 &&
                row.life == ResyncLife::Alive && row.hp > 0 && row.maxHp >= row.hp;
        });
}

// A relay result alone is insufficient: correlate it with the actual native
// snapshot delivered by this transaction and this exact current incarnation.
// Native callers must also retain their own two-frame convergence receipt.
inline bool RecoveryResultMatches(const Scope& scope, const ResyncBegin& begin,
    const ResyncSnapshot& snapshot, const ResyncResult& result) {
    if (!ValidScope(scope) || result.reason != ResyncResultReason::Converged ||
        !begin.key.requestId || begin.key != result.key ||
        begin.key.sessionId != std::string_view(scope.session, sizeof(scope.session)) ||
        begin.key.hostConnectionId != scope.hostConnection || !begin.snapshotCut ||
        (begin.phase != ResyncPhase::Bootstrap && begin.phase != ResyncPhase::Checkpoint) ||
        !sameResyncRoom(begin.room, snapshot.room) || !RecoverySnapshotSupported(snapshot) ||
        !begin.targetCount || begin.targetCount > begin.targets.size() ||
        !result.targetCount || result.targetCount > result.targets.size()) return false;
    const ResyncTarget wanted {static_cast<std::uint8_t>(scope.slot), scope.selfConnection, scope.targetDelivery};
    unsigned included = 0, converged = 0;
    for (std::size_t i = 0; i < begin.targetCount; ++i) included += begin.targets[i] == wanted;
    for (std::size_t i = 0; i < result.targetCount; ++i) {
        const auto& row = result.targets[i];
        if (row.target != wanted) continue;
        if (row.status != ResyncAckStatus::Converged || row.appliedCut != begin.snapshotCut ||
            row.fingerprint != snapshot.nativeFingerprint) return false;
        ++converged;
    }
    return included == 1 && converged == 1;
}
} // namespace kh2coop::eventhold
