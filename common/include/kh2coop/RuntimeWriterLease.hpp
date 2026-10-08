#pragma once
#include <atomic>
#include <cstdint>
namespace kh2coop {
// A lease is renewed by the runtime pump, never a background watchdog.
// Once expired, the raw generation stays retired even if its writer resumes.
class RuntimeWriterLease {
public:
    static constexpr std::uint32_t TimeoutMs = 5000;
    bool Available(std::uint32_t generation, std::uint32_t pid,
                   std::uint32_t heartbeat, std::uint32_t now) const noexcept {
        if (!pid && !generation) return true; // never-armed standalone bridge
        auto retired = retired_.load(std::memory_order_acquire);
        // Monotonic modulo32 generations (no observer may lag by2^31 sessions).
        // An older loading-thread snapshot cannot overwrite a newer tombstone.
        if (generation && retired && static_cast<std::int32_t>(generation - retired) <= 0) return false;
        bool bindingValid = true;
        if (generation && pid) {
            const auto wanted = (static_cast<std::uint64_t>(generation) << 32) | pid;
            auto binding = binding_.load(std::memory_order_acquire);
            for (;;) {
                const auto priorGeneration = static_cast<std::uint32_t>(binding >> 32);
                if (priorGeneration == generation) { bindingValid = binding == wanted; break; }
                if (priorGeneration && static_cast<std::int32_t>(generation - priorGeneration) <= 0) { bindingValid = false; break; }
                if (binding_.compare_exchange_weak(binding, wanted, std::memory_order_acq_rel)) break;
            }
        }
        if (bindingValid && pid && static_cast<std::uint32_t>(now - heartbeat) <= TimeoutMs) return true;
        while (generation && (!retired || static_cast<std::int32_t>(generation - retired) > 0)) {
            if (retired_.compare_exchange_weak(retired, generation, std::memory_order_acq_rel)) break;
        }
        return false;
    }
private:
    mutable std::atomic<std::uint32_t> retired_ {0};
    mutable std::atomic<std::uint64_t> binding_ {0};
};
} // namespace kh2coop
