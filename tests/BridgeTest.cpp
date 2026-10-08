// BridgeTest — the DLL <-> runtime plumbing for world events, no KH2.
//
// Unit: PacketRing (order, wraparound, full ring refuses instead of
// overwriting, a producer thread against a consumer thread).
// End to end: a fake host "DLL" pushes an encoded RoomTransition into its
// WorldBridge; the host runtime pump forwards it through the relay; the
// client runtime hands it to the client's WorldBridge, where a fake client
// "DLL" decodes it. A client DLL's HitClaim travels back to the host DLL with
// the attacker slot stamped. Non-world packets from a DLL are rejected.
// Exit code 0 = all checks passed.

#include "kh2coop/Codec.hpp"
#include "kh2coop/NetworkClient.hpp"
#include "kh2coop/ProgressAllowList.hpp"
#include "kh2coop/PacketRing.hpp"
#include "kh2coop/SessionHost.hpp"
#include "kh2coop/WorldBridge.hpp"
#include "kh2coop/WorldPump.hpp"

#include <enet/enet.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <functional>
#include <iostream>
#include <memory>
#include <thread>
#include <vector>

using namespace kh2coop;

namespace {

int g_errors = 0;

void check(bool ok, const std::string& what) {
    std::cout << (ok ? "  PASS: " : "  FAIL: ") << what << "\n";
    if (!ok) ++g_errors;
}

std::uint64_t nowMs() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
}

bool unwrapWorld(const std::vector<std::uint8_t>& framed, WorldEnvelope& envelope) {
    const std::uint8_t* payload = nullptr;
    std::size_t size = 0;
    if (decodePacketHeader(framed.data(), framed.size(), payload, size) != PacketType::WorldEnvelope) return false;
    ByteReader reader(payload, size);
    read(reader, envelope);
    return reader.atEnd();
}

std::vector<std::uint8_t> bytesOf(std::uint32_t seed, std::size_t n) {
    std::vector<std::uint8_t> v(n);
    for (std::size_t i = 0; i < n; ++i) v[i] = static_cast<std::uint8_t>(seed * 31 + i);
    return v;
}

void testPacketRing() {
    std::cout << "\n=== PacketRing ===\n";
    constexpr std::uint32_t cap = 256;
    std::vector<std::uint8_t> mem(PacketRing::bytesFor(cap));
    PacketRing::format(mem.data(), cap);
    PacketRing ring(mem.data());

    std::vector<std::uint8_t> out;
    check(!ring.pop(out), "empty ring pops nothing");
    check(ring.push(bytesOf(1, 5)) && ring.push(bytesOf(2, 9)), "push two records");
    check(ring.pop(out) && out == bytesOf(1, 5) && ring.pop(out) && out == bytesOf(2, 9) &&
              !ring.pop(out),
          "records come out in order with exact lengths");

    // Cycle odd sizes many times to force every wrap position.
    bool intact = true;
    for (std::uint32_t i = 0; i < 2000; ++i) {
        const auto rec = bytesOf(i, 1 + (i * 7) % 60);
        intact &= ring.push(rec) && ring.pop(out) && out == rec;
    }
    check(intact, "2000 push/pop cycles across wraparound stay intact");

    int accepted = 0;
    while (ring.push(bytesOf(3, 40))) ++accepted;
    check(accepted > 0 && ring.dropped() > 0, "a full ring refuses pushes (counted)");
    int drained = 0;
    while (ring.pop(out)) {
        if (out != bytesOf(3, 40)) intact = false;
        ++drained;
    }
    check(drained == accepted && intact, "nothing was overwritten when full");
    check(!ring.push(bytesOf(4, 200)), "a record larger than half the ring is refused");

    // Producer and consumer on different threads.
    std::vector<std::uint8_t> mem2(PacketRing::bytesFor(4096));
    PacketRing::format(mem2.data(), 4096);
    PacketRing a(mem2.data()), b(mem2.data());
    constexpr int kCount = 20000;
    std::thread producer([&] {
        for (int i = 0; i < kCount;) {
            if (a.push(bytesOf(static_cast<std::uint32_t>(i), 1 + i % 100))) ++i;
        }
    });
    int got = 0;
    bool ordered = true;
    const auto deadline = nowMs() + 10000;
    while (got < kCount && nowMs() < deadline) {
        if (b.pop(out)) {
            ordered &= out == bytesOf(static_cast<std::uint32_t>(got), 1 + got % 100);
            ++got;
        }
    }
    producer.join();
    check(got == kCount && ordered, "20000 records across threads, in order, intact");
}

