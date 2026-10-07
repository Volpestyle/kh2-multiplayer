#pragma once
// ============================================================================
// SpawnPick: host-agreed random spawn picks (VUH-1515, live run 102159).
//
// The area-script opcode dispatcher FUN_1403a24c0 draws RandomSpawn (opcode 2:
// one of n spawn groups) and CasualSpawn (opcode 3: a group with p% chance) from
// the game-wide LCG at RVA 0x783CA0 (seed *= 0x10dcd). That LCG has 274
// references in 57 functions, so two machines never share its state; in 102159
// the host drew Gargoyle Knights (b_80) and the friend Gargoyle Warriors (b_81).
//
// Here both machines replace the draw with the same deterministic pick:
//   hash(salt, epoch, world, room, map, btl, evt, opcode, opOffset, n, args)
// salt: the relay's world incarnation id (both runtimes receive it in
//   SessionState; each publishes FNV-1a64 of it in the world bridge header).
// epoch: the host's instance epoch for this visit, so every visit rerolls as in
//   vanilla. The host predicts it at load (g_epoch + 1, the value its
//   RoomTransition will carry); a client uses the epoch of the host-issued load
//   it is executing (Warp's HostIssuedLoadEpoch).
// Pure rules only: no game memory, no hooks (SpawnPickHook.cpp owns those).
// ============================================================================

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <intrin.h>

