#pragma once
#include <cstdint>

namespace kh2coop {
// Observation-time transport context; no network gameplay layouts are needed
// by shared-memory producers or consumers of legacy diagnostic channels.
struct ProducerWorldContext {
    std::uint32_t generation{0};
    std::uint64_t deliverySerial{0}, hostSourceSerial{0};
};
} // namespace kh2coop
