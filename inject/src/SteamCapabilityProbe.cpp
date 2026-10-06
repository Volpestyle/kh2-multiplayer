#include "SteamCapabilityProbe.hpp"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <initializer_list>

namespace kh2coop::steamprobe {
namespace {
// Narrow flat-API ABI, pinned to the shipped Windows x64 accessors below.
// Layouts/constants: Valve's isteamnetworkingsockets.h / isteamnetworkingutils.h
// and steamnetworkingtypes.h; provenance in the VUH-1493 probe review packet.
struct AuthenticationStatus { std::int32_t availability; char debug[256]; };
struct RelayStatus {
    std::int32_t availability, measuring, configuration, anyRelay;
    char debug[256];
};
static_assert(sizeof(AuthenticationStatus) == 260);
static_assert(sizeof(RelayStatus) == 272);
static_assert(sizeof(void*) == 8, "Steam capability probe is Windows x64 only");
constexpr int kCurrent = 100, kConnectionScope = 4, kInt32 = 1, kPointer = 5;
constexpr int kIce = 104, kConnectionCallback = 201;
constexpr ULONGLONG kBudgetMs = 60000;
std::atomic<std::uint32_t> g_expectedConnection{0}, g_callbacks{0}, g_callbackThread{0};
int g_moduleMarker = 0;

// May be dispatched by the game's existing SteamAPI_RunCallbacks. No Steam
// calls, logging, allocation, game state or exception propagation here. Only
// read the documented leading HSteamNetConnection field of callback1221.
void __cdecl ConnectionChanged(void* callback) noexcept {
    if (!callback) return;
    std::uint32_t connection = 0;
    std::memcpy(&connection, callback, sizeof(connection));
    if (connection != 0 && connection == g_expectedConnection.load(std::memory_order_acquire)) {
        g_callbackThread.store(GetCurrentThreadId(), std::memory_order_relaxed);
        g_callbacks.fetch_add(1, std::memory_order_release);
    }
}

template<class T> bool Resolve(HMODULE module, T& out, const char* name) noexcept {
    const auto symbol = GetProcAddress(module, name);
    static_assert(sizeof(out) == sizeof(symbol));
    std::memcpy(&out, &symbol, sizeof(out));
    return symbol != nullptr;
}

struct Api {
    int (__cdecl *userHandle)() = nullptr;
    int (__cdecl *pipeHandle)() = nullptr;
    void* (__cdecl *user)() = nullptr;
    void* (__cdecl *utils)() = nullptr;
    void* (__cdecl *sockets)() = nullptr;
    void* (__cdecl *networkUtils)() = nullptr;
    void* (__cdecl *messages)() = nullptr;
    void* (__cdecl *friends)() = nullptr;
    void* (__cdecl *matchmaking)() = nullptr;
    bool (__cdecl *loggedOn)(void*) = nullptr;
    std::uint64_t (__cdecl *steamId)(void*) = nullptr;
    std::uint32_t (__cdecl *appId)(void*) = nullptr;
    int (__cdecl *initAuthentication)(void*) = nullptr;
    int (__cdecl *authentication)(void*, AuthenticationStatus*) = nullptr;
    void (__cdecl *initRelay)(void*) = nullptr;
    int (__cdecl *relay)(void*, RelayStatus*) = nullptr;
    bool (__cdecl *pair)(void*, std::uint32_t*, std::uint32_t*, bool, const void*, const void*) = nullptr;
    bool (__cdecl *close)(void*, std::uint32_t, int, const char*, bool) = nullptr;
    bool (__cdecl *setConfig)(void*, int, int, std::intptr_t, int, const void*) = nullptr;
    int (__cdecl *getConfig)(void*, int, int, std::intptr_t, int*, void*, std::size_t*) = nullptr;

