#pragma once
// The raw input collector's order after the native collector ran (HookedInputCollector), shared with its
// test so the test drives the real sequence (VUH-2640, Quill r2): the mailbox, the orphaned pause's owner
// service and the event hold's apply, the prologue hold, the revive prompt and spectating, then the
// logically downed player's neutralization last. That neutralization keeps only the orphaned pause's own
// press emitted in this callback (an expired lifetime closing its own pause), never gameplay input.
#include "kh2coop/KH2Offsets.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <cstdint>

namespace kh2coop::inject::collectororder {

// A logically downed player has no input of its own: neutral sticks, and no buttons but `keep`.
inline bool NeutralizeHeld(void* inputStruct, std::uint16_t keep) noexcept {
    __try {
        auto* raw = reinterpret_cast<std::uint8_t*>(inputStruct) + offsets::input::RAW_SLOT0;
        *reinterpret_cast<std::uint16_t*>(raw + offsets::input::BUTTONS) = keep;
        raw[offsets::input::LSTICK_X] = raw[offsets::input::LSTICK_Y] = 128;
        raw[offsets::input::RSTICK_X] = raw[offsets::input::RSTICK_Y] = 128;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

template <class Ops>
void AfterNativeCollector(Ops& ops, void* inputStruct, bool control) {
    const bool held = control && ops.HoldingInput();
    if (ops.PollMailbox() && !held) ops.ApplyMailbox(inputStruct);
    if (control) ops.ServiceOrphanedPause(inputStruct); // VUH-2640 F2: decides; ApplyInput emits
    const bool eventHeld = control && ops.ApplyInput(inputStruct);
    if (eventHeld) ops.ConsumeMailbox();
    // VUH-2464: a prologue field holds all local control until both seats bound here.
    // VUH-2359: not while the host's event hold owns the input: it already neutralizes it,
    // and zeroing here would erase the Start press that opens its pause.
    if (!eventHeld && ops.PrologueHold(inputStruct)) ops.ConsumeMailbox();
    // VUH-1504 revive prompt: raw slot-0 buttons after the mailbox/event-hold apply.
    ops.PromptInput(inputStruct);
    ops.SpectateInput(inputStruct);
    // VUH-2465: a logically downed player has no input of its own; VUH-2640: but the orphaned pause's own
    // press emitted in this callback passes, so a downed seat's expired lifetime still closes its pause.
    std::uint16_t orphan = 0;
    const bool orphanEmitted = control && ops.OrphanPress(orphan);
    if (ops.DownedHold(inputStruct, orphanEmitted ? orphan : static_cast<std::uint16_t>(0))) ops.ConsumeMailbox();
}

} // namespace kh2coop::inject::collectororder
