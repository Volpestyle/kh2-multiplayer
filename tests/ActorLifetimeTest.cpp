#include "ActorLifetime.hpp"
#include <cstdio>
#include <cstring>
#include <map>
#include <vector>

namespace L = kh2coop::inject::lifetime;
namespace {
unsigned errors = 0;
void check(bool ok, const char* label) {
    std::printf("%s: %s\n", ok ? "PASS" : "FAIL", label);
    if (!ok) ++errors;
}
// A sparse fake address space. Reads outside it fail (as the guarded native read would), and every
// read address is recorded so a test can prove a freed actor was never touched.
struct Memory {
    std::map<std::uintptr_t, std::uint8_t> bytes;
    std::vector<std::uintptr_t> reads;
    template <class T> void put(std::uintptr_t at, T value) {
        std::uint8_t raw[sizeof(T)];
        std::memcpy(raw, &value, sizeof(T));
        for (std::size_t i = 0; i < sizeof(T); ++i) bytes[at + i] = raw[i];
    }
    bool operator()(std::uintptr_t at, void* out, std::size_t n) {
        reads.push_back(at);
        auto* dst = static_cast<std::uint8_t*>(out);
        for (std::size_t i = 0; i < n; ++i) {
            const auto it = bytes.find(at + i);
            if (it == bytes.end()) return false;
            dst[i] = it->second;
        }
        return true;
    }
    bool touched(std::uintptr_t lo, std::uintptr_t hi) const {
        for (const auto a : reads) if (a >= lo && a < hi) return true;
        return false;
    }
};
constexpr std::uintptr_t base = 0x7FF625000000ull;
constexpr std::uintptr_t kRegion = 0x7FF626000000ull; // bucket 3: low 25 bits clear
constexpr std::uint32_t kBucket = 3u << 25;
std::uint32_t handle(std::uintptr_t actor) { return kBucket | static_cast<std::uint32_t>(actor - kRegion); }
} // namespace

int main() {
    namespace list = kh2coop::offsets::active_entity_list;
    const std::uintptr_t a = kRegion + 0x40000, b = kRegion + 0x48000, clone = kRegion + 0x4F4F0;
    Memory m;
    m.put<std::uintptr_t>(base + list::HEAD, a);
    m.put<std::uintptr_t>(base + list::HANDLE_REGION_TABLE + 3 * 8, kRegion);
    m.put<std::uint32_t>(a + 0xA90, handle(b));
    m.put<std::uint32_t>(b + 0xA90, 0); // end of the list
    // The freed clone's slot still reads (a reused pool), with a float where its descriptor was.
    m.put<std::uintptr_t>(clone + 0x918, 0x3F800000u);
    m.put<std::uint32_t>(clone + 0xA90, handle(a));

    check(L::Listed(base, a, m) && L::Listed(base, b, m), "listed actors are found by the native walk");
    m.reads.clear();
    check(!L::Listed(base, clone, m) && !m.touched(clone, clone + 0x1000),
          "a clone freed under us is refused without reading any of its memory");
    check(!L::DescriptorInImage(base, 0x3F800000u) && L::DescriptorInImage(base, base + 0x2B10000) &&
              !L::DescriptorInImage(base, 0) && !L::DescriptorInImage(base, base + 0x3000000),
          "a descriptor outside the exe image (the dumps' float 1.0) is never followed");

    Memory corrupt = m;
    corrupt.put<std::uintptr_t>(base + list::HANDLE_REGION_TABLE + 3 * 8, kRegion | 0x10);
    check(!L::Listed(base, b, corrupt), "a region entry with low bits set ends the walk");
    Memory cycle = m;
    cycle.put<std::uint32_t>(b + 0xA90, handle(a));
    check(!L::Listed(base, clone, cycle), "a cyclic list stops at the traversal cap");
    check(!L::Listed(base, 0, m), "a null actor is never listed");

    // The cap: the 256th actor on the list is found; the 257th reports Capped, not gone.
    Memory chain;
    chain.put<std::uintptr_t>(base + list::HANDLE_REGION_TABLE + 3 * 8, kRegion);
    const auto node = [](std::uint32_t i) { return kRegion + 0x1000 + i * 0x1000; };
    chain.put<std::uintptr_t>(base + list::HEAD, node(0));
    for (std::uint32_t i = 0; i < list::MAX_TRAVERSAL + 4; ++i) chain.put<std::uint32_t>(node(i) + 0xA90, handle(node(i + 1)));
    check(L::Find(base, node(list::MAX_TRAVERSAL - 1), chain) == L::Walk::Listed &&
              L::Find(base, node(list::MAX_TRAVERSAL), chain) == L::Walk::Capped,
          "the walk cap is reported as Capped, never as not listed");

    // VUH-2614: ForEach visits exactly the listed actors, in list order, and never a freed one.
    std::vector<std::uintptr_t> seen;
    m.reads.clear();
    const auto all = L::ForEach(base, m, [&](std::uintptr_t actor) { seen.push_back(actor); return false; });
    check(all == L::Walk::NotListed && seen == std::vector<std::uintptr_t> {a, b} && !m.touched(clone, clone + 0x1000),
          "ForEach visits the listed actors in order and nothing of a freed clone");
    seen.clear();
    check(L::ForEach(base, m, [&](std::uintptr_t actor) { seen.push_back(actor); return actor == a; }) == L::Walk::Listed &&
              seen.size() == 1, "ForEach stops when the visitor says so");
    std::uint32_t visits = 0;
    check(L::ForEach(base, chain, [&](std::uintptr_t) { ++visits; return false; }) == L::Walk::Capped &&
              visits == list::MAX_TRAVERSAL, "ForEach stops at the traversal cap and reports Capped");
    check(L::ForEach(base, corrupt, [&](std::uintptr_t) { return false; }) == L::Walk::NotListed,
          "ForEach ends at a corrupt region entry");

    // Readable: a committed page reads; a no-access page and a released one do not.
    auto* page = static_cast<std::uint8_t*>(VirtualAlloc(nullptr, 0x2000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    const auto at = reinterpret_cast<std::uintptr_t>(page);
    DWORD old = 0;
    const bool guarded = page && VirtualProtect(page + 0x1000, 0x1000, PAGE_NOACCESS, &old);
    check(guarded && L::Readable(at, 0x1000) && !L::Readable(at + 0xFF8, 0x10) && !L::Readable(at + 0x1000, 8),
          "a no-access page (or a read that runs into one) is refused before any copy");
    VirtualFree(page, 0, MEM_RELEASE);
    check(!L::Readable(at, 8), "a released page is refused before any copy");
    return errors ? 1 : 0;
}
