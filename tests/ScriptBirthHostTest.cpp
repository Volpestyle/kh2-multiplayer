// VUH-2605 S3: the host's script-birth publication ledger (inject/src/ScriptBirthHost.hpp).
#include "ScriptBirthHost.hpp"
#include <cstdio>
#include <map>
#include <set>
#include <vector>

namespace sb = kh2coop::inject::scriptbirth;
namespace {
unsigned errors = 0;
void check(bool ok, const char* label) {
    std::printf("%s: %s\n", ok ? "PASS" : "FAIL", label);
    if (!ok) ++errors;
}
struct Sent { sb::Emit kind; std::uint64_t occurrence; std::uint16_t parent, child; };
struct World {
    std::map<std::uintptr_t, std::uint16_t> census; // child address -> announced netId
    std::set<std::uint16_t> deadParents;
    std::vector<Sent> sent;
    std::uint32_t generation = 1;
    bool full = false;
    void tick(sb::HostBirths& b, std::uint32_t frame) {
        b.Tick(frame, generation,
               [&](std::uintptr_t c) -> std::uint16_t { auto it = census.find(c); return it == census.end() ? 0 : it->second; },
               [&](std::uint16_t parent) { return !deadParents.count(parent); },
               [&](sb::Emit k, const sb::HostBirth& r) {
                   if (full) return false;
                   sent.push_back({k, r.occurrence, r.parentNetId, r.childNetId});
                   return true;
               });
    }
};
}

