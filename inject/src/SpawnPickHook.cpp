// SpawnPickHook: detour of the area-script opcode dispatcher FUN_1403a24c0 (VUH-1515, run 102159).
// Opcodes 2 (RandomSpawn) and 3 (CasualSpawn) are re-implemented with the shared pick from
// SpawnPick.hpp; every other opcode, and 2/3 without a shared context, runs the original.
#include "SpawnPickHook.hpp"
#include "SpawnPick.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <MinHook.h>

#include <cstring>

namespace kh2coop::inject::spawnpick {
namespace {

using DispatchFn = char(__fastcall*)(void* script, void* op);
using RegisterFn = void(__fastcall*)(std::uint32_t groupName, char flag);

LogFn g_log = nullptr;
ContextFn g_context = nullptr;
std::uintptr_t g_exeBase = 0;
DispatchFn g_original = nullptr;
RegisterFn g_register = nullptr;
unsigned g_logs = 0;
constexpr unsigned kLogBudget = 256;

bool CopyCode(std::uintptr_t address, void* out, std::size_t size) {
    __try {
        std::memcpy(out, reinterpret_cast<const void*>(address), size);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

char Shared(void* script, void* opPtr, const Context& c) {
    const auto* op = static_cast<const std::uint8_t*>(opPtr);
    std::int16_t opcode = 0, n = 0;
    std::memcpy(&opcode, op, 2);
    std::memcpy(&n, op + 2, 2);
    const auto* args = reinterpret_cast<const std::uint32_t*>(op + 4);
    auto* lcg = reinterpret_cast<std::uint32_t*>(g_exeBase + RVA_LCG);
    // The native step advances the shared LCG once per op; keep that, so the local RNG stream keeps its
    // vanilla length for every other consumer. Its value only feeds the "native" diagnostic.
    const std::uint32_t seed = LcgNext(*lcg);
    *lcg = seed;
    Key key;
    key.salt = c.salt; key.epoch = c.epoch;
    key.world = c.world; key.room = c.room; key.map = c.map; key.btl = c.btl; key.evt = c.evt;
    key.opcode = static_cast<std::uint16_t>(opcode);
    (void)OpOffset(reinterpret_cast<std::uintptr_t>(script), reinterpret_cast<std::uintptr_t>(op), key.opOffset);  // checked by Detour
    key.n = n;
    key.args = args;
    const std::uint64_t hash = Hash(key);
    if (opcode == kOpRandomSpawn) {
        const std::uint32_t idx = PickIndex(hash, n);
        const std::uint32_t native = NativeIndex(seed, n);
        if (g_log && g_logs < kLogBudget) {
            ++g_logs;
            g_log("[spawn-pick] role=%u loc=%02X/%02X map=%u btl=%u evt=%u epoch=%u off=%u op=2 n=%d idx=%u native=%u group=%.4s",
                  c.role, c.world, c.room, c.map, c.btl, c.evt, c.epoch, key.opOffset, n, idx, native,
                  reinterpret_cast<const char*>(&args[idx]));
        }
        g_register(args[idx], 0);
        return 1;
    }
    // CasualSpawn: args[0] = percent, args[1] = group
    const std::uint32_t percent = args[0];
    const bool fired = CasualFires(hash, percent);
    const bool native = NativeRoll(seed) < percent;
    if (g_log && g_logs < kLogBudget) {
        ++g_logs;
        g_log("[spawn-pick] role=%u loc=%02X/%02X map=%u btl=%u evt=%u epoch=%u off=%u op=3 percent=%u fired=%u native=%u group=%.4s",
              c.role, c.world, c.room, c.map, c.btl, c.evt, c.epoch, key.opOffset, percent, fired ? 1u : 0u,
              native ? 1u : 0u, reinterpret_cast<const char*>(&args[1]));
    }
    if (fired) {
        g_register(args[1], 0);
        return 0;  // native: a fired CasualSpawn ends the program
    }
    return 1;
}

char __fastcall Detour(void* script, void* op) {
    std::int16_t opcode = 0, n = 0;
    std::memcpy(&opcode, op, 2);
    std::memcpy(&n, static_cast<const std::uint8_t*>(op) + 2, 2);
    const bool candidate = (opcode == kOpRandomSpawn && n >= 1) || (opcode == kOpCasualSpawn && n >= 2);
    if (!candidate) return g_original(script, op);
    Context c;
    const char* reason = "context";
    std::uint32_t offset = 0;
    const bool inside = OpOffset(reinterpret_cast<std::uintptr_t>(script), reinterpret_cast<std::uintptr_t>(op), offset);
    if (!inside) reason = "offset";  // review F2: not a script-relative op; native, like every other unknown
    if (!inside || !g_context || !g_context(c, reason) || !c.salt || !c.epoch) {
        if (g_log && g_logs < kLogBudget) {
            ++g_logs;
            g_log("[spawn-pick] native op=%d n=%d reason=%s", opcode, n, reason ? reason : "-");
        }
        return g_original(script, op);
    }
    return Shared(script, op, c);
}

}  // namespace

bool Install(std::uintptr_t exeBase, LogFn log, ContextFn context) {
    g_log = log;
    char setting[2] {};
    const bool requested = GetEnvironmentVariableA("KH2COOP_SPAWN_PICK", setting, sizeof(setting)) == 1 && setting[0] == '1';
    if (!requested) return false;  // default off: no detour, no log line, vanilla behaviour
    g_exeBase = exeBase;
    g_context = context;
    bool shape = ShapeTargetsConsistent();
    for (const auto& s : kShapes) {
        std::uint8_t bytes[96] {};
        shape = shape && s.size <= sizeof(bytes) && CopyCode(exeBase + s.rva, bytes, s.size) &&
                std::memcmp(bytes, s.bytes, s.size) == 0;
    }
    if (!shape || !context) {
        if (log) log("[spawn-pick] configured=1 hooked=0 reason=%s", shape ? "no-context" : "shape");
        return false;
    }
    g_register = reinterpret_cast<RegisterFn>(exeBase + RVA_REGISTER);
    auto* target = reinterpret_cast<void*>(exeBase + RVA_DISPATCH);
    MH_STATUS st = MH_CreateHook(target, reinterpret_cast<void*>(&Detour), reinterpret_cast<void**>(&g_original));
    if (st == MH_OK) st = MH_EnableHook(target);
    if (st != MH_OK) {
        if (log) log("[spawn-pick] configured=1 hooked=0 reason=minhook status=%d", static_cast<int>(st));
        return false;
    }
    if (log) log("[spawn-pick] configured=1 hooked=1 dispatch=%llX register=%llX lcg=%llX",
                 static_cast<unsigned long long>(RVA_DISPATCH), static_cast<unsigned long long>(RVA_REGISTER),
                 static_cast<unsigned long long>(RVA_LCG));
    return true;
}

}  // namespace kh2coop::inject::spawnpick
