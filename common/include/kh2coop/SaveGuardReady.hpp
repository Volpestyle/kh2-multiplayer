#pragma once
#include <Windows.h>
#include <string>

namespace kh2coop::preinject {
// A launcher-created, initially unsignalled event. Only a complete guard
// installation may acknowledge it; it is never created by the injected DLL.
inline std::wstring ReadyEventName(DWORD pid) {
    return L"Local\\kh2coop_saveguard_ready_" + std::to_wstring(pid);
}
} // namespace kh2coop::preinject
