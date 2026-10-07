// VUH-1515: host-agreed random spawn picks (inject/src/SpawnPick.hpp). Pure rules; no game, no hooks, no sockets.
#include "SpawnPick.hpp"
#include "kh2coop/WorldBridge.hpp"
#include <cstdio>
#include <string>
#include <vector>

using namespace kh2coop::inject::spawnpick;
static int g_fail = 0, g_checks = 0;
#define CHECK(c) do { ++g_checks; if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++g_fail; } } while (0)

static std::vector<std::uint8_t> Hex(const char* s) {
    std::vector<std::uint8_t> out;
    for (std::size_t i = 0; s[i] && s[i + 1]; i += 2) {
        auto v = [](char c) { return c <= '9' ? c - '0' : c - 'a' + 10; };
        out.push_back(static_cast<std::uint8_t>(v(s[i]) * 16 + v(s[i + 1])));
    }
    return out;
}
static std::uint32_t Name(const char* four) {
    std::uint32_t v = 0;
    std::memcpy(&v, four, 4);
    return v;
}

// BB Entrance Hall bb00.ard "btl" (BAR type 0xD), 420 bytes, from the game's kh2_fourth package.
static const char* kBb00Btl =
    "0a002000040001002800000000000100625f323002000200625f3830625f38310b003400040001002a00000005000100cccc2400"
    "030002001e000000625f333000000100625f323102000200625f3830625f383101001c00040001002800000000000100625f3030"
    "000001007a5f373002003800040001002800000000000100625f303102000600625f3830625f3831625f3832625f3833625f3834"
    "625f3835000001007a5f373003002800040001002b00000000000100625f303202000200625f3830625f3831000001007a5f3730"
    "19000c00000001007a5f373024003c00040001002300000000000100625f323102000600625f3830625f3831625f3832625f3833"
    "625f3834625f38350300020032000000625f33304b0040000f00010002000000140000001500090048000000424230305f4d5332"
    "30320000000000000000000000000000000000000000000000000100625f3630600048000f000100020000001400000015000900"
    "48000000424230305f4d533230320000000000000000000000000000000000000000000000000100625f3630000001007a5f3730"
    "ffff0000";

