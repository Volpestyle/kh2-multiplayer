#include "PartyEmptyPackage.hpp"
#include <array>
#include <cstring>
#include <limits>
#ifdef _WIN32
#include <windows.h>
#endif

namespace kh2coop::inject::partyemptypackage {
namespace {
// Generated from the exact Steam assets and native header inventory recorded in
// research/package-inventory.json; no runtime filesystem/package access.
#include "PartyEmptyPackageData.inc"

bool ReadNative(std::uintptr_t address, void* destination, std::size_t bytes) {
#ifdef _WIN32
    __try { std::memcpy(destination, reinterpret_cast<const void*>(address), bytes); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
#else
    (void)address; (void)destination; (void)bytes; return false;
#endif
}
bool Span(std::uintptr_t address, std::size_t bytes) {
    return address != 0 && bytes <= std::numeric_limits<std::uintptr_t>::max() - address;
}
std::uint32_t U32(const unsigned char* p) {
    std::uint32_t v = 0; std::memcpy(&v, p, sizeof(v)); return v;
}
bool Decode(std::uintptr_t base, std::uint32_t handle, ReadFn read, std::uintptr_t& address) {
    if (!handle) { address = 0; return true; }
    const auto cell = base + HANDLE_TABLE_RVA + 8u * ((handle & 0x7fffffffu) >> 25);
    std::uintptr_t first = 0, second = 0;
    if (!Span(cell, sizeof(first)) || !read(cell, &first, sizeof(first)) ||
        !read(cell, &second, sizeof(second)) || first != second) return false;
    address = first | static_cast<std::uintptr_t>(handle & 0x01ffffffu);
    return address != 0;
}
bool MatchPayload(std::uintptr_t root, const Package& pin, std::uint32_t off,
                  std::uint32_t size, ReadFn read) {
    if (off > pin.size || size > pin.size - off || !Span(root + off, size)) return false;
    std::array<unsigned char, 512> buffer {};
    for (std::uint32_t done = 0; done < size;) {
        const auto n = static_cast<std::size_t>(size - done < buffer.size() ? size - done : buffer.size());
        if (!read(root + off + done, buffer.data(), n)) return false;
        for (std::size_t j = 0; j < n; ++j) {
            const auto pos = off + done + static_cast<std::uint32_t>(j);
            bool activation = false;
            for (std::size_t k = 0; k < pin.activationCount; ++k)
                if (pos == pin.activations[k]) { activation = true; break; }
            if (activation ? buffer[j] > 1 : buffer[j] != pin.bytes[pos]) return false;
        }
        done += static_cast<std::uint32_t>(n);
    }
    return true;
}
bool Pass(std::uintptr_t base, std::uintptr_t root, const Package& pin, ReadFn read) {
    std::array<unsigned char, 16> header {};
    if (!Span(root, pin.size) || !read(root, header.data(), header.size()) ||
        header[0] != 'B' || header[1] != 'A' || header[2] != 'R' ||
        (header[3] & 0x0f) != 1 || ((header[3] & 0xc0) != 0 && (header[3] & 0xc0) != 0x80) ||
        U32(header.data() + 4) != U32(pin.bytes + 4)) return false;
    std::uintptr_t self = 0;
    if (!Decode(base, U32(header.data() + 8), read, self) || self != root) return false;
    const auto count = U32(pin.bytes + 4); // pinned: 12 or 58; never an unbounded native count
    for (std::uint32_t i = 0; i < count; ++i) {
        const auto tableOffset = 16u + 16u * i;
        const auto* expected = pin.bytes + tableOffset;
        std::array<unsigned char, 16> entry {}, after {};
        if (!read(root + tableOffset, entry.data(), entry.size()) ||
            std::memcmp(entry.data(), expected, 8) != 0 ||
            U32(entry.data() + 12) != U32(expected + 12)) return false;
        const auto off = U32(expected + 8), size = U32(expected + 12);
        std::uintptr_t payload = 0, again = 0;
        if (!Decode(base, U32(entry.data() + 8), read, payload) ||
            payload != (size ? root + off : 0) ||
            (size && !MatchPayload(root, pin, off, size, read)) ||
            !read(root + tableOffset, after.data(), after.size()) || entry != after ||
            !Decode(base, U32(after.data() + 8), read, again) || again != payload) return false;
    }
    std::array<unsigned char, 16> afterHeader {};
    return read(root, afterHeader.data(), afterHeader.size()) && afterHeader == header;
}
} // namespace

bool Qualified(std::uintptr_t base, std::uint8_t world, std::uint8_t room,
               std::uint16_t evt, ReadFn read) {
    if (!base || world != 4 || evt || (room != 0x1a && room != 0x0a) ||
        base > std::numeric_limits<std::uintptr_t>::max() - HANDLE_TABLE_RVA - 64u * 8u) return false;
    if (!read) read = ReadNative;
    const Package& pin = room == 0x1a ? kHb26 : kHb10;
    std::uintptr_t root = 0, middle = 0, finalRoot = 0;
    return read(base + ROOT_RVA, &root, sizeof(root)) && root &&
        Pass(base, root, pin, read) && read(base + ROOT_RVA, &middle, sizeof(middle)) && middle == root &&
        Pass(base, root, pin, read) && read(base + ROOT_RVA, &finalRoot, sizeof(finalRoot)) && finalRoot == root;
}
} // namespace kh2coop::inject::partyemptypackage
