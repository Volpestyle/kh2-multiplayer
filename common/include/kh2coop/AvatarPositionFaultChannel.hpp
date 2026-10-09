#pragma once
#include "kh2coop/AvatarPositionFault.hpp"
#include <Windows.h>
#include <cstring>
#include <cwchar>
namespace kh2coop::avatarfault {
struct Shared {
    volatile LONG offerSequence {};
    Offer offer {};
    volatile LONG requestSequence {};
    Request request {};
};
inline void MappingName(wchar_t* out, std::size_t n, DWORD pid) {
    swprintf_s(out,n,L"Local\\KH2Coop_AvatarPositionFault_%lu",static_cast<unsigned long>(pid));
}
template<class T> bool Snapshot(volatile LONG& sequence, const T& shared, T& out) {
    const LONG before = InterlockedCompareExchange(&sequence,0,0);
    if (!before || (before & 1)) return false;
    std::memcpy(&out,&shared,sizeof(out)); MemoryBarrier();
    return before == InterlockedCompareExchange(&sequence,0,0);
}
inline std::uint64_t ProcessCreation() {
    FILETIME creation{},exit{},kernel{},user{};
    if (!GetProcessTimes(GetCurrentProcess(),&creation,&exit,&kernel,&user)) return 0;
    return (static_cast<std::uint64_t>(creation.dwHighDateTime)<<32)|creation.dwLowDateTime;
}
}