namespace kh2coop::inject::spawnpick {

inline constexpr std::uintptr_t RVA_DISPATCH = 0x3A24C0;  // FUN_1403a24c0(script, op) -> char (0 ends the program)
inline constexpr std::uintptr_t RVA_REGISTER = 0x3A4E80;  // FUN_1403a4e80(groupName u32, flag u8)
inline constexpr std::uintptr_t RVA_LCG = 0x783CA0;       // u32 seed, *= 0x10dcd per draw
inline constexpr std::uint32_t kLcgMultiplier = 0x10dcd;
inline constexpr std::uint16_t kOpRandomSpawn = 2, kOpCasualSpawn = 3;

// ---- the native draw, exactly as compiled (see the shape bytes below) -------------------------
inline std::uint32_t LcgNext(std::uint32_t seed) noexcept { return seed * kLcgMultiplier; }
// case 2: idx = (u64)seed / (((u64)(u32)(n - 1) + 2^32) / (u64)(u32)n)
inline std::uint32_t NativeIndex(std::uint32_t seed, std::int16_t n) noexcept {
    const auto un = static_cast<std::uint64_t>(static_cast<std::uint32_t>(static_cast<std::int32_t>(n)));
    if (un == 0) return 0;  // native divides by zero here; the hook never calls it with n <= 0
    const std::uint64_t q = (static_cast<std::uint64_t>(static_cast<std::uint32_t>(static_cast<std::int32_t>(n) - 1)) +
                             0x100000000ull) / un;
    return static_cast<std::uint32_t>(static_cast<std::uint64_t>(seed) / q);
}
// case 3: fired when seed / 0x28f5c29 < percent (the compiler's magic multiply, kept as both forms for tests)
inline std::uint32_t NativeRoll(std::uint32_t seed) noexcept { return seed / 0x28f5c29u; }
inline std::uint32_t NativeRollMagic(std::uint32_t seed) noexcept {
    return static_cast<std::uint32_t>(__umulh(seed, 0xC7FFFFFCE000000Dull) >> 25);
}

// ---- the shared pick --------------------------------------------------------------------------
inline std::uint64_t Mix(std::uint64_t z) noexcept {  // SplitMix64 finaliser
    z += 0x9E3779B97F4A7C15ull;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}
inline std::uint64_t Fold(std::uint64_t h, std::uint64_t v) noexcept { return Mix(h ^ Mix(v)); }

struct Key {
    std::uint64_t salt = 0;
    std::uint32_t epoch = 0;
    std::uint16_t world = 0, room = 0, map = 0, btl = 0, evt = 0;
    std::uint16_t opcode = 0;
    std::uint32_t opOffset = 0;  // op address - script base, bytes
    std::int16_t n = 0;
    const std::uint32_t* args = nullptr;  // the op's n argument words
};

inline std::uint64_t Hash(const Key& k) noexcept {
    std::uint64_t h = Mix(k.salt);
    h = Fold(h, k.epoch);
    h = Fold(h, (static_cast<std::uint64_t>(k.world) << 48) | (static_cast<std::uint64_t>(k.room) << 32) |
                    (static_cast<std::uint64_t>(k.map) << 16) | k.btl);
    h = Fold(h, (static_cast<std::uint64_t>(k.evt) << 48) | (static_cast<std::uint64_t>(k.opcode) << 32) | k.opOffset);
    h = Fold(h, static_cast<std::uint16_t>(k.n));
    for (std::int16_t i = 0; k.args && i < k.n; ++i) h = Fold(h, k.args[i]);
    return h;
}
// The op's offset from the script base, only when it is plausibly inside the script (review F2): the
// dispatcher's first argument is the script base for FUN_1403a23e0, but FUN_1403a2470's caller is unknown,
// so a pointer pair outside [0, 0x10000) would hash a per-machine heap distance. False keeps native.
inline constexpr std::uintptr_t kMaxOpOffset = 0x10000;
inline bool OpOffset(std::uintptr_t script, std::uintptr_t op, std::uint32_t& out) noexcept {
    if (op < script || op - script >= kMaxOpOffset) return false;
    out = static_cast<std::uint32_t>(op - script);
    return true;
}

// RandomSpawn: an index in [0, n); n must be >= 1.
inline std::uint32_t PickIndex(std::uint64_t hash, std::int16_t n) noexcept {
    return n > 0 ? static_cast<std::uint32_t>(hash % static_cast<std::uint64_t>(n)) : 0;
}
// CasualSpawn: fired with percent% probability (percent >= 100 always, 0 never).
inline bool CasualFires(std::uint64_t hash, std::uint32_t percent) noexcept {
    return static_cast<std::uint32_t>(hash % 100) < percent;
}

// FNV-1a 64 of the relay's world incarnation id; 0 means "no salt" and is never produced for a non-empty id.
inline std::uint64_t SaltFromSession(const char* id, std::size_t length) noexcept {
    if (!id || length == 0) return 0;
    std::uint64_t h = 0xcbf29ce484222325ull;
    for (std::size_t i = 0; i < length; ++i) { h ^= static_cast<unsigned char>(id[i]); h *= 0x100000001b3ull; }
    return h ? h : 1;
}

// ---- shape check: the native bytes the re-implementation reproduces (PE 9002B2DE...) ----------
struct Shape { std::uintptr_t rva; const std::uint8_t* bytes; std::size_t size; };
// Dispatcher prologue (the MinHook target).
inline constexpr std::uint8_t kDispatchEntry[] = {
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57,
    0x48, 0x83, 0xEC, 0x20, 0x48, 0x0F, 0xBF, 0x02, 0x48, 0x8B, 0xDA, 0x48, 0x8B, 0xF1};
// case 2 at 0x3A2545: movsx edx,[rdx+2]; mov rcx,2^32; imul eax,[0x783CA0],0x10dcd; mov [0x783CA0],eax; mov r8d,eax;
// lea eax,[rdx-1]; add rax,rcx; mov ecx,edx; xor edx,edx; div rcx; xor edx,edx; mov rcx,rax; mov eax,r8d; div rcx;
// xor edx,edx; movsxd rcx,eax; mov ecx,[rbx+rcx*4+4]; call 0x3A4E80
inline constexpr std::uint8_t kCase2[] = {
    0x0F, 0xBF, 0x52, 0x02, 0x48, 0xB9, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x69, 0x05, 0x43, 0x17, 0x3E, 0x00, 0xCD, 0x0D, 0x01, 0x00, 0x89, 0x05, 0x3D, 0x17, 0x3E, 0x00,
    0x44, 0x8B, 0xC0, 0x8D, 0x42, 0xFF, 0x48, 0x03, 0xC1, 0x8B, 0xCA, 0x33, 0xD2, 0x48, 0xF7, 0xF1,
    0x33, 0xD2, 0x48, 0x8B, 0xC8, 0x41, 0x8B, 0xC0, 0x48, 0xF7, 0xF1, 0x33, 0xD2, 0x48, 0x63, 0xC8,
    0x8B, 0x4C, 0x8B, 0x04, 0xE8, 0xF4, 0x28, 0x00, 0x00};
inline constexpr std::uintptr_t RVA_CASE2 = 0x3A2545;
// case 3 at 0x3A2591: imul eax,[0x783CA0],0x10dcd; mov [0x783CA0],eax; mov ecx,eax; mov rax,0xC7FFFFFCE000000D;
// mul rcx; shr rdx,25; cmp edx,[rbx+4]; jae end; mov ecx,[rbx+8]; xor edx,edx; call 0x3A4E80; xor al,al
inline constexpr std::uint8_t kCase3[] = {
    0x69, 0x05, 0x05, 0x17, 0x3E, 0x00, 0xCD, 0x0D, 0x01, 0x00, 0x89, 0x05, 0xFF, 0x16, 0x3E, 0x00,
    0x8B, 0xC8, 0x48, 0xB8, 0x0D, 0x00, 0x00, 0xE0, 0xFC, 0xFF, 0xFF, 0xC7, 0x48, 0xF7, 0xE1,
    0x48, 0xC1, 0xEA, 0x19, 0x3B, 0x53, 0x04, 0x0F, 0x83, 0x73, 0x04, 0x00, 0x00, 0x8B, 0x4B, 0x08,
    0x33, 0xD2, 0xE8, 0xB9, 0x28, 0x00, 0x00, 0x32, 0xC0};
inline constexpr std::uintptr_t RVA_CASE3 = 0x3A2591;
// FUN_1403a4e80 prologue (called directly by the re-implementation).
inline constexpr std::uint8_t kRegisterEntry[] = {
    0x40, 0x55, 0x56, 0x41, 0x56, 0x48, 0x83, 0xEC, 0x20, 0x44, 0x0F, 0xB6, 0xF2, 0x8B, 0xF1};

inline constexpr Shape kShapes[] = {
    {RVA_DISPATCH, kDispatchEntry, sizeof(kDispatchEntry)},
    {RVA_CASE2, kCase2, sizeof(kCase2)},
    {RVA_CASE3, kCase3, sizeof(kCase3)},
    {RVA_REGISTER, kRegisterEntry, sizeof(kRegisterEntry)},
};

// rip-relative imul/mov targets inside the case bytes must resolve to the LCG, and the calls to the
// register function: these tie the shape bytes to RVA_LCG / RVA_REGISTER rather than trusting the constants.
inline std::uintptr_t RipTarget(std::uintptr_t instrRva, std::size_t instrLength, std::int32_t disp) noexcept {
    return instrRva + instrLength + static_cast<std::intptr_t>(disp);
}
inline std::int32_t Disp32(const std::uint8_t* p) noexcept {
    std::int32_t d = 0;
    std::memcpy(&d, p, sizeof(d));
    return d;
}
inline bool ShapeTargetsConsistent() noexcept {
    // case 2: imul at +14 (10 bytes, disp at +16), mov at +24 (6 bytes, disp at +26), call at +66 (5 bytes)
    // case 3: imul at +0, mov at +10, call at +49
    return RipTarget(RVA_CASE2 + 14, 10, Disp32(kCase2 + 16)) == RVA_LCG &&
           RipTarget(RVA_CASE2 + 24, 6, Disp32(kCase2 + 26)) == RVA_LCG &&
           RipTarget(RVA_CASE2 + 66, 5, Disp32(kCase2 + 67)) == RVA_REGISTER &&
           RipTarget(RVA_CASE3 + 0, 10, Disp32(kCase3 + 2)) == RVA_LCG &&
           RipTarget(RVA_CASE3 + 10, 6, Disp32(kCase3 + 12)) == RVA_LCG &&
           RipTarget(RVA_CASE3 + 49, 5, Disp32(kCase3 + 50)) == RVA_REGISTER;
}

// ---- script walking (tests and diagnostics; the hook never parses) -----------------------------
// Programs: [i16 id, i16 byteSize] then ops [i16 opcode, i16 n, n x u32]; id -1 ends the script.
struct Op { std::int16_t program; std::uint32_t offset; std::int16_t opcode, n; const std::uint8_t* args; };
template <class Fn>
inline bool ForEachOp(const std::uint8_t* script, std::size_t size, Fn&& fn) noexcept {
    std::size_t o = 0;
    while (o + 4 <= size) {
        std::int16_t id = 0, bytes = 0;
        std::memcpy(&id, script + o, 2);
        std::memcpy(&bytes, script + o + 2, 2);
        if (id == -1) return true;
        if (bytes < 4 || o + static_cast<std::size_t>(bytes) > size) return false;
        std::size_t p = o + 4;
        const std::size_t end = o + static_cast<std::size_t>(bytes);
        while (p + 4 <= end) {
            std::int16_t op = 0, n = 0;
            std::memcpy(&op, script + p, 2);
            std::memcpy(&n, script + p + 2, 2);
            if (n < 0 || p + 4 + 4 * static_cast<std::size_t>(n) > end) return false;
            fn(Op {id, static_cast<std::uint32_t>(p), op, n, script + p + 4});
            p += 4 + 4 * static_cast<std::size_t>(n);
        }
        o = end;
    }
    return false;  // no terminator
}

}  // namespace kh2coop::inject::spawnpick
