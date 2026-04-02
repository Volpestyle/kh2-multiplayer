#pragma once
// ============================================================================
// InputMailbox — Cross-process shared memory for runtime→inject input delivery
//
// The runtime process creates the shared memory and writes InputFrame data
// received from the network. The inject DLL (inside KH2) opens the same
// shared memory and reads the latest frame for each friend slot.
//
// Naming: "Local\kh2coop_input_<KH2_PID>"
//   - The inject DLL uses GetCurrentProcessId() (it IS KH2)
//   - The runtime uses the KH2 PID it attached to via OpenProcess
//   - This supports multiple KH2 instances on the same machine
//
// Layout (all POD, cache-line aligned):
//   MailboxHeader  { magic, version, runtimePid, flags }
//   MailboxSlot[0] — Friend1 input (64 bytes, cache-line aligned)
//   MailboxSlot[1] — Friend2 input (64 bytes, cache-line aligned)
//
// Synchronization: Seqlock per slot (single writer, single reader).
//   Writer: increment sequence to odd → write data → increment to even.
//   Reader: check sequence is even and changed → read data → verify stable.
//   Torn reads are silently discarded; the reader reuses the previous frame.
//   This is lock-free and wait-free for the reader (game thread never blocks).
//
// Thread safety:
//   - ONE writer (runtime network thread) per slot
//   - ONE reader (KH2 game thread via PerEntityUpdate hook) per slot
//   - Multiple slots are independent (no cross-slot synchronization)
// ============================================================================

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include <Windows.h>

#include "kh2coop/Types.hpp"

namespace kh2coop {

// Shared memory name prefix. Append the KH2 process ID for the full name.
// Example: "Local\kh2coop_input_12345"
static constexpr const char* MAILBOX_NAME_PREFIX = "Local\\kh2coop_input_";

// Magic value for header validation: "KH2C" as little-endian uint32.
static constexpr std::uint32_t MAILBOX_MAGIC   = 0x4332484B;
static constexpr std::uint32_t MAILBOX_VERSION = 1;
static constexpr int           MAILBOX_MAX_SLOTS = 2;

// ============================================================================
// Button bitmask — POD-safe packing for shared memory
//
// Maps 1:1 to InputButtons fields. Used instead of bool members to ensure
// a fixed binary layout across compilation units.
// ============================================================================

enum MailboxButton : std::uint32_t {
    MB_ATTACK   = 1u << 0,
    MB_JUMP     = 1u << 1,
    MB_GUARD    = 1u << 2,
    MB_DODGE    = 1u << 3,
    MB_LOCK_ON  = 1u << 4,
    MB_MAGIC1   = 1u << 5,
    MB_MAGIC2   = 1u << 6,
    MB_SPECIAL1 = 1u << 7,
    MB_SPECIAL2 = 1u << 8,
};

inline std::uint32_t PackButtons(const InputButtons& b) {
    std::uint32_t packed = 0;
    if (b.attack)   packed |= MB_ATTACK;
    if (b.jump)     packed |= MB_JUMP;
    if (b.guard)    packed |= MB_GUARD;
    if (b.dodge)    packed |= MB_DODGE;
    if (b.lockOn)   packed |= MB_LOCK_ON;
    if (b.magic1)   packed |= MB_MAGIC1;
    if (b.magic2)   packed |= MB_MAGIC2;
    if (b.special1) packed |= MB_SPECIAL1;
    if (b.special2) packed |= MB_SPECIAL2;
    return packed;
}

inline InputButtons UnpackButtons(std::uint32_t packed) {
    InputButtons b {};
    b.attack   = (packed & MB_ATTACK)   != 0;
    b.jump     = (packed & MB_JUMP)     != 0;
    b.guard    = (packed & MB_GUARD)    != 0;
    b.dodge    = (packed & MB_DODGE)    != 0;
    b.lockOn   = (packed & MB_LOCK_ON)  != 0;
    b.magic1   = (packed & MB_MAGIC1)   != 0;
    b.magic2   = (packed & MB_MAGIC2)   != 0;
    b.special1 = (packed & MB_SPECIAL1) != 0;
    b.special2 = (packed & MB_SPECIAL2) != 0;
    return b;
}

// ============================================================================
// Shared memory structures — POD with fixed binary layout
//
// Synchronization rationale: these structs live in a Windows named shared
// memory region (CreateFileMapping) mapped into two separate processes — the
// runtime (writer) and KH2+inject DLL (reader). We use a seqlock pattern
// with volatile fields and std::atomic_thread_fence rather than std::atomic
// because std::atomic's memory layout is formally implementation-defined
// (though identical to the underlying type on MSVC/x64 in practice).
// On x86-64 (TSO), stores are already ordered w.r.t. other stores and loads
// w.r.t. other loads; the fences serve as compiler barriers to prevent
// reordering across the sequence read/write boundaries.
// ============================================================================

// One slot per friend. Cache-line aligned to prevent false sharing between
// Friend1 and Friend2 data (they may be written/read independently).
struct alignas(64) MailboxSlot {
    volatile std::uint32_t sequence;       // Seqlock: odd = write in progress
    float    leftStickX;                   // -1.0 .. +1.0
    float    leftStickY;                   // -1.0 .. +1.0
    float    rightStickX;                  // -1.0 .. +1.0
    float    rightStickY;                  // -1.0 .. +1.0
    std::uint32_t buttons;                 // Packed MailboxButton bitmask
    std::uint32_t ownedActorId;            // Network actor ID of the input source
    std::uint32_t requestedTargetId;       // Lock-on target
    std::uint64_t clientTimeMs;            // Sender timestamp
    std::uint8_t  _pad[16];               // Pad to exactly 64 bytes
};

static_assert(sizeof(MailboxSlot) == 64,
    "MailboxSlot must be exactly one cache line (64 bytes)");

// File header — sits at offset 0 in the shared memory region.
struct MailboxHeader {
    std::uint32_t magic;                   // MAILBOX_MAGIC
    std::uint32_t version;                 // MAILBOX_VERSION
    std::uint32_t runtimePid;              // PID of the runtime process
    std::uint32_t flags;                   // Reserved (0)
    // Compiler inserts 48 bytes of padding here to align slots to 64
    MailboxSlot   slots[MAILBOX_MAX_SLOTS];
};

static constexpr std::size_t MAILBOX_SIZE = sizeof(MailboxHeader);

// Validate that the compiler placed slots[] at the expected cache-line boundary.
// The header fields (magic + version + runtimePid + flags) total 16 bytes;
// alignas(64) on MailboxSlot forces 48 bytes of padding before slots[0].
static_assert(offsetof(MailboxHeader, slots) == 64,
    "MailboxHeader::slots must start at offset 64 (cache-line aligned)");

// ============================================================================
// MailboxReadResult — returned by the reader on successful reads
// ============================================================================

struct MailboxReadResult {
    float         leftStickX;
    float         leftStickY;
    float         rightStickX;
    float         rightStickY;
    std::uint32_t buttons;                 // Packed MailboxButton bitmask
    std::uint32_t ownedActorId;
    std::uint32_t requestedTargetId;
    std::uint64_t clientTimeMs;
};

// ============================================================================
// MailboxWriter — used by the runtime process to publish InputFrames
//
// Usage:
//   MailboxWriter writer;
//   writer.Create(kh2ProcessId);
//   // ... in network receive callback:
//   writer.WriteSlot(0, incomingInputFrame);  // Friend1
//   // ... on shutdown:
//   writer.Close();
// ============================================================================

class MailboxWriter {
public:
    MailboxWriter() = default;
    ~MailboxWriter() { Close(); }