void testVersion11Layout() {
    WorldBridge producer, consumer, absent;
    const DWORD pid = 0x7FF50000u + (GetCurrentProcessId() & 0xFFFFu);
    check(!absent.OpenExisting(pid), "operator OpenExisting does not manufacture absent mapping");
    check(producer.Open(pid) && consumer.OpenExisting(pid), "version12 owned fixture mapping opens");
    check(consumer.RuntimeWriter().pid == 0, "new mapping has no runtime writer");
    producer.PulseRuntimeWriter();
    const auto writer = consumer.RuntimeWriter();
    check(writer.pid == GetCurrentProcessId() && static_cast<DWORD>(GetTickCount() - writer.heartbeat) < 1000,
          "runtime heartbeat PID and timestamp cross the real shared mapping");
    check(WORLD_BRIDGE_VERSION == 12 && consumer.DeliverySerial() == 0 &&
          consumer.PeerDeliverySerial(0) == 0 && consumer.PeerDeliverySerial(1) == 0 && consumer.PeerDeliverySerial(2) == 0,
          "version12 delivery floors initialize unavailable");
    constexpr std::uint64_t wide = 0xFEDCBA9876543210ULL;
    producer.SetDeliverySerial(wide);
    producer.SetPeerDeliverySerials({wide, 0x100000001ULL, 0x200000002ULL});
    check(consumer.DeliverySerial() == wide && consumer.PeerDeliverySerial(0) == wide &&
          consumer.PeerDeliverySerial(1) == 0x100000001ULL && consumer.PeerDeliverySerial(2) == 0x200000002ULL && consumer.PeerDeliverySerial(3) == 0,
          "all aligned delivery fields preserve full64-bit values independently");
    // VUH-1515 spawn-pick salt at [120,128): zero when formatted, full 64-bit, independent of the other fields
    check(consumer.SpawnPickSalt() == 0, "spawn-pick salt formats to zero (no salt)");
    producer.SetSpawnPickSalt(wide ^ 0x0F0F0F0F0F0F0F0FULL);
    check(consumer.SpawnPickSalt() == (wide ^ 0x0F0F0F0F0F0F0F0FULL) && consumer.DeliverySerial() == wide &&
          consumer.PeerDeliverySerial(2) == 0x200000002ULL, "spawn-pick salt round-trips without touching the delivery fields");
    check(WorldBridge::SpawnPickSaltFromSession("") == 0 &&
          WorldBridge::SpawnPickSaltFromSession("0123456789abcdef0123456789abcdef") != 0,
          "spawn-pick salt from the incarnation id: zero only for an empty id");
    producer.SetPeerDeliverySerials({wide, 0, 0x300000003ULL});
    check(consumer.PeerDeliverySerial(1) == 0 && consumer.PeerDeliverySerial(2) == 0x300000003ULL,
          "atomic requester floors retire one peer without aliasing another");
    const auto body = encode(StateHash {7,4,26,123,456});
    const ProducerWorldContext context {0x87654321u, wide, 0xABCDEF1234567890ULL};
    check(producer.SendToRuntime(body, context), "owned producer enqueues exact captured context");
    const auto name = std::string(WORLD_BRIDGE_PREFIX) + std::to_string(pid);
    HANDLE handle = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, name.c_str());
    auto* view = handle ? static_cast<std::uint8_t*>(MapViewOfFile(handle, FILE_MAP_ALL_ACCESS, 0, 0, 0)) : nullptr;
    check(view != nullptr, "test opens only its own mapping for ABI byte verification");
    if (view) {
        PacketRing outgoing(view + 128);
        std::vector<std::uint8_t> record;
        const bool received = outgoing.pop(record);
        bool exact = received && record.size() == body.size() + 20;
        if (exact) {
            ByteReader reader(record);
            exact = reader.readU32() == context.generation && reader.readU64() == context.deliverySerial &&
                    reader.readU64() == context.hostSourceSerial && std::equal(body.begin(), body.end(), record.begin() + 20);
        }
        check(exact, "128-byte header is followed by generation32/delivery64/source64 and exact packet bytes");
        // Exercise held intermediate states on this test-owned mapping only.
        auto* mailboxState = reinterpret_cast<volatile LONG*>(view + 88);
        const ProducerWorldContext commandContext {0x87654321u, wide, 0};
        std::uint8_t mask = 0;
        std::uint64_t hostConnection = 0;
        ProducerWorldContext commandDecoded;
        InterlockedExchange(mailboxState, 1);
        check(!producer.QueueResyncCommand(6, commandContext, wide) &&
              !consumer.ReceiveResyncCommand(mask, commandDecoded, hostConnection),
              "held Writing mailbox neither overwrites nor publishes partial fields");
        InterlockedExchange(mailboxState, 3);
        check(!producer.QueueResyncCommand(6, commandContext, wide) &&
              !consumer.ReceiveResyncCommand(mask, commandDecoded, hostConnection),
              "held Reading mailbox stays explicitly busy");
        InterlockedExchange(mailboxState, 0);
        check(!producer.QueueResyncCommand(3, commandContext, wide) &&
              !producer.QueueResyncCommand(6, context, wide) &&
              !producer.QueueResyncCommand(6, {0,wide,0}, wide) &&
              !producer.QueueResyncCommand(6, {1,0,0}, wide) &&
              !producer.QueueResyncCommand(6, commandContext, 0),
              "mailbox rejects invalid mask, native source, unavailable generation/delivery/host");
        check(producer.SendToRuntime(body, context), "native SPSC record queued before concurrent operator producers");
        std::atomic<bool> go {false};
        bool queuedA = false, queuedB = false;
        std::thread first([&] { while (!go.load(std::memory_order_acquire)) std::this_thread::yield();
            queuedA = producer.QueueResyncCommand(2, commandContext, wide); });
        std::thread second([&] { while (!go.load(std::memory_order_acquire)) std::this_thread::yield();
            queuedB = consumer.QueueResyncCommand(4, {7,0x100000001ULL,0}, 0x200000002ULL); });
        go.store(true, std::memory_order_release);
        first.join(); second.join();
        check(queuedA != queuedB && !producer.QueueResyncCommand(6, commandContext, wide),
              "concurrent operators have exactly one winner and Ready mailbox refuses overwrite");
        const bool consumed = consumer.ReceiveResyncCommand(mask, commandDecoded, hostConnection);
        check(consumed && commandDecoded.hostSourceSerial == 0 &&
              (queuedA ? (mask == 2 && commandDecoded.generation == commandContext.generation &&
                          commandDecoded.deliverySerial == wide && hostConnection == wide)
                       : (mask == 4 && commandDecoded.generation == 7 &&
                          commandDecoded.deliverySerial == 0x100000001ULL && hostConnection == 0x200000002ULL)) &&
              !consumer.ReceiveResyncCommand(mask, commandDecoded, hostConnection),
              "winning command preserves full-width capture, consumes once and frees mailbox");
        check(outgoing.pop(record) && record.size() == body.size() + 20 &&
              std::equal(body.begin(), body.end(), record.begin() + 20) && !outgoing.pop(record),
              "operator mailbox leaves the native SPSC ring intact with no command records");
        check(producer.QueueResyncCommand(6, commandContext, wide) &&
              consumer.ReceiveResyncCommand(mask, commandDecoded, hostConnection) && mask == 6,
              "mailbox is reusable after successful consume");
        UnmapViewOfFile(view);
    }
    if (handle) CloseHandle(handle);
    ProducerWorldContext decoded;std::vector<std::uint8_t> output;
    check(producer.SendToRuntime(body, context) && consumer.ReceiveFromDll(output, decoded) && output == body &&
          decoded.generation == context.generation && decoded.deliverySerial == context.deliverySerial && decoded.hostSourceSerial == context.hostSourceSerial,
          "actual bridge decoder preserves captured widths without reading replacement header values");
}