    bool Load(HMODULE m) noexcept {
#define GET(field, name) if (!Resolve(m, field, name)) return false
        GET(userHandle, "SteamAPI_GetHSteamUser");
        GET(pipeHandle, "SteamAPI_GetHSteamPipe");
        GET(user, "SteamAPI_SteamUser_v023"); GET(utils, "SteamAPI_SteamUtils_v010");
        GET(sockets, "SteamAPI_SteamNetworkingSockets_SteamAPI_v012");
        GET(networkUtils, "SteamAPI_SteamNetworkingUtils_SteamAPI_v004");
        GET(messages, "SteamAPI_SteamNetworkingMessages_SteamAPI_v002");
        GET(friends, "SteamAPI_SteamFriends_v017"); GET(matchmaking, "SteamAPI_SteamMatchmaking_v009");
        GET(loggedOn, "SteamAPI_ISteamUser_BLoggedOn"); GET(steamId, "SteamAPI_ISteamUser_GetSteamID");
        GET(appId, "SteamAPI_ISteamUtils_GetAppID");
        GET(initAuthentication, "SteamAPI_ISteamNetworkingSockets_InitAuthentication");
        GET(authentication, "SteamAPI_ISteamNetworkingSockets_GetAuthenticationStatus");
        GET(initRelay, "SteamAPI_ISteamNetworkingUtils_InitRelayNetworkAccess");
        GET(relay, "SteamAPI_ISteamNetworkingUtils_GetRelayNetworkStatus");
        GET(pair, "SteamAPI_ISteamNetworkingSockets_CreateSocketPair");
        GET(close, "SteamAPI_ISteamNetworkingSockets_CloseConnection");
        GET(setConfig, "SteamAPI_ISteamNetworkingUtils_SetConfigValue");
        GET(getConfig, "SteamAPI_ISteamNetworkingUtils_GetConfigValue");
#undef GET
        return true;
    }
};

bool Stop(HANDLE event, ULONGLONG deadline, DWORD wait = 0) noexcept {
    return GetTickCount64() >= deadline || !event ||
        WaitForSingleObject(event, wait) != WAIT_TIMEOUT;
}

struct Pair {
    Api& api; void* sockets;
    std::uint32_t first = 0, second = 0;
    FILE* log;
    ~Pair() {
        g_expectedConnection.store(0, std::memory_order_release);
        for (auto* handle : {&first, &second}) {
            if (*handle) {
                const bool ok = api.close(sockets, *handle, 0, "capability probe done", false);
                std::fprintf(log, "[steam-probe] close handle=%u ok=%u\n", *handle, unsigned(ok));
                *handle = 0;
            }
        }
    }
};

void Observe(FILE* log, HANDLE stopEvent) {
    const auto started = GetTickCount64(), deadline = started + kBudgetMs;
    // A queued Steam callback can outlive its connection handle. Diagnostic
    // mode pins this DLL until PROCESS EXIT, so late callbacks cannot jump into
    // unloaded code. OnShutdown still stops the worker. Off mode never pins.
    HMODULE pinned = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                           reinterpret_cast<LPCWSTR>(&g_moduleMarker), &pinned)) {
        std::fprintf(log, "[steam-probe] refused module-pin error=%lu\n", GetLastError()); return;
    }
    std::fprintf(log, "[steam-probe] begin pid=%lu thread=%lu budgetMs=60000 modulePinnedUntilProcessExit=1\n",
                 GetCurrentProcessId(), GetCurrentThreadId());
    Api api; HMODULE steam = nullptr;
    while (!Stop(stopEvent, deadline)) {
        steam = GetModuleHandleW(L"steam_api64.dll"); // Never LoadLibrary/SteamAPI_Init.
        if (steam) break;
        if (Stop(stopEvent, deadline, 100)) break;
    }
    if (!steam || !api.Load(steam)) { std::fprintf(log, "[steam-probe] unavailable shipped-module-or-exports\n"); return; }
    while (!Stop(stopEvent, deadline) && (!api.userHandle() || !api.pipeHandle())) {
        if (Stop(stopEvent, deadline, 100)) break;
    }
    if (Stop(stopEvent, deadline)) { std::fprintf(log, "[steam-probe] unavailable existing-session deadline-or-stop\n"); return; }
    void* user = api.user(); void* utils = api.utils();
    if (!user || !utils) { std::fprintf(log, "[steam-probe] unavailable user-or-utils\n"); return; }
    const auto app = api.appId(utils);
    if (app != 2552430) { std::fprintf(log, "[steam-probe] refused appId=%u expected=2552430\n", app); return; }
    while (!Stop(stopEvent, deadline) && !api.loggedOn(user)) {
        if (Stop(stopEvent, deadline, 100)) break;
    }
    if (Stop(stopEvent, deadline)) { std::fprintf(log, "[steam-probe] unavailable logged-on deadline-or-stop\n"); return; }
    const auto identity = api.steamId(user);
    void* sockets = api.sockets(); void* nu = api.networkUtils();
    void* messages = api.messages(); void* friends = api.friends(); void* matchmaking = api.matchmaking();
    std::fprintf(log, "[steam-probe] session appId=%u steamId=%llu loggedOn=1 user=%p utils=%p sockets012=%p networkUtils004=%p messages002=%p friends017=%p matchmaking009=%p\n",
                 app, static_cast<unsigned long long>(identity), user, utils, sockets, nu, messages, friends, matchmaking);
    if (!identity || !sockets || !nu || !messages || !friends || !matchmaking) {
        std::fprintf(log, "[steam-probe] unavailable identity-or-interface\n"); return;
    }
    api.initRelay(nu); // Auth/config/relay readiness only, no peer/listen socket.
    const int authStart = api.initAuthentication(sockets);
    std::fprintf(log, "[steam-probe] authentication-start availability=%d\n", authStart);
    bool ready = false;
    for (unsigned sample = 0; !Stop(stopEvent, deadline); ++sample) {
        AuthenticationStatus auth{}; RelayStatus relay{};
        const int a = api.authentication(sockets, &auth), r = api.relay(nu, &relay);
        std::fprintf(log, "[steam-probe] status sample=%u elapsedMs=%llu auth=%d authField=%d authDebug=%.255s relay=%d relayField=%d config=%d anyRelay=%d measuring=%d relayDebug=%.255s\n",
                     sample, GetTickCount64()-started, a, auth.availability, auth.debug,
                     r, relay.availability, relay.configuration, relay.anyRelay, relay.measuring, relay.debug);
        if (api.appId(utils) != app || !api.loggedOn(user) || api.steamId(user) != identity) {
            std::fprintf(log, "[steam-probe] refused session-changed\n"); return;
        }
        ready = a == kCurrent && r == kCurrent && relay.configuration == kCurrent && relay.anyRelay == kCurrent;
        if (ready) break;
        if (Stop(stopEvent, deadline, 1000)) break;
    }
    if (!ready || Stop(stopEvent, deadline)) { std::fprintf(log, "[steam-probe] unavailable auth-relay deadline-or-stop\n"); return; }
    Pair pair{api, sockets, 0, 0, log};
    // false = internal buffers only; no local UDP socket, identity override,
    // P2P rendezvous or public listener. Both handles are closed on every path.
    if (!api.pair(sockets, &pair.first, &pair.second, false, nullptr, nullptr)) {
        std::fprintf(log, "[steam-probe] unavailable socket-pair\n"); return;
    }
    const std::int32_t disabled = 0;
    bool iceOff = true;
    for (const auto handle : {pair.first, pair.second}) {
        std::int32_t actual = -1; int type = 0; std::size_t size = sizeof(actual);
        const bool set = api.setConfig(nu, kIce, kConnectionScope, handle, kInt32, &disabled);
        const int get = api.getConfig(nu, kIce, kConnectionScope, handle, &type, &actual, &size);
        iceOff = iceOff && set && get == 1 && type == kInt32 && size == sizeof(actual) && actual == 0;
        std::fprintf(log, "[steam-probe] pair-ice handle=%u set=%u get=%d type=%d bytes=%zu value=%d\n", handle, unsigned(set), get, type, size, actual);
    }
    if (!iceOff) { std::fprintf(log, "[steam-probe] refused ICE-off-unsupported\n"); return; }
    auto callback = &ConnectionChanged;
    g_callbacks.store(0, std::memory_order_relaxed);
    g_expectedConnection.store(pair.second, std::memory_order_release);
    if (!api.setConfig(nu, kConnectionCallback, kConnectionScope, pair.second, kPointer, &callback)) {
        std::fprintf(log, "[steam-probe] unavailable callback-config\n"); return;
    }
    const bool closed = api.close(sockets, pair.first, 0, "capability callback test", false);
    std::fprintf(log, "[steam-probe] pair-close-first handle=%u ok=%u expectedCallbackHandle=%u\n", pair.first, unsigned(closed), pair.second);
    if (!closed) return;
    pair.first = 0;
    const auto callbackDeadline = (std::min)(deadline, GetTickCount64() + 5000);
    while (!Stop(stopEvent, callbackDeadline) && g_callbacks.load(std::memory_order_acquire) == 0) {
        if (Stop(stopEvent, callbackDeadline, 50)) break;
    }
    // Do NOT call SteamAPI_RunCallbacks or sockets RunCallbacks. This observes
    // dispatch provided by the existing game; absence remains unavailable.
    const auto count = g_callbacks.load(std::memory_order_acquire);
    const bool sameSession = api.appId(utils) == app && api.loggedOn(user) && api.steamId(user) == identity;
    const bool complete = count != 0 && sameSession && !Stop(stopEvent, deadline);
    std::fprintf(log, "[steam-probe] result complete=%u callbackCount=%u callbackThread=%u sameSession=%u elapsedMs=%llu scope=single-game-internal-pair-not-P2P\n",
                 unsigned(complete), count, g_callbackThread.load(std::memory_order_relaxed), unsigned(sameSession), GetTickCount64()-started);
}
} // namespace

void Run(HANDLE stopEvent) noexcept {
    char enabled[8]{};
    if (GetEnvironmentVariableA("KH2COOP_STEAM_CAPABILITY_PROBE", enabled, sizeof(enabled)) != 1 || enabled[0] != '1') return;
    wchar_t directory[32768]{};
    const DWORD n = GetEnvironmentVariableW(L"KH2COOP_LOG_DIR", directory, 32768);
    if (!n || n >= 32768 || (GetFileAttributesW(directory) & FILE_ATTRIBUTE_DIRECTORY) == 0 ||
        GetFileAttributesW(directory) == INVALID_FILE_ATTRIBUTES) return;
    wchar_t path[32768]{};
    if (swprintf_s(path, L"%s\\steam-capability_%lu.log", directory, GetCurrentProcessId()) < 0) return;
    FILE* log = nullptr;
    if (_wfopen_s(&log, path, L"wx") != 0 || !log) return; // Never overwrite a receipt.
    setvbuf(log, nullptr, _IONBF, 0);
    try { Observe(log, stopEvent); }
    catch (...) { std::fprintf(log, "[steam-probe] exception unavailable\n"); }
    std::fclose(log);
}
} // namespace kh2coop::steamprobe