    // Non-copyable (owns OS handles)
    MailboxWriter(const MailboxWriter&) = delete;
    MailboxWriter& operator=(const MailboxWriter&) = delete;

    // Create the shared memory region for the given KH2 process.
    // Returns true on success. The runtime should call this once during init.
    bool Create(DWORD kh2Pid) {
        if (view_) return true;  // Already open

        char name[128];
        std::snprintf(name, sizeof(name), "%s%lu", MAILBOX_NAME_PREFIX,
                      static_cast<unsigned long>(kh2Pid));

        hMapping_ = CreateFileMappingA(
            INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
            0, static_cast<DWORD>(MAILBOX_SIZE), name);
        if (!hMapping_) return false;

        view_ = static_cast<MailboxHeader*>(
            MapViewOfFile(hMapping_, FILE_MAP_ALL_ACCESS, 0, 0, MAILBOX_SIZE));
        if (!view_) {
            CloseHandle(hMapping_);
            hMapping_ = nullptr;
            return false;
        }

        // Zero-init and write header
        std::memset(view_, 0, MAILBOX_SIZE);
        view_->magic      = MAILBOX_MAGIC;
        view_->version    = MAILBOX_VERSION;
        view_->runtimePid = GetCurrentProcessId();
        view_->flags      = 0;

        return true;
    }

    // Write an InputFrame to a friend slot (0 = Friend1, 1 = Friend2).
    // Uses seqlock protocol: odd sequence signals write-in-progress.
    void WriteSlot(int slotIndex, const InputFrame& frame) {
        if (!view_ || slotIndex < 0 || slotIndex >= MAILBOX_MAX_SLOTS) return;

        volatile MailboxSlot* slot = &view_->slots[slotIndex];

        // Read current sequence and advance to odd (write-in-progress)
        std::uint32_t seq = slot->sequence;
        slot->sequence = seq + 1;
        std::atomic_thread_fence(std::memory_order_release);

        // Write payload
        slot->leftStickX       = frame.leftStickX;
        slot->leftStickY       = frame.leftStickY;
        slot->rightStickX      = frame.rightStickX;
        slot->rightStickY      = frame.rightStickY;
        slot->buttons           = PackButtons(frame.buttons);
        slot->ownedActorId      = frame.ownedActorId;
        slot->requestedTargetId = frame.requestedTargetId;
        slot->clientTimeMs      = frame.clientTimeMs;

        // Advance to even (write complete)
        std::atomic_thread_fence(std::memory_order_release);
        slot->sequence = seq + 2;
    }