void testWorldInbox() {
    std::cout << "\n=== Deferred world delivery ===\n";
    WorldBridge runtime, dll;
    WorldInbox inbox;
    WorldPumpStats stats;
    const WorldScope hostScope {std::string(32,'a'), 0x100000001ULL, 1, 0x200000002ULL, 0x300000003ULL, 4};
    const auto transition = encode(WorldEnvelope{hostScope, encode(RoomTransition {12, 4, 26, 3, 1, 2, 0})});
    const auto hold = encode(WorldEnvelope{hostScope, encode(EventHold {12, true, 7})});
    check(inbox.Receive(runtime, transition, stats) &&
              inbox.Receive(runtime, hold, stats) && inbox.PendingCount() == 2,
          "pre-attach transition and hold are retained in order");
    const auto activation = encode(WorldEnvelope{hostScope, encode(ActivationRequest {{12, 4, 26, 3, 1, 2, 0}, {11, 22}, 1, 1})});
    check(inbox.Receive(runtime, activation, stats) && inbox.PendingCount() == 2 &&
              stats.ephemeralDropped == 1,
          "pre-attach activation is dropped without joining reliable backlog");
    const DWORD pid = 0x7FF20000u + (GetCurrentProcessId() & 0xFFFFu);
    check(runtime.Open(pid) && dll.Open(pid), "delayed bridge opens");
    inbox.Flush(runtime, stats);
    std::vector<std::uint8_t> out;
    check(inbox.PendingCount() == 0 && dll.ReceiveFromRuntime(out) && out == transition &&
              dll.ReceiveFromRuntime(out) && out == hold && !dll.ReceiveFromRuntime(out),
          "initial snapshot reaches the DLL intact after attach");

    const auto filler = bytesOf(99, 65536);
    int filled = 0;
    while (runtime.SendToDll(filler)) ++filled;
    const auto smallFiller = bytesOf(98, 1);
    while (runtime.SendToDll(smallFiller)) ++filled;
    check(filled > 0 && inbox.Receive(runtime, transition, stats) &&
              inbox.Receive(runtime, hold, stats) && inbox.PendingCount() == 2,
          "full bridge retains both world packets instead of dropping them");
    check(inbox.Receive(runtime, activation, stats) && inbox.PendingCount() == 2 &&
              stats.ephemeralDropped == 2,
          "activation neither bypasses nor enlarges a blocked reliable backlog");
    int drained = 0;
    while (dll.ReceiveFromRuntime(out)) ++drained;
    inbox.Flush(runtime, stats);
    check(drained == filled && dll.ReceiveFromRuntime(out) && out == transition &&
              dll.ReceiveFromRuntime(out) && out == hold && inbox.PendingCount() == 0,
          "backpressure clears without loss or reordering");
    check(inbox.Receive(runtime, activation, stats) && dll.ReceiveFromRuntime(out) &&
              out == activation && inbox.PendingCount() == 0,
          "fresh activation can follow drained reliable state");

    runtime.Close();
    bool accepted = true;
    for (std::size_t i = 0; i < WorldInbox::kMaxBytes / filler.size(); ++i) {
        accepted &= inbox.Receive(runtime, filler, stats);
    }
    check(accepted && !inbox.Receive(runtime, transition, stats) && stats.inboxOverflow == 1,
          "unattached inbox is bounded and reports overflow");
    inbox.Clear();
    check(inbox.PendingCount() == 0 && inbox.Receive(runtime, hold, stats),
          "disconnect clearing removes buffered state and frees capacity");
    check(!inbox.Receive(runtime, bytesOf(0, WORLD_RING_BYTES / 2), stats),
          "a record too large for the bridge fails instead of blocking the inbox");
}

void testSessionReset() {
    std::cout << "\n=== Ordered world session boundaries ===\n";
    WorldBridge runtime, dll;
    WorldInbox inbox;
    WorldPumpStats stats;
    const DWORD pid = 0x7FF30000u + (GetCurrentProcessId() & 0xFFFFu);
    check(runtime.Open(pid) && dll.Open(pid), "session-reset bridges open");
    runtime.SetLocalSlot(1);
    const auto oldRoom = encode(RoomTransition {99, 4, 26, 3, 1, 2, 0});
    const auto newRoom = encode(RoomTransition {1, 2, 0, 0, 1, 2, 0});
    const auto reset = encodeWorldSessionReset(1, 1);
    runtime.SendToDll(oldRoom);
    // The DLL is between its frame-start check and its packet loop. Runtime
    // can remove and replace the host now without losing the new low epoch.
    runtime.SetLocalSlot(WORLD_SLOT_UNKNOWN);
    inbox.Receive(runtime, reset, stats);
    runtime.SetLocalSlot(1);
    inbox.Receive(runtime, reset, stats);
    inbox.Receive(runtime, newRoom, stats);
    std::uint32_t epoch = 99;
    int resets = 0;
    std::vector<std::uint8_t> out;
    while (dll.ReceiveFromRuntime(out)) {
        const std::uint8_t* payload = nullptr;
        std::size_t size = 0;
        const auto type = decodePacketHeader(out.data(), out.size(), payload, size);
        if (type == PacketType::SessionState && size == 12) {
            epoch = 0;
            ++resets;
        } else if (type == PacketType::RoomTransition) {
            RoomTransition room;
            ByteReader r(payload, size);
            read(r, room);
            if (room.epoch > epoch) epoch = room.epoch;
        }
    }
    check(resets == 2 && epoch == 1 && dll.LocalSlot() == 1,
          "reset published between frame check and dequeue preserves the new host transition");
    check(!isWorldPacket(PacketType::SessionState),
          "bridge-local reset cannot pass the DLL-to-network world filter");
}

