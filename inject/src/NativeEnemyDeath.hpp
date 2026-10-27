#pragma once

#include "OrdinaryEnemyBinding.hpp"

namespace kh2coop::inject::nativedeath {

// A copied occurrence at the verified lethal notification, never an actor to
// dereference later. Completion must re-prove roots and the entire owner scope.
struct Receipt {
    ordinarybinding::ScriptActorScope scope{};
    ordinarybinding::Identity identity{};
    std::uint32_t netId{}, flags{};
    bool available{};
};
inline bool Completed(const Receipt& before, const Receipt& after) noexcept {
    return before.available && after.available && before.netId &&
        before.netId == after.netId && before.scope == after.scope &&
        before.identity == after.identity && !(before.flags & 4u) && (after.flags & 4u);
}
inline bool Pending(const Receipt& receipt, const ordinarybinding::ScriptActorScope& current,
                    bool announced, bool sent, bool retired) noexcept {
    return receipt.available && receipt.netId && receipt.scope == current && announced && !sent && !retired;
}

} // namespace kh2coop::inject::nativedeath