int main() {
    // ---- the native formulas
    CHECK(LcgNext(1) == 0x10dcd && LcgNext(0) == 0 && LcgNext(0xFFFFFFFFu) == static_cast<std::uint32_t>(0u - 0x10dcdu));
    std::uint32_t seed = 12345;
    bool magicOk = true, rangeOk = true;
    for (int i = 0; i < 200000; ++i) {
        seed = LcgNext(seed);
        magicOk = magicOk && NativeRoll(seed) == NativeRollMagic(seed) && NativeRoll(seed) < 100;
        for (const int nn : {1, 2, 6, 8}) {
            const auto n = static_cast<std::int16_t>(nn);
            rangeOk = rangeOk && NativeIndex(seed, n) < static_cast<std::uint32_t>(n);
        }
    }
    CHECK(magicOk);  // the compiled magic multiply (0xC7FFFFFCE000000D >> 89) is seed / 0x28f5c29
    for (std::uint32_t s : {0u, 1u, 0x28f5c28u, 0x28f5c29u, 0x7FFFFFFFu, 0xFFFFFFFFu})
        CHECK(NativeRoll(s) == NativeRollMagic(s));
    CHECK(rangeOk);
    CHECK(NativeIndex(0, 2) == 0 && NativeIndex(0xFFFFFFFFu, 2) == 1 && NativeIndex(0x80000000u, 2) == 1 &&
          NativeIndex(0x7FFFFFFFu, 2) == 0 && NativeIndex(0xFFFFFFFFu, 8) == 7 && NativeIndex(5, 1) == 0);
    CHECK(NativeRoll(0xFFFFFFFFu) == 99 && NativeRoll(0) == 0);

    // ---- the shape bytes are tied to the LCG and the register function by their own rip targets
    CHECK(ShapeTargetsConsistent());
    CHECK(sizeof(kCase2) == 71 && sizeof(kCase3) == 56 && sizeof(kDispatchEntry) == 30 && sizeof(kRegisterEntry) == 15);
    CHECK(kCase2[14] == 0x69 && kCase2[20] == 0xCD && kCase2[66] == 0xE8 && kCase3[0] == 0x69 && kCase3[49] == 0xE8);

    // ---- the shared pick: both machines agree; inputs change it
    const std::uint32_t gargoyles[2] = {Name("b_80"), Name("b_81")};
    Key host;
    host.salt = 0x1234; host.epoch = 7; host.world = 5; host.room = 0; host.map = 0; host.btl = 3; host.evt = 5;
    host.opcode = kOpRandomSpawn; host.opOffset = 256; host.n = 2; host.args = gargoyles;
    Key client = host;  // the friend executes the same op of the same script at the same visit
    CHECK(Hash(host) == Hash(client) && PickIndex(Hash(host), 2) == PickIndex(Hash(client), 2));
    auto differs = [&](auto mutate) {
        int changed = 0;
        for (std::uint32_t e = 1; e <= 64; ++e) {
            Key a = host, b = host;
            a.epoch = b.epoch = e;
            mutate(b);
            changed += Hash(a) != Hash(b);
        }
        return changed == 64;
    };
    CHECK(differs([](Key& k) { k.salt ^= 1; }));
    CHECK(differs([](Key& k) { k.opOffset += 4; }));
    CHECK(differs([](Key& k) { k.btl = 2; }));
    CHECK(differs([](Key& k) { k.room = 6; }));
    CHECK(differs([](Key& k) { k.opcode = kOpCasualSpawn; }));
    const std::uint32_t swapped[2] = {Name("b_81"), Name("b_80")};
    CHECK(differs([&](Key& k) { k.args = swapped; }));
    // every visit rerolls: over 64 epochs both Gargoyle groups are drawn, roughly evenly
    int first = 0;
    for (std::uint32_t e = 1; e <= 64; ++e) { Key k = host; k.epoch = e; first += PickIndex(Hash(k), 2) == 0; }
    CHECK(first > 16 && first < 48);
    // n = 6 covers every index over 300 epochs
    const std::uint32_t six[6] = {Name("b_80"), Name("b_81"), Name("b_82"), Name("b_83"), Name("b_84"), Name("b_85")};
    bool seen[6] {};
    for (std::uint32_t e = 1; e <= 300; ++e) { Key k = host; k.n = 6; k.args = six; k.epoch = e; seen[PickIndex(Hash(k), 6)] = true; }
    CHECK(seen[0] && seen[1] && seen[2] && seen[3] && seen[4] && seen[5]);
    CHECK(PickIndex(Hash(host), 1) == 0 && PickIndex(123, 0) == 0);
    // CasualSpawn boundaries and rate
    CHECK(!CasualFires(0, 0) && CasualFires(0, 1) && CasualFires(~0ull, 100) && !CasualFires(99, 99) && CasualFires(98, 99));
    int fired = 0;
    for (std::uint32_t e = 1; e <= 1000; ++e) { Key k = host; k.opcode = kOpCasualSpawn; k.epoch = e; fired += CasualFires(Hash(k), 30); }
    CHECK(fired > 230 && fired < 370);

    // ---- review F2: only script-relative ops inside [0, 0x10000) take the shared path
    std::uint32_t off = 7;
    CHECK(OpOffset(0x1000, 0x1100, off) && off == 0x100);
    CHECK(OpOffset(0x1000, 0x1000, off) && off == 0);
    CHECK(OpOffset(0x1000, 0x1000 + 0xFFFF, off) && off == 0xFFFF);
    off = 7;
    CHECK(!OpOffset(0x1000, 0x1000 + 0x10000, off) && off == 7);  // just past the bound: native, out untouched
    CHECK(!OpOffset(0x2000, 0x1000, off) && off == 7);             // op below the base: native
    CHECK(!OpOffset(0x1000, 0x7FF600000000ull, off));              // a heap distance: native

    // ---- the salt: the runtime's bridge value and the DLL's rule are one function
    const std::string id = "0123456789abcdef0123456789abcdef";
    CHECK(SaltFromSession(id.data(), id.size()) == kh2coop::WorldBridge::SpawnPickSaltFromSession(id));
    CHECK(SaltFromSession(nullptr, 0) == 0 && kh2coop::WorldBridge::SpawnPickSaltFromSession("") == 0);
    CHECK(SaltFromSession("a", 1) != SaltFromSession("b", 1) && SaltFromSession("a", 1) != 0);

    // ---- the script layout the key's opOffset relies on: bb00's btl
    const auto btl = Hex(kBb00Btl);
    CHECK(btl.size() == 420);
    int random = 0, casual = 0;
    std::uint32_t prog3Offset = 0;
    std::uint32_t prog3Names[2] {};
    const bool walked = ForEachOp(btl.data(), btl.size(), [&](const Op& op) {
        if (op.opcode == kOpRandomSpawn) {
            ++random;
            if (op.program == 3) { prog3Offset = op.offset; std::memcpy(prog3Names, op.args, 8); }
        }
        if (op.opcode == kOpCasualSpawn) ++casual;
    });
    CHECK(walked);
    CHECK(random == 5 && casual == 2);  // programs 2, 3, 10, 11, 36 / 11, 36 (census data/random_ops.csv)
    CHECK(prog3Names[0] == Name("b_80") && prog3Names[1] == Name("b_81"));
    CHECK(prog3Offset > 0 && prog3Offset < btl.size());
    CHECK(btl[prog3Offset] == 2 && btl[prog3Offset + 2] == 2);  // the op the dispatcher receives: opcode 2, n 2
    bool inside = true;
    CHECK(!ForEachOp(btl.data(), 100, [&](const Op& op) {      // truncated script: refused, never walked past
        inside = inside && op.offset + 4 + 4u * static_cast<std::uint32_t>(op.n) <= 100;
    }));
    CHECK(inside);

    std::printf("%d/%d checks passed\n", g_checks - g_fail, g_checks);
    return g_fail ? 1 : 0;
}
