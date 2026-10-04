#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace kh2coop::inject::ownedemitter {

// Internal x64 code-generation input, not an execution permit or a native
// lifetime/creator fence. A caller must separately establish every execution
// precondition. No production caller is installed by this component.
// One externally serialized, stable owned activation; not a thread-safe ledger.
// s/w/d/e are actual native stack addresses; gatewayRA/emitterRA/wrapperRA are
// their expected return PCs. continueInput is a supplied 0/1 conditional only.
struct PrimitiveState {
    std::uint64_t live{}, s{}, w{}, d{}, e{}, gatewayRA{}, emitterRA{}, wrapperRA{};
    std::uint64_t controller{}, record{}, actualW{}, actualRA{}, actualC{}, actualR{};
    std::uint64_t entrySerial{}, invocation{}, rawRax{}, stops{};
    std::uint64_t continueInput{}, retired{}, unwound{}, gateS{}, index{}, qualified{}, invalid{};
    std::uint64_t activation{}, phase{}, attempts{}, expectedHeader{}, recordBase{};
    std::uint64_t admissionSerial{}, unknown{}, returned{}, preStops{}, postStops{};
};
// phase: 0 idle, 1 reserved, 2 entered, 3 returned-nonnull, 4 returned-null,
// 5 unknown/denied. The independent unknown bit can coexist with 3/4; neither
// a phase nor a nonnull return establishes actor readiness or permission.
// attempts counts A reservations, not actual wrapper entries. ActualW/RA/C/R
// and entrySerial report the latter. recordBase is retained but not checked by
// these gates. Live identity survives cancellation until real return/unwind;
// lifecycle retirement, overflow handling and a persistent ledger are external.
static_assert(std::is_standard_layout_v<PrimitiveState> &&
              std::is_trivially_copyable_v<PrimitiveState>);
static_assert(sizeof(PrimitiveState) == 280 && alignof(PrimitiveState) == 8);

inline constexpr std::size_t GateCount = 3, CodeCapacity = 512, InstructionCapacity = 96;
enum class Gate : std::uint8_t { A, B, C };
enum class CodeStatus : std::uint8_t {
    Unavailable,
    Complete,
    InvalidStateAddress,
    InvalidNativeLayout,
    InvalidAddressRange,
    InsufficientExtent,
    OverlappingExtents,
    InternalLimit
};

struct CodeRequest {
    std::uint64_t stateAddress{}; // stable, aligned 280-byte POD; not dereferenced here
    std::uint64_t emitterBegin{}, emitterEnd{}, emitterUnwind{};
    std::uint32_t emitterUnwindCapacity{}; // at least the saved 60-byte native info
    std::uint64_t aContinuation{}, bNullContinuation{}, bNonNullContinuation{};
    std::uint64_t cContinuation{}, epilogue{};
    std::array<std::uint64_t, GateCount> islandAddresses{};
    std::array<std::uint32_t, GateCount> islandCapacities{};
    // One caller-chosen 4-byte-aligned RVA base for all returned entries and the
    // full native parent. This is arithmetic only, not a mapped-image proof.
    std::uint64_t unwindAddressBase{}, chainInfoAddress{};
    std::uint32_t chainInfoCapacity{};
};

struct IslandCode {
    std::array<std::uint8_t, CodeCapacity> bytes{};
    std::array<std::uint16_t, InstructionCapacity> instructionOffsets{};
    std::uint16_t size{}, instructionCount{};
    // Offsets identify start of MOV R11,imm64; JMP R11 transfer pairs.
    std::uint16_t replayTail{}, stopTail{}, nullTail{}; // nullTail only used by B
};
struct BranchPlan {
    std::uint64_t address{}, target{};
    std::array<std::uint8_t, 7> expected{}, replacement{};
    std::uint8_t size{};
};
struct RuntimeFunctionDescription {
    std::uint32_t begin{}, end{}, unwindData{};
};
struct CodePlan {
    CodeStatus status = CodeStatus::Unavailable;
    std::array<IslandCode, GateCount> islands{}; // A, B, C
    std::array<BranchPlan, GateCount> branches{};
    std::array<RuntimeFunctionDescription, GateCount> functions{}; // A/B/C, not sorted
    RuntimeFunctionDescription parent{};
    // UNW_FLAG_CHAININFO, zero extra prologue/codes, full native parent entry.
    std::array<std::uint8_t, 16> chainInfo{};
};

// Complete means a finite byte/extent/range plan only. On failure out contains
// only the failure status; no partially usable bytes or runtime entries.
// Caller must verify exact loaded bytes/native parent unwind identity, register
// and retain correct sorted runtime tables before exposure, and handle the real
// concurrent patch lifecycle. This function makes no OS calls or code writes.
// Proven liveness contract: R11 dead at each native site; S+[0,8) and S+8 are
// outgoing shadow scratch. R10 is restored; C restores incoming CF before INC.
// No additional stack frame, helper, native call, or positive authority exists.
CodeStatus BuildCodePlan(const CodeRequest& request, CodePlan& out) noexcept;

} // namespace kh2coop::inject::ownedemitter
