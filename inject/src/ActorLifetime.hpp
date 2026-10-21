#pragma once
// VUH-2612: a held actor pointer is followed only while that actor is still on the native active list.
// KH2 runs with the CLR in-process, and its vectored handler sees a first-chance AV before our __except
// (docs/LESSONS_LEARNED.md, 2026-10-18), so a guarded read of a freed clone can still end the process.
// The walk reads only listed actors: the list head, then each actor's +0xA90 next handle resolved through
// the handle region table (object = region | (handle & 0x1FFFFFF), FUN_14040D7A0). A freed clone is not
// on the list, and nothing of it is read.
#include "kh2coop/KH2Offsets.hpp"
#include <Windows.h>
#include <cstddef>
#include <cstdint>

namespace kh2coop::inject::lifetime {

namespace list = kh2coop::offsets::active_entity_list;

enum class Walk : std::uint8_t { Listed, NotListed, Capped };
// read(address, out, bytes) -> bool, never faulting for an unreadable address. Capped: the walk checked
// MAX_TRAVERSAL actors without reaching the end of the list (not proof that the actor is gone).
template <class Read>
Walk Find(std::uintptr_t base, std::uintptr_t actor, Read&& read) {
    std::uintptr_t cursor = 0;
    if (!actor || !read(base + list::HEAD, &cursor, sizeof(cursor))) return Walk::NotListed;
    for (std::uint32_t i = 0; cursor; ++i) {
        if (i == list::MAX_TRAVERSAL) return Walk::Capped;
        if (cursor == actor) return Walk::Listed;
        std::uint32_t link = 0;
        std::uintptr_t region = 0;
        if (!read(cursor + kh2coop::offsets::actor::LINKED_NEXT_HANDLE, &link, sizeof(link)) || !link) return Walk::NotListed;
        const auto bucket = (link & 0x7FFFFFFFu) >> list::HANDLE_BUCKET_SHIFT;
        if (!read(base + list::HANDLE_REGION_TABLE + bucket * sizeof(std::uintptr_t), &region, sizeof(region)) ||
            !region || region == ~std::uintptr_t {} || (region & list::HANDLE_LOW_MASK))
            return Walk::NotListed;
        cursor = region | (link & list::HANDLE_LOW_MASK);
    }
    return Walk::NotListed;
}
template <class Read>
bool Listed(std::uintptr_t base, std::uintptr_t actor, Read&& read) {
    return Find(base, actor, read) == Walk::Listed;
}

// VUH-2614: visits each listed actor in list order, with Find's reads, until visit(actor) returns true
// (Listed), the list ends (NotListed) or MAX_TRAVERSAL actors were visited (Capped).
template <class Read, class Visit>
Walk ForEach(std::uintptr_t base, Read&& read, Visit&& visit) {
    std::uintptr_t cursor = 0;
    if (!read(base + list::HEAD, &cursor, sizeof(cursor))) return Walk::NotListed;
    for (std::uint32_t i = 0; cursor; ++i) {
        if (i == list::MAX_TRAVERSAL) return Walk::Capped;
        if (visit(cursor)) return Walk::Listed;
        std::uint32_t link = 0;
        std::uintptr_t region = 0;
        if (!read(cursor + kh2coop::offsets::actor::LINKED_NEXT_HANDLE, &link, sizeof(link)) || !link) return Walk::NotListed;
        const auto bucket = (link & 0x7FFFFFFFu) >> list::HANDLE_BUCKET_SHIFT;
        if (!read(base + list::HANDLE_REGION_TABLE + bucket * sizeof(std::uintptr_t), &region, sizeof(region)) ||
            !region || region == ~std::uintptr_t {} || (region & list::HANDLE_LOW_MASK))
            return Walk::NotListed;
        cursor = region | (link & list::HANDLE_LOW_MASK);
    }
    return Walk::NotListed;
}

// The in-process CLR sees a first-chance AV before any __except, so a guarded read checks the pages first:
// committed, readable, neither guard nor no-access. Defense in depth only: a freed-and-reused slot stays
// readable, which is why held actors are walked (Find) before they are read.
inline bool Readable(std::uintptr_t p, std::size_t n) {
    constexpr DWORD readable = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ |
                               PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    if (!n || p + n < p) return false;
    for (std::uintptr_t at = p; at < p + n;) {
        MEMORY_BASIC_INFORMATION info {};
        if (!VirtualQuery(reinterpret_cast<LPCVOID>(at), &info, sizeof(info)) || info.State != MEM_COMMIT ||
            (info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) || !(info.Protect & readable))
            return false;
        at = reinterpret_cast<std::uintptr_t>(info.BaseAddress) + info.RegionSize;
    }
    return true;
}

// Objentry descriptors live in the exe image (EnemySync bounds them the same way); actor+0x918 of a freed
// or reused slot can hold anything, e.g. the float 1.0 (0x3F800000) the 2026-10-21 dumps followed.
inline bool DescriptorInImage(std::uintptr_t base, std::uintptr_t descriptor) {
    return descriptor > base && descriptor < base + 0x3000000;
}

} // namespace kh2coop::inject::lifetime
