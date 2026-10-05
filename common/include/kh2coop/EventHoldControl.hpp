#pragma once
// Windows-only, separate from WorldBridge. One runtime producer, one input
// consumer, one native-owner ACK writer. No world consumption or game writes.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <type_traits>

namespace kh2coop::eventhold {
inline constexpr std::uint32_t Version = 1;
inline constexpr std::uint32_t Capacity = 32;
inline constexpr std::size_t SourceCapacity = 128;
inline constexpr std::uint64_t HeartbeatTimeoutMs = 1500;

struct Scope {
    char session[32] {}; // exact lowercase hexadecimal bytes, never a hash
    std::uint32_t generation {}, slot {};
    std::uint64_t hostConnection {}, selfConnection {}, hostDelivery {}, targetDelivery {};
    bool operator==(const Scope&) const = default;
};
inline bool ValidIdentity(const Scope& s) noexcept {
    for (const char ch : s.session)
        if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f'))) return false;
    return s.slot >= 1 && s.slot <= 2 && s.hostConnection &&
        s.selfConnection && s.hostConnection != s.selfConnection && s.hostDelivery && s.targetDelivery;
}
inline bool ValidScope(const Scope& s) noexcept { return s.generation && ValidIdentity(s); }
enum class Kind : std::uint32_t { Reset = 1, Acquire, Release, Transition };
enum class Abort : LONG { None, BindingReset, Shutdown, Overflow, HeartbeatExpired,
    WrongScope, Replay, InvalidOrder, Unsupported, SourceLimit, ConsumerOrder };
enum class ReadResult { Unavailable, Empty, Record, Aborted };
enum AckFlags : std::uint32_t { SafeConverged = 1, LiveEligible = 2 };
struct Command {
    Scope scope {};
    std::uint64_t ordinal {}, hostSourceSerial {};
    Kind kind {Kind::Reset};
    std::uint32_t epoch {};
    std::uint16_t world {}, room {}, door {}, map {}, battle {}, eventProgram {};
    bool operator==(const Command&) const = default;
};
struct Ack {
    Scope scope {};
    std::uint64_t processedOrdinal {}, processedHostSourceSerial {};
    std::uint32_t epoch {}, flags {};
};
static_assert(std::is_trivially_copyable_v<Command> && std::is_standard_layout_v<Command>);
static_assert(std::is_trivially_copyable_v<Ack> && sizeof(Ack) % 8 == 0);
static_assert(sizeof(Scope) == 72 && sizeof(Command) == 112 && sizeof(Ack) == 96,
              "versioned Windows control ABI changed");

class Channel {
    struct alignas(8) Shared {
        volatile LONG init {}, version {}, bytes {}, producer {}, bound {}, abort {};
        alignas(8) volatile LONG64 heartbeat {}, timeout {};
        Scope scope {}; // immutable after release-publication of bound
        alignas(8) volatile LONG64 written {}, consumed {};
        std::array<Command, Capacity> fifo {};
        // Immutable bounded receipts let the native owner correlate an original
        // envelope source with a local ordinal without consuming the input FIFO.
        std::array<Command, SourceCapacity+2> history {};
        alignas(8) volatile LONG64 ackSequence {};
        std::array<LONG64, sizeof(Ack)/8> ackWords {};
    };
    static LONG Load(volatile LONG& v) noexcept { return InterlockedCompareExchange(&v, 0, 0); }
    static LONG64 Load(volatile LONG64& v) noexcept { return InterlockedCompareExchange64(&v, 0, 0); }
public:
    Channel() = default;
    ~Channel() { Close(); }
    Channel(const Channel&) = delete;
    Channel& operator=(const Channel&) = delete;

