#pragma once
// ============================================================================
// PacketRing — single-producer / single-consumer ring of variable-length
// packets in caller-provided memory (shared memory between the inject DLL and
// the runtime). Each record is [u32 length][bytes], padded to 4 bytes; a zero
// length record means "wrap to the start". Lock-free: the producer only
// writes `head`, the consumer only writes `tail`, both monotonic byte counts.
// push() fails (never blocks or overwrites) when the ring is full, so the game
// thread can't stall on a slow runtime.
// ============================================================================

#include <atomic>
#include <cstdint>
#include <cstring>
#include <vector>

namespace kh2coop {

struct PacketRingHeader {
    volatile std::uint32_t head;     // bytes produced (mod 2^32)
    std::uint32_t _pad0[15];
    volatile std::uint32_t tail;     // bytes consumed (mod 2^32)
    std::uint32_t _pad1[15];
    std::uint32_t capacity;          // data bytes, multiple of 4
    std::uint32_t dropped;           // pushes refused because full
    std::uint32_t _pad2[14];
};

static_assert(sizeof(PacketRingHeader) == 192);

class PacketRing {
public:
    // `memory` holds a PacketRingHeader followed by `capacity` data bytes.
    static constexpr std::size_t bytesFor(std::uint32_t capacity) {
        return sizeof(PacketRingHeader) + capacity;
    }

    // Formats fresh memory. Call once, by whoever creates the mapping.
    static void format(void* memory, std::uint32_t capacity) {
        std::memset(memory, 0, bytesFor(capacity));
        static_cast<PacketRingHeader*>(memory)->capacity = capacity & ~3u;
    }

    PacketRing() = default;
    explicit PacketRing(void* memory)
        : hdr_(static_cast<PacketRingHeader*>(memory)),
          data_(static_cast<std::uint8_t*>(memory) + sizeof(PacketRingHeader)) {}

    [[nodiscard]] bool valid() const { return hdr_ != nullptr && hdr_->capacity > 0; }

    bool push(const std::uint8_t* bytes, std::uint32_t length) {
        if (!valid() || length == 0) return false;
        const std::uint32_t cap = hdr_->capacity;
        const std::uint32_t need = 4 + pad(length);
        if (need > cap / 2) {
            ++hdr_->dropped;
            return false;
        }
        const std::uint32_t head = hdr_->head;
        const std::uint32_t tail = hdr_->tail;
        std::atomic_thread_fence(std::memory_order_acquire);
        const std::uint32_t used = head - tail;
        std::uint32_t pos = head % cap;
        std::uint32_t skip = 0;
        if (pos + need > cap) skip = cap - pos; // wrap marker + gap
        if (used + skip + need > cap) {
            ++hdr_->dropped;
            return false;
        }
        if (skip) {
            if (skip >= 4) writeU32(pos, 0); // zero length = wrap
            pos = 0;
        }
        writeU32(pos, length);
        std::memcpy(data_ + pos + 4, bytes, length);
        std::atomic_thread_fence(std::memory_order_release);
        hdr_->head = head + skip + need;
        return true;
    }

    bool push(const std::vector<std::uint8_t>& bytes) {
        return push(bytes.data(), static_cast<std::uint32_t>(bytes.size()));
    }

    bool pop(std::vector<std::uint8_t>& out) {
        if (!valid()) return false;
        const std::uint32_t cap = hdr_->capacity;
        std::uint32_t tail = hdr_->tail;
        const std::uint32_t head = hdr_->head;
        std::atomic_thread_fence(std::memory_order_acquire);
        if (tail == head) return false;
        std::uint32_t pos = tail % cap;
        // A wrap marker, or too little room left for a length: skip to 0.
        if (cap - pos < 4 || readU32(pos) == 0) {
            tail += cap - pos;
            pos = 0;
            if (tail == head) {
                hdr_->tail = tail;
                return false;
            }
        }
        const std::uint32_t length = readU32(pos);
        out.assign(data_ + pos + 4, data_ + pos + 4 + length);
        std::atomic_thread_fence(std::memory_order_release);
        hdr_->tail = tail + 4 + pad(length);
        return true;
    }

    [[nodiscard]] std::uint32_t dropped() const { return valid() ? hdr_->dropped : 0; }

private:
    static constexpr std::uint32_t pad(std::uint32_t n) { return (n + 3u) & ~3u; }
    void writeU32(std::uint32_t pos, std::uint32_t v) { std::memcpy(data_ + pos, &v, 4); }
    [[nodiscard]] std::uint32_t readU32(std::uint32_t pos) const {
        std::uint32_t v;
        std::memcpy(&v, data_ + pos, 4);
        return v;
    }

    PacketRingHeader* hdr_ = nullptr;
    std::uint8_t* data_ = nullptr;
};

} // namespace kh2coop