void testSessionGeneration() {
    WorldBridge runtime, dll;
    WorldInbox inbox;
    WorldPumpStats stats;
    const auto progress = encode(ProgressUpdate {1, true, {{0x1C00, {7}}}});
    const auto room = encode(RoomTransition {1, 5, 6, 0, 1, 1, 0});
    inbox.Receive(runtime, encodeWorldSessionReset(0), stats);
    inbox.Receive(runtime, progress, stats);
    inbox.Receive(runtime, room, stats);
    const DWORD pid = 0x7FF40000u + (GetCurrentProcessId() & 0xFFFFu);
    check(runtime.Open(pid) && dll.Open(pid), "generation bridges open");
    constexpr std::uint64_t delivery = 0x100000001ULL;
    runtime.SetDeliverySerial(delivery);
    const auto first = runtime.AdvanceSessionGeneration();
    check(first != 0 && dll.SessionGeneration() == first &&
              inbox.BindSessionGeneration(first, stats, delivery),
          "attachment publishes generation and binds the retained current-session reset");
    inbox.Flush(runtime, stats);
    std::vector<std::uint8_t> out;
    check(dll.ReceiveFromRuntime(out) && out == encodeWorldSessionReset(first, delivery) &&
              dll.ReceiveFromRuntime(out) && out == progress &&
              dll.ReceiveFromRuntime(out) && out == room && !dll.ReceiveFromRuntime(out),
          "generation rebind preserves first full progress and transition after matching marker");
    runtime.SendToDll(encodeWorldSessionReset(first, delivery)); // deliberately delayed old marker
    const auto second = runtime.AdvanceSessionGeneration();
    check(second != first && dll.SessionGeneration() == second,
          "header invalidates old authority before the new marker is queued");
    inbox.Receive(runtime, encodeWorldSessionReset(second, delivery), stats);
    std::uint32_t armed = 0;
    check(dll.ReceiveFromRuntime(out), "old generation marker remains ordered");
    const auto consume = [&](const std::vector<std::uint8_t>& bytes) {
        const std::uint8_t* payload = nullptr;
        std::size_t size = 0;
        const auto type = decodePacketHeader(bytes.data(), bytes.size(), payload, size);
        if (type == PacketType::SessionState && size == 12) {
            ByteReader reader(payload, size);
            const auto generation = reader.readU32();
            const auto serial = reader.readU64();
            if (generation != 0 && serial != 0 && generation == dll.SessionGeneration() && serial == dll.DeliverySerial()) armed = generation;
        }
    };
    consume(out);
    check(armed == 0, "old marker cannot arm the current header generation");
    check(dll.ReceiveFromRuntime(out), "matching generation marker arrives");
    consume(out);
    check(armed == second, "only ordered matching marker arms current generation");
    armed = 0;
    runtime.SetDeliverySerial(delivery + 1);
    runtime.SendToDll(encodeWorldSessionReset(second, delivery));
    check(dll.ReceiveFromRuntime(out), "retired serial marker remains observable");consume(out);
    check(armed == 0, "matching generation with retired delivery serial cannot arm current header");
    runtime.SendToDll(encodeWorldSessionReset(0, delivery + 1));
    check(dll.ReceiveFromRuntime(out), "zero-generation marker remains observable");consume(out);
    check(armed == 0, "zero-generation lazy marker remains unarmed even with current serial");
    runtime.SendToDll(encodeWorldSessionReset(second, delivery + 1));
    check(dll.ReceiveFromRuntime(out), "matching full marker arrives");consume(out);
    check(armed == second, "both current generation and serial are required by marker fixture");
}

void testLazyResyncInbox() {
    WorldBridge runtime, dll;WorldInbox inbox;WorldPumpStats stats;
    ResyncSnapshot s;s.room={7,4,26,0,0,3,0};s.hold={7,false,0};s.progress.version=1;s.progress.full=true;
    for (const auto& range : verifiedProgressAllowList())s.progress.spans.push_back({range.offset,std::vector<std::uint8_t>(range.length,0)});
    s.hpSequence=1;s.coverageMask=ResyncComplete;s.generation=1;s.loadSerial=1;s.captureFrameBefore=10;s.captureFrameAfter=11;s.nativeFingerprint=resyncNativeFingerprint(s);
    ResyncPlan plan;plan.request.key={std::string(32,'a'),11,1};plan.request.room=s.room;plan.request.targetMask=2;plan.request.connections={11,22,0};plan.targets[0]={1,22,2};plan.targetCount=1;plan.remainingMs=30000;plan.stage=ResyncPlanStage::Fenced;
    const auto bytes=encodeResyncSnapshot(s);ResyncBegin begin;begin.key=plan.request.key;begin.room=s.room;begin.targets=plan.targets;begin.targetCount=1;begin.snapshotCut=3;begin.totalBytes=static_cast<std::uint32_t>(bytes.size());begin.partCount=static_cast<std::uint16_t>((bytes.size()+RESYNC_MAX_PART_BYTES-1)/RESYNC_MAX_PART_BYTES);begin.sha256=desyncSha256(bytes);
    const auto encodedPlan=encode(plan), immutable=encodeNativeResyncSnapshot(begin,s);
    check(inbox.Receive(runtime,encodeWorldSessionReset(0,2),stats)&&inbox.Receive(runtime,encodedPlan,stats),
          "unattached inbox retains immutable plan behind unarmed marker");
    const DWORD pid=0x7FF60000u+(GetCurrentProcessId()&0xFFFFu);
    check(runtime.Open(pid)&&dll.Open(pid),"lazy resync owned mappings open");
    runtime.SetDeliverySerial(2);runtime.SetPeerDeliverySerials({1,2,0});runtime.SetConnectionIds({11,22,0});runtime.SetLocalSlot(1);
    const auto attached=runtime.AdvanceSessionGeneration();
    check(inbox.BindSessionGeneration(attached,stats,2)&&inbox.PendingCount()==2,"attachment binds initial marker without dropping queued plan");
    // Runtime defers immutable snapshot outside WorldInbox while unattached. Its
    // Bootstrap delivery clears deferred old state, requeues Plan, then its new
    // matching reset, then the immutable snapshot. Exercise the real queue here.
    inbox.Clear();const auto bootstrap=runtime.AdvanceSessionGeneration();
    check(inbox.Receive(runtime,encodedPlan,stats)&&inbox.Receive(runtime,encodeWorldSessionReset(bootstrap,2),stats)&&inbox.Receive(runtime,immutable,stats),
          "bootstrap enqueues plan then new-generation marker then immutable full snapshot");
    std::vector<std::uint8_t> out;
    check(dll.ReceiveFromRuntime(out)&&out==encodedPlan&&dll.ReceiveFromRuntime(out)&&out==encodeWorldSessionReset(bootstrap,2)&&dll.ReceiveFromRuntime(out)&&out==immutable&&!dll.ReceiveFromRuntime(out),
          "actual FIFO preserves lazy Plan-reset-snapshot order and exact immutable bytes");
    ResyncBegin decodedBegin;ResyncSnapshot decodedSnapshot;decodeNativeResyncSnapshot(immutable,decodedBegin,decodedSnapshot);
    check(bootstrap!=attached&&dll.SessionGeneration()==bootstrap&&decodedBegin.key==begin.key&&decodedBegin.targets==begin.targets&&decodedSnapshot.nativeFingerprint==s.nativeFingerprint,
          "fresh local generation changes neither portable target serials nor captured canonical state");
}

} // namespace

