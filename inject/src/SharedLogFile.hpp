#pragma once
// A per-process log that the rig can read while the game still runs.
//
// fopen_s/_wfopen_s open files with no sharing at all, so every reader (the canonical runner polling the
// Steam broker log) fails with a sharing violation until the game exits. _wfsopen with _SH_DENYWR keeps the
// "wx" semantics (create new and fail if it exists; text mode) but lets readers open the file, while other
// writers are still refused. Unbuffered, so a reader sees each line at once. tests/SharedLogFileTest.cpp
// checks all of this, including that the UCRT honours "x" in _wfsopen.
#include <cstdio>
#include <share.h>

namespace kh2coop {

inline FILE* CreateSharedLogW(const wchar_t* path) noexcept {
    FILE* f = _wfsopen(path, L"wx", _SH_DENYWR);
    if (f) setvbuf(f, nullptr, _IONBF, 0);
    return f;
}

}  // namespace kh2coop