    // Unmap and close the shared memory.
    void Close() {
        if (view_) {
            UnmapViewOfFile(view_);
            view_ = nullptr;
        }
        if (hMapping_) {
            CloseHandle(hMapping_);
            hMapping_ = nullptr;
        }
    }

    bool IsOpen() const { return view_ != nullptr; }

private:
    HANDLE          hMapping_ = nullptr;
    MailboxHeader*  view_     = nullptr;
};

// ============================================================================
// MailboxReader — used by the inject DLL to consume InputFrames
//
// Usage:
//   MailboxReader reader;
//   reader.Open();   // uses GetCurrentProcessId() — we ARE KH2
//   // ... per frame, for each friend slot:
//   MailboxReadResult result;
//   if (reader.TryReadSlot(0, result)) {
//       // New consistent frame available — use result.leftStickX, etc.
//   } else {
//       // No new data or torn read — reuse previous frame
//   }
// ============================================================================

class MailboxReader {
public:
    MailboxReader() = default;
    ~MailboxReader() { Close(); }

    // Non-copyable
    MailboxReader(const MailboxReader&) = delete;
    MailboxReader& operator=(const MailboxReader&) = delete;

    // Open existing shared memory created by the runtime.
    // The inject DLL uses its own PID (it IS the KH2 process).
    bool Open() {
        if (view_) return true;  // Already open

        DWORD myPid = GetCurrentProcessId();
        char name[128];
        std::snprintf(name, sizeof(name), "%s%lu", MAILBOX_NAME_PREFIX,
                      static_cast<unsigned long>(myPid));

        hMapping_ = OpenFileMappingA(FILE_MAP_READ, FALSE, name);
        if (!hMapping_) return false;

        auto* mapped = static_cast<const MailboxHeader*>(
            MapViewOfFile(hMapping_, FILE_MAP_READ, 0, 0, MAILBOX_SIZE));
        if (!mapped) {
            CloseHandle(hMapping_);
            hMapping_ = nullptr;
            return false;
        }

        // Validate header
        if (mapped->magic != MAILBOX_MAGIC ||
            mapped->version != MAILBOX_VERSION) {
            UnmapViewOfFile(mapped);
            CloseHandle(hMapping_);
            hMapping_ = nullptr;
            return false;
        }

        view_ = mapped;
        return true;
    }

    // Try to read a slot. Returns true if a NEW, CONSISTENT frame was read.
    //
    // Failure modes (all return false, caller reuses previous data):
    //   - Mailbox not open
    //   - Slot index out of range
    //   - Write in progress (sequence is odd)
    //   - No new data (sequence unchanged since last read)
    //   - Torn read (sequence changed between start and end of read)
    bool TryReadSlot(int slotIndex, MailboxReadResult& out) {
        if (!view_ || slotIndex < 0 || slotIndex >= MAILBOX_MAX_SLOTS)
            return false;

        volatile const MailboxSlot* slot = &view_->slots[slotIndex];

        // Snapshot sequence
        std::uint32_t s1 = slot->sequence;
        if (s1 & 1u) return false;                     // Write in progress
        if (s1 == lastSeq_[slotIndex]) return false;    // No new data

        std::atomic_thread_fence(std::memory_order_acquire);

        // Read payload
        out.leftStickX       = slot->leftStickX;
        out.leftStickY       = slot->leftStickY;
        out.rightStickX      = slot->rightStickX;
        out.rightStickY      = slot->rightStickY;
        out.buttons           = slot->buttons;
        out.ownedActorId      = slot->ownedActorId;
        out.requestedTargetId = slot->requestedTargetId;
        out.clientTimeMs      = slot->clientTimeMs;

        std::atomic_thread_fence(std::memory_order_acquire);

        // Verify consistency
        std::uint32_t s2 = slot->sequence;
        if (s1 != s2) return false;  // Torn read — discard

        lastSeq_[slotIndex] = s1;
        return true;
    }

    // Check if the mailbox is open and mapped.
    bool IsOpen() const { return view_ != nullptr; }

    // Get the runtime's PID for liveness checking.
    DWORD RuntimePid() const {
        return view_ ? view_->runtimePid : 0;
    }

    // Unmap the shared memory.
    void Close() {
        if (view_) {
            UnmapViewOfFile(view_);
            view_ = nullptr;
        }
        if (hMapping_) {
            CloseHandle(hMapping_);
            hMapping_ = nullptr;
        }
        lastSeq_[0] = 0;
        lastSeq_[1] = 0;
    }

private:
    HANDLE                   hMapping_ = nullptr;
    const MailboxHeader*     view_     = nullptr;
    std::uint32_t            lastSeq_[MAILBOX_MAX_SLOTS] = {};
};

} // namespace kh2coop