    // Call only at setup/teardown, never in the raw callback. Existing-only
    // never creates, formats, waits, takes over a crashed initializer, or falls
    // back. A concurrent in-progress initializer returns false; retry outside
    // native callbacks. Any creator may win the zero->initializing CAS.
    bool Open(DWORD pid, bool existingOnly = false) noexcept {
        if (data_) return pid == pid_;
        if (!pid) return false;
        char name[96] {};
        std::snprintf(name, sizeof(name), "Local\\kh2coop_event_hold_v1_%lu", static_cast<unsigned long>(pid));
        map_ = existingOnly ? OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, name)
            : CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
                                 static_cast<DWORD>(sizeof(Shared)), name);
        if (!map_) return false;
        data_ = static_cast<Shared*>(MapViewOfFile(map_, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(Shared)));
        if (!data_) { Close(); return false; }
        if (!existingOnly && InterlockedCompareExchange(&data_->init, 1, 0) == 0) {
            // Pagefile mappings start zeroed. No memset can race another opener.
            data_->version = static_cast<LONG>(Version);
            data_->bytes = static_cast<LONG>(sizeof(Shared));
            InterlockedExchange(&data_->init, 2);
        }
        if (Load(data_->init) != 2 || data_->version != static_cast<LONG>(Version) ||
            data_->bytes != static_cast<LONG>(sizeof(Shared))) {
            Close(); return false;
        }
        pid_ = pid;
        return true;
    }
    bool OpenExisting(DWORD pid) noexcept { return Open(pid, true); }
    bool IsOpen() const noexcept { return data_ != nullptr; }
    std::uint64_t PublishedOrdinal() const noexcept {
        return data_ ? static_cast<std::uint64_t>(Load(data_->written)) : 0;
    }
    void Close() noexcept {
        if (producer_) Invalidate(Abort::Shutdown);
        if (data_) UnmapViewOfFile(data_);
        if (map_) CloseHandle(map_);
        data_ = nullptr; map_ = nullptr; pid_ = 0; producer_ = false;
        next_ = 0; read_ = 0; sourceCount_ = 0; epoch_ = 0; active_ = false;
    }
    // No producer takeover/rearm within a mapping lifetime, including after a
    // runtime crash. Native teardown must close its old mapping before a new run.
    bool Bind(const Scope& scope, std::uint64_t now,
              std::uint64_t timeout = HeartbeatTimeoutMs) noexcept {
        if (!data_ || producer_ || !ValidScope(scope) || !now || timeout < 50 || timeout > 5000 ||
            InterlockedCompareExchange(&data_->producer, static_cast<LONG>(GetCurrentProcessId()), 0) != 0)
            return false;
        producer_ = true;
        data_->scope = scope;
        InterlockedExchange64(&data_->timeout, static_cast<LONG64>(timeout));
        InterlockedExchange64(&data_->heartbeat, static_cast<LONG64>(now));
        InterlockedExchange(&data_->bound, 1);
        Command reset {}; reset.scope = scope;
        return Push(reset);
    }
    bool BoundScope(Scope& out) const noexcept {
        if (!data_ || Load(data_->bound) != 1) return false;
        out = data_->scope;
        return true;
    }
    Abort Reason() const noexcept {
        return data_ ? static_cast<Abort>(Load(data_->abort)) : Abort::Unsupported;
    }
    void Invalidate(Abort reason) noexcept {
        if (data_ && reason != Abort::None)
            InterlockedCompareExchange(&data_->abort, static_cast<LONG>(reason), 0);
    }
    // Immediate invalidation plus ordered terminal reset if space remains.
    // Never clear/overwrite a queued acquire/release to make room for reset.
    void Reset(Abort reason) noexcept {
        if (!producer_ || !data_) return;
        Invalidate(reason);
        Command reset {}; reset.scope = data_->scope;
        Push(reset); // failure leaves the first abort reason intact
    }
    bool Healthy(std::uint64_t now) noexcept {
        if (!data_ || Load(data_->bound) != 1 || Reason() != Abort::None) return false;
        const auto beat = static_cast<std::uint64_t>(Load(data_->heartbeat));
        const auto timeout = static_cast<std::uint64_t>(Load(data_->timeout));
        // A concurrent runtime publication can be newer than the caller's
        // just-captured now. It is fresh, not a backwards-clock failure.
        if (!beat || (now >= beat && now-beat > timeout)) {
            Invalidate(Abort::HeartbeatExpired); return false;
        }
        return true;
    }
    // GetTickCount64 clock in both processes. A gap cannot be renewed away.
    bool Heartbeat(std::uint64_t now) noexcept {
        if (!producer_ || !Healthy(now)) return false;
        InterlockedExchange64(&data_->heartbeat, static_cast<LONG64>(now));
        return Reason() == Abort::None;
    }
    // Owner-only writer. All payload words are interlocked, so a seqlock retry
    // never races a plain C++ load/store. No spin or allocation; contention fails.
    bool PublishAck(const Ack& ack) noexcept {
        Scope scope {};
        Command processed {};
        Ack previous {};
        if (!BoundScope(scope) || scope != ack.scope || !ack.epoch ||
            (ack.flags & ~(SafeConverged | LiveEligible)) || !ack.processedOrdinal ||
            !ReadPublished(ack.processedOrdinal, processed) ||
            ack.processedHostSourceSerial != processed.hostSourceSerial ||
            (processed.kind != Kind::Reset && ack.epoch != processed.epoch)) return false;
        if (ReadAck(previous) && (ack.processedOrdinal < previous.processedOrdinal ||
            ack.epoch < previous.epoch || (ack.processedOrdinal == previous.processedOrdinal &&
            (ack.processedHostSourceSerial != previous.processedHostSourceSerial || ack.epoch != previous.epoch))))
            return false;
        const auto sequence = Load(data_->ackSequence);
        if ((sequence & 1) || sequence > INT64_MAX-2 ||
            InterlockedCompareExchange64(&data_->ackSequence, sequence+1, sequence) != sequence) return false;
        std::array<LONG64, sizeof(Ack)/8> words {};
        std::memcpy(words.data(), &ack, sizeof(ack));
        for (std::size_t i = 0; i < words.size(); ++i)
            InterlockedExchange64(&data_->ackWords[i], words[i]);
        InterlockedExchange64(&data_->ackSequence, sequence+2);
        return true;
    }
    bool ReadAck(Ack& out) const noexcept {
        if (!data_) return false;
        for (unsigned attempt = 0; attempt < 3; ++attempt) {
            const auto before = Load(data_->ackSequence);
            if (!before || (before & 1)) continue;
            std::array<LONG64, sizeof(Ack)/8> words {};
            for (std::size_t i = 0; i < words.size(); ++i) words[i] = Load(data_->ackWords[i]);
            if (before != Load(data_->ackSequence)) continue;
            std::memcpy(&out, words.data(), sizeof(out));
            return true;
        }
        return false;
    }
    bool ReadPublished(std::uint64_t ordinal, Command& out) const noexcept {
        if (!data_ || !ordinal || ordinal > data_->history.size() ||
            ordinal > static_cast<std::uint64_t>(Load(data_->written))) return false;
        out = data_->history[ordinal-1];
        return out.ordinal == ordinal;
    }
    // Runtime primes the baseline only after native owner confirms scope,
    // ordered reset and safe convergence; never infer readiness from a cache.
    bool Arm(std::uint64_t now) noexcept {
        Ack ack {};
        if (!producer_ || !Healthy(now) || !ReadAck(ack) || ack.scope != data_->scope ||
            ack.processedOrdinal != 1 || ack.processedHostSourceSerial != 0 ||
            ack.flags != (SafeConverged | LiveEligible) || !ack.epoch) return false;
        if (epoch_ != 0) return true;
        epoch_ = ack.epoch;
        return true;
    }
    bool Armed() const noexcept { return epoch_ != 0; }
    // Preserve excluded startup control source identities without advancing a
    // global serial floor. Cached RoomTransition/EventHold can share a serial.
    bool ExcludeSource(std::uint64_t source) noexcept {
        for (std::size_t i = 0; i < sourceCount_; ++i) if (sources_[i] == source) return true;
        if (!source || sourceCount_ == sources_.size()) { Invalidate(Abort::SourceLimit); return false; }
        sources_[sourceCount_++] = source;
        return true;
    }
    bool Publish(Command command, std::uint64_t now) noexcept {
        if (!producer_ || !Healthy(now)) return false;
        if (command.scope != data_->scope) { Invalidate(Abort::WrongScope); return false; }
        if (!Armed() || !command.hostSourceSerial || !command.epoch || command.ordinal || command.kind == Kind::Reset) {
            Invalidate(Abort::InvalidOrder); return false;
        }
        for (std::size_t i = 0; i < sourceCount_; ++i) if (sources_[i] == command.hostSourceSerial) {
            Invalidate(Abort::Replay); return false;
        }
        if (sourceCount_ == sources_.size()) { Invalidate(Abort::SourceLimit); return false; }
        if (command.kind == Kind::Transition) {
            if (command.epoch <= epoch_) { Invalidate(Abort::InvalidOrder); return false; }
        } else if ((command.kind != Kind::Acquire && command.kind != Kind::Release) ||
                   command.epoch != epoch_ || (command.kind == Kind::Acquire ? active_ : !active_)) {
            Invalidate(Abort::InvalidOrder); return false;
        }
        if (!Push(command)) return false;
        sources_[sourceCount_++] = command.hostSourceSerial;
        // A successor room wakes an owned pause, but the outstanding acquire
        // survives until its new-epoch release and native convergence ACK.
        if (command.kind == Kind::Transition) epoch_ = command.epoch;
        else active_ = command.kind == Kind::Acquire;
        return true;
    }
    // Input-only consumer, after Open on a setup thread. Check Healthy again
    // immediately before native overlay mutation; receipt is not a hold/ACK.
    ReadResult Read(std::uint64_t now, Command& out) noexcept {
        if (!data_ || Load(data_->bound) != 1) return ReadResult::Unavailable;
        if (!Healthy(now)) return ReadResult::Aborted;
        const auto consumed = static_cast<std::uint64_t>(Load(data_->consumed));
        const auto written = static_cast<std::uint64_t>(Load(data_->written));
        if (consumed == written) return ReadResult::Empty;
        if (consumed != read_ || written < consumed || written-consumed > Capacity) {
            Invalidate(Abort::ConsumerOrder); return ReadResult::Aborted;
        }
        out = data_->fifo[consumed % Capacity];
        if (out.ordinal != consumed+1 || out.scope != data_->scope) {
            Invalidate(Abort::ConsumerOrder); return ReadResult::Aborted;
        }
        InterlockedExchange64(&data_->consumed, static_cast<LONG64>(++read_));
        return Healthy(now) ? ReadResult::Record : ReadResult::Aborted;
    }
private:
    bool Push(Command command) noexcept {
        const auto consumed = static_cast<std::uint64_t>(Load(data_->consumed));
        if (next_ >= data_->history.size() || consumed > next_ || next_-consumed >= Capacity) {
            Invalidate(Abort::Overflow); return false;
        }
        command.ordinal = next_+1;
        data_->fifo[next_ % Capacity] = command;
        data_->history[next_] = command;
        InterlockedExchange64(&data_->written, static_cast<LONG64>(++next_));
        return true;
    }
    HANDLE map_ {};
    Shared* data_ {};
    DWORD pid_ {};
    bool producer_ {}, active_ {};
    std::uint64_t next_ {}, read_ {};
    std::uint32_t epoch_ {};
    std::array<std::uint64_t, SourceCapacity> sources_ {};
    std::size_t sourceCount_ {};
};
} // namespace kh2coop::eventhold
