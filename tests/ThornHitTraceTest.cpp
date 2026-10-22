// VUH-2617 (a) trace: which updating actor may be read (ThornHitTrace.hpp). A fake reader over owned memory
// counts every read by address, so a rejected candidate is shown to read nothing (Quill r2 finding 1).
#include "ThornHitTrace.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace kh2coop::inject::thornhittrace;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)

namespace {
constexpr Layout kLayout {0x918, 0x5C0, 0x00, 0x04, 0x08, 0x640 + 0x30, 0x180, 0x158 + 0x44, 0xD10};

struct Memory {
    std::vector<std::uint8_t> bytes = std::vector<std::uint8_t>(0x20000);
    std::uintptr_t base() const { return reinterpret_cast<std::uintptr_t>(bytes.data()); }
    std::vector<std::uintptr_t> reads;
    template <class T> void Put(std::uintptr_t at, T v) { std::memcpy(reinterpret_cast<void*>(at), &v, sizeof(v)); }
    bool Read(std::uintptr_t at, void* out, std::size_t n) {
        reads.push_back(at);
        if (at < base() || at + n > base() + bytes.size()) return false;
        std::memcpy(out, reinterpret_cast<const void*>(at), n);
        return true;
    }
    unsigned ReadsIn(std::uintptr_t lo, std::uintptr_t hi) const {
        unsigned n = 0;
        for (const auto r : reads) n += r >= lo && r < hi;
        return n;
    }
};

// An actor at base + slot * 0x2000 with an objentry at base + 0x18000 + slot * 0x40.
std::uintptr_t MakeActor(Memory& m, unsigned slot, std::uint32_t objectId, std::uintptr_t status, float x) {
    const auto actor = m.base() + slot * 0x2000, objentry = m.base() + 0x18000 + slot * 0x40;
    m.Put(actor + kLayout.objentry, objentry);
    m.Put(actor + kLayout.status, status);
    m.Put(objentry + kLayout.objectId, objectId);
    m.Put<std::uint8_t>(objentry + kLayout.type, 0);
    m.Put<std::uint16_t>(objentry + kLayout.name, 0x5F50);  // "P_"
    m.Put(actor + kLayout.pos, x);
    m.Put(actor + kLayout.pos + 4, 0.0f);
    m.Put(actor + kLayout.pos + 8, -800.0f);
    m.Put(actor + kLayout.mask, 0x200u);
    m.Put(actor + kLayout.motion, 202u);
    m.Put(actor + kLayout.motionTime, 393.0f);
    return actor;
}
Identity IdentityOf(Memory& m, std::uintptr_t actor) {
    Identity id;
    id.known = true;
    std::memcpy(&id.objentry, reinterpret_cast<const void*>(actor + kLayout.objentry), sizeof(id.objentry));
    std::memcpy(&id.status, reinterpret_cast<const void*>(actor + kLayout.status), sizeof(id.status));
    std::memcpy(&id.objectId, reinterpret_cast<const void*>(id.objentry + kLayout.objectId), sizeof(id.objectId));
    id.type = 0;
    id.namePrefix = 0x5F50;
    (void)m;
    return id;
}
} // namespace

