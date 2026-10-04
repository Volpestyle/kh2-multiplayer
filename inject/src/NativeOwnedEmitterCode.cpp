#include "NativeOwnedEmitterCode.hpp"

#include <cstring>
#include <initializer_list>
#include <limits>

namespace kh2coop::inject::ownedemitter {
namespace {
using U = std::uint64_t;
static_assert(offsetof(PrimitiveState, live) == 0);
static_assert(offsetof(PrimitiveState, s) == 8);
static_assert(offsetof(PrimitiveState, w) == 16);
static_assert(offsetof(PrimitiveState, d) == 24);
static_assert(offsetof(PrimitiveState, e) == 32);
static_assert(offsetof(PrimitiveState, gatewayRA) == 40);
static_assert(offsetof(PrimitiveState, emitterRA) == 48);
static_assert(offsetof(PrimitiveState, wrapperRA) == 56);
static_assert(offsetof(PrimitiveState, controller) == 64);
static_assert(offsetof(PrimitiveState, record) == 72);
static_assert(offsetof(PrimitiveState, actualW) == 80);
static_assert(offsetof(PrimitiveState, actualRA) == 88);
static_assert(offsetof(PrimitiveState, actualC) == 96);
static_assert(offsetof(PrimitiveState, actualR) == 104);
static_assert(offsetof(PrimitiveState, entrySerial) == 112);
static_assert(offsetof(PrimitiveState, invocation) == 120);
static_assert(offsetof(PrimitiveState, rawRax) == 128);
static_assert(offsetof(PrimitiveState, stops) == 136);
static_assert(offsetof(PrimitiveState, continueInput) == 144);
static_assert(offsetof(PrimitiveState, retired) == 152);
static_assert(offsetof(PrimitiveState, unwound) == 160);
static_assert(offsetof(PrimitiveState, gateS) == 168);
static_assert(offsetof(PrimitiveState, index) == 176);
static_assert(offsetof(PrimitiveState, qualified) == 184);
static_assert(offsetof(PrimitiveState, invalid) == 192);
static_assert(offsetof(PrimitiveState, activation) == 200);
static_assert(offsetof(PrimitiveState, phase) == 208);
static_assert(offsetof(PrimitiveState, attempts) == 216);
static_assert(offsetof(PrimitiveState, expectedHeader) == 224);
static_assert(offsetof(PrimitiveState, recordBase) == 232);
static_assert(offsetof(PrimitiveState, admissionSerial) == 240);
static_assert(offsetof(PrimitiveState, unknown) == 248);
static_assert(offsetof(PrimitiveState, returned) == 256);
static_assert(offsetof(PrimitiveState, preStops) == 264);
static_assert(offsetof(PrimitiveState, postStops) == 272);

constexpr std::array<std::uint16_t, 3> Sizes{388, 433, 246};
constexpr std::uint32_t NativeSize = 0x32a, NativeUnwindSize = 60;

// Private finite writer for these three instruction sequences, not a public
// assembler. Every append/fixup is bounded; no dynamic allocation or OS action.
struct Builder {
    IslandCode& code;
    std::size_t at = 0;
    bool valid = true;
    void byte(std::uint8_t value) {
        if (at >= code.bytes.size()) {
            valid = false;
            return;
        }
        code.bytes[at++] = value;
    }
    void op(std::initializer_list<std::uint8_t> bytes) {
        if (code.instructionCount >= code.instructionOffsets.size()) {
            valid = false;
            return;
        }
        code.instructionOffsets[code.instructionCount++] = static_cast<std::uint16_t>(at);
        for (auto v : bytes)
            byte(v);
    }
    void u32(std::uint32_t n) {
        for (unsigned i = 0; i < 4; ++i)
            byte(static_cast<std::uint8_t>(n >> (i * 8)));
    }
    void u64(U n) {
        for (unsigned i = 0; i < 8; ++i)
            byte(static_cast<std::uint8_t>(n >> (i * 8)));
    }
    void imm11(U n) {
        op({0x49, 0xbb});
        u64(n);
    }
    void jump(U dest) {
        imm11(dest);
        op({0x41, 0xff, 0xe3});
    }
    std::size_t jcc(std::uint8_t condition) {
        op({0x0f, condition});
        auto p = at;
        u32(0);
        return p;
    }
    std::size_t jne() { return jcc(0x85); }
    std::size_t jz() { return jcc(0x84); }
    std::size_t jmp() {
        op({0xe9});
        auto p = at;
        u32(0);
        return p;
    }
    void patch(std::size_t p, std::size_t target) {
        if (p > at || at - p < 4 || target > at) {
            valid = false;
            return;
        }
        const auto delta = static_cast<std::int32_t>(target) - static_cast<std::int32_t>(p) - 4;
        const auto saved = at;
        at = p;
        u32(static_cast<std::uint32_t>(delta));
        at = saved;
    }
    void load10(unsigned off) {
        op({0x4d, 0x8b, 0x93});
        u32(off);
    }
    void cmp10(unsigned off) {
        op({0x4d, 0x3b, 0x93});
        u32(off);
    }
    void cmpq(unsigned off, unsigned value) {
        op({0x49, 0x83, 0xbb});
        u32(off);
        byte(static_cast<std::uint8_t>(value));
    }
    void setq(unsigned off, unsigned value) {
        op({0x49, 0xc7, 0x83});
        u32(off);
        u32(value);
    }
    void incq(unsigned off) {
        op({0x49, 0xff, 0x83});
        u32(off);
    }
    void store10(unsigned off) {
        op({0x4d, 0x89, 0x93});
        u32(off);
    }
};
struct Fixups {
    std::array<std::size_t, 32> values{};
    std::size_t size = 0;
    bool valid = true;
    void push_back(std::size_t value) {
        if (size == values.size()) {
            valid = false;
            return;
        }
        values[size++] = value;
    }
    const std::size_t* begin() const { return values.data(); }
    const std::size_t* end() const { return values.data() + size; }
};
struct Extent {
    U begin, end;
};
bool ExtentOf(U address, U bytes, Extent& out) {
    if (!address || !bytes || bytes > std::numeric_limits<U>::max() - address)
        return false;
    out = {address, address + bytes};
    return true;
}
bool Relative(U base, U address, std::uint32_t& out) {
    if (address < base || address - base > std::numeric_limits<std::uint32_t>::max())
        return false;
    out = static_cast<std::uint32_t>(address - base);
    return true;
}
bool Rel32(U from, U target, std::uint32_t& out) {
    if (target >= from) {
        if (target - from > 0x7fffffffULL)
            return false;
        out = static_cast<std::uint32_t>(target - from);
    } else {
        if (from - target > 0x80000000ULL)
            return false;
        out = 0U - static_cast<std::uint32_t>(from - target);
    }
    return true;
}
void Put32(std::uint8_t* at, std::uint32_t n) {
    for (unsigned i = 0; i < 4; ++i)
        at[i] = static_cast<std::uint8_t>(n >> (i * 8));
}
} // namespace

CodeStatus BuildCodePlan(const CodeRequest& r, CodePlan& out) noexcept {
    out = {};
    auto fail = [&](CodeStatus status) {
        out.status = status;
        return status;
    };
    Extent state{};
    if (r.stateAddress % alignof(PrimitiveState) != 0 ||
        !ExtentOf(r.stateAddress, sizeof(PrimitiveState), state))
        return fail(CodeStatus::InvalidStateAddress);
    Extent native{};
    if (!ExtentOf(r.emitterBegin, NativeSize, native) || native.end != r.emitterEnd ||
        r.aContinuation != r.emitterBegin + 0x140 ||
        r.bNullContinuation != r.emitterBegin + 0x2b3 ||
        r.bNonNullContinuation != r.emitterBegin + 0x29e ||
        r.cContinuation != r.emitterBegin + 0x2ba || r.epilogue != r.emitterBegin + 0x2e5)
        return fail(CodeStatus::InvalidNativeLayout);
    if (r.emitterUnwindCapacity < NativeUnwindSize || r.chainInfoCapacity < 16)
        return fail(CodeStatus::InsufficientExtent);
    if (r.unwindAddressBase % 4 || r.emitterUnwind % 4 || r.chainInfoAddress % 4)
        return fail(CodeStatus::InvalidAddressRange);
    // Include declared storage extents, not just emitted prefix bytes. State
    // is absolute, but every runtime/unwind address must fit one unsigned RVA base.
    std::array<Extent, 7> extents{};
    extents[0] = native;
    extents[6] = state;
    if (!ExtentOf(r.emitterUnwind, r.emitterUnwindCapacity, extents[1]) ||
        !ExtentOf(r.chainInfoAddress, r.chainInfoCapacity, extents[2]))
        return fail(CodeStatus::InvalidAddressRange);
    for (std::size_t i = 0; i < GateCount; ++i) {
        if (r.islandCapacities[i] < Sizes[i])
            return fail(CodeStatus::InsufficientExtent);
        if (!ExtentOf(r.islandAddresses[i], r.islandCapacities[i], extents[3 + i]))
            return fail(CodeStatus::InvalidAddressRange);
    }
    for (std::size_t i = 0; i < extents.size(); ++i) {
        if (i != 6) {
            std::uint32_t unused{};
            if (!Relative(r.unwindAddressBase, extents[i].begin, unused) ||
                !Relative(r.unwindAddressBase, extents[i].end, unused))
                return fail(CodeStatus::InvalidAddressRange);
        }
        for (std::size_t j = 0; j < i; ++j)
            if (extents[i].begin < extents[j].end && extents[j].begin < extents[i].end)
                return fail(CodeStatus::OverlappingExtents);
    }
    CodePlan plan{};
    std::uint32_t chainRva{};
    if (!Relative(r.unwindAddressBase, r.chainInfoAddress, chainRva) ||
        !Relative(r.unwindAddressBase, r.emitterBegin, plan.parent.begin) ||
        !Relative(r.unwindAddressBase, r.emitterEnd, plan.parent.end) ||
        !Relative(r.unwindAddressBase, r.emitterUnwind, plan.parent.unwindData))
        return fail(CodeStatus::InvalidAddressRange);
    constexpr std::array<unsigned, 3> SiteOffsets{0x139, 0x299, 0x2b3};
    constexpr std::array<std::uint8_t, 3> BranchSizes{7, 5, 7};
    constexpr std::array<std::array<std::uint8_t, 7>, 3> Expected{
        {{0x48, 0x83, 0xc1, 0x2c, 0x49, 0x63, 0xc6},
         {0x48, 0x85, 0xc0, 0x74, 0x15, 0, 0},
         {0x48, 0x8b, 0x4f, 0x08, 0x41, 0xff, 0xc6}}};
    for (std::size_t i = 0; i < GateCount; ++i) {
        auto& branch = plan.branches[i];
        branch.address = r.emitterBegin + SiteOffsets[i];
        branch.target = r.islandAddresses[i];
        branch.expected = Expected[i];
        branch.size = BranchSizes[i];
        std::uint32_t delta{};
        if (!Rel32(branch.address + 5, branch.target, delta))
            return fail(CodeStatus::InvalidAddressRange);
        branch.replacement.fill(0x90);
        branch.replacement[0] = 0xe9;
        Put32(branch.replacement.data() + 1, delta);
        auto& f = plan.functions[i];
        f.unwindData = chainRva;
        if (!Relative(r.unwindAddressBase, r.islandAddresses[i], f.begin) ||
            !Relative(r.unwindAddressBase, r.islandAddresses[i] + Sizes[i], f.end))
            return fail(CodeStatus::InvalidAddressRange);
    }
    plan.chainInfo[0] = 0x21;
    Put32(plan.chainInfo.data() + 4, plan.parent.begin);
    Put32(plan.chainInfo.data() + 8, plan.parent.end);
    Put32(plan.chainInfo.data() + 12, plan.parent.unwindData);
    Builder b{plan.islands[1]};
    Fixups no, bad;
    b.op({0x4c, 0x89, 0x14, 0x24}); // R10 spill in outgoing shadow space; no RSP movement
    b.imm11(r.stateAddress);
    b.op({0x49, 0x83, 0x3b, 1});
    no.push_back(b.jne());
    b.op({0x49, 0x3b, 0xa3});
    b.u32(8);
    no.push_back(b.jne());
    b.op({0x4c, 0x8b, 0x94, 0x24});
    b.u32(0x128);
    b.cmp10(48);
    no.push_back(b.jne());
    b.op({0x4c, 0x8b, 0x94, 0x24});
    b.u32(0x178);
    b.cmp10(40);
    no.push_back(b.jne());
    b.op({0x49, 0x3b, 0xbb});
    b.u32(64);
    bad.push_back(b.jne());
    b.op({0x4d, 0x3b, 0xb3});
    b.u32(176);
    bad.push_back(b.jne());
    b.op({0x4c, 0x8d, 0x54, 0x24, 0xf8});
    b.cmp10(80);
    bad.push_back(b.jne());
    b.cmpq(208, 2);
    bad.push_back(b.jne());
    b.load10(200);
    b.cmp10(120);
    bad.push_back(b.jne());
    b.load10(240);
    b.cmp10(200);
    bad.push_back(b.jne());
    b.load10(88);
    b.cmp10(56);
    bad.push_back(b.jne());
    b.load10(96);
    b.cmp10(64);
    bad.push_back(b.jne());
    b.load10(104);
    b.cmp10(72);
    bad.push_back(b.jne());
    b.load10(112);
    b.cmp10(120);
    bad.push_back(b.jne());
    b.op({0x49, 0xc7, 0x83});
    b.u32(184);
    b.u32(1);
    b.op({0xe9});
    auto common = b.at;
    b.u32(0);
    const auto invalid = b.at;
    for (auto pc : bad)
        b.patch(pc, invalid);
    b.op({0x49, 0xc7, 0x83});
    b.u32(184);
    b.u32(0);
    b.op({0x49, 0xff, 0x83});
    b.u32(192);
    b.setq(248, 1);
    b.patch(common, b.at);
    b.op({0x49, 0x89, 0x83});
    b.u32(128);
    b.op({0x49, 0x89, 0xa3});
    b.u32(168);
    b.setq(256, 1);
    b.setq(208, 3);
    b.op({0x48, 0x85, 0xc0});
    auto notNull = b.jne();
    b.setq(208, 4);
    b.patch(notNull, b.at);
    b.op({0x48, 0x85, 0xc0});
    auto nonzero = b.jne();
    b.op({0x49, 0xff, 0x83});
    b.u32(136);
    b.op({0x4c, 0x8b, 0x14, 0x24});
    b.op({0x48, 0x85, 0xc0});
    plan.islands[1].stopTail = static_cast<std::uint16_t>(b.at);
    b.jump(r.epilogue);
    const auto natural = b.at;
    for (auto x : no)
        b.patch(x, natural);
    b.patch(nonzero, natural);
    b.op({0x4c, 0x8b, 0x14, 0x24});
    b.op({0x48, 0x85, 0xc0});
    auto null = b.jz();
    plan.islands[1].replayTail = static_cast<std::uint16_t>(b.at);
    b.jump(r.bNonNullContinuation);
    const auto nullPC = b.at;
    plan.islands[1].nullTail = static_cast<std::uint16_t>(b.at);
    b.patch(null, nullPC);
    b.jump(r.bNullContinuation);
    auto frame = [&](Builder& x, Fixups& no) {
        x.op({0x4c, 0x89, 0x14, 0x24});
        x.imm11(r.stateAddress);
        x.cmpq(0, 1);
        no.push_back(x.jne());
        x.op({0x49, 0x3b, 0xa3});
        x.u32(8);
        no.push_back(x.jne());
        x.op({0x4c, 0x8b, 0x94, 0x24});
        x.u32(0x128);
        x.cmp10(48);
        no.push_back(x.jne());
        x.op({0x4c, 0x8b, 0x94, 0x24});
        x.u32(0x178);
        x.cmp10(40);
        no.push_back(x.jne());
    };
    Builder a{plan.islands[0]};
    Fixups aNo, aBad;
    frame(a, aNo);
    a.op({0x49, 0x3b, 0xbb});
    a.u32(64);
    aBad.push_back(a.jne());
    a.op({0x49, 0x3b, 0x8b});
    a.u32(224);
    aBad.push_back(a.jne()); // actual RCX header
    a.op({0x41, 0x83, 0xfe, 4});
    aBad.push_back(a.jcc(0x87));
    a.op({0x4d, 0x8b, 0xd6});
    a.op({0x49, 0xc1, 0xe2, 6});
    a.op({0x49, 0x3b, 0xf2});
    aBad.push_back(a.jne());
    a.load10(200);
    a.cmp10(120);
    aBad.push_back(a.jne());
    a.cmpq(120, 0);
    aBad.push_back(a.jz());
    a.cmpq(144, 1);
    aBad.push_back(a.jne());
    a.cmpq(208, 0);
    aBad.push_back(a.jne());
    a.cmpq(216, 0);
    aBad.push_back(a.jne());
    a.cmpq(248, 0);
    aBad.push_back(a.jne());
    a.op({0x4d, 0x89, 0xb3});
    a.u32(176); // current native index
    a.op({0x4c, 0x8d, 0x54, 0x31, 0x2c});
    a.store10(72); // actual header+offset+2C
    a.op({0x4c, 0x8d, 0x54, 0x24, 0xf8});
    a.store10(16);
    a.load10(200);
    a.store10(240);
    a.setq(112, 0);
    a.setq(208, 1);
    a.incq(216);
    auto aToReplay = a.jmp();
    const auto aDenied = a.at;
    for (auto pc : aBad)
        a.patch(pc, aDenied);
    a.setq(248, 1);
    a.setq(208, 5);
    a.incq(192);
    a.incq(136);
    a.incq(264);
    a.op({0x4c, 0x8b, 0x14, 0x24});
    plan.islands[0].stopTail = static_cast<std::uint16_t>(a.at);
    a.jump(r.epilogue);
    const auto aReplay = a.at;
    for (auto pc : aNo)
        a.patch(pc, aReplay);
    a.patch(aToReplay, aReplay);
    a.op({0x4c, 0x8b, 0x14, 0x24});
    a.op({0x48, 0x83, 0xc1, 0x2c});
    a.op({0x49, 0x63, 0xc6});
    plan.islands[0].replayTail = static_cast<std::uint16_t>(a.at);
    a.jump(r.aContinuation);
    Builder c{plan.islands[2]};
    Fixups cNo, cStop;
    c.op({0x0f, 0x92, 0x44, 0x24, 8}); // SETC outgoing shadow byte, flags unchanged
    frame(c, cNo);
    c.cmpq(256, 0);
    cStop.push_back(c.jne());
    c.cmpq(144, 1);
    cStop.push_back(c.jne());
    c.cmpq(248, 0);
    cStop.push_back(c.jne());
    c.cmpq(208, 0);
    cStop.push_back(c.jne());
    auto cToReplay = c.jmp();
    const auto cStopped = c.at;
    for (auto pc : cStop)
        c.patch(pc, cStopped);
    c.cmpq(256, 0);
    auto cReturned = c.jne();
    c.setq(248, 1);
    c.setq(208, 5);
    c.patch(cReturned, c.at);
    c.incq(136);
    c.incq(272);
    c.op({0x4c, 0x8b, 0x14, 0x24});
    plan.islands[2].stopTail = static_cast<std::uint16_t>(c.at);
    c.jump(r.epilogue);
    const auto cReplay = c.at;
    for (auto pc : cNo)
        c.patch(pc, cReplay);
    c.patch(cToReplay, cReplay);
    c.op({0x4c, 0x8b, 0x14, 0x24});
    c.op({0x0f, 0xba, 0x64, 0x24, 8, 0}); // BT bit0 restores CF consumed by original INC
    c.op({0x48, 0x8b, 0x4f, 8});
    c.op({0x41, 0xff, 0xc6});
    plan.islands[2].replayTail = static_cast<std::uint16_t>(c.at);
    c.jump(r.cContinuation);

    if (!a.valid || !b.valid || !c.valid || !no.valid || !bad.valid || !aNo.valid || !aBad.valid ||
        !cNo.valid || !cStop.valid || a.at != Sizes[0] || b.at != Sizes[1] || c.at != Sizes[2])
        return fail(CodeStatus::InternalLimit);
    for (std::size_t i = 0; i < GateCount; ++i)
        plan.islands[i].size = Sizes[i];
    plan.status = CodeStatus::Complete;
    out = plan;
    return out.status;
}
} // namespace kh2coop::inject::ownedemitter
