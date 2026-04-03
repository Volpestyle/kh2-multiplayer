// ============================================================================
// DllMain — kh2coop_inject DLL entry point
//
// Loading methods:
//   1. Panacea plugin (recommended): place kh2coop_inject.dll in the
//      mod's dll/ folder. Panacea calls OnInit/OnFrame automatically.
//   2. Manual injection: use LoadLibrary or any DLL injector. DllMain
//      spawns an init thread that installs hooks after a short delay.
//      Explicit unloaders must call OnShutdown() before FreeLibrary.
//
// Both methods converge on kh2coop::inject::Initialize() which installs
// the PerEntityUpdate hook via MinHook.
// ============================================================================

#include <Windows.h>
#include <atomic>
#include "EntityHook.hpp"

namespace {

std::atomic<bool> g_initStarted{false};
std::atomic<bool> g_initDone{false};
std::atomic<bool> g_shutdownRequested{false};
HMODULE g_dllModule = nullptr;
HANDLE g_initThread = nullptr;
DWORD g_initThreadId = 0;
HANDLE g_stopEvent = nullptr;

void RequestShutdown() {
    g_shutdownRequested.store(true, std::memory_order_release);
    if (g_stopEvent != nullptr) {
        SetEvent(g_stopEvent);
    }
}

bool IsShutdownRequested() {
    if (g_shutdownRequested.load(std::memory_order_acquire)) {
        return true;
    }

    return g_stopEvent != nullptr &&
           WaitForSingleObject(g_stopEvent, 0) == WAIT_OBJECT_0;
}

// Attempt initialization. Returns true if hooks are installed.
bool TryInit() {
    if (g_initDone.load(std::memory_order_acquire)) return true;
    if (IsShutdownRequested()) return false;

    bool expected = false;
    if (!g_initStarted.compare_exchange_strong(
            expected, true, std::memory_order_acq_rel)) {
        return g_initDone.load(std::memory_order_acquire);
    }

    if (IsShutdownRequested()) {
        g_initStarted.store(false, std::memory_order_release);
        return false;
    }

    auto exeBase = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    if (exeBase == 0) {
        g_initStarted.store(false, std::memory_order_release);
        return false;
    }

    if (IsShutdownRequested()) {
        g_initStarted.store(false, std::memory_order_release);
        return false;
    }

    if (kh2coop::inject::Initialize(exeBase)) {
        if (IsShutdownRequested()) {
            kh2coop::inject::Shutdown();
            g_initStarted.store(false, std::memory_order_release);
            return false;
        }
        g_initDone.store(true, std::memory_order_release);
        return true;
    }

    g_initStarted.store(false, std::memory_order_release);
    return false;
}

// Background thread for standalone (non-Panacea) loading.
// Polls until the game is ready, then installs hooks.
DWORD WINAPI InitThread(LPVOID /*param*/) {
    // Wait for the game to finish CRT/module initialization.
    // The entity update loop won't run until the game is fully loaded,
    // so it's safe to install hooks as soon as the module is mapped.
    for (int attempt = 0; attempt < 600; ++attempt) {  // ~10 seconds
        if (g_stopEvent != nullptr &&
            WaitForSingleObject(g_stopEvent, 16) == WAIT_OBJECT_0) {
            return 0;
        }
        if (g_shutdownRequested.load(std::memory_order_acquire)) {
            return 0;
        }
        if (TryInit()) return 0;
    }
    // Failed to initialize within timeout.
    // This is not fatal — the DLL just won't hook anything.
    return 1;
}

void StopInitThread() {
    RequestShutdown();

    if (g_initThread != nullptr && g_initThreadId != GetCurrentThreadId()) {
        WaitForSingleObject(g_initThread, INFINITE);
    }

    if (g_initThread != nullptr) {
        CloseHandle(g_initThread);
        g_initThread = nullptr;
        g_initThreadId = 0;
    }

    if (g_stopEvent != nullptr) {
        CloseHandle(g_stopEvent);
        g_stopEvent = nullptr;
    }
}

void ShutdownOutsideLoaderLock() {
    StopInitThread();

    if (g_initDone.exchange(false, std::memory_order_acq_rel)) {
        kh2coop::inject::Shutdown();
    }

    g_initStarted.store(false, std::memory_order_release);
}

void CloseInitHandles() {
    if (g_initThread != nullptr) {
        CloseHandle(g_initThread);
        g_initThread = nullptr;
        g_initThreadId = 0;
    }

    if (g_stopEvent != nullptr) {
        CloseHandle(g_stopEvent);
        g_stopEvent = nullptr;
    }
}

} // anonymous namespace

// ============================================================================
// DllMain
// ============================================================================

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpReserved) {
    (void)lpReserved;

    switch (fdwReason) {
    case DLL_PROCESS_ATTACH:
        g_dllModule = hinstDLL;
        g_shutdownRequested.store(false, std::memory_order_release);
        g_initStarted.store(false, std::memory_order_release);
        g_initDone.store(false, std::memory_order_release);
        DisableThreadLibraryCalls(hinstDLL);
        // Spawn init thread — don't initialize under loader lock.
        g_stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        g_initThread = CreateThread(nullptr, 0, InitThread, nullptr, 0,
                                    &g_initThreadId);
        if (g_initThread == nullptr) {
            CloseInitHandles();
            return FALSE;
        }
        break;

    case DLL_PROCESS_DETACH:
        // Never wait on worker threads or unhook from inside DllMain.
        // Explicit unloaders must call OnShutdown() before FreeLibrary.
        RequestShutdown();
        CloseInitHandles();
        g_initDone.store(false, std::memory_order_release);
        g_initStarted.store(false, std::memory_order_release);
        break;
    }

    return TRUE;
}

// ============================================================================
// Panacea Plugin Exports
//
// If loaded by Panacea (OpenKH mod loader), these are called:
//   OnInit(modPath) — once at startup, after the game module is loaded
//   OnFrame()       — every game frame
//   OnShutdown()    — explicit cleanup path for manual unloaders
//
// These also work for standalone loading — OnFrame provides periodic
// gamepad reading and status logging even without Panacea.
// ============================================================================

extern "C" {

__declspec(dllexport) void OnInit(const wchar_t* modPath) {
    (void)modPath;
    TryInit();
}

__declspec(dllexport) void OnFrame() {
    if (g_initDone.load(std::memory_order_acquire)) {
        kh2coop::inject::OnFrame();
    }
}

__declspec(dllexport) void OnShutdown() {
    ShutdownOutsideLoaderLock();
}

} // extern "C"