int main() {
    testPacketRing();
    testVersion11Layout();
    testWorldInbox();
    testSessionReset();
    testSessionGeneration();
    testLazyResyncInbox();
    if (enet_initialize() != 0) return 2;

    std::cout << "\n=== WorldBridge end to end ===\n";
    SessionConfig cfg;
    cfg.port = 17797;
    cfg.bindAddress = "127.0.0.1";
    cfg.maxPeers = 3;
    cfg.gameBuild = "bridge-build";
    cfg.contentHash = "bridge-content";
    cfg.modHash = "bridge-mod";
    cfg.sessionId = "bridge-test";
    SessionHost relay(cfg, {});
    check(relay.start(), "relay starts");

    const DWORD hostPid = 0x7FF10000u + (GetCurrentProcessId() & 0xFFFFu);
    const DWORD clientPid = hostPid + 1;
    // Each "runtime" and its "DLL" open the same named mapping.
    WorldBridge hostRuntimeSide, hostDll, clientRuntimeSide, clientDll;
    check(hostRuntimeSide.Open(hostPid) && hostDll.Open(hostPid) &&
              clientRuntimeSide.Open(clientPid) && clientDll.Open(clientPid),
          "bridges open for both machines");
    check(hostDll.LocalSlot() == WORLD_SLOT_UNKNOWN,
          "slot is unknown until the runtime sets it (not 0 = host)");
    check(hostDll.ConnectionId(0) == 0 && hostDll.ConnectionId(1) == 0 && hostDll.ConnectionId(2) == 0,
          "bridge connection identities default to unknown");
    const auto identityGeneration = hostDll.SessionGeneration();
    hostRuntimeSide.SetConnectionIds({11, 22, 33});
    check(hostDll.ConnectionId(0) == 11 && hostDll.ConnectionId(1) == 22 && hostDll.ConnectionId(2) == 33,
          "bridge publishes each slot's connection identity");
    hostRuntimeSide.SetConnectionIds({11, 0, 44});
    check(hostDll.ConnectionId(1) == 0 && hostDll.ConnectionId(2) == 44 &&
              hostDll.SessionGeneration() == identityGeneration,
          "bridge retires and replaces friend identities without resetting world generation");
    hostRuntimeSide.SetConnectionIds({0, 0, 0});
    hostRuntimeSide.SetLocalSlot(0);
    clientRuntimeSide.SetLocalSlot(1);
    check(hostDll.LocalSlot() == 0 && clientDll.LocalSlot() == 1,
          "DLL side reads the slot the runtime set");
    check(hostDll.NetStats().rttMs == WORLD_NET_UNKNOWN &&
              hostDll.NetStats().lossPermille == WORLD_NET_UNKNOWN,
          "net stats are unknown until the runtime publishes them");
    hostRuntimeSide.SetNetStats(42, 15);
    check(hostDll.NetStats().rttMs == 42 && hostDll.NetStats().lossPermille == 15,
          "DLL side reads the runtime's RTT and loss");

    WorldPumpStats hostStats, clientStats;
    WorldInbox hostInbox, clientInbox;
    ClientCallbacks hostCb, clientCb;
    const auto publishRoster = [](WorldBridge& bridge, const SessionState& state, std::uint8_t localSlot) {
        std::array<std::uint64_t, 3> ids {};
        for (const auto& actor : state.actors) {
            const auto slot = static_cast<std::size_t>(actor.slot);
            if (slot < ids.size()) ids[slot] = actor.connectionId;
        }
        if (ids[0] != bridge.ConnectionId(0) || ids[localSlot] != bridge.ConnectionId(localSlot) ||
            (ids[0] && ids[localSlot] && bridge.SessionGeneration() == 0))
            bridge.AdvanceSessionGeneration();
        bridge.SetConnectionIds(ids);
        std::array<std::uint64_t,3> serials{};
        for (std::size_t slot = 0; slot < ids.size(); ++slot) if (ids[slot]) serials[slot] = 1;
        bridge.SetPeerDeliverySerials(serials);
        bridge.SetLocalSlot(localSlot);
    };
    hostCb.onSessionState = [&](const SessionState& ss) { publishRoster(hostRuntimeSide, ss, 0); };
    clientCb.onSessionState = [&](const SessionState& ss) { publishRoster(clientRuntimeSide, ss, 2); };

    hostCb.onWorldBinding = [&](const WorldBinding& b) {
        if (hostRuntimeSide.DeliverySerial() == b.deliverySerial) return;
        hostRuntimeSide.SetDeliverySerial(b.deliverySerial);
        hostInbox.BindSessionGeneration(hostRuntimeSide.SessionGeneration(), hostStats, b.deliverySerial);
    };
    clientCb.onWorldBinding = [&](const WorldBinding& b) {
        if (clientRuntimeSide.DeliverySerial() == b.deliverySerial) return;
        clientRuntimeSide.SetDeliverySerial(b.deliverySerial);
        clientInbox.BindSessionGeneration(clientRuntimeSide.SessionGeneration(), clientStats, b.deliverySerial);
    };
    hostCb.onWorldEnvelope = [&](const WorldEnvelope& e) {
        if (e.scope.kind != WorldSourceKind::Native) return;
        hostInbox.Receive(hostRuntimeSide, encode(e), hostStats);
    };
    clientCb.onWorldEnvelope = [&](const WorldEnvelope& e) {
        if (e.scope.kind != WorldSourceKind::Native) return;
        clientInbox.Receive(clientRuntimeSide, encode(e), clientStats);
    };
    NetworkClient host("127.0.0.1", cfg.port, cfg.gameBuild, cfg.modHash, "host",
                       SlotType::Player, std::move(hostCb), RuntimeMode::CampaignCoop,
                       cfg.contentHash);
    NetworkClient client("127.0.0.1", cfg.port, cfg.gameBuild, cfg.modHash, "client",
                         SlotType::Friend2, std::move(clientCb), RuntimeMode::CampaignCoop,
                         cfg.contentHash);
    host.connect();
    client.connect();

    auto pump = [&](int ms) {
        const auto end = nowMs() + ms;
        while (nowMs() < end) {
            relay.tick(0);
            host.tick(0);
            client.tick(0);
            hostInbox.Flush(hostRuntimeSide, hostStats);
            clientInbox.Flush(clientRuntimeSide, clientStats);
            pumpDllToNet(hostRuntimeSide, host, hostStats);
            pumpDllToNet(clientRuntimeSide, client, clientStats);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    };
    // Evaluates cond once per poll (cond may consume, e.g. pop a ring).
    auto waitFor = [&](const std::function<bool()>& cond) {
        const auto end = nowMs() + 3000;
        while (nowMs() < end) {
            if (cond()) return true;
            pump(5);
        }
        return false;
    };
    check(waitFor([&] { return relay.verifiedPeerCount() == 2 && host.worldReady() && client.worldReady() && clientDll.ConnectionId(2) != 0; }),
          "host and client verified with published requester identity");

    hostInbox.Flush(hostRuntimeSide, hostStats);
    clientInbox.Flush(clientRuntimeSide, clientStats);
    std::vector<std::uint8_t> initialMarker;
    check(hostDll.ReceiveFromRuntime(initialMarker) && initialMarker == encodeWorldSessionReset(hostDll.SessionGeneration(), host.deliverySerial()) &&
          clientDll.ReceiveFromRuntime(initialMarker) && initialMarker == encodeWorldSessionReset(clientDll.SessionGeneration(), client.deliverySerial()),
          "binding delivery serial is published before exact initial reset markers");
    const auto captured = [](WorldBridge& bridge, NetworkClient& net) {
        auto context = net.makeTestingWorldContext();
        context.generation = bridge.SessionGeneration();
        return context;
    };

    // A real receive callback can precede the runtime's game attach even when
    // the DLL has already created its mapping.
    clientRuntimeSide.Close();

    // Host DLL detects a transition and emits it.
    check(hostDll.SendToRuntime(encode(RoomTransition {7, 4, 0x1A, 3, 1, 2, 0}), captured(hostDll, host)),
          "host DLL queues a RoomTransition");
    check(waitFor([&] { return clientInbox.PendingCount() == 1; }),
          "relay transition received before runtime attach is buffered");
    check(clientRuntimeSide.Open(clientPid), "client runtime attaches after its snapshot arrives");
    clientRuntimeSide.SetLocalSlot(2);
    clientInbox.Flush(clientRuntimeSide, clientStats);
    std::vector<std::uint8_t> pkt;
    RoomTransition got;
    WorldScope roomScope;
    const bool arrived = waitFor([&] {
        if (!clientDll.ReceiveFromRuntime(pkt)) return false;
        WorldEnvelope envelope;
        if (!unwrapWorld(pkt, envelope)) return false;
        const std::uint8_t* payload = nullptr;
        std::size_t size = 0;
        if (decodePacketHeader(envelope.packet.data(), envelope.packet.size(), payload, size) !=
            PacketType::RoomTransition) {
            return false;
        }
        ByteReader r(payload, size);
        read(r, got);
        roomScope = envelope.scope;
        return true;
    });
    check(arrived && got.epoch == 7 && got.roomId == 0x1A && got.door == 3,
          "client DLL decodes the host's transition from its bridge");
    check(roomScope.sourceConnectionId == hostDll.ConnectionId(0) && roomScope.sourceDeliverySerial == hostDll.DeliverySerial() &&
          roomScope.hostSourceSerial != 0 && roomScope.targetConnectionId == clientDll.ConnectionId(2) && roomScope.targetDeliverySerial == clientDll.DeliverySerial(),
          "DLL receives immutable admitted source and destination scopes alongside transition");

    EnemyManifestEntry target;
    target.netId = 5;
    target.objectId = 309;
    hostDll.SendToRuntime(encode(EnemyManifest {7, true, {target}}), captured(hostDll, host));
    check(waitFor([&] { return relay.manifestSize() == 1 && clientDll.ConnectionId(2) != 0; }),
          "typed manifest and requester connection identity are available before claiming");

    // Client DLL claims a hit; host DLL receives it with the real slot.
    HitClaim claim;
    claim.epoch = 7;
    claim.seq = 1;
    claim.netId = 5;
    claim.objectId = 309;
    claim.requesterConnectionId = clientDll.ConnectionId(2);
    claim.damage = 33;
    claim.attackerSlot = SlotType::Player; // forged
    clientDll.SendToRuntime(encode(claim), captured(clientDll, client));
    HitClaim hostGot;
    WorldScope claimScope;
    const bool claimed = waitFor([&] {
        if (!hostDll.ReceiveFromRuntime(pkt)) return false;
        WorldEnvelope envelope;
        if (!unwrapWorld(pkt, envelope)) return false;
        const std::uint8_t* payload = nullptr;
        std::size_t size = 0;
        if (decodePacketHeader(envelope.packet.data(), envelope.packet.size(), payload, size) != PacketType::HitClaim) {
            return false;
        }
        ByteReader r(payload, size);
        read(r, hostGot);
        claimScope = envelope.scope;
        return true;
    });
    check(claimed && hostGot.netId == 5 && hostGot.objectId == 309 &&
              hostGot.requesterConnectionId == claim.requesterConnectionId && hostGot.damage == 33 &&
              hostGot.attackerSlot == SlotType::Friend2,
          "host DLL receives the client's hit claim with the real attacker slot");
    check(claimScope.sourceConnectionId == claim.requesterConnectionId && claimScope.sourceDeliverySerial == hostDll.PeerDeliverySerial(2) &&
          claimScope.hostSourceSerial == 0 && claimScope.targetConnectionId == hostDll.ConnectionId(0),
          "reverse claim keeps requester delivery serial for native dequeue-time retirement");

    // Preserve actual old producer-generation records across reconnect. The
    // production pump must retire all three before any network send.
    claim.seq = 2;
    check(clientDll.SendToRuntime(encode(claim), captured(clientDll, client)), "old-connection claim is queued in the DLL ring");
    const auto oldGeneration = clientDll.SessionGeneration();
    const auto retiredBefore = clientStats.retiredOutgoing;
    check(oldGeneration != 0 && clientDll.SendToRuntime(encode(TransitionAck {7, 4, 26, true}), captured(clientDll, client)) &&
          clientDll.SendToRuntime(encode(StateHash {7, 4, 26, 123, 456}), captured(clientDll, client)),
          "old generation ACK and StateHash are queued beside old claim");
    const auto networkOnlyWait = [&](const std::function<bool()>& condition) {
        const auto deadline = nowMs() + 3000;
        while (nowMs() < deadline && !condition()) {
            relay.tick(0); host.tick(0); client.tick(0);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return condition();
    };
    client.disconnect();
    clientRuntimeSide.AdvanceSessionGeneration();
    clientRuntimeSide.SetConnectionIds({});
    clientRuntimeSide.SetDeliverySerial(0);
    clientRuntimeSide.SetPeerDeliverySerials({});
    clientRuntimeSide.SetLocalSlot(WORLD_SLOT_UNKNOWN);
    check(networkOnlyWait([&] { return relay.verifiedPeerCount() == 1; }), "old bridge requester disconnect is observed");
    client.connect();
    check(networkOnlyWait([&] { return relay.verifiedPeerCount() == 2 && client.worldReady() &&
                                      clientDll.ConnectionId(2) != claim.requesterConnectionId; }),
          "reconnected bridge requester receives a different connection identity");
    const auto rejectedBefore = relay.rejectedWorldMessages();
    pumpDllToNet(clientRuntimeSide, client, clientStats);
    check(clientStats.retiredOutgoing == retiredBefore + 3 && relay.rejectedWorldMessages() == rejectedBefore,
          "queued old claim/ACK/hash are retired locally by captured producer generation");
    const auto* rejoinedPeer = relay.peerBySlot(SlotType::Friend2);
    check(rejoinedPeer && rejoinedPeer->ackEpoch == 0 && !rejoinedPeer->hasHash,
          "retired old ACK/hash cannot update rejoined relay peer state");
    check(clientDll.SendToRuntime(encode(TransitionAck {7, 4, 26, true}), oldGeneration) &&
          clientDll.SendToRuntime(encode(StateHash {7, 4, 26, 123, 456}), 0),
          "explicit stale and unavailable producer tags are retained in outgoing envelope");
    pumpDllToNet(clientRuntimeSide, client, clientStats);
    check(clientStats.retiredOutgoing == retiredBefore + 5,
          "late stale producer and generation zero are rejected without relabeling");
    // Independent relay defense: send the original retired connection echo
    // directly on the new transport, bypassing only the local generation gate.
    client.sendNativeWorld(encode(claim), captured(clientDll, client), true);
    check(waitFor([&] { return relay.rejectedWorldMessages() == rejectedBefore + 1; }),
          "relay independently rejects old requester echo on a fresh transport");
    check(clientDll.SendToRuntime(encode(TransitionAck {7, 4, 26, true}), captured(clientDll, client)) &&
          clientDll.SendToRuntime(encode(StateHash {7, 4, 26, 123, 456}), captured(clientDll, client)),
          "current producer queues fresh ACK and StateHash");
    check(waitFor([&] { const auto* ps = relay.peerBySlot(SlotType::Friend2);
              return ps && ps->ackEpoch == 7 && ps->ackArrived && ps->hasHash && ps->lastHash.enemiesHash == 123; }),
          "current generation ACK/hash update only the new peer state");
    pump(30);
    check(!hostDll.ReceiveFromRuntime(pkt), "old queued claim never reaches host DLL");
    claim.requesterConnectionId = clientDll.ConnectionId(2);
    claim.seq = 1;
    clientDll.SendToRuntime(encode(claim), captured(clientDll, client));
    const auto freshArrived = waitFor([&] { return hostDll.ReceiveFromRuntime(pkt); });
    if (freshArrived) {
        WorldEnvelope envelope;
        if (!unwrapWorld(pkt, envelope)) throw std::runtime_error("expected scoped fresh claim");
        const std::uint8_t* payload = nullptr;
        std::size_t size = 0;
        decodePacketHeader(envelope.packet.data(), envelope.packet.size(), payload, size);
        ByteReader reader(payload, size);
        read(reader, hostGot);
    }
    check(freshArrived && hostGot.requesterConnectionId == claim.requesterConnectionId && hostGot.seq == 1 &&
              hostGot.objectId == claim.objectId && hostGot.attackerSlot == SlotType::Friend2,
          "fresh bridge connection can restart its claim sequence");

    // A DLL can't push arbitrary traffic (e.g. a forged session state) out.
    hostDll.SendToRuntime(encodePacket(PacketType::SessionState, {1, 2, 3}));
    hostDll.SendToRuntime(std::vector<std::uint8_t> {0xFF});
    pump(100);
    check(hostStats.rejected == 2, "non-world and malformed DLL packets are rejected");
    check(hostStats.toNet >= 1 && clientStats.toNet >= 1 && clientStats.toDll >= 1 &&
              hostStats.toDll >= 1,
          "pump counters account for both directions");

    const auto retiredHostBefore = hostStats.retiredOutgoing;
    auto staleContext = captured(hostDll, host);
    ++staleContext.deliverySerial;
    check(hostDll.SendToRuntime(encode(StateHash{7,4,26,123,456}), staleContext), "stale delivery source is retained exactly in outgoing ring");
    pumpDllToNet(hostRuntimeSide, host, hostStats);
    check(hostStats.retiredOutgoing == retiredHostBefore + 1, "same-generation wrong-delivery record is retired without re-tagging");
    const auto rejectedHostBefore = hostStats.rejected;
    const ProducerWorldContext missingSource {hostDll.SessionGeneration(), hostDll.DeliverySerial(), 0};
    hostDll.SendToRuntime(encode(StateHash{7,4,26,123,456}), missingSource);
    pumpDllToNet(hostRuntimeSide, host, hostStats);
    check(hostStats.rejected == rejectedHostBefore + 1, "host record without captured nonzero native source is not assigned one by pump");
    check(hostDll.SendToRuntime(encode(StateHash{7,4,26,123,456}), captured(hostDll,host)) &&
          waitFor([&] { const auto* ps = relay.peerBySlot(SlotType::Player); return ps && ps->hasHash && ps->lastHash.enemiesHash == 123; }),
          "fresh captured host context positively reaches relay after stale controls");

    const auto hostRejected = hostStats.rejected, clientRejected = clientStats.rejected;
    hostDll.SendToRuntime(encodePacket(PacketType::LocalResyncCommand,{3}));
    clientDll.SendToRuntime(encodeLocalResyncCommand(4));
    pumpDllToNet(hostRuntimeSide,host,hostStats);pumpDllToNet(clientRuntimeSide,client,clientStats);
    check(hostStats.rejected == hostRejected + 1 && clientStats.rejected == clientRejected + 1 && !relay.activeResyncPlan(),
          "legacy operator commands on the native SPSC ring are rejected for host and friend");
    hostRuntimeSide.SetPuppetAuthorityMode(PuppetAuthorityMode::Network);
    clientRuntimeSide.SetPuppetAuthorityMode(PuppetAuthorityMode::Network);
    const ProducerWorldContext operatorContext {hostDll.SessionGeneration(),hostDll.DeliverySerial(),0};
    const auto operatorHost = hostDll.ConnectionId(0);
    check(!hostDll.QueueResyncCommand(3,operatorContext,operatorHost), "invalid operator target mask never enters mailbox");
    const auto friendRetired = clientStats.retiredOutgoing;
    check(clientDll.QueueResyncCommand(4,{clientDll.SessionGeneration(),clientDll.DeliverySerial(),0},operatorHost),
          "friend operator mailbox records request without promoting host authority");
    pumpDllToNet(clientRuntimeSide,client,clientStats);
    check(clientStats.retiredOutgoing == friendRetired + 1 && !relay.activeResyncPlan(), "friend operator command is retired by production authority gate");
    const auto operatorRetired = hostStats.retiredOutgoing;
    auto badOperator = operatorContext;
    ++badOperator.generation;
    check(hostDll.QueueResyncCommand(4,badOperator,operatorHost), "stale operator generation is retained at enqueue");
    pumpDllToNet(hostRuntimeSide,host,hostStats);
    badOperator = operatorContext; ++badOperator.deliverySerial;
    check(hostDll.QueueResyncCommand(4,badOperator,operatorHost), "stale operator delivery is retained at enqueue");
    pumpDllToNet(hostRuntimeSide,host,hostStats);
    check(hostDll.QueueResyncCommand(4,operatorContext,operatorHost + 1), "stale operator host identity is retained at enqueue");
    pumpDllToNet(hostRuntimeSide,host,hostStats);
    check(hostStats.retiredOutgoing == operatorRetired + 3 && !relay.activeResyncPlan(),
          "generation, delivery and host capture mismatches retire without re-tagging or request");
    check(hostDll.QueueResyncCommand(4,operatorContext,operatorHost), "valid local operator mailbox queues without claiming native convergence");
    check(waitFor([&] { return relay.activeResyncPlan().has_value(); }) && relay.activeResyncPlan()->request.targetMask == 4 &&
          relay.activeResyncPlan()->targetCount == 1 && relay.activeResyncPlan()->targets[0].connectionId == clientDll.ConnectionId(2) && !relay.lastResyncResult(),
          "production pump turns local command into authenticated fixed-target capture request only");

    host.disconnect();
    client.disconnect();
    relay.stop();
    enet_deinitialize();

    std::cout << "\n=======================================\n"
              << (g_errors == 0 ? "ALL CHECKS PASSED" : "CHECKS FAILED: " + std::to_string(g_errors))
              << "\n";
    return g_errors == 0 ? 0 : 1;
}