int main() {
    Memory m;
    const auto reader = [&](std::uintptr_t at, void* out, std::size_t n) { return m.Read(at, out, n); };
    const auto local = MakeActor(m, 0, 90, 0x1000, 10.0f), clone = MakeActor(m, 1, 90, 0x2000, 330.0f);
    const auto boss = MakeActor(m, 2, 307, 0x3000, 0.0f), other = MakeActor(m, 3, 318, 0x4000, 5.0f);
    Candidates c;
    c.head = local; c.canonical = local;
    c.clone[0] = clone; c.cloneApplied[0] = true; c.cloneCurrent[0] = true; c.cloneBound[0] = IdentityOf(m, clone);

    // Admitted: the local player (head and canonical), the bound clone, the boss bound now.
    { Fields f; CHECK(Capture(local, c, kLayout, reader, f) == Role::Local && f.x == 10.0f && f.mask == 0x200 && f.time < 0); }
    { Fields f; CHECK(Capture(clone, c, kLayout, reader, f) == Role::Clone0 && f.x == 330.0f && f.motion == 202); }
    { auto b = c; b.bossBoundNow = true; Fields f;
      CHECK(Capture(boss, b, kLayout, reader, f) == Role::Boss && f.time == 393.0f && f.mask == 0x200); }
    // The mask is (D10 | D14) & ~D18.
    { m.Put(local + kLayout.mask + 4, 0x400u); m.Put(local + kLayout.mask + 8, 0x200u); Fields f;
      CHECK(Capture(local, c, kLayout, reader, f) == Role::Local && f.mask == 0x400); }

    // Rejected, with no read of the actor at all:
    m.reads.clear();
    { Fields f; CHECK(Capture(other, c, kLayout, reader, f) == Role::None); }                // not a candidate
    { auto b = c; b.bossBoundNow = false; Fields f;
      CHECK(Capture(boss, b, kLayout, reader, f) == Role::None); }                          // a boss retired since its binding
    { auto r = c; r.cloneApplied[0] = false; Fields f;
      CHECK(Capture(clone, r, kLayout, reader, f) == Role::None); }                          // the driver released it
    { auto r = c; r.cloneCurrent[0] = false; Fields f;
      CHECK(Capture(clone, r, kLayout, reader, f) == Role::None); }                          // not a current clone
    { auto r = c; r.cloneBound[0].known = false; Fields f;
      CHECK(Capture(clone, r, kLayout, reader, f) == Role::None); }                          // no binding snapshot
    { auto r = c; r.head = other; Fields f;
      CHECK(Capture(local, r, kLayout, reader, f) == Role::None); }                          // canonical, not the head
    { Fields f; CHECK(Capture(0, c, kLayout, reader, f) == Role::None); }
    CHECK(m.reads.empty());

    // An in-field free and reuse: the clone's address now holds another actor (another objentry and status).
    // Only its identity is read (it is the actor being updated); no position or mask read follows.
    m.reads.clear();
    { const auto objentry = m.base() + 0x18000 + 5 * 0x40;
      m.Put(clone + kLayout.objentry, objentry); m.Put(objentry + kLayout.objectId, 318u);
      m.Put(clone + kLayout.status, std::uintptr_t {0x9000});
      Fields f;
      CHECK(Capture(clone, c, kLayout, reader, f) == Role::None);
      CHECK(m.ReadsIn(clone + kLayout.pos, clone + kLayout.pos + 12) == 0 &&
            m.ReadsIn(clone + kLayout.mask, clone + kLayout.mask + 12) == 0 &&
            m.ReadsIn(clone + kLayout.motion, clone + kLayout.motion + 4) == 0);
      CHECK(m.ReadsIn(clone + kLayout.objentry, clone + kLayout.objentry + 8) == 1); }
    // Same objentry but a different status (another incarnation of the same kind): refused too.
    { auto r = c; r.cloneBound[0].status = 0x2000; m.Put(clone + kLayout.objentry, r.cloneBound[0].objentry);
      m.Put(clone + kLayout.status, std::uintptr_t {0x2100}); m.reads.clear(); Fields f;
      CHECK(Capture(clone, r, kLayout, reader, f) == Role::None && m.ReadsIn(clone + kLayout.pos, clone + kLayout.pos + 12) == 0); }

    // A failed field read refuses the whole snapshot.
    { auto b = c; b.bossBoundNow = true; Fields f;
      const auto outside = m.base() + m.bytes.size() - 0x100;  // pos beyond owned memory
      CHECK(Capture(outside, b, kLayout, reader, f) == Role::None); }

    std::printf("thornhittrace: %s\n", failures ? "FAIL" : "ok");
    return failures ? 1 : 0;
}
