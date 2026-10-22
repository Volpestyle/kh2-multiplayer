#include "EventHoldNative.hpp"
#include "EventHoldInputState.hpp"
#include "NativeSpawnController.hpp"
#include "EventStartMirror.hpp"
#include "kh2coop/EventScenes.hpp"
#include "kh2coop/KH2Offsets.hpp"
#include <atomic>
#include <array>
#include <cstring>

namespace kh2coop::inject::eventholdnative {
namespace {
eventhold::Channel channel;
std::atomic<bool> enabled {false}, inputHeld {false};
std::atomic<bool> inputRejected {false};
std::atomic_flag inputEntry = ATOMIC_FLAG_INIT;
uintptr_t base = 0;
LogFn log = nullptr;
InputState input;
eventhold::Scope inputScope {}, ownerScope {};
eventhold::Command release {};
std::array<eventhold::Command, 32> consumed {};
std::size_t consumedCount = 0;
std::uint64_t ownerOrdinal = 0, loggedOrdinal = 0;
std::uint32_t loggedFlags = UINT32_MAX;

struct NativeInputSample {
    InputState::Sample value {};
    uintptr_t context = 0;
    std::int32_t eventState = -1, frozen = -1;
    std::uint32_t blockers = UINT32_MAX;
    std::uint8_t inField = 0;
};
bool ReadInput(void* ptr, NativeInputSample& out) noexcept {
    // This is only the actual input collector's supplied instance. No retained
    // native pointer or plain world/warp state is read by the input consumer.
    __try {
        if (!ptr || *reinterpret_cast<uintptr_t*>(base + offsets::INPUT_STRUCT_PTR) != reinterpret_cast<uintptr_t>(ptr)) return false;
        out.value.menu = *reinterpret_cast<std::uint8_t*>(base + offsets::OPEN_MENU);
        out.eventState = *reinterpret_cast<std::int32_t*>(base + offsets::CUTSCENE_STATE);
        out.context = *reinterpret_cast<uintptr_t*>(base + offsets::EVENT_CONTEXT);
        out.frozen = *reinterpret_cast<std::int32_t*>(base + offsets::CONTROLLABLE);
        out.blockers = *reinterpret_cast<std::uint32_t*>(base + offsets::PAUSE_STATUS);
        out.inField = *reinterpret_cast<std::uint8_t*>(base + offsets::IN_FIELD);
        return *reinterpret_cast<uintptr_t*>(base + offsets::INPUT_STRUCT_PTR) == reinterpret_cast<uintptr_t>(ptr);
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool Overlay(void* ptr, std::uint16_t buttons) noexcept {
    __try {
        if (*reinterpret_cast<uintptr_t*>(base + offsets::INPUT_STRUCT_PTR) != reinterpret_cast<uintptr_t>(ptr)) return false;
        auto* raw = reinterpret_cast<std::uint8_t*>(ptr) + offsets::input::RAW_SLOT0;
        *reinterpret_cast<std::uint16_t*>(raw + offsets::input::BUTTONS) = buttons;
        raw[offsets::input::LSTICK_X] = raw[offsets::input::LSTICK_Y] = 128;
        raw[offsets::input::RSTICK_X] = raw[offsets::input::RSTICK_Y] = 128;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool RecoveryInputReady() noexcept {
    __try {
        NativeInputSample native;
        auto* collector = reinterpret_cast<void*>(*reinterpret_cast<uintptr_t*>(base + offsets::INPUT_STRUCT_PTR));
        return Enabled() && !input.Holding() && ReadInput(collector, native) &&
            native.inField && native.value.menu == 255 && native.eventState == 0 &&
            native.context == 0 && native.frozen == 0 && native.blockers == 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
// Caller owns inputEntry. Observe a rejection that arrived during this callback
// before a replay/overlay; rejected foreign callers never access the mapping.
bool ConsumeInputRejection() noexcept {
    if (!inputRejected.exchange(false, std::memory_order_acq_rel)) return false;
    channel.Invalidate(eventhold::Abort::Unsupported);
    input.Fail();
    return true;
}
bool SameFact(eventhold::Command a, eventhold::Command b) {
    a.ordinal = b.ordinal = 0;
    return a == b;
}
}
bool Enabled() noexcept { return enabled.load(std::memory_order_acquire); }
eventhold::Channel& Control() noexcept { return channel; }
void Install(uintptr_t exeBase, LogFn sink) {
    char flag[2] {};
    if (GetEnvironmentVariableA("KH2COOP_EVENT_HOLD_CONTROL", flag, sizeof(flag)) != 1 || flag[0] != '1') return;
    base = exeBase; log = sink;
    if (!channel.Open(GetCurrentProcessId())) {
        if (log) log("[event-control] native setup failed; input control disabled");
        return;
    }
    enabled.store(true, std::memory_order_release);
    if (log) log("[event-control] native configured=1 populatedRooms=1 noScriptFreeze=1");
}
void Disable() noexcept {
    enabled.store(false, std::memory_order_release);
    // Serialize against the owner-only mapping handoff. If input is in flight,
    // it sees enabled=false; never unmap or wait inside hook retirement.
    if (!inputEntry.test_and_set(std::memory_order_acquire)) {
        channel.Invalidate(eventhold::Abort::Shutdown);
        inputEntry.clear(std::memory_order_release);
    }
    // Keep the view alive until DLL lifetime ends. A callback already in flight
    // can observe invalidation without racing an unmap during hook retirement.
}
bool EnterInput() noexcept {
    if (!Enabled()) return false;
    // VUH-2359: a New Game session binds the channel during the title/opening, before any actor
    // update has registered the game thread. That is not a foreign caller: take no input control
    // yet rather than aborting the channel for the whole session (prologue friends never paused).
    if (!spawncontroller::DiagnosticGameThreadKnown()) return false;
    if (!spawncontroller::IsDiagnosticGameThread()) {
        // Rejected readers must not touch a view the owner can now replace.
        // The next registered collector latches the fault under its guard.
        inputRejected.store(true, std::memory_order_release);
        return false;
    }
    if (inputEntry.test_and_set(std::memory_order_acquire)) {
        inputRejected.store(true, std::memory_order_release);
        return false;
    }
    if (inputRejected.exchange(false, std::memory_order_acq_rel)) {
        channel.Invalidate(eventhold::Abort::Unsupported);
        inputEntry.clear(std::memory_order_release);
        return false;
    }
    return true;
}
void AbortInput() noexcept { channel.Invalidate(eventhold::Abort::Unsupported); }
void LeaveInput() noexcept { inputEntry.clear(std::memory_order_release); }
bool HoldingInput() noexcept { return Enabled() && inputHeld.load(std::memory_order_acquire); }
bool LifetimeHeartbeatExpired() noexcept {
    return Enabled() && channel.Reason() == eventhold::Abort::HeartbeatExpired;
}
void RetireOwner() noexcept {
    if (eventhold::ValidScope(ownerScope)) channel.Invalidate(eventhold::Abort::BindingReset);
    ownerScope = {}; consumedCount = 0; ownerOrdinal = 0;
}
bool BeginRecoveredLifetime(const eventhold::Scope& scope, std::uint32_t epoch) {
    if (!Enabled() || !epoch || !eventhold::ValidScope(scope) ||
        !spawncontroller::IsDiagnosticGameThread()) return false;
    eventhold::Scope old {};
    if (!channel.BoundScope(old) || channel.Reason() != eventhold::Abort::HeartbeatExpired ||
        std::memcmp(old.session, scope.session, sizeof(old.session)) != 0 ||
        old.hostConnection != scope.hostConnection || old.slot != scope.slot ||
        old.selfConnection == scope.selfConnection || scope.generation <= old.generation) return false;
    if (inputEntry.test_and_set(std::memory_order_acquire)) return false;
    // The raw callback and this owner are the registered game thread. Disable
    // also takes this guard; a failed open leaves the old view untouched.
    if (!RecoveryInputReady() || inputRejected.load(std::memory_order_acquire)) {
        inputEntry.clear(std::memory_order_release); return false;
    }
    eventhold::Channel next;
    if (!next.OpenScoped(GetCurrentProcessId(), scope) || !Enabled() ||
        inputRejected.load(std::memory_order_acquire)) {
        inputEntry.clear(std::memory_order_release); return false;
    }
    channel.Swap(next);
    input = InputState {};
    inputScope = {}; ownerScope = scope; release = {};
    consumedCount = 0; ownerOrdinal = 0;
    loggedOrdinal = 0; loggedFlags = UINT32_MAX;
    inputHeld.store(false, std::memory_order_release);
    // next owns the old view. Close it before letting another local reader in;
    // the runtime's independent old view still contains the immutable abort.
    next.Close();
    inputEntry.clear(std::memory_order_release);
    if (log) log("[event-control] recovered lifetime generation=%u self=%llu delivery=%llu epoch=%u resetAckRequired=1",
        scope.generation, static_cast<unsigned long long>(scope.selfConnection),
        static_cast<unsigned long long>(scope.targetDelivery), epoch);
    return true;
}
void Observe(const eventhold::Scope& scope, eventhold::Kind kind, std::uint64_t source,
             std::uint32_t epoch, const RoomTransition* room, std::uint16_t eventProgram,
             const EventStart* start) {
    if (!Enabled() || !eventhold::ValidScope(scope) || !source || !epoch) return;
    if (eventhold::ValidScope(ownerScope) && ownerScope != scope) { RetireOwner(); return; }
    ownerScope = scope;
    eventhold::Command fact {}; fact.scope = scope; fact.kind = kind;
    fact.hostSourceSerial = source; fact.epoch = epoch; fact.eventProgram = eventProgram;
    if (room) {
        fact.world = room->worldId; fact.room = room->roomId; fact.door = room->door;
        fact.map = room->mapProgram; fact.battle = room->battleProgram;
    }
    if (start) {
        fact.startWorld = start->worldId; fact.startRoom = start->roomId; fact.startId = start->id;
        fact.startFlags = start->flags; fact.startParams = start->params;
    }
    for (std::size_t i = 0; i < consumedCount; ++i) if (SameFact(consumed[i], fact)) return;
    if (consumedCount == consumed.size()) { channel.Invalidate(eventhold::Abort::Overflow); return; }
    consumed[consumedCount++] = fact;
}
void OwnerAck(const eventhold::Scope& scope, std::uint32_t epoch, bool eligible, bool converged) {
    if (!Enabled() || !epoch || scope != ownerScope || !eventhold::ValidScope(scope)) return;
    eventhold::Scope bound {};
    if (!channel.BoundScope(bound) || bound != scope) return;
    eventhold::Command processed {};
    if (!ownerOrdinal) {
        if (!channel.ReadPublished(1, processed) || processed.kind != eventhold::Kind::Reset || processed.scope != scope) return;
        ownerOrdinal = 1;
    }
    // World consumption can beat the runtime's separate control publication.
    // Retained validated facts allow correlation on a later ordinary frame.
    for (unsigned n = 0; n < 8; ++n) {
        eventhold::Command next {};
        if (!channel.ReadPublished(ownerOrdinal+1, next)) break;
        bool found = false;
        for (std::size_t i = 0; i < consumedCount; ++i) found |= SameFact(next, consumed[i]);
        if (!found) break;
        ++ownerOrdinal;
    }
    if (!channel.ReadPublished(ownerOrdinal, processed)) return;
    eventhold::Ack ack {}; ack.scope = scope; ack.processedOrdinal = ownerOrdinal;
    ack.processedHostSourceSerial = processed.hostSourceSerial;
    ack.epoch = processed.kind == eventhold::Kind::Reset ? epoch : processed.epoch;
    if (epoch == ack.epoch && eligible) ack.flags |= eventhold::LiveEligible;
    if (epoch == ack.epoch && converged) ack.flags |= eventhold::SafeConverged;
    if (channel.PublishAck(ack) && (loggedOrdinal != ownerOrdinal || loggedFlags != ack.flags)) {
        if (log) log("[event-control] owner ack ordinal=%llu source=%llu epoch=%u flags=%u tid=%lu generation=%u delivery=%llu",
            static_cast<unsigned long long>(ownerOrdinal), static_cast<unsigned long long>(ack.processedHostSourceSerial),
            ack.epoch, ack.flags, GetCurrentThreadId(), scope.generation, static_cast<unsigned long long>(scope.targetDelivery));
        loggedOrdinal = ownerOrdinal; loggedFlags = ack.flags;
    }
}
bool ApplyInput(void* inputStruct) {
    if (!Enabled()) return false;
    ConsumeInputRejection();
    const bool wasHeld = input.Holding();
    const auto oldPhase = input.Current();
    NativeInputSample native;
    if (!spawncontroller::IsDiagnosticGameThread() || !ReadInput(inputStruct, native)) {
        channel.Invalidate(eventhold::Abort::Unsupported); input.Fail();
    }
    native.value.now = GetTickCount64();
    native.value.healthy = Enabled() && channel.Healthy(native.value.now);
    native.value.safeToPause = native.inField && native.value.menu == 255 && native.eventState == 0 &&
        native.context == 0 && native.frozen == 0 && native.blockers == 0;
    eventhold::Command command {};
    // One record per actual callback: never collapse an acquire into a release.
    const auto result = channel.Read(native.value.now, command);
    if (result == eventhold::ReadResult::Record) {
        if (command.kind == eventhold::Kind::Reset) inputScope = command.scope;
        else if (command.scope != inputScope) { channel.Invalidate(eventhold::Abort::WrongScope); input.Fail(); }
        else if (command.kind == eventhold::Kind::Acquire) {
            // VUH-2268: a hold opens the pause menu only for a client the owner
            // reports converged in the host's current room (enemies allowed).
            // Anything else skips this one hold and keeps the channel, so a
            // client elsewhere keeps control instead of pausing in that room.
            release = {};
            if (input.Supersede() && log)
                log("[event-control] previous hold superseded by acquire epoch=%u", command.epoch);
            eventhold::Ack ack {};
            const bool hostRoom = channel.ReadAck(ack) && ack.scope == command.scope && ack.epoch == command.epoch &&
                ack.flags == (eventhold::SafeConverged | eventhold::LiveEligible);
            // VUH-2359: the host's checked in-field event plays natively here instead of the pause; the
            // friend leaves it like a scene (its own exit blocked, the host's next room taken).
            EventStart start {};
            start.worldId = command.startWorld; start.roomId = command.startRoom; start.id = command.startId;
            start.flags = static_cast<std::uint8_t>(command.startFlags); start.params = command.startParams;
            const bool mirrored = hostRoom && command.startId && command.startFlags <= 0xFF && !input.Holding() &&
                native.value.healthy && native.value.safeToPause && !ConsumeInputRejection() &&
                channel.Healthy(GetTickCount64()) && eventstartmirror::Replay(start, command.eventProgram);
            if (mirrored) {
                if (log) log("[event-mirror] client plays the host's in-field event epoch=%u id=%X", command.epoch, command.startId);
            } else if ((!hostRoom || !input.Acquire(native.value)) && log)
                log("[event-control] hold skipped epoch=%u hostRoom=%u ackEpoch=%u ackFlags=%u menu=%u safe=%u holding=%u",
                    command.epoch, hostRoom ? 1u : 0u, ack.epoch, ack.flags, native.value.menu,
                    native.value.safeToPause ? 1u : 0u, input.Holding() ? 1u : 0u);
        } else if (command.kind == eventhold::Kind::Transition || command.kind == eventhold::Kind::Release) {
            // Ordinary host-led room changes also use this channel. Between
            // events they service the world without opening/closing a pause.
            // A release with no hold (a skipped one) has nothing to give back.
            if (input.Holding()) {
                // VUH-2359: into a qualified scene the friend follows the host natively, so the hold
                // ends once its pause has closed instead of waiting for the host's release.
                const bool scene = command.kind == eventhold::Kind::Transition &&
                    eventscenes::Qualified(command.world, command.room, command.door, command.map, command.battle,
                                           command.eventProgram);
                if (scene) {
                    input.FollowScene();
                    if (log) log("[event-scene] client follows the host's scene epoch=%u at=%02X/%02X evt=%u",
                                 command.epoch, command.world, command.room, command.eventProgram);
                } else {
                    input.Wake();
                }
                if (command.kind == eventhold::Kind::Release) { release = command; input.ReleaseReceived(native.value.now); }
            }
        }
        if (log) log("[event-control] input receipt ordinal=%llu source=%llu kind=%u epoch=%u tid=%lu",
            static_cast<unsigned long long>(command.ordinal), static_cast<unsigned long long>(command.hostSourceSerial),
            static_cast<unsigned>(command.kind), command.epoch, GetCurrentThreadId());
    }
    eventhold::Ack ack {};
    native.value.ownerReleased = release.ordinal && channel.ReadAck(ack) && ack.scope == release.scope &&
        ack.processedOrdinal == release.ordinal && ack.processedHostSourceSerial == release.hostSourceSerial &&
        ack.epoch == release.epoch && ack.flags == (eventhold::SafeConverged | eventhold::LiveEligible);
    native.value.healthy = !ConsumeInputRejection() && Enabled() && channel.Healthy(GetTickCount64());
    auto output = input.Tick(native.value);
    const auto buttons = static_cast<std::uint16_t>(output.buttons | (output.start ? InputState::StartButton : 0));
    if (output.neutral && (ConsumeInputRejection() || !channel.Healthy(GetTickCount64()) || !Enabled() || !Overlay(inputStruct, buttons))) {
        channel.Invalidate(eventhold::Abort::Unsupported); input.Fail(); output.failed = true;
    }
    inputHeld.store(input.Holding(), std::memory_order_release);
    // VUH-2537: say why, once: the player has the input back and the next hold and room follow once
    // the menu is closed.
    if (output.stuck && log)
        log("[event-control] hold could not close the pause: menu %u still open after %u unwind presses "
            "(Start, then Circle/Cross/Start); input returned to the player, later holds and the host's "
            "next room resume once the menu closes", native.value.menu, input.Unwound());
    if (output.timedOut && log)
        log("[event-control] owner did not converge %llu ms after release epoch=%u; input returned",
            static_cast<unsigned long long>(InputState::ReleaseTimeoutMs), release.epoch);
    if (log && (oldPhase != input.Current() || buttons || output.released))
        log("[event-control] input phase=%u menu=%u neutral=%u start=%u failed=%u released=%u ordinal=%llu tid=%lu abort=%lu buttons=%04X unwound=%u",
            static_cast<unsigned>(input.Current()), native.value.menu, output.neutral ? 1u : 0u, output.start ? 1u : 0u,
            output.failed ? 1u : 0u, output.released ? 1u : 0u, static_cast<unsigned long long>(release.ordinal),
            GetCurrentThreadId(), static_cast<unsigned long>(channel.Reason()), buttons, input.Unwound());
    return wasHeld || output.neutral || output.released;
}
} // namespace kh2coop::inject::eventholdnative
