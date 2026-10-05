#include "EventHoldNative.hpp"
#include "EventHoldInputState.hpp"
#include "NativeSpawnController.hpp"
#include "kh2coop/KH2Offsets.hpp"
#include <atomic>
#include <array>
#include <cstring>

namespace kh2coop::inject::eventholdnative {
namespace {
eventhold::Channel channel;
std::atomic<bool> enabled {false}, inputHeld {false};
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
bool Overlay(void* ptr, bool start) noexcept {
    __try {
        if (*reinterpret_cast<uintptr_t*>(base + offsets::INPUT_STRUCT_PTR) != reinterpret_cast<uintptr_t>(ptr)) return false;
        auto* raw = reinterpret_cast<std::uint8_t*>(ptr) + offsets::input::RAW_SLOT0;
        *reinterpret_cast<std::uint16_t*>(raw + offsets::input::BUTTONS) = start ? 0x0008 : 0;
        raw[offsets::input::LSTICK_X] = raw[offsets::input::LSTICK_Y] = 128;
        raw[offsets::input::RSTICK_X] = raw[offsets::input::RSTICK_Y] = 128;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
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
    if (log) log("[event-control] native configured=1 firstSliceEmptyRoomsOnly=1 noScriptFreeze=1");
}
void Disable() noexcept {
    enabled.store(false, std::memory_order_release);
    channel.Invalidate(eventhold::Abort::Shutdown);
    // Keep the view alive until DLL lifetime ends. A callback already in flight
    // can observe invalidation without racing an unmap during hook retirement.
}
bool EnterInput() noexcept {
    if (!Enabled()) return false;
    if (!spawncontroller::IsDiagnosticGameThread()) {
        eventhold::Scope bound {};
        if (channel.BoundScope(bound)) channel.Invalidate(eventhold::Abort::Unsupported);
        return false;
    }
    if (inputEntry.test_and_set(std::memory_order_acquire)) {
        channel.Invalidate(eventhold::Abort::Unsupported);
        return false;
    }
    return true;
}
void AbortInput() noexcept { channel.Invalidate(eventhold::Abort::Unsupported); }
void LeaveInput() noexcept { inputEntry.clear(std::memory_order_release); }
bool HoldingInput() noexcept { return Enabled() && inputHeld.load(std::memory_order_acquire); }
void RetireOwner() noexcept {
    if (eventhold::ValidScope(ownerScope)) channel.Invalidate(eventhold::Abort::BindingReset);
    ownerScope = {}; consumedCount = 0; ownerOrdinal = 0;
}
void Observe(const eventhold::Scope& scope, eventhold::Kind kind, std::uint64_t source,
             std::uint32_t epoch, const RoomTransition* room, std::uint16_t eventProgram) {
    if (!Enabled() || !eventhold::ValidScope(scope) || !source || !epoch) return;
    if (eventhold::ValidScope(ownerScope) && ownerScope != scope) { RetireOwner(); return; }
    ownerScope = scope;
    eventhold::Command fact {}; fact.scope = scope; fact.kind = kind;
    fact.hostSourceSerial = source; fact.epoch = epoch; fact.eventProgram = eventProgram;
    if (room) {
        fact.world = room->worldId; fact.room = room->roomId; fact.door = room->door;
        fact.map = room->mapProgram; fact.battle = room->battleProgram;
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
            eventhold::Ack ack {};
            if (!channel.ReadAck(ack) || ack.scope != command.scope || ack.epoch != command.epoch ||
                ack.flags != (eventhold::SafeConverged | eventhold::LiveEligible) || !input.Acquire(native.value)) {
                channel.Invalidate(eventhold::Abort::Unsupported); input.Fail();
            }
        } else if (command.kind == eventhold::Kind::Transition || command.kind == eventhold::Kind::Release) {
            if (!input.Holding()) { channel.Invalidate(eventhold::Abort::InvalidOrder); input.Fail(); }
            else { input.Wake(); if (command.kind == eventhold::Kind::Release) release = command; }
        }
        if (log) log("[event-control] input receipt ordinal=%llu source=%llu kind=%u epoch=%u tid=%lu",
            static_cast<unsigned long long>(command.ordinal), static_cast<unsigned long long>(command.hostSourceSerial),
            static_cast<unsigned>(command.kind), command.epoch, GetCurrentThreadId());
    }
    eventhold::Ack ack {};
    native.value.ownerReleased = release.ordinal && channel.ReadAck(ack) && ack.scope == release.scope &&
        ack.processedOrdinal == release.ordinal && ack.processedHostSourceSerial == release.hostSourceSerial &&
        ack.epoch == release.epoch && ack.flags == (eventhold::SafeConverged | eventhold::LiveEligible);
    native.value.healthy = Enabled() && channel.Healthy(GetTickCount64());
    auto output = input.Tick(native.value);
    if (output.neutral && (!channel.Healthy(GetTickCount64()) || !Enabled() || !Overlay(inputStruct, output.start))) {
        channel.Invalidate(eventhold::Abort::Unsupported); input.Fail(); output.failed = true;
    }
    inputHeld.store(input.Holding(), std::memory_order_release);
    if (log && (oldPhase != input.Current() || output.start || output.released))
        log("[event-control] input phase=%u menu=%u neutral=%u start=%u failed=%u released=%u ordinal=%llu tid=%lu abort=%lu",
            static_cast<unsigned>(input.Current()), native.value.menu, output.neutral ? 1u : 0u, output.start ? 1u : 0u,
            output.failed ? 1u : 0u, output.released ? 1u : 0u, static_cast<unsigned long long>(release.ordinal),
            GetCurrentThreadId(), static_cast<unsigned long>(channel.Reason()));
    return wasHeld || output.neutral || output.released;
}
} // namespace kh2coop::inject::eventholdnative