int main() {
    {
        sb::HostBirths b; World w;
        check(b.Add(1, 0x1000, 100, 1) == sb::Observe::Added, "an exact return is recorded");
        w.tick(b, 101);
        check(w.sent.empty(), "nothing is published before the host census announces the child");
        w.census[0x1000] = 2;
        w.tick(b, 102);
        check(w.sent.size() == 1 && w.sent[0].kind == sb::Emit::Begin && w.sent[0].parent == 1 && w.sent[0].child == 2 &&
              w.sent[0].occurrence == 1, "begin names parent, announced child netId and the occurrence");
        w.tick(b, 102 + sb::kHeartbeatFrames - 1);
        check(w.sent.size() == 1, "no repeat before the heartbeat");
        w.tick(b, 102 + sb::kHeartbeatFrames);
        check(w.sent.size() == 2 && w.sent[1].kind == sb::Emit::Begin && w.sent[1].occurrence == 1, "heartbeat repeats the identical begin");
        w.census.erase(0x1000);
        w.tick(b, 200);
        check(w.sent.size() == 3 && w.sent[2].kind == sb::Emit::End && w.sent[2].child == 2 && b.Live() == 0,
              "child no longer announced: one end, row retired");
        w.tick(b, 300);
        check(w.sent.size() == 3, "an end is sent exactly once");
    }
    {
        sb::HostBirths b; World w;
        b.Add(1, 0x1000, 0, 1);
        w.tick(b, sb::kMapFrames);
        check(w.sent.empty() && b.Live() == 0, "a return the census never announces is dropped without any record");
    }
    {
        sb::HostBirths b; World w;
        check(b.Add(0, 0x1000, 0, 1) == sb::Observe::Refused && b.Add(1, 0, 0, 1) == sb::Observe::Refused &&
              b.Add(1, 0x1000, 0, 0) == sb::Observe::Refused, "no parent, no child or no generation is refused");
        b.Add(1, 0x1000, 0, 1); w.census[0x1000] = 1; w.tick(b, 1);
        check(w.sent.empty(), "a child that resolves to the parent's own netId is never published");
    }
    {
        sb::HostBirths b; World w;
        b.Add(1, 0x1000, 0, 1); w.census[0x1000] = 2; w.tick(b, 1);
        w.census[0x1000] = 3; // the census now names a different enemy at that address
        w.tick(b, 2);
        check(w.sent.size() == 2 && w.sent[1].kind == sb::Emit::End && w.sent[1].child == 2, "a relabelled address ends the old child");
    }
    {
        sb::HostBirths b; World w;
        b.Add(1, 0x1000, 0, 1); w.census[0x1000] = 2; w.tick(b, 1);
        check(b.Add(1, 0x1000, 5, 1) == sb::Observe::Added, "a second return at the same address is a new body");
        w.census[0x1000] = 4; w.tick(b, 6);
        check(w.sent.size() == 3 && w.sent[1].kind == sb::Emit::End && w.sent[1].occurrence == 1 &&
              w.sent[2].kind == sb::Emit::Begin && w.sent[2].occurrence == 2 && w.sent[2].child == 4,
              "the earlier child at that address ends before its successor begins, with a new occurrence");
    }
    // Sage S3 P2-1: an address reused before the census relabels it must not republish the old netId.
    {
        sb::HostBirths b; World w;
        b.Add(1, 0x1000, 0, 1); w.census[0x1000] = 2; w.tick(b, 1);
        b.Add(1, 0x1000, 5, 1); // new body; the census row still carries the old child's netId
        w.tick(b, 6);
        check(w.sent.size() == 2 && w.sent[1].kind == sb::Emit::End && w.sent[1].child == 2 && w.sent[1].occurrence == 1,
              "reuse with a cached netId: the old child ends");
        w.tick(b, 7);
        check(w.sent.size() == 2, "the new body is never published under the old child's netId");
        w.census[0x1000] = 5; w.tick(b, 8);
        check(w.sent.size() == 3 && w.sent[2].kind == sb::Emit::Begin && w.sent[2].child == 5 && w.sent[2].occurrence == 2,
              "once the census announces a fresh netId, the waiting child is published under it");
    }
    {
        sb::HostBirths b; World w;
        b.Add(1, 0x1000, 0, 1); w.census[0x1000] = 2; w.tick(b, 1);
        b.Add(1, 0x1000, 5, 1); w.tick(b, 6);
        w.tick(b, 5 + sb::kMapFrames);
        check(w.sent.size() == 2 && b.Live() == 0, "a reused address that keeps the old netId is dropped unpublished");
    }
    {
        sb::HostBirths b; World w;
        b.Add(1, 0x1000, 0, 1); w.census[0x1000] = 2; w.tick(b, 1);
        w.census.erase(0x1000); w.tick(b, 2);
        b.Add(1, 0x2000, 3, 1); w.census[0x2000] = 2; // a delayed census row naming the ended child
        w.tick(b, 4);
        check(w.sent.size() == 2, "a netId already published this room is never published again at another address");
        w.census[0x2000] = 6; w.tick(b, 5);
        check(w.sent.size() == 3 && w.sent[2].kind == sb::Emit::Begin && w.sent[2].child == 6, "the qualified new child is published");
    }
    {
        sb::HostBirths b; World w;
        b.Add(1, 0x1000, 0, 1); w.census[0x1000] = 2; w.tick(b, 1);
        w.census.erase(0x1000); w.full = true; w.tick(b, 2);
        check(w.sent.size() == 1, "a full sender holds the end");
        w.full = false; w.tick(b, 3);
        check(w.sent.size() == 2 && w.sent[1].kind == sb::Emit::End, "the held end is retried, not lost");
    }
    {
        sb::HostBirths b; World w;
        b.Add(1, 0x1000, 0, 1); w.census[0x1000] = 2; w.full = true; w.tick(b, 1);
        check(w.sent.empty(), "a full sender holds the first begin");
        w.full = false; w.tick(b, 2);
        check(w.sent.size() == 1 && w.sent[0].kind == sb::Emit::Begin, "the first begin goes out on the next tick");
    }
    {
        sb::HostBirths b;
        for (unsigned i = 0; i < sb::kHostCap; ++i) b.Add(1, 0x1000 + i * 0x10, 0, 1);
        check(b.Add(1, 0x9000, 0, 1) == sb::Observe::Full, "the ledger is bounded and never evicts a recorded birth");
    }
    {
        sb::HostBirths b; World w;
        b.Add(1, 0x1000, 0, 1); w.census[0x1000] = 2; w.tick(b, 1);
        const auto next = b.NextOccurrence();
        b.Reset();
        check(b.Live() == 0 && b.NextOccurrence() == next, "a room reset drops rows but never rewinds occurrences");
        b.Add(1, 0x1000, 2, 1); w.tick(b, 3);
        check(w.sent.back().occurrence == next && w.sent.back().child == 2,
              "after a room reset the next birth takes a fresh occurrence and the new room's netIds");
    }
    // Sage S3 P2-2: Host -> Off -> Host. Rows never cross a session generation, with or without a reset.
    {
        sb::HostBirths b; World w;
        b.Add(1, 0x1000, 0, 1); w.census[0x1000] = 2; w.tick(b, 1); // published in generation 1
        b.Add(1, 0x2000, 1, 1);                                     // recorded, not yet announced
        w.generation = 2; w.census[0x2000] = 3; w.tick(b, 2);
        check(w.sent.size() == 1 && b.Live() == 0,
              "a new generation drops the old generation's rows: no end and no late begin");
        b.Add(1, 0x3000, 3, 2); w.census[0x3000] = 4; w.tick(b, 4);
        check(w.sent.size() == 2 && w.sent[1].kind == sb::Emit::Begin && w.sent[1].child == 4,
              "the new generation's own birth publishes normally");
    }
    {
        sb::HostBirths b; World w;
        b.Add(1, 0x1000, 0, 1); w.census[0x1000] = 2; w.tick(b, 1);
        b.Reset(); // host stops
        w.generation = 2; w.tick(b, 2);
        check(w.sent.size() == 1 && b.Live() == 0, "a stopped host keeps nothing to resume in its next session");
    }
    // Sage S3 P2-3: a dead parent stops begins and heartbeats; its children's ends still go out.
    {
        sb::HostBirths b; World w;
        b.Add(1, 0x1000, 0, 1); w.census[0x1000] = 2; w.tick(b, 1);
        b.Add(1, 0x2000, 2, 1);                     // recorded, not yet announced
        w.deadParents.insert(1);
        w.census[0x2000] = 3;
        w.tick(b, 1 + sb::kHeartbeatFrames);
        check(w.sent.size() == 1 && b.Live() == 1, "after parent death no heartbeat and no first begin");
        w.census.erase(0x1000); w.tick(b, 2 + sb::kHeartbeatFrames);
        check(w.sent.size() == 2 && w.sent[1].kind == sb::Emit::End && w.sent[1].child == 2 && b.Live() == 0,
              "the published child of a dead parent still ends exactly once");
    }
    std::printf("failures=%u\n", errors);
    return errors ? 1 : 0;
}
