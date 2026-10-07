// Actual EnemySync consumer/publisher over owned memory. No game, process
// attachment, hook installation, named mapping or desktop access is involved.
#if !defined(_WIN32) || !defined(_MSC_VER)
#error NativeHitClaimTest requires Windows and MSVC-compatible SEH.
#endif
#include "kh2coop/WorldBridge.hpp"
#include <deque>
#include <exception>
#include <cstdarg>
#include <cstdio>
#include <iostream>

namespace kh2coop {
// Replace only the transport boundary; native census, identity gates, queues,
// sequence handling, codec and native-call fault boundary remain production.
class HeadlessWorldBridge {
public:
    bool open = true, sendAllowed = true;
    std::uint8_t slot = 0;
    mutable std::uint32_t generation = 1;
    std::uint64_t deliverySerial = 1;
    std::array<std::uint64_t, 3> peerDeliverySerials {1, 1, 1};
    mutable unsigned generationReads = 0;
    unsigned replaceGenerationOnRead = 0;
    // Deterministic owned-memory change between production capture reads.
    unsigned mutateByteOnGenerationRead = 0;
    uintptr_t mutateByteAddress = 0;
    PuppetAuthorityMode puppetMode = PuppetAuthorityMode::Network;
    mutable std::array<std::uint64_t, 3> connections {100, 101, 102};
    mutable unsigned thirdConnectionReads = 0;
    unsigned replaceThirdOnRead = 0;
    std::deque<std::vector<std::uint8_t>> incoming;
    std::size_t incomingCapacity = static_cast<std::size_t>(-1);
    std::vector<std::vector<std::uint8_t>> outgoing;
    std::vector<std::uint32_t> outgoingGenerations;
    std::vector<ProducerWorldContext> outgoingContexts;
    bool Open(DWORD) { open = true; return true; }
    void Close() { open = false; }
    bool IsOpen() const { return open; }
    std::uint64_t DeliverySerial() const { return deliverySerial; }
    std::uint64_t PeerDeliverySerial(std::uint8_t slotIndex) const {
        return slotIndex < peerDeliverySerials.size() ? peerDeliverySerials[slotIndex] : 0;
    }
    void SetPeerDeliverySerials(const std::array<std::uint64_t, 3>& serials) { peerDeliverySerials = serials; }
    std::uint8_t LocalSlot() const { return slot; }
    std::uint32_t SessionGeneration() const {
        if (++generationReads == replaceGenerationOnRead) ++generation;
        if (generationReads == mutateByteOnGenerationRead && mutateByteAddress)
            *reinterpret_cast<std::uint8_t*>(mutateByteAddress) ^= 1;
        return generation;
    }
    PuppetAuthorityMode GetPuppetAuthorityMode() const { return puppetMode; }
    std::uint64_t spawnPickSalt = 0; // VUH-1515 spawn picks: none unless a test sets it
    std::uint64_t SpawnPickSalt() const { return spawnPickSalt; }
    std::uint64_t ConnectionId(std::uint8_t index) const {
        if (index == 2 && ++thirdConnectionReads == replaceThirdOnRead) connections[2] += 0x100000000ULL;
        return index < connections.size() ? connections[index] : 0;
    }
    WorldBridge::NetStatsView NetStats() const { return {}; }
    bool SendToRuntime(const std::vector<std::uint8_t>& packet) {
        if (!sendAllowed) return false;
        outgoing.push_back(packet);
        return true;
    }
    bool SendToRuntime(const std::vector<std::uint8_t>& packet, std::uint32_t sourceGeneration) {
        if (!SendToRuntime(packet)) return false;
        outgoingGenerations.push_back(sourceGeneration);
        return true;
    }
    bool SendToRuntime(const std::vector<std::uint8_t>& packet, const ProducerWorldContext& context) {
        if (!SendToRuntime(packet, context.generation)) return false;
        outgoingContexts.push_back(context); return true;
    }
    bool ReceiveFromRuntime(std::vector<std::uint8_t>& packet) {
        if (incoming.empty()) return false;
        packet = std::move(incoming.front()); incoming.pop_front(); return true;
    }
    bool SendToDll(const std::vector<std::uint8_t>& packet) {
        std::size_t used = 0;
        for (const auto& queued : incoming) used += queued.size() + 4;
        if (used > incomingCapacity || packet.size() + 4 > incomingCapacity - used) return false;
        incoming.push_back(packet);
        return true;
    }
    bool ReceiveFromDll(std::vector<std::uint8_t>&, std::uint32_t&) { return false; }
    bool ReceiveFromDll(std::vector<std::uint8_t>&, ProducerWorldContext&) { return false; }
    // This native harness has no operator mailbox or mapped command producer.
    bool ReceiveResyncCommand(std::uint8_t&, ProducerWorldContext&, std::uint64_t&) { return false; }
};
}
#define Pop TestResourcePop
#define GetStatistics TestResourceGetStatistics
#define PopNativeConstructionLineage TestPopNativeConstructionLineage
#define WorldBridge HeadlessWorldBridge
#include "../inject/src/EnemySync.cpp"
#include "kh2coop/WorldPump.hpp"
#undef WorldBridge
#undef PopNativeConstructionLineage
#undef Pop
#undef GetStatistics
#include "../inject/src/ProgressSync.cpp"
// Keep the established claim tests' controlled lifecycle boundary. The focused
// reset tests route those adapters to this second, actual Warp implementation;
// only its namespace differs. No hook installation or named mapping is used.
namespace kh2coop::inject::recoverywarp {
using LogFn = kh2coop::inject::warp::LogFn;
RoomTransition ReadLocation();
bool TransitionPending();
bool QueueHostTransition(const RoomTransition&, const ProducerWorldContext&, const WorldScope*, const ResyncBegin*, const ResyncTarget*);
}
#define warp recoverywarp
#include "../inject/src/Warp.cpp"
#undef warp

namespace {
int errors = 0;
std::uint32_t transition = 7, load = 11;
bool pendingTransition = false, arrived = true, productionWorldMode = false;
unsigned mutableArrivalCalls = 0;
kh2coop::RoomTransition arrivalTarget {};
uintptr_t image = 0, player = 0, enemy = 0, object = 0, status = 0;
unsigned nativeCalls = 0;
int callbackMode = 0;
bool nestedRejected = false;
constexpr DWORD deliberateException = 0xE0484954;
std::uint32_t progressChecksum = 0;
unsigned hookCalls = 0, warpCalls = 0, deathCalls = 0;
kh2coop::WarpChannel testWarpChannel {};
void Check(bool condition, const char* label) {
    std::cout << (condition ? "PASS: " : "FAIL: ") << label << '\n';
    if (!condition) ++errors;
}
template<class T> void Put(uintptr_t address, T value) {
    std::memcpy(reinterpret_cast<void*>(address), &value, sizeof(value));
}
}

MH_STATUS WINAPI MH_CreateHook(LPVOID, LPVOID, LPVOID*) { ++hookCalls; return MH_ERROR_NOT_INITIALIZED; }
MH_STATUS WINAPI MH_EnableHook(LPVOID) { ++hookCalls; return MH_ERROR_NOT_INITIALIZED; }
MH_STATUS WINAPI MH_DisableHook(LPVOID) { ++hookCalls; return MH_ERROR_NOT_INITIALIZED; }
MH_STATUS WINAPI MH_RemoveHook(LPVOID) { ++hookCalls; return MH_ERROR_NOT_INITIALIZED; }

namespace kh2coop::inject::warp {
void SetClientAuthority(bool enabled) { if (productionWorldMode) recoverywarp::SetClientAuthority(enabled); }
bool QueueHostTransition(const RoomTransition& room) { return !productionWorldMode || recoverywarp::QueueHostTransition(room); }
bool QueueHostTransition(const RoomTransition& room, const ProducerWorldContext& context,
                         const WorldScope* scope, const ResyncBegin* begin, const ResyncTarget* target) {
    return !productionWorldMode || recoverywarp::QueueHostTransition(room, context, scope, begin, target);
}
bool HostTransitionArrived(std::uint32_t epoch) {
    ++mutableArrivalCalls;
    return productionWorldMode ? recoverywarp::HostTransitionArrived(epoch) : arrived;
}
bool MatchesArrivedHostTransition(const RoomTransition& location) noexcept {
    if (productionWorldMode) return recoverywarp::MatchesArrivedHostTransition(location);
    return arrived && location.epoch == arrivalTarget.epoch &&
           enemysync::SameLocation(location, arrivalTarget);
}
bool TransitionPending() { return productionWorldMode ? recoverywarp::TransitionPending() : pendingTransition; }
std::uint32_t TransitionSerial() { return productionWorldMode ? recoverywarp::TransitionSerial() : transition; }
std::uint32_t LoadSerial() { return productionWorldMode ? recoverywarp::LoadSerial() : load; }
RoomTransition ReadLocation() {
    RoomTransition result;
    enemysync::ReadLocationChecked(result);
    return result;
}
std::uint32_t HostIssuedLoadEpoch(const RoomTransition&) { return 0; }
void HostTargetSpawnPick(std::uint8_t& shared, std::uint32_t& saltTag) { shared = 0; saltTag = 0; }  // VUH-1515 spawn picks: no host-issued load here
}
namespace kh2coop::inject::lifecycletrace {
bool PopEvent(Event&) { return false; }
Stats GetStats() { return {}; }
}
// VUH-1519 party-native is default off; these harnesses keep it inert (its policy has its own controls).
namespace kh2coop::inject::partynative {
bool Requested() { return false; }
void NoteLayout(const PartyLayout&, std::uint8_t, std::uint32_t, const std::array<std::uint64_t, 3>&, bool) {}
void NoteReapply(const PartyReapply&, std::uint32_t) {}
void NoteIntent(const PartyIntent&, std::uint8_t, std::uint32_t, const std::array<std::uint64_t, 3>&, bool) {}
bool HostIntentToPublish(std::uint32_t, const std::array<std::uint64_t, 3>&, PartyIntent&) { return false; }
void NoteHostIntentSent(const PartyIntent&, std::uint32_t) {}
void Observe(std::uint32_t, const std::array<std::uint64_t, 3>&, std::uint8_t, bool) {}
bool HostLayoutToPublish(std::uint32_t, const RoomTransition&, const std::array<std::uint64_t, 3>&, PartyLayout&) { return false; }
void NoteHostSent(const PartyLayout&, std::uint32_t) {}
}

namespace {
std::deque<kh2coop::inject::spawncontroller::NativeConstructionLineage> constructionRows;
unsigned constructionPopCalls = 0;
}
namespace kh2coop::inject::spawncontroller {
bool TestPopNativeConstructionLineage(NativeConstructionLineage& out) {
    ++constructionPopCalls;
    if (constructionRows.empty()) return false;
    out = constructionRows.front(); constructionRows.pop_front(); return true;
}
}

namespace {
std::deque<kh2coop::inject::resourcetrace::ResourceObservation> resourceRows;
kh2coop::inject::resourcetrace::Statistics resourceStats;
unsigned resourcePopCalls = 0, resourceStatsCalls = 0;
}
namespace kh2coop::inject::resourcetrace {
bool TestResourcePop(ResourceObservation& out) {
    ++resourcePopCalls;
    if (resourceRows.empty()) return false;
    out = resourceRows.front(); resourceRows.pop_front(); return true;
}
Statistics TestResourceGetStatistics() { ++resourceStatsCalls; return resourceStats; }
}

namespace {
using namespace kh2coop;
using namespace kh2coop::inject::enemysync;

void __fastcall NativeDamage(void* actor, int delta, int idx, std::uint8_t react) {
    ++nativeCalls;
    if (reinterpret_cast<uintptr_t>(actor) != enemy || delta >= 0 || idx != 0 || react != 1)
        ++errors;
    if (callbackMode == 1) RaiseException(deliberateException, 0, 0, nullptr);
    if (callbackMode == 2) ++load;
    if (callbackMode == 3) {
        Put(image + offsets::active_entity_list::TAIL, player);
        Put(player + offsets::actor::LINKED_NEXT_HANDLE, std::uint32_t {0});
    }
    if (callbackMode == 4) Put(enemy + offsets::actor::LINKED_NEXT_HANDLE, std::uint32_t {1});
    if (callbackMode == 5) {
        std::memcpy(reinterpret_cast<void*>(object + 0x100), reinterpret_cast<void*>(object), 0x80);
        Put(enemy + offsets::actor::OBJENTRY_PTR, object + 0x100);
    }
    if (callbackMode == 6) ++g_bridge.generation;
    if (callbackMode == 7) {
        OnFrameStart(6);
        auto nested = CaptureNativeCensus();
        nestedRejected = !ProcessHostHitClaims(nested);
    }
    auto* hp = reinterpret_cast<std::int32_t*>(status);
    *hp = (std::max)(0, *hp + delta);
}

uintptr_t RecordController(std::size_t index = 0) { return image + 0x50000 + index * 0x100; }
uintptr_t RecordHeader(std::size_t index = 0) { return image + 0x60000 + index * 0x1000; }
uintptr_t RecordData(std::size_t definition = 0, std::size_t record = 0) {
    return RecordHeader(definition) + 44 + record * 64;
}
void SeedRecordCatalog(const std::vector<std::uint16_t>& counts = {1}) {
    constexpr uintptr_t tableRva = 0x2A10010, countRva = 0x2A10418;
    Put(image + countRva, static_cast<std::int32_t>(counts.size()));
    for (std::size_t i = 0; i < counts.size(); ++i) {
        // Separate owned controller/header spans, with complete record bytes.
        const auto controller = RecordController(i), header = RecordHeader(i);
        std::memset(reinterpret_cast<void*>(controller), 0, 64);
        std::memset(reinterpret_cast<void*>(header), 0, 44 + counts[i] * 64 + 1);
        Put(image + tableRva + i * 16, std::uint32_t{808476514});
        Put(image + tableRva + i * 16 + 4, std::uint32_t{2});
        Put(image + tableRva + i * 16 + 8, controller);
        Put(controller, std::uint32_t{808476514}); Put(controller + 4, std::uint32_t{2});
        Put(controller + 8, header); Put(controller + 0x30, header + 44);
        Put(controller + 0x38, header + 44 + counts[i] * 64);
        Put(header, std::uint8_t{2}); Put(header + 2, static_cast<std::uint16_t>(30 + i));
        Put(header + 4, counts[i]);
        for (std::size_t r = 0; r < counts[i]; ++r) {
            Put(RecordData(i, r), std::uint32_t{309});
            Put(RecordData(i, r) + 0x1C, std::uint8_t{2});
            Put(RecordData(i, r) + 0x1E, static_cast<std::uint16_t>(100 + i * 8 + r));
        }
    }
    // Actual native reader provenance offsets, independent of manifest index.
    Put(enemy + 0x9E8, RecordController()); Put(enemy + 0x9F0, RecordData());
}
void Reset(bool client = false, bool registerReader = true) {
    std::memset(reinterpret_cast<void*>(image), 0, 0x3000000);
    productionWorldMode = false;
    transition = 7; load = 11; pendingTransition = false; arrived = true;
    mutableArrivalCalls = 0;
    arrivalTarget = {}; arrivalTarget.epoch = 9;
    arrivalTarget.worldId = 5; arrivalTarget.roomId = 2; arrivalTarget.door = 3;
    arrivalTarget.mapProgram = 4; arrivalTarget.battleProgram = 6; arrivalTarget.eventProgram = 8;
    if (registerReader) kh2coop::inject::spawncontroller::RegisterDiagnosticGameThread();
    nativeCalls = 0; callbackMode = 0;
    nestedRejected = false; g_hostClaimProcessing = false;
    player = image + 0x10000; enemy = image + 0x22000;
    object = image + 0x30000; status = image + 0x40000;
    g_exeBase = image; g_log = nullptr; g_logBudget = 400;
    g_takeDamage = NativeDamage; g_envRole = Role::Off;
    g_role = client ? Role::Client : Role::Host;
    g_bridge = HeadlessWorldBridge {}; g_bridge.slot = client ? 1 : 0;
    g_inst = {}; g_inst.live = true;
    g_inst.world = 5; g_inst.room = 2; g_inst.door = 3;
    g_inst.map = 4; g_inst.btl = 6; g_inst.evt = 8;
    g_seenTransition = transition; g_seenLoad = load; g_epoch = 9;
    g_hpSourceSequence = g_hostHpSequence = 0;
    g_hpSourceExhaustionLogged = false;
    g_activationGeneration = 1; g_activationOrderedGeneration = 1;
    g_lastOrderedGeneration = 1; g_resyncAppliedCut = 0; g_requesterDeliveries = {};
    g_orderedDeliverySerial = 1;
    g_worldSourceSerial = g_worldSendFailures = 0; g_worldSourceExhaustionLogged = false;
    g_resyncPlan.reset(); g_nativeResync.reset(); g_resyncOutput.clear();
    g_activationRecovery.reset(); g_activationReceiptSequence = g_activationReceiptGaps = 0;
    g_clientClaimHold = {}; g_clientManifestRevision = 0;
    g_survivingPackEnabled = false; g_survivingPack = SurvivingPackPreparation {};
    g_resyncDeadline = g_resyncFailureFloor = 0; g_resyncHostCaptured = false;
    g_resyncPriorOrderedGeneration = 0;
    g_resyncWriteFence = ResyncWriteFence::None;
    g_resyncRecordAuthority.reset();
    g_pendingHostRoomContext = {}; g_resyncOutputContext = {};
    g_hostBeginPending = false; g_censusInterrupted = false; g_manifestSent = true;
    g_lastHashMs = 0;
    g_host = {}; g_host.arrived = true; g_host.epoch = 9;
    g_host.world = 5; g_host.room = 2; g_host.btl = 6;
    g_claimSequences = {}; ClearPendingHits();
    g_localClaimConnection = 0; g_localClaimSequence = 0;
    Put(image + offsets::WORLD_ID, std::uint8_t {5});
    Put(image + offsets::ROOM_ID, std::uint8_t {2});
    Put(image + offsets::NOW + 2, std::uint8_t {3});
    Put(image + offsets::MAP_PROGRAM, std::uint16_t {4});
    Put(image + offsets::BATTLE_PROGRAM, std::uint16_t {6});
    Put(image + offsets::EVENT_PROGRAM, std::uint16_t {8});
    Put(image + offsets::IN_FIELD, std::uint8_t {1});
    Put(image + offsets::OPEN_MENU, std::uint8_t {0xFF});
    namespace progress = kh2coop::inject::progresssync;
    progress::g_exeBase = image; progress::g_log = nullptr; progress::g_send = SendCapturedWorld;
    progress::Reset();
    progress::g_role = client ? progress::Role::Client : progress::Role::Host;
    progress::g_version = 1; progress::g_desiredGeneration = 1;
    progress::g_clientFull = client; progress::g_hostFull = !client;
    progress::g_desiredApplied = true; progress::g_hostSampleReady = true;
    std::memcpy(reinterpret_cast<void*>(image + progress::SAVE_RVA), "KH2J", 4);
    progressChecksum = progress::HashSave(progress::Save {});
    Put(image + 0x2A105D0, player);
    Put(image + offsets::active_entity_list::HEAD, player);
    Put(image + offsets::active_entity_list::TAIL, enemy);
    const auto region = enemy & ~uintptr_t {offsets::active_entity_list::HANDLE_LOW_MASK};
    Put(image + offsets::active_entity_list::HANDLE_REGION_TABLE, region);
    Put(player + offsets::actor::LINKED_NEXT_HANDLE,
        static_cast<std::uint32_t>(enemy & offsets::active_entity_list::HANDLE_LOW_MASK));
    Put(enemy + offsets::actor::OBJENTRY_PTR, object);
    Put(object + offsets::objentry::TYPE_FLAGS, std::uint8_t {offsets::objentry::TYPE_MOB});
    Put(object + offsets::objentry::OBJECT_ID, std::uint32_t {309});
    Put(object + offsets::objentry::NAME, char {'M'});
    Put(enemy + ACTOR_STATUS, status);
    Put(status, std::int32_t {1000}); Put(status + 4, std::int32_t {1000});
    Spawn spawn; spawn.actor = enemy; spawn.objentry = object; spawn.status = status;
    spawn.objectId = 309; spawn.present = true; spawn.announced = true;
    spawn.objectType = offsets::objentry::TYPE_MOB; spawn.lastMaxHp = 1000;
    spawn.lastHp = 1000; spawn.netId = 1;
    g_inst.spawns.push_back(spawn); g_inst.byActor[enemy] = 0;
    HostEnemy hostEnemy; hostEnemy.objectId = 309; hostEnemy.hp = 1000;
    hostEnemy.battleProgram = 6; g_host.enemies[1] = hostEnemy;
    SeedRecordCatalog();
}

HitClaim Claim(std::uint32_t seq = 1) {
    HitClaim claim; claim.epoch = 9; claim.seq = seq; claim.netId = 1;
    claim.attackId = 65; claim.damage = 7; claim.objectId = 309;
    claim.attackerSlot = SlotType::Friend1; claim.requesterConnectionId = 101;
    return claim;
}

bool Process() {
    auto census = CaptureNativeCensus();
    return ProcessHostHitClaims(census);
}

bool HasHp(std::uint16_t netId, int hp) {
    for (const auto& packet : g_bridge.outgoing) {
        const std::uint8_t* payload = nullptr; std::size_t size = 0;
        if (decodePacketHeader(packet.data(), packet.size(), payload, size) != PacketType::EnemyHp) continue;
        ByteReader reader(payload, size); EnemyHp message; read(reader, message);
        for (const auto& entry : message.entries) if (entry.netId == netId && entry.hp == hp) return true;
    }
    return false;
}

bool HasHash(std::uint32_t enemiesHash) {
    for (const auto& packet : g_bridge.outgoing) {
        const std::uint8_t* payload = nullptr; std::size_t size = 0;
        if (decodePacketHeader(packet.data(), packet.size(), payload, size) != PacketType::StateHash) continue;
        ByteReader reader(payload, size); StateHash message; read(reader, message);
        if (message.epoch == 9 && message.worldId == 5 && message.roomId == 2 &&
            message.progressHash == progressChecksum && message.enemiesHash == enemiesHash) return true;
    }
    return false;
}

LocalPlayerEnemyHit LocalHit() { return {enemy, player, 65, 7, {1, 2, 3}}; }
void QueueTestClaim(const HitClaim& claim) {
    kh2coop::inject::enemysync::ReceiveHostHitClaim(claim, 1);
}
DWORD WINAPI ForeignPublisher(void* result) {
    const bool drops = DropLocalEnemyDamage(enemy);
    const bool published = RecordLocalPlayerEnemyHit(LocalHit());
    *static_cast<bool*>(result) = drops || published;
    return 0;
}
DWORD WINAPI ForeignHitContext(void* result) {
    const auto context = CaptureNativeHitContext();
    *static_cast<bool*>(result) = context.available || context.readMask != 0 ||
                                 context.generation != 0 || context.connectionId != 0;
    return 0;
}

void __fastcall NativeWarp(const kh2coop::inject::recoverywarp::LocationPacket*,
                          std::uint32_t fade, int mode, std::uint8_t flag, int extra) {
    ++warpCalls;
    Check(fade == 1 && mode == 0 && flag == 0 && extra == 0, "host warp preserves native arguments");
}
int __fastcall NativeDeath(void* actor, int delta, int stat, int react) {
    ++deathCalls;
    Check(reinterpret_cast<uintptr_t>(actor) == enemy && delta == -1000 && stat == 0 && react == 0,
          "client death preserves native arguments");
    Put(status, std::int32_t {0});
    return 0;
}
void ResetWorld() {
    Reset(true);
    productionWorldMode = true;
    namespace w = kh2coop::inject::recoverywarp;
    testWarpChannel = {};
    w::g_channel = &testWarpChannel; w::g_exeBase = image; w::g_log = nullptr;
    w::g_ready = true; w::g_clientAuthority = true; w::g_transitionPending = false;
    w::g_transitionSerial = transition; w::g_loadSerial = load;
    w::g_hostQueued = w::g_hostIssued = false; w::g_hostGeneration = 0;
    w::g_requestTransition = NativeWarp;
    g_applyStatDelta = NativeDeath;
    warpCalls = deathCalls = 0;
}
ProgressUpdate FullProgress(std::uint8_t value) {
    namespace p = kh2coop::inject::progresssync;
    p::Save desired {}; desired[0x10] = value;
    return {3, true, p::g_policy.snapshot(desired.data(), desired.size())};
}
std::vector<std::uint8_t> HostEnvelope(const std::vector<std::uint8_t>& packet,
                                     std::uint64_t source = 1) {
    if (!packet.empty() && (packet[0] == static_cast<std::uint8_t>(PacketType::SessionState) ||
                           packet[0] == static_cast<std::uint8_t>(PacketType::WorldEnvelope))) return packet;
    WorldEnvelope envelope;
    envelope.scope = {std::string(32, 'a'), 100, 1, source,
                      g_bridge.connections[g_bridge.slot], g_bridge.deliverySerial};
    // Start from the actual serializer; replacing only its inner record also
    // permits deliberately malformed inner frames to reach the real decoder.
    envelope.packet = encode(EnemyHp {9, {{1, 1000}}, 1});
    auto bytes = encode(envelope);
    const auto prefix = bytes.size() - envelope.packet.size();
    bytes.resize(prefix); bytes[prefix - 2] = static_cast<std::uint8_t>(packet.size());
    bytes[prefix - 1] = static_cast<std::uint8_t>(packet.size() >> 8);
    bytes.insert(bytes.end(), packet.begin(), packet.end());
    const auto payload = bytes.size() - 3;
    bytes[1] = static_cast<std::uint8_t>(payload); bytes[2] = static_cast<std::uint8_t>(payload >> 8);
    return bytes;
}
bool QueueHostWorld(const std::vector<std::uint8_t>& packet) { return g_bridge.SendToDll(HostEnvelope(packet)); }
void TestWorldRetirement() {
    namespace p = kh2coop::inject::progresssync;
    namespace w = kh2coop::inject::recoverywarp;
    ResetWorld();
    Check(w::QueueHostTransition(arrivalTarget), "current armed generation accepts queued host warp");
    g_host.enemies[1].hp = 100;
    WorldInbox inbox; WorldPumpStats stats;
    const auto oldFull = HostEnvelope(encode(FullProgress(0x11)));
    const auto oldRoom = HostEnvelope(encode(arrivalTarget));
    const auto oldHp = HostEnvelope(encode(EnemyHp {9, {{1, 100}}, 1}));
    const auto oldDeath = HostEnvelope(encode(EnemyDeath {9, 1}));
    g_bridge.incomingCapacity = oldFull.size() + oldRoom.size() + oldHp.size() + oldDeath.size() + 16;
    Check(QueueHostWorld(oldFull) && QueueHostWorld(oldRoom) &&
          QueueHostWorld(oldHp) && QueueHostWorld(oldDeath),
          "bounded bridge is filled with old-session world records");
    ++g_bridge.generation; // no native frame observes an intervening Off role
    Check(inbox.Receive(g_bridge, encodeWorldSessionReset(2, 1), stats) && inbox.PendingCount() == 1,
          "actual WorldInbox defers reset behind a full old bridge");
    auto census = CaptureNativeCensus();
    Check(!ClientFrame(census) && Read<int>(status) == 1000,
          "header change alone blocks old cached client HP before reset consumption");
    g_host.enemies[1].dead = true;
    Check(!ClientFrame(census) && deathCalls == 0, "header change alone blocks old cached native death");
    w::IssueHostTransition();
    Check(warpCalls == 0 && Read<std::uint8_t>(image + p::SAVE_RVA + 0x10) == 0,
          "header change blocks old queued warp and old progress application");
    OnFrameStart(1);
    Check(g_host.epoch == 0 && g_host.enemies.empty() && !g_host.arrived &&
          !p::g_clientFull && !w::g_hostQueued && !w::g_hostIssued && !WorldSessionGeneration() &&
          g_inst.spawns[0].netId == -1 && inbox.PendingCount() == 1,
          "same-role frame retires world state immediately and ignores pre-marker old records");
    Check(g_bridge.incoming.empty() && g_bridge.outgoing.empty() && deathCalls == 0 && warpCalls == 0,
          "unarmed frame drains old input without native effects or world publication");
    QueueHostWorld(encodeWorldSessionReset(1, 1));
    QueueHostWorld(oldFull);
    ReceiveWorldPackets();
    Check(!WorldSessionGeneration() && !p::g_clientFull,
          "stale reset marker cannot arm or restore old full progress");
    inbox.Flush(g_bridge, stats);
    Check(inbox.PendingCount() == 0 && inbox.Receive(g_bridge, HostEnvelope(encode(FullProgress(0x22))), stats) &&
          inbox.Receive(g_bridge, oldRoom, stats), "fresh bootstrap follows the deferred matching marker");
    OnFrameStart(2);
    Check(WorldSessionGeneration() == 2 && p::g_clientFull && p::g_version == 3 &&
          p::g_desired[0x10] == 0x22 && p::g_desiredGeneration == 2 &&
          g_host.epoch == 9 && !g_host.arrived && w::g_hostQueued && w::g_hostGeneration == 2,
          "matching marker preserves first full progress and same-epoch room in one frame");
    w::IssueHostTransition();
    Check(warpCalls == 1 && p::g_desiredApplied && w::g_transitionPending &&
          Read<std::uint8_t>(image + p::SAVE_RVA + 0x10) == 0x22,
          "fresh progress applies before exactly one current-generation native warp");
    w::IssueHostTransition();
    Check(warpCalls == 1 && !w::HostTransitionArrived(9),
          "issued warp neither repeats nor claims arrival from an unchanged old native load");
    w::CompleteLoad();
    Check(w::HostTransitionArrived(9), "fresh native completion and full matching tuple establish arrival");

    ResetWorld();
    NativeEnemy native; bool isEnemy = false;
    Check(ReadNativeEnemy(enemy, native, isEnemy) && isEnemy, "owned native target is available for write guards");
    g_bridge.replaceGenerationOnRead = 1;
    Check(!WriteNativeHp(native, 100, 1) && Read<int>(status) == 1000,
          "HP leaf rechecks header immediately before native write");
    ResetWorld(); ReadNativeEnemy(enemy, native, isEnemy); g_bridge.replaceGenerationOnRead = 1;
    Check(!ApplyNativeDeath(native, 1000, 1) && deathCalls == 0,
          "death leaf rechecks header immediately before native call");
    ResetWorld(); g_host.enemies[1].hp = 100;
    census = CaptureNativeCensus(); g_bridge.replaceGenerationOnRead = 3;
    Check(!ClientFrame(census) && Read<int>(status) == 1000,
          "generation changing after ClientFrame admission is rejected at the HP leaf");
    ResetWorld(); g_host.enemies[1].hp = 100;
    Check(ClientFrame(CaptureNativeCensus()) && Read<int>(status) == 100,
          "current-generation client HP application remains enabled");
    // 8f6a386: an ordinary host tombstone applies only to a body with a
    // continuous living binding from the immediately preceding frame.
    ResetWorld(); g_hitTraceFrame = 40; g_host.enemies[1].dead = true;
    Check(ClientFrame(CaptureNativeCensus()) && deathCalls == 0 && Read<int>(status) == 1000,
          "current-generation death without an established binding is held");
    ResetWorld(); g_hitTraceFrame = 40; g_host.enemies[1].hp = 1000;
    Check(ClientFrame(CaptureNativeCensus()) && deathCalls == 0 && Read<int>(status) == 1000,
          "current-generation live frame establishes a continuous ordinary binding");
    g_host.enemies[1].dead = true; g_hitTraceFrame = 41;
    Check(ClientFrame(CaptureNativeCensus()) && deathCalls == 1 && Read<int>(status) == 0,
          "current-generation native death remains enabled exactly once");
    g_hitTraceFrame = 42;
    ClientFrame(CaptureNativeCensus());
    Check(deathCalls == 1 && Read<int>(status) == 0, "applied native death is never repeated");

    ResetWorld(); p::g_desired[0x10] = 1; p::g_desired[0x11] = 2;
    g_bridge.replaceGenerationOnRead = 5;
    Check(!p::ApplyAtRoomBoundary(1) && Read<std::uint8_t>(image + p::SAVE_RVA + 0x10) == 1 &&
          Read<std::uint8_t>(image + p::SAVE_RVA + 0x11) == 0 && !p::g_personalFailure,
          "progress rechecks each byte and stops after generation retirement without claiming rollback");
    ResetWorld(); p::g_desired[0x10] = 1;
    w::QueueHostTransition(arrivalTarget);
    g_bridge.generationReads = 0; g_bridge.replaceGenerationOnRead = 9;
    w::IssueHostTransition();
    Check(warpCalls == 0 && !w::g_transitionPending && !w::g_hostIssued,
          "retirement after progress application still blocks the final native warp call");
    ResetWorld(); w::QueueHostTransition(arrivalTarget);
    g_bridge.generation = g_activationGeneration = g_activationOrderedGeneration = 2;
    w::IssueHostTransition();
    Check(warpCalls == 0, "a newly armed generation cannot validate an older captured warp target");
    Check(Send(encode(EnemyDeath {9, 1})) && g_bridge.outgoingGenerations.back() == 2,
          "world output carries the captured armed generation");
    ++g_bridge.generation;
    Check(!Send(encode(EnemyDeath {9, 1})) && g_bridge.outgoing.size() == 1,
          "retired generation cannot publish another world record");
    Check(hookCalls == 0, "production warp tests never install or remove hooks");
    productionWorldMode = false;
}

EnemyHp OrderedHp(std::uint64_t sequence, int hp, std::uint32_t epoch = 9) {
    return {epoch, {{1, hp, 1000}}, sequence};
}

std::optional<EnemyHp> LastPublishedHp() {
    for (auto it = g_bridge.outgoing.rbegin(); it != g_bridge.outgoing.rend(); ++it) {
        const std::uint8_t* payload = nullptr; std::size_t size = 0;
        if (decodePacketHeader(it->data(), it->size(), payload, size) != PacketType::EnemyHp) continue;
        ByteReader reader(payload, size); EnemyHp message; read(reader, message);
        return message;
    }
    return {};
}

void QueueHpManifest(std::uint32_t epoch, bool replace = true) {
    EnemyManifest manifest; manifest.epoch = epoch; manifest.replace = replace;
    EnemyManifestEntry entry; entry.netId = 1; entry.objectId = 309;
    entry.battleProgram = 6;
    manifest.entries.push_back(entry);
    QueueHostWorld(encode(manifest));
}

// Positive publication fixtures must earn release through the actual packet
// consumer and complete native readback. No direct hold/knowledge assignments.
void AdmitClientClaimFixture() {
    QueueHpManifest(g_host.epoch, true);
    QueueHostWorld(encode(OrderedHp(g_hostHpSequence + 1, 1000, g_host.epoch)));
    ReceiveWorldPackets();
    if (!g_host.manifestComplete || !g_host.enemies.at(1).hpKnown ||
        g_host.enemies.at(1).maxHp != 1000 || !ReleaseClientClaims(30))
        throw std::runtime_error("client claim fixture lacks complete admitted release witness");
}

void TestEnemyHpOrdering() {
    // Actual codec -> world-ring consumer -> checked native HP write. The
    // sequence intentionally exceeds 32 bits; transport admission is covered
    // by the separate real NetworkClient/relay controls.
    constexpr std::uint64_t base = 0x100000000ULL;
    Reset(true);
    QueueHostWorld(encode(OrderedHp(base + 2, 720)));
    QueueHostWorld(encode(OrderedHp(base + 1, 410)));
    ReceiveWorldPackets();
    Check(g_hostHpSequence == base + 2 && g_host.enemies[1].hp == 720 &&
          ClientFrame(CaptureNativeCensus()) && Read<int>(status) == 720,
          "reversed 64-bit HP samples cannot roll native HP back to the older sample");
    QueueHostWorld(encode(OrderedHp(base + 2, 720)));
    ReceiveWorldPackets();
    Check(ClientFrame(CaptureNativeCensus()) && Read<int>(status) == 720 &&
          g_hostHpSequence == base + 2,
          "equal trusted HP replay is idempotent through the actual native consumer");

    // A replacement manifest clears its HP cache but must retain ordering;
    // replaying the equal reliable cache is needed to repopulate that cache.
    QueueHpManifest(9);
    QueueHostWorld(encode(OrderedHp(base + 1, 410)));
    ReceiveWorldPackets();
    Check(g_host.enemies[1].hp == -1 && g_hostHpSequence == base + 2,
          "replace manifest does not lower the DLL HP floor");
    QueueHostWorld(encode(OrderedHp(base + 2, 720)));
    ReceiveWorldPackets();
    Check(g_host.enemies[1].hp == 720 && ClientFrame(CaptureNativeCensus()) && Read<int>(status) == 720,
          "equal cache replay repopulates a replaced manifest without changing native HP");

    QueueHostWorld(encode(OrderedHp(base + 100, 410, 8)));
    auto outerTrailing = encode(OrderedHp(base + 101, 410)); outerTrailing.push_back(0);
    QueueHostWorld(outerTrailing);
    auto truncated = encode(OrderedHp(base + 102, 410)); truncated.pop_back();
    // Preserve valid outer framing while truncating the declared entry itself.
    const auto payloadLength = truncated.size() - 3;
    truncated[1] = static_cast<std::uint8_t>(payloadLength);
    truncated[2] = static_cast<std::uint8_t>(payloadLength >> 8);
    QueueHostWorld(truncated);
    auto zero = encode(OrderedHp(base + 103, 410));
    std::fill_n(zero.begin() + 7, 8, std::uint8_t {0}); // frame header + epoch
    QueueHostWorld(zero);
    ReceiveWorldPackets();
    Check(g_host.enemies[1].hp == 720 && g_hostHpSequence == base + 2,
          "wrong-epoch, malformed and zero-sequence HP cannot poison cache or ordering floor");
    QueueHostWorld(encode(OrderedHp(base + 3, 600)));
    ReceiveWorldPackets();
    Check(ClientFrame(CaptureNativeCensus()) && Read<int>(status) == 600 && g_hostHpSequence == base + 3,
          "next valid HP sample applies after rejected high-sequence packets");

    auto nextRoom = arrivalTarget; nextRoom.epoch = 10;
    QueueHostWorld(encode(nextRoom)); QueueHpManifest(10);
    QueueHostWorld(encode(OrderedHp(base + 2, 410, 10)));
    ReceiveWorldPackets();
    Check(g_host.epoch == 10 && g_host.enemies[1].hp == -1 && g_hostHpSequence == base + 3,
          "ordinary room transition preserves HP ordering independently of HostRoom replacement");
    RetireWorldSession();
    Check(g_hostHpSequence == base + 3, "role-only world retirement preserves the HP floor");
    QueueHostWorld(encodeWorldSessionReset(1, 1));
    QueueHostWorld(encode(nextRoom)); QueueHpManifest(10);
    QueueHostWorld(encode(OrderedHp(base + 2, 410, 10)));
    ReceiveWorldPackets();
    Check(WorldSessionGeneration() == 1 && g_host.enemies[1].hp == -1 && g_hostHpSequence == base + 3,
          "duplicate matching ordered reset does not admit previously rejected old HP");
    ++g_bridge.generation;
    QueueHostWorld(encode(OrderedHp(base + 200, 410, 10)));
    QueueHostWorld(encodeWorldSessionReset(2, 1));
    QueueHostWorld(encode(nextRoom)); QueueHpManifest(10);
    QueueHostWorld(encode(OrderedHp(1, 800, 10)));
    ReceiveWorldPackets();
    Check(WorldSessionGeneration() == 2 && g_hostHpSequence == 1 && g_host.enemies[1].hp == 800,
          "genuine header retirement ignores pre-marker HP and permits the new ordered namespace");

    // Exercise production HostFrame at its actual send point over the native
    // census. Reset() here is test isolation, never a production counter reset.
    Reset();
    g_hpSourceSequence = base;
    auto census = CaptureNativeCensus();
    Check(HostFrame(1, {}, census) && !LastPublishedHp() && g_hpSourceSequence == base,
          "host non-publication frame does not allocate an HP sequence");
    HostFrame(6, {}, census);
    auto published = LastPublishedHp();
    Check(published && published->sequence == base + 1 && published->epoch == 9 &&
          published->entries.size() == 1 && published->entries[0].hp == 1000,
          "actual HostFrame sends its checked native sample with a full-width source sequence");
    g_bridge.outgoing.clear(); g_bridge.sendAllowed = false;
    Put(status, std::int32_t {900}); census = CaptureNativeCensus();
    HostFrame(12, {}, census);
    Check(g_hpSourceSequence == base + 2 && !LastPublishedHp(),
          "failed HP enqueue consumes the source sample sequence");
    g_bridge.sendAllowed = true;
    Put(status, std::int32_t {800}); census = CaptureNativeCensus();
    HostFrame(18, {}, census); published = LastPublishedHp();
    Check(published && published->sequence == base + 3 && published->entries[0].hp == 800,
          "later host sample does not reuse failed-send sequence or stale HP");
    QueueHostBeginInstance(arrivalTarget);
    Check(HostBeginInstance(), "host room announcement succeeds in sequence-lifetime control");
    HostFrame(24, {0}, census); published = LastPublishedHp();
    Check(published && published->epoch == 10 && published->sequence == base + 4,
          "host room and replacement manifest preserve source sequence");
    ++g_bridge.generation;
    CheckActivationGeneration();
    QueueHostWorld(encodeWorldSessionReset(2, 1)); ReceiveWorldPackets();
    HostFrame(30, {}, census); published = LastPublishedHp();
    Check(published && published->sequence == base + 5,
          "genuine world retirement does not reset the DLL-lifetime host source sequence");
    g_bridge.outgoing.clear();
    g_hpSourceSequence = std::numeric_limits<std::uint64_t>::max() - 1;
    HostFrame(36, {}, census); published = LastPublishedHp();
    Check(published && published->sequence == std::numeric_limits<std::uint64_t>::max(),
          "host may emit final nonzero 64-bit HP sequence");
    g_bridge.outgoing.clear();
    HostFrame(42, {}, census); HostFrame(48, {}, census);
    Check(!LastPublishedHp() && g_hpSourceSequence == std::numeric_limits<std::uint64_t>::max() &&
          g_hpSourceExhaustionLogged,
          "source exhaustion is explicit and never wraps or reuses the terminal sequence");
}

void TestFreshProgressCapture() {
    namespace p = kh2coop::inject::progresssync;
    Reset();
    Put(image + p::SAVE_RVA + 0x10, std::uint8_t {0x5A});
    Put(image + p::SAVE_RVA + 0x23AC, std::uint8_t {0xFF});
    Put(image + p::SAVE_RVA + 0x23DF, std::uint8_t {0xFF});
    Put(image + p::SAVE_RVA + 0x24F0, std::uint8_t {0xA5});
    // Cached mirrors intentionally disagree with native bytes.
    p::g_baseline[0x10] = 0x11; p::g_desired[0x10] = 0x22;
    ProgressUpdate captured; std::uint32_t hash = 0;
    const auto capturedOk = p::CaptureFull(captured, hash);
    p::Save reconstructed {};
    std::array<bool, p::SAVE_END> seen {};
    bool exactAllowed = true; std::size_t count = 0;
    for (const auto& span : captured.spans) {
        for (std::size_t i = 0; i < span.bytes.size(); ++i) {
            const auto offset = span.offset + i;
            if (offset >= reconstructed.size() || !verifiedProgressByteMask(static_cast<std::uint32_t>(offset)) ||
                seen[offset]) { exactAllowed = false; continue; }
            seen[offset] = true; ++count; reconstructed[offset] = span.bytes[i];
        }
    }
    Check(capturedOk && captured.full && captured.version == 1 && exactAllowed && count == 8108 &&
          reconstructed[0x10] == 0x5A && reconstructed[0x23AC] == 0xFE && reconstructed[0x23DF] == 0x0F &&
          hash == p::HashSave(reconstructed),
          "fresh full capture returns all 8108 masked native bytes and their hash, not cached mirrors");
    Check(p::g_baseline[0x10] == 0x11 && p::g_desired[0x10] == 0x22 && g_bridge.outgoing.empty() &&
          Read<std::uint8_t>(image + p::SAVE_RVA + 0x24F0) == 0xA5 &&
          Read<std::uint8_t>(image + p::SAVE_RVA + 0x23AC) == 0xFF &&
          Read<std::uint8_t>(image + p::SAVE_RVA + 0x23DF) == 0xFF,
          "progress capture neither publishes nor changes cached desired/native personal or unverified bits");
    const auto prior = encode(captured); const auto priorHash = hash;
    Reset(); g_bridge.mutateByteAddress = image + p::SAVE_RVA + 0x10;
    g_bridge.mutateByteOnGenerationRead = 3;
    Check(!p::CaptureFull(captured, hash) && encode(captured) == prior && hash == priorHash,
          "native allowed-byte change between capture reads is unavailable with outputs intact");
    Reset(); g_bridge.replaceGenerationOnRead = 3;
    Check(!p::CaptureFull(captured, hash) && encode(captured) == prior && hash == priorHash,
          "capture rejects generation retirement between native reads without partial output");
    Reset(); g_bridge.mutateByteAddress = reinterpret_cast<uintptr_t>(&load);
    g_bridge.mutateByteOnGenerationRead = 3;
    Check(!p::CaptureFull(captured, hash) && encode(captured) == prior && hash == priorHash,
          "native load change during progress capture invalidates the bracket");
    Reset(true);
    Check(!p::CaptureFull(captured, hash), "client desired progress cannot masquerade as a host capture");
    Reset(); p::g_version = 0;
    Check(!p::CaptureFull(captured, hash), "fresh host capture requires an established nonzero version");
    Reset(); p::g_packet = {1};
    Check(!p::CaptureFull(captured, hash), "unsent host progress prevents a coherent full capture");
    Reset(); Put(image + p::SAVE_RVA, std::uint32_t {0});
    Check(!p::CaptureFull(captured, hash) && encode(captured) == prior && hash == priorHash,
          "uninitialized native SAVE is unavailable rather than an empty full snapshot");
    Reset(); pendingTransition = true;
    Check(!p::CaptureFull(captured, hash), "pending native load prevents fresh full progress capture");
    Reset();
    Check(p::CaptureFull(captured, hash) && captured.full && p::SpanBytes(captured.spans) == 8108,
          "valid native SAVE with zero-valued allowed bytes remains a complete full snapshot");
    Reset(); g_bridge.mutateByteAddress = image + p::SAVE_RVA + 0x23AC;
    g_bridge.mutateByteOnGenerationRead = 3;
    Check(p::CaptureFull(captured, hash) && Read<std::uint8_t>(image + p::SAVE_RVA + 0x23AC) == 1 &&
          hash == p::HashSave(p::Save {}),
          "unverified chest bit changes are excluded from masked capture without being overwritten");
    // Exercise the actual Warp::TransitionPending read, not the boolean mock.
    ResetWorld(); g_role = Role::Host; g_bridge.slot = 0;
    p::g_role = p::Role::Host; p::g_hostFull = p::g_hostSampleReady = true;
    const auto beforeFault = encode(captured); const auto hashBeforeFault = hash;
    const auto expected = FullProgress(0);
    DWORD oldProtection = 0, ignoredProtection = 0;
    const bool protectedPage = VirtualProtect(reinterpret_cast<void*>(image + offsets::IN_FIELD), 1,
                                               PAGE_NOACCESS, &oldProtection) != 0;
    bool captureAfterFault = true, matchAfterFault = true;
    if (protectedPage) {
        captureAfterFault = p::CaptureFull(captured, hash);
        matchAfterFault = p::MatchesFull(expected, hash);
    }
    const bool restoredPage = !protectedPage || VirtualProtect(
        reinterpret_cast<void*>(image + offsets::IN_FIELD), 1, oldProtection, &ignoredProtection) != 0;
    Check(protectedPage && restoredPage && !captureAfterFault && !matchAfterFault &&
          encode(captured) == beforeFault && hash == hashBeforeFault,
          "actual owned PAGE_NOACCESS IN_FIELD fault returns unavailable from both fresh progress readers without output mutation");
    productionWorldMode = false;
}

ResyncPlan NativePlan() {
    ResyncPlan plan;
    plan.request.key = {std::string(32, 'a'), 100, 1};
    plan.request.room = arrivalTarget; plan.request.targetMask = 2;
    plan.request.connections = {100, 101, 102};
    plan.targets[0] = {1, 101, 2}; plan.targetCount = 1; plan.remainingMs = 30000;
    return plan;
}
ResyncBegin NativeBegin(const ResyncPlan& plan, const ResyncSnapshot& snapshot, std::uint64_t cut) {
    ResyncBegin begin;
    begin.key = plan.request.key; begin.room = snapshot.room; begin.phase = plan.phase;
    begin.targets = plan.targets; begin.targetCount = plan.targetCount; begin.snapshotCut = cut;
    const auto bytes = encodeResyncSnapshot(snapshot);
    begin.totalBytes = static_cast<std::uint32_t>(bytes.size());
    begin.partCount = static_cast<std::uint16_t>((bytes.size() + RESYNC_MAX_PART_BYTES - 1) / RESYNC_MAX_PART_BYTES);
    begin.sha256 = desyncSha256(bytes); return begin;
}
template<class T> T DecodeNativePacket(const std::vector<std::uint8_t>& packet) {
    const std::uint8_t* payload = nullptr; std::size_t size = 0;
    decodePacketHeader(packet.data(), packet.size(), payload, size);
    ByteReader reader(payload, size); T value; read(reader, value); return value;
}
void StageNativeBootstrap(const ResyncPlan& original, const std::vector<std::uint8_t>& packet) {
    auto plan = original; plan.stage = ResyncPlanStage::Fenced;
    g_bridge.deliverySerial = 2;
    ReceiveResyncPlan(plan); // fence is visible before the immutable snapshot/reset
    ++g_bridge.generation;
    g_bridge.incoming.push_back(encodeWorldSessionReset(g_bridge.generation, g_bridge.deliverySerial));
    g_bridge.incoming.push_back(packet);
    ReceiveWorldPackets();
}
void TestNativeResync() {
    namespace p = kh2coop::inject::progresssync;
    namespace w = kh2coop::inject::recoverywarp;
    Reset();
    const auto plan = NativePlan(); ReceiveResyncPlan(plan);
    Put(status, std::int32_t {777}); Put(image + p::SAVE_RVA + 0x10, std::uint8_t {0x31});
    ResyncSnapshot captured; ProducerWorldContext cut;
    Check(CaptureHostResync(40, captured, cut) && captured.coverageMask == ResyncNativeComplete &&
          captured.livingCount == 1 && captured.deadCount == 0 && captured.enemies[0].hp == 777 &&
          captured.enemies[0].maxHp == 1000 && captured.enemies[0].objectType == offsets::objentry::TYPE_MOB &&
          captured.progress.full && p::SpanBytes(captured.progress.spans) == 8108 &&
          captured.hpSequence && cut.hostSourceSerial && captured.loadSerial == load,
          "actual host snapshot captures fresh complete progress, typed native HP and lifecycle/source cut");
    const auto begin = NativeBegin(plan, captured, cut.hostSourceSerial);
    const auto packet = encodeNativeResyncSnapshot(begin, captured);
    ResyncBegin decodedBegin; ResyncSnapshot decodedSnapshot;
    decodeNativeResyncSnapshot(packet, decodedBegin, decodedSnapshot);
    Check(decodedBegin.sha256 == begin.sha256 && decodedSnapshot.nativeFingerprint == captured.nativeFingerprint &&
          g_inst.spawns[0].lastHp == 1000 && p::g_baseline[0x10] == 0,
          "complete local snapshot roundtrips without substituting stale cached HP or progress");

    Reset(); ReceiveResyncPlan(plan); Put(enemy + offsets::actor::LINKED_NEXT_HANDLE, std::uint32_t {1});
    ResyncSnapshot unavailable; ProducerWorldContext unused;
    Check(!CaptureHostResync(41, unavailable, unused), "incomplete native census is unavailable, never empty");
    Reset(); ReceiveResyncPlan(plan);
    Put(image + offsets::active_entity_list::TAIL, player);
    Put(player + offsets::actor::LINKED_NEXT_HANDLE, std::uint32_t {0});
    g_inst.spawns[0].present = false;
    Check(!CaptureHostResync(42, unavailable, unused), "absent actor with positive last HP is not fresh death history");
    g_inst.spawns[0].lastHp = 0;
    Check(!CaptureHostResync(43, unavailable, unused),
          "absent dead history cannot supply fresh native record content association");
    auto dead = captured; dead.enemies[0].hp = 0;
    dead.enemies[0].life = ResyncLife::ObservedDeadHistory; dead.livingCount = 0; dead.deadCount = 1;
    dead.nativeFingerprint = resyncNativeFingerprint(dead);
    auto deadBegin = NativeBegin(plan, dead, begin.snapshotCut);
    g_inst.spawns.clear(); g_inst.byActor.clear(); g_manifestSent = false;
    Check(CaptureHostResync(44, unavailable, unused) && unavailable.enemies.empty() &&
          !unavailable.livingCount && !unavailable.deadCount && unavailable.coverageMask == ResyncNativeComplete,
          "complete native empty population produces explicit empty manifest and HP sections");

    ResetWorld(); g_bridge.deliverySerial = 2;
    auto fenced = plan; fenced.stage = ResyncPlanStage::Fenced; ReceiveResyncPlan(fenced);
    g_host.enemies[1].hp = 1; g_host.enemies[1].dead = true;
    Check(!WorldSessionGeneration() && !ClientFrame(CaptureNativeCensus()) && !DropLocalEnemyDamage(enemy) &&
          deathCalls == 0 && Read<int>(status) == 1000,
          "delivery fence alone quarantines cached native HP/death and old suppression before reset");
    Check(!w::QueueHostTransition(arrivalTarget) && !p::ApplyAtRoomBoundary(1),
          "delivery fence blocks old progress and warp without waiting for a reset marker");
    g_bridge.incoming.push_back(encodeWorldSessionReset(1, 1)); ReceiveWorldPackets();
    Check(!WorldSessionGeneration() && !p::g_clientFull && !w::g_hostQueued,
          "old same-generation reset carries its old delivery and cannot arm the new header delivery");
    ResetWorld(); StageNativeBootstrap(plan, packet);
    Check(g_nativeResync && WorldSessionGeneration() == 2 && g_host.epoch == 9 && !g_host.arrived &&
          p::g_clientFull && p::g_desired[0x10] == 0x31 && w::g_hostQueued &&
          Read<int>(status) == 1000 && Read<std::uint8_t>(image + p::SAVE_RVA + 0x10) == 0,
          "complete snapshot stages desired state atomically before any native reload or HP write");
    Check(!ObserveNativeResync(90) && g_resyncOutput.empty(), "same native load cannot certify bootstrap convergence");
    ReceiveResyncSnapshot(packet);
    Check(warpCalls == 0 && w::g_hostQueued, "duplicate complete snapshot does not issue a second reload");
    w::IssueHostTransition();
    Check(warpCalls == 1 && Read<std::uint8_t>(image + p::SAVE_RVA + 0x10) == 0x31 &&
          p::g_desiredApplied && w::g_transitionPending,
          "actual guarded reload applies full allowed progress before the native callback");
    w::CompleteLoad(); OnFrameStart(100);
    Check(g_nativeResync && g_nativeResync->firstObserved && !g_nativeResync->finished &&
          Read<int>(status) == 777 && g_resyncOutput.empty(),
          "new native load and one complete HP/progress observation are insufficient for convergence");
    OnFrameStart(100);
    Check(!g_nativeResync->finished, "repeated same-frame observation cannot satisfy two-frame witness");
    OnFrameStart(101);
    Check(g_nativeResync->finished && !g_resyncOutput.empty(), "two distinct actual census/progress frames produce an ACK");
    const auto ack = DecodeNativePacket<ResyncAck>(g_resyncOutput);
    Check(ack.status == ResyncAckStatus::Converged && ack.checksMask == ResyncChecksComplete &&
          ack.loadBefore != ack.loadAfter && ack.observationFrame1 == 100 && ack.observationFrame2 == 101 &&
          ack.snapshotCut == begin.snapshotCut && ack.observedFingerprint == captured.nativeFingerprint &&
          sameResyncRoom(ack.observedRoom, captured.room),
          "converged ACK binds request, cut, digest, fresh load, distinct frames and actual observed state");
    TickNativeResync(102);
    Check(g_resyncOutput.empty() && g_bridge.outgoingContexts.back().deliverySerial == 2 &&
          g_bridge.outgoingContexts.back().generation == 2,
          "native ACK retains the staged target delivery/generation context through enqueue");

    auto checkpoint = fenced; checkpoint.phase = ResyncPhase::Checkpoint;
    ReceiveResyncPlan(checkpoint);
    auto checkpointBegin = NativeBegin(checkpoint, captured, begin.snapshotCut + 1);
    ReceiveResyncSnapshot(encodeNativeResyncSnapshot(checkpointBegin, captured));
    const auto checkpointLoad = w::LoadSerial(); const auto checkpointWarps = warpCalls;
    OnFrameStart(200); OnFrameStart(201);
    auto checkpointAck = DecodeNativePacket<ResyncAck>(g_resyncOutput);
    Check(checkpointAck.status == ResyncAckStatus::Converged && checkpointAck.phase == ResyncPhase::Checkpoint &&
          checkpointAck.loadBefore == checkpointAck.loadAfter && warpCalls == checkpointWarps &&
          w::LoadSerial() == checkpointLoad,
          "matching checkpoint verifies two frames without reset, binding loss or another native reload");

    const auto prepareCheckpoint = [&] {
        ResetWorld(); StageNativeBootstrap(plan, packet);
        w::IssueHostTransition(); w::CompleteLoad(); OnFrameStart(100); OnFrameStart(101); TickNativeResync(102);
        ReceiveResyncPlan(checkpoint);
    };
    prepareCheckpoint();
    auto changedProgress = captured; changedProgress.progress = FullProgress(0x66);
    changedProgress.nativeFingerprint = resyncNativeFingerprint(changedProgress);
    auto changedBegin = NativeBegin(checkpoint, changedProgress, begin.snapshotCut + 1);
    ReceiveResyncSnapshot(encodeNativeResyncSnapshot(changedBegin, changedProgress)); OnFrameStart(200);
    Check(DecodeNativePacket<ResyncAck>(g_resyncOutput).status == ResyncAckStatus::Unavailable &&
          Read<std::uint8_t>(image + p::SAVE_RVA + 0x10) == 0x31 && warpCalls == 1,
          "checkpoint progress mismatch reports unavailable without applying SAVE or reloading");
    prepareCheckpoint();
    ReceiveResyncSnapshot(encodeNativeResyncSnapshot(checkpointBegin, captured));
    Put(image + offsets::active_entity_list::TAIL, player);
    Put(player + offsets::actor::LINKED_NEXT_HANDLE, std::uint32_t {0});
    OnFrameStart(200); OnFrameStart(201);
    Check(!g_nativeResync->finished && !g_nativeResync->firstObserved && g_resyncOutput.empty(),
          "missing native population cannot certify a nonempty checkpoint from cached manifest rows");
    prepareCheckpoint();
    ReceiveResyncSnapshot(encodeNativeResyncSnapshot(checkpointBegin, captured));
    OnFrameStart(200); Put(status + 4, std::int32_t {999}); OnFrameStart(201);
    Check(!g_nativeResync->finished && !g_nativeResync->firstObserved && g_resyncOutput.empty(),
          "changed checked max HP invalidates the second observation despite unchanged netId/current HP");
    prepareCheckpoint();
    ReceiveResyncSnapshot(encodeNativeResyncSnapshot(checkpointBegin, captured));
    OnFrameStart(200); ++g_bridge.deliverySerial; TickNativeResync(201, true);
    Check(!g_nativeResync->finished && !g_nativeResync->firstObserved && g_resyncOutput.empty(),
          "new target delivery serial invalidates an in-progress two-frame witness");

    ResetWorld(); StageNativeBootstrap(plan, encodeNativeResyncSnapshot(deadBegin, dead));
    const auto deadAck = DecodeNativePacket<ResyncAck>(g_resyncOutput);
    Check(deadAck.status == ResyncAckStatus::Unavailable && deadAck.checksMask == 0 &&
          deadAck.observedRoom.epoch == 0 && !w::g_hostQueued && !p::g_clientFull &&
          Read<int>(status) == 1000 && deathCalls == 0,
          "dead bootstrap remains unavailable before desired writes or lethal enrollment and claims no observed room");

    ResetWorld(); auto corrupt = packet; corrupt.back() ^= 1;
    StageNativeBootstrap(plan, corrupt);
    Check(!g_nativeResync && !p::g_clientFull && g_host.epoch == 0 && !w::g_hostQueued,
          "bad complete snapshot digest cannot mutate desired progress, manifest or queued warp");
    ResetWorld(); auto wrongBegin = begin; wrongBegin.key.requestId = 99;
    StageNativeBootstrap(plan, encodeNativeResyncSnapshot(wrongBegin, captured));
    Check(!g_nativeResync && !p::g_clientFull && !w::g_hostQueued,
          "wrong request snapshot is rejected before native desired-state staging");
    ResetWorld(); g_bridge.deliverySerial = g_orderedDeliverySerial = 2;
    ReceiveResyncPlan(fenced); ReceiveResyncSnapshot(packet);
    Check(DecodeNativePacket<ResyncAck>(g_resyncOutput).status == ResyncAckStatus::Unavailable &&
          g_host.epoch == 9 && p::g_desired[0x10] == 0 && !w::g_hostQueued,
          "bootstrap cannot replace a still-armed old room without a fresh ordered reset");
    ResetWorld(); StageNativeBootstrap(plan, packet);
    g_resyncDeadline = GetTickCount64(); TickNativeResync(1);
    Check(DecodeNativePacket<ResyncAck>(g_resyncOutput).status == ResyncAckStatus::Unavailable && warpCalls == 0,
          "expired native convergence produces unavailable without manufacturing a fresh load");

    Reset(true); p::g_version = 9; const auto desiredBefore = p::g_desired;
    Check(!p::StageFull(FullProgress(0x55), 1) && p::g_desired == desiredBefore && p::g_version == 9,
          "recognized but stale full progress is not reported as staged acceptance");
    Reset(); ReceiveResyncPlan(plan); TickNativeResync(50, true);
    g_bridge.sendAllowed = false; TickNativeResync(51);
    Check(!g_resyncOutput.empty() && g_resyncOutput.front() == static_cast<std::uint8_t>(PacketType::ResyncResult) &&
          DecodeNativePacket<ResyncResult>(g_resyncOutput).reason == ResyncResultReason::Overflow,
          "failed native snapshot enqueue becomes a typed terminal failure rather than a cut with missing bytes");
    Reset(); ProducerWorldContext first, second;
    Check(CaptureWorldContext(first), "host captures a native source serial at publication");
    g_bridge.sendAllowed = false;
    Check(!SendCapturedWorld(encode(EnemyDeath {9, 1}), first), "failed enqueue remains observable");
    RetireWorldSession(); g_bridge.sendAllowed = true;
    Check(CaptureWorldContext(second) && second.hostSourceSerial > first.hostSourceSerial,
          "world retirement and failed send never reuse the DLL-lifetime source serial");
    g_bridge.deliverySerial = 2;
    Check(!SendCapturedWorld(encode(EnemyDeath {9, 1}), second), "retained output cannot relabel itself to a newer delivery");
    Reset(); p::g_packet = encode(FullProgress(0x44)); CaptureWorldContext(p::g_packetContext);
    g_bridge.deliverySerial = 2;
    Check(!p::FlushHostPacket() && p::g_packet.empty() && g_bridge.outgoing.empty(),
          "queued progress is retired rather than stamped with a newer delivery context");
    Reset(); g_worldSourceSerial = UINT64_MAX;
    Check(!CaptureWorldContext(second) && g_worldSourceSerial == UINT64_MAX && g_worldSourceExhaustionLogged,
          "native source counter exhaustion is explicit and cannot wrap");

    Reset(true);
    g_bridge.incoming.push_back(encode(EnemyHp {9, {{1, 1}}, 10})); ReceiveWorldPackets();
    Check(g_host.enemies[1].hp == 1000 && g_hostHpSequence == 0,
          "raw unscoped HP cannot enter the native authoritative consumer");
    auto scoped = DecodeNativePacket<WorldEnvelope>(HostEnvelope(encode(EnemyHp {9, {{1, 1}}, 10})));
    scoped.scope.targetDeliverySerial = 9; g_bridge.incoming.push_back(encode(scoped)); ReceiveWorldPackets();
    Check(g_host.enemies[1].hp == 1000 && g_hostHpSequence == 0,
          "wrong target delivery is rejected before a high HP sequence can poison the floor");
    scoped.scope.targetDeliverySerial = 1; scoped.scope.sourceConnectionId = 102;
    g_bridge.incoming.push_back(encode(scoped)); ReceiveWorldPackets();
    Check(g_host.enemies[1].hp == 1000, "nonhost scope cannot supply authoritative native HP");
    ResetWorld(); StageNativeBootstrap(plan, packet);
    g_bridge.incoming.push_back(HostEnvelope(encode(EnemyHp {9, {{1, 1}}, 999}), begin.snapshotCut));
    ReceiveWorldPackets();
    Check(g_host.enemies[1].hp == 777 && g_hostHpSequence == captured.hpSequence,
          "snapshot source cut rejects an older native FIFO packet even with a higher HP sequence");
    g_bridge.incoming.push_back(HostEnvelope(encode(EnemyHp {9, {{1, 700}}, 999}), begin.snapshotCut + 1));
    ReceiveWorldPackets();
    Check(g_host.enemies[1].hp == 700, "post-cut native continuation retains its own captured source identity");

    Reset(); ReceiveResyncPlan(plan);
    WorldEnvelope claimEnvelope;
    claimEnvelope.scope = {std::string(32, 'a'), 101, 1, 0, 100, 1};
    claimEnvelope.packet = encode(Claim());
    g_bridge.peerDeliverySerials[1] = 2;
    g_bridge.incoming.push_back(encode(claimEnvelope)); ReceiveWorldPackets(); Process();
    Check(nativeCalls == 0 && !g_hitCount,
          "old requester delivery in an already queued host FIFO envelope cannot survive the selected-target fence");
    claimEnvelope.scope.sourceDeliverySerial = 2;
    g_bridge.incoming.push_back(encode(claimEnvelope)); ReceiveWorldPackets();
    Check(g_hitCount == 1 && g_pendingHits[g_hitHead].requesterDeliverySerial == 2,
          "admitted hit claim retains authenticated requester delivery with its immutable pending identity");
    g_requesterDeliveries[1].minimum = 3; g_bridge.peerDeliverySerials[1] = 3; Process();
    Check(nativeCalls == 0 && g_claimSequences[1].consumed == 0,
          "requester delivery retirement after queue admission blocks consumption and native damage");
    claimEnvelope.scope.sourceDeliverySerial = 3;
    g_bridge.incoming.push_back(encode(claimEnvelope)); ReceiveWorldPackets(); Process();
    Check(nativeCalls == 1 && Read<int>(status) == 993,
          "current requester delivery preserves the original once-only native hit path");
    Reset(); QueueTestClaim(Claim());
    g_bridge.peerDeliverySerials[1] = 2; Process();
    Check(nativeCalls == 0 && g_claimSequences[1].consumed == 0 && !g_resyncPlan,
          "atomic peer delivery header retires admitted claims before the host Plan reaches the DLL");
    claimEnvelope.scope.sourceDeliverySerial = 1;
    g_bridge.incoming.push_back(encode(claimEnvelope)); ReceiveWorldPackets();
    Check(!g_hitCount, "old reverse FIFO envelope is rejected during the pre-Plan fencing window");
    claimEnvelope.scope.sourceDeliverySerial = 3;
    g_bridge.incoming.push_back(encode(claimEnvelope)); ReceiveWorldPackets();
    Check(!g_hitCount, "unconfirmed future requester delivery is rejected rather than advancing authority");
    Check(hookCalls == 0, "native resync controls never install a hook or attach to a game");
}

// Exercise the real linked native catalog reader through the actual EnemySync
// resync consumer. Only owned image bytes and the existing transport boundary
// are synthetic; record matching, current-catalog reads and write fences are not.
void TestNativeRecordResync() {
    namespace w = kh2coop::inject::recoverywarp;
    namespace p = kh2coop::inject::progresssync;
    Reset();
    const auto plan = NativePlan();
    auto capture = [&](const std::vector<std::uint16_t>& counts,
                       ResyncSnapshot& snapshot, ResyncBegin& begin) {
        Reset(); SeedRecordCatalog(counts); ReceiveResyncPlan(plan);
        Put(status, std::int32_t{777});
        ProducerWorldContext context;
        const bool complete = CaptureHostResync(300, snapshot, context);
        Check(complete && snapshot.coverageMask == ResyncNativeComplete &&
              snapshot.recordDefinitions.size() == counts.size() && snapshot.enemies.size() == 1 &&
              snapshot.enemies[0].record.definitionIndex == 0 && snapshot.enemies[0].record.recordIndex == 0,
              "host capture carries complete actual-reader catalog and exact native record reference");
        if (!complete || snapshot.enemies.size() != 1) return false;
        begin = NativeBegin(plan, snapshot, context.hostSourceSerial);
        return true;
    };
    auto stage = [&](const ResyncSnapshot& snapshot, const ResyncBegin& begin,
                     const std::vector<std::uint16_t>& counts = std::vector<std::uint16_t>{1}) {
        ResetWorld(); SeedRecordCatalog(counts);
        StageNativeBootstrap(plan, encodeNativeResyncSnapshot(begin, snapshot));
        if (w::g_hostQueued) { w::IssueHostTransition(); w::CompleteLoad(); }
    };
    auto noWrites = [&] { return Read<int>(status) == 1000 && deathCalls == 0 && nativeCalls == 0; };
    ResyncSnapshot snapshot; ResyncBegin begin;
    if (!capture({1}, snapshot, begin)) return;
    Check(snapshot.recordDefinitions[0].header.size() == 44 &&
          snapshot.recordDefinitions[0].records.size() == 1 &&
          snapshot.recordDefinitions[0].records[0][0x1E] == 100,
          "host native definition preserves full header and record bytes instead of positional identity");

    stage(snapshot, begin); OnFrameStart(400);
    Check(Read<int>(status) == 777 && deathCalls == 0 && g_resyncWriteFence == ResyncWriteFence::Exact,
          "exact current native record association permits the intended positive HP update");

    stage(snapshot, begin); Put(RecordHeader() + 14, std::uint8_t{1}); OnFrameStart(400);
    Check(Read<int>(status) == 777 && deathCalls == 0 && g_resyncWriteFence == ResyncWriteFence::Exact &&
          snapshot.recordDefinitions[0].header[14] == 0,
          "stable peer-local activation marker difference is portable-compatible and permits exact HP update");

    stage(snapshot, begin); Put(RecordData() + 40, std::uint8_t{1});
    Check(g_host.enemies[1].objectId == 309 && g_host.enemies[1].spawnIndex == 0 &&
          SamePoint(g_host.enemies[1].spawnPos, Vec3{}),
          "wrong-record adversary retains identical legacy object point and manifest index");
    OnFrameStart(400);
    Check(noWrites() && g_resyncWriteFence != ResyncWriteFence::None &&
          !g_inst.spawns.empty() && g_inst.spawns[0].netId < 0,
          "different full record bytes deny HP repair despite matching legacy object point and index");

    stage(snapshot, begin); Put(enemy + 0x9F0, uintptr_t{0}); OnFrameStart(400);
    Check(noWrites() && g_resyncWriteFence != ResyncWriteFence::None,
          "missing native record pointer never falls back to manifest point or index");
    stage(snapshot, begin); Put(enemy + 0x9F0, RecordData() + 1); OnFrameStart(400);
    Check(noWrites(), "nonintegral native record pointer cannot authorize an HP write");
    stage(snapshot, begin); Put(RecordHeader() + 15, std::uint8_t{1}); OnFrameStart(400);
    Check(noWrites(), "non-excluded header byte mismatch blocks target HP mutation");
    stage(snapshot, begin); Put(image + 0x2A10418, std::int32_t{2});
    std::memcpy(reinterpret_cast<void*>(image + 0x2A10010 + 16), reinterpret_cast<void*>(image + 0x2A10010), 16);
    OnFrameStart(400);
    Check(noWrites(), "duplicate ordinary controller association cannot select the first matching actor record");
    stage(snapshot, begin); Put(image + 0x2A10010 + 4, std::uint32_t{4}); OnFrameStart(400);
    Check(noWrites(), "unknown ordinary table flags leave current catalog unsupported and HP unchanged");
    stage(snapshot, begin); Put(image + 0x2A10418, std::int32_t{65}); OnFrameStart(400);
    Check(noWrites(), "partial over-cap current catalog cannot authorize HP from a matching prefix");
    stage(snapshot, begin); Put(RecordController() + 0x38, RecordData() + 128); OnFrameStart(400);
    Check(noWrites(), "inconsistent full native record extent remains unavailable before target writes");

    stage(snapshot, begin);
    DWORD oldHeaderProtection = 0, ignoredHeaderProtection = 0;
    const bool headerProtected = VirtualProtect(reinterpret_cast<void*>(RecordHeader()), 1,
                                                PAGE_NOACCESS, &oldHeaderProtection) != 0;
    if (headerProtected) OnFrameStart(400);
    const bool headerRestored = !headerProtected || VirtualProtect(reinterpret_cast<void*>(RecordHeader()), 1,
        oldHeaderProtection, &ignoredHeaderProtection) != 0;
    Check(headerProtected && headerRestored && noWrites(),
          "actual owned PAGE_NOACCESS catalog read failure denies target HP and death writes");
    if (!headerRestored) return; // never memset an unrestored owned page in Reset

    auto legacy = snapshot; legacy.coverageMask = ResyncComplete; legacy.recordDefinitions.clear();
    for (auto& row : legacy.enemies) row.record = {};
    legacy.nativeFingerprint = resyncNativeFingerprint(legacy);
    auto legacyBegin = NativeBegin(plan, legacy, begin.snapshotCut);
    stage(legacy, legacyBegin); OnFrameStart(400);
    Check(noWrites() && !w::g_hostQueued && warpCalls == 0,
          "legacy coverage127 without record references is unavailable to native recovery without positional fallback");

    ResyncSnapshot pair; ResyncBegin pairBegin;
    if (!capture({2}, pair, pairBegin)) return;
    stage(pair, pairBegin, {2}); Put(RecordData(0, 1) + 40, std::uint8_t{1}); OnFrameStart(400);
    Check(noWrites(), "changed unselected record invalidates whole-array compatibility before selected actor HP repair");
    stage(pair, pairBegin, {2}); Put(enemy + 0x9F0, RecordData(0, 1)); OnFrameStart(400);
    Check(noWrites(), "different native record index with same object and point never inherits the host reference");

    ResyncSnapshot multiple; ResyncBegin multipleBegin;
    if (!capture({1, 0}, multiple, multipleBegin)) return;
    stage(multiple, multipleBegin, {1, 0}); Put(RecordHeader(1) + 15, std::uint8_t{1}); OnFrameStart(400);
    Check(noWrites(), "unreferenced definition header mismatch remains in whole-current-catalog write qualification");

    stage(multiple, multipleBegin, {1, 0}); OnFrameStart(400);
    const auto current = CaptureNativeCensus();
    RecordCatalog wholeBefore, wholeAfter;
    const bool beforeComplete = FreshRecordCatalog(current, wholeBefore);
    Put(RecordHeader(1) + 15, std::uint8_t{1});
    const bool afterComplete = FreshRecordCatalog(current, wholeAfter);
    Check(beforeComplete && afterComplete && !SameRecordCatalogSample(wholeBefore, wholeAfter) &&
          wholeBefore.entries[1].content.header[15] != wholeAfter.entries[1].content.header[15],
          "actual complete whole-catalog bookends retain and reject unreferenced definition drift");

    // The failure fence must survive the terminal result's cleanup of transient
    // snapshot state; restoring bytes must not silently restore legacy writes.
    stage(snapshot, begin); OnFrameStart(400);
    Put(RecordData() + 40, std::uint8_t{1}); g_host.enemies[1].dead = true;
    const auto beforeFailure = Read<int>(status);
    OnFrameStart(401);
    Check(Read<int>(status) == beforeFailure && deathCalls == 0,
          "already bound actor with changed record cannot receive cached host death");
    QueueNativeAck(ResyncAckStatus::Unavailable, "controlled record mismatch");
    Check(g_resyncWriteFence == ResyncWriteFence::Failed,
          "terminal native unavailable ACK leaves a persistent failed write fence");
    ResyncResult terminal; terminal.key = plan.request.key; terminal.reason = ResyncResultReason::NativeUnavailable;
    terminal.targetCount = 1; terminal.targets[0].target = plan.targets[0];
    terminal.targets[0].status = ResyncAckStatus::Unavailable;
    terminal.targets[0].error = "controlled record mismatch";
    g_bridge.incoming.push_back(encode(terminal)); ReceiveWorldPackets();
    Put(RecordData() + 40, std::uint8_t{0}); g_host.enemies[1].dead = false; g_host.enemies[1].hp = 1;
    ClientFrame(CaptureNativeCensus());
    Check(!g_nativeResync && !g_resyncPlan && g_resyncWriteFence == ResyncWriteFence::Failed &&
          Read<int>(status) == beforeFailure && deathCalls == 0,
          "terminal result cleanup cannot restore positional HP writes after failed exact association");
    g_host.enemies[1].dead = true; ClientFrame(CaptureNativeCensus());
    Check(Read<int>(status) == beforeFailure && deathCalls == 0,
          "persistent failure fence also blocks cached lethal repair after result cleanup");

    stage(snapshot, begin); OnFrameStart(400); OnFrameStart(401); TickNativeResync(402);
    auto checkpoint = plan; checkpoint.stage = ResyncPlanStage::Fenced; checkpoint.phase = ResyncPhase::Checkpoint;
    ReceiveResyncPlan(checkpoint);
    const auto checkpointBegin = NativeBegin(checkpoint, snapshot, begin.snapshotCut + 1);
    ReceiveResyncSnapshot(encodeNativeResyncSnapshot(checkpointBegin, snapshot));
    Check(g_resyncWriteFence == ResyncWriteFence::ObserveOnly,
          "checkpoint establishes observe-only fence before its first native observation");
    Put(status, std::int32_t{500}); const auto warpsBeforeCheckpoint = warpCalls;
    OnFrameStart(500); OnFrameStart(501);
    Check(Read<int>(status) == 500 && deathCalls == 0 && warpCalls == warpsBeforeCheckpoint &&
          g_resyncWriteFence != ResyncWriteFence::Exact && g_resyncWriteFence != ResyncWriteFence::None,
          "checkpoint reports actual HP mismatch without repairing HP or issuing another reload");
    g_host.enemies[1].dead = true; ClientFrame(CaptureNativeCensus());
    Check(Read<int>(status) == 500 && deathCalls == 0,
          "checkpoint observe-only state cannot consume cached host death as a native lethal write");

    Reset(); ReceiveResyncPlan(plan); Put(enemy + 0x9E8, uintptr_t{0});
    ResyncSnapshot unavailable; ProducerWorldContext context;
    Check(!CaptureHostResync(600, unavailable, context),
          "host cannot publish coverage255 from a present actor lacking current record association");
    Reset(); ReceiveResyncPlan(plan); Put(image + 0x2A10010 + 4, std::uint32_t{4});
    Check(!CaptureHostResync(601, unavailable, context),
          "host cannot omit an unsupported current native catalog from a supposedly complete snapshot");
    Check(hookCalls == 0 && p::g_exeBase == image,
          "record consumer controls use actual reader over owned memory without hook or game attachment");
}

// Failure injection stays at the existing mocked native callee: the production
// write/check/SEH path and post-call reads run unchanged over owned memory.
unsigned recordDeathFault = 0;
int recordDeathDelta = 0;
bool recordDeathArguments = false, recordDeathProtected = false;
DWORD recordDeathOldProtection = 0;
int __fastcall RecordGuardDeath(void* actor, int delta, int stat, int react) {
    ++deathCalls; recordDeathDelta = delta;
    recordDeathArguments = reinterpret_cast<uintptr_t>(actor) == enemy && stat == 0 && react == 0;
    Put(status, std::int32_t{0});
    if (recordDeathFault == 1)
        recordDeathProtected = VirtualProtect(reinterpret_cast<void*>(status), 1, PAGE_NOACCESS,
                                             &recordDeathOldProtection) != 0;
    if (recordDeathFault == 2)
        Put(enemy + offsets::actor::LINKED_NEXT_HANDLE, std::uint32_t{1});
    return 0;
}

void SeedPreparationShadows(int hp) {
    SeedRecordCatalog({5});
    constexpr std::array<std::uint16_t, 5> ids {11, 12, 13, 14, 18};
    Put(object + offsets::objentry::OBJECT_ID, std::uint32_t {302});
    g_inst.spawns.clear(); g_inst.byActor.clear();
    for (std::size_t i = 0; i < ids.size(); ++i) {
        const auto actor = enemy + i * 0x2000, actorStatus = status + i * 0x100;
        Put(RecordData(0, i), std::uint32_t {302});
        Put(RecordData(0, i) + 0x1E, ids[i]);
        Put(RecordData(0, i) + 0x2A, std::uint16_t {8});
        Put(actor + offsets::actor::OBJENTRY_PTR, object); Put(actor + ACTOR_STATUS, actorStatus);
        Put(actorStatus, static_cast<std::int32_t>(hp)); Put(actorStatus + 4, std::int32_t {20});
        Put(actor + 0x9E8, RecordController()); Put(actor + 0x9F0, RecordData(0, i));
        const auto next = i + 1 < ids.size() ? static_cast<std::uint32_t>(
            (actor + 0x2000) & offsets::active_entity_list::HANDLE_LOW_MASK) : 0;
        Put(actor + offsets::actor::LINKED_NEXT_HANDLE, next);
        Spawn spawn; spawn.actor = actor; spawn.objentry = object; spawn.status = actorStatus;
        spawn.objectId = 302; spawn.objectType = 4; spawn.present = spawn.announced = true;
        spawn.spawnIndex = static_cast<std::uint16_t>(i); spawn.netId = static_cast<int>(i + 1);
        spawn.lastHp = hp; spawn.lastMaxHp = 20;
        g_inst.byActor[actor] = g_inst.spawns.size(); g_inst.spawns.push_back(spawn);
    }
    Put(image + offsets::active_entity_list::TAIL, enemy + 4 * 0x2000);
}

void TestPackOccupancyProjection() {
    namespace w = kh2coop::inject::recoverywarp;
    namespace sc = kh2coop::inject::spawncontroller;
    Reset(); SeedPreparationShadows(17);
    const auto plan = NativePlan(); ReceiveResyncPlan(plan);
    ResyncSnapshot snapshot; ProducerWorldContext context;
    if (!CaptureHostResync(390, snapshot, context)) { Check(false, "projection captures source fixture"); return; }
    const auto begin = NativeBegin(plan, snapshot, context.hostSourceSerial);
    ResetWorld(); SeedPreparationShadows(20); g_survivingPackEnabled = true;
    StageNativeBootstrap(plan, encodeNativeResyncSnapshot(begin, snapshot));
    if (w::g_hostQueued) { w::IssueHostTransition(); w::CompleteLoad(); }
    g_seenLoad = w::LoadSerial(); g_seenTransition = w::TransitionSerial(); g_host.arrived = true;
    const auto census = CaptureNativeCensus();
    RecordCatalog catalog;
    Check(g_survivingPack.Intent() && FreshRecordCatalog(census, catalog),
          "projection uses actual checked native catalog and immutable receiver intent");
    if (!g_survivingPack.Intent() || catalog.status != NativeRecordContentStatus::Complete) return;
    std::vector<NativeRecordContentDefinition> definitions;
    for (std::uint32_t d = 0; d < catalog.entryCount; ++d) definitions.push_back(catalog.entries[d].content);
    const auto candidates = RecordCandidates(definitions);
    std::vector<PackReadyReference> ready;
    std::vector<SurvivingPackMember> members;
    sc::NativeSelectedOccupancy baseline;
    baseline.selectedDefinition = 0; baseline.catalogStable = baseline.lifecycleStable = baseline.mutationStable = true;
    baseline.listedComplete = baseline.cacheAvailable = baseline.controllerStateAvailable = true;
    baseline.cache[0].room = baseline.cache[1].room = census.location.roomId;
    for (const auto& native : census.enemies) {
        sc::NativeRecordMembership membership;
        if (!RecordMember(catalog, census, native.actor, membership)) { Check(false, "projection checks every native membership"); return; }
        ready.push_back({native.actor, native.objentry, native.status, membership.controller[0], membership.record[0],
            membership.tableIndex, membership.recordIndex, native.objectId, native.objectType});
        members.push_back({{static_cast<std::uint16_t>(membership.tableIndex), membership.recordIndex},
            native.objectId, native.objectType, native.hp, native.maxHp, true});
        sc::NativeOccupancyNode node;
        node.actor = native.actor; node.recordIndex = membership.recordIndex;
        node.stable = node.exactSelectedReference = true;
        auto& f = node.fields[0]; f.readMask = 1023;
        f.object = native.objentry; f.status = native.status;
        f.controller = membership.controller[0]; f.record = membership.record[0];
        f.objectId = 302; f.objectType = 4;
        std::memcpy(&f.recordId, catalog.entries[0].content.records[node.recordIndex].data() + 0x1E, 2);
        node.fields[1] = f; baseline.nodes.push_back(node);
        ++baseline.activeReferenceCounts[node.recordIndex]; ++baseline.selectedReferenceCount;
        baseline.cache[0].ids[node.recordIndex] = f.recordId;
        ++baseline.cacheSelectedCounts[node.recordIndex]; ++baseline.cacheEntryCount;
    }
    const auto intent = *g_survivingPack.Intent();
    auto project = [&](const auto& raw, const auto& refs) {
        SurvivingPackFacts facts;
        facts.context = intent.context; facts.arrived = true; facts.load = census.load;
        facts.mutation = {1, 0, true, false, true}; // supplied projection control, not installed-hook evidence
        facts.catalogStatus = NativeRecordContentStatus::Complete; facts.catalog = candidates;
        facts.readyMembersComplete = true; facts.readyMembers = members;
        ProjectPackOccupancy(raw, catalog, census.location.roomId, refs, facts); return facts;
    };
    auto classify = [&](const auto& facts) {
        SurvivingPackPreparation reducer;
        if (!reducer.Begin(true, intent, GetTickCount64())) return SurvivingPackClassification::Terminal;
        return reducer.Observe(facts, GetTickCount64());
    };
    const auto full = project(baseline, ready);
    Check(full.listedReadyMask == 31 && full.cacheSelectedMask == 31 && full.cacheEntryCount == 5 &&
          classify(full) == SurvivingPackClassification::FullSetAlreadyPresent,
          "root projection joins all five exact actor object status controller record and index receipts");
    auto raw = baseline; auto refs = ready; refs[0].actor += 8;
    Check(project(raw, refs).listed == SurvivingPackListed::Pending, "different ready actor never qualifies a raw selected return");
    refs = ready; refs[0].status += 8;
    Check(project(raw, refs).listed == SurvivingPackListed::Pending, "nonnull raw status alone cannot supply ready membership");
    refs = ready; refs[0].controller += 8;
    Check(project(raw, refs).listed == SurvivingPackListed::Pending, "different controller provenance holds selected readiness");
    refs = ready; refs[0].record += 64;
    Check(project(raw, refs).listed == SurvivingPackListed::Pending, "different native record pointer holds selected readiness");
    refs = ready; refs[0].recordIndex = 1;
    Check(project(raw, refs).listed == SurvivingPackListed::Pending, "different native record index holds selected readiness");
    raw = baseline; raw.nodes[0].fields[0].objectType = 0; raw.nodes[0].fields[1] = raw.nodes[0].fields[0];
    Check(project(raw, ready).listed == SurvivingPackListed::Pending,
          "same actor and pointers with contradictory raw combat type cannot qualify ready membership");
    raw.listedComplete = false;
    Check(project(raw, ready).listed == SurvivingPackListed::Pending && !project(raw, ready).listedReadyMask,
          "partial list retains raw type contradiction without publishing a positive ready mask");
    raw = baseline; raw.nodes[0].fields[0].objectName = {'F', '_'}; raw.nodes[0].fields[1] = raw.nodes[0].fields[0];
    Check(project(raw, ready).listed == SurvivingPackListed::Pending,
          "same actor and pointers with raw noncombat name cannot qualify ready membership");
    raw.listedComplete = false;
    Check(project(raw, ready).listed == SurvivingPackListed::Pending && !project(raw, ready).listedReadyMask,
          "partial list retains raw noncombat name contradiction without publishing a positive ready mask");
    raw = baseline;
    refs = ready; refs.push_back(refs[0]);
    Check(project(raw, refs).listed == SurvivingPackListed::Pending, "ambiguous ready receipts cannot collapse to one selected mask bit");
    raw = baseline; ++raw.activeReferenceCounts[0];
    Check(project(raw, ready).listed == SurvivingPackListed::Conflict, "raw duplicate multiplicity is rejected before mask folding");
    raw = baseline; raw.deferredReferenceCounts[0] = 1; raw.activeReferenceCounts[0] = 0;
    Check(project(raw, ready).listed == SurvivingPackListed::Deferred, "deferred selected actor remains held despite ready-census pointer");
    raw = baseline; raw.pendingNodeCount = 1;
    Check(project(raw, ready).listed == SurvivingPackListed::Pending, "pending constructor receipt is held without another entry");
    raw = baseline; raw.unclassifiableNodeCount = 1;
    Check(project(raw, ready).listed == SurvivingPackListed::Unclassifiable, "unclassifiable raw row prevents selected absence inference");
    raw = baseline; raw.listedComplete = false; raw.conflictCount = 1;
    Check(classify(project(raw, ready)) == SurvivingPackClassification::Conflict,
          "late failed list sample retains an already observed conflict over independent five-ready membership");
    raw = baseline; raw.listedComplete = false; raw.pendingNodeCount = 1;
    Check(project(raw, ready).listed == SurvivingPackListed::Pending,
          "partial raw list cannot erase its retained pending actor");
    raw = baseline; raw.listedComplete = false; raw.unclassifiableNodeCount = 1;
    Check(project(raw, ready).listed == SurvivingPackListed::Unclassifiable,
          "partial raw list cannot erase its retained unclassifiable actor");
    raw = baseline; raw.listedComplete = false;
    Check(classify(project(raw, ready)) == SurvivingPackClassification::FullSetAlreadyPresent,
          "benign unavailable raw list retains independently ready set without claiming complete occupancy");
    raw = baseline; raw.cache[1].room += 1;
    Check(!project(raw, ready).cacheAvailable, "mismatched cache room bookend cannot qualify the current-room bucket");
    raw = baseline; raw.cache[0].ids[5] = 65535; ++raw.cacheEntryCount;
    Check(project(raw, ready).cacheConflict, "unresolved room cache ID conflicts against whole checked native catalog");
    raw = baseline; raw.cacheDuplicateIds = true;
    Check(project(raw, ready).cacheConflict, "native duplicate cache receipt remains a conflict");
    raw = baseline; raw.cacheAvailable = false; raw.cache[0].bytesRead = true; raw.cacheDuplicateIds = true;
    Check(project(raw, ready).cacheConflict && !project(raw, ready).cacheAvailable,
          "failed second cache read retains first full current-room duplicate without positive qualification");
    raw = baseline; raw.cacheAvailable = false;
    Check(!project(raw, ready).cacheConflict && !project(raw, ready).cacheAvailable,
          "benign unreadable cache remains missing without inventing a negative finding");
    raw = baseline; raw.nodes.clear(); raw.selectedReferenceCount = 0; raw.activeReferenceCounts = {};
    raw.cache[0].ids = {}; raw.cacheSelectedCounts = {}; raw.cacheEntryCount = 0;
    raw.noSelectedReferencesAtSamples = true;
    const std::vector<PackReadyReference> empty;
    auto zero = project(raw, empty); zero.readyMembers = {};
    Check(classify(zero) == SurvivingPackClassification::ListedEmptyUnqualified,
          "complete zero raw sample joins independently complete empty ready census without creation authority");
    raw.cache[1].room += 1; zero = project(raw, empty); zero.readyMembers = {};
    Check(classify(zero) == SurvivingPackClassification::Unavailable,
          "wrong-room zero cache cannot supply empty-baseline classification");
    Check(deathCalls == 0 && nativeCalls == 0 && Read<int>(status) == 20,
          "root projection controls perform no native call or HP store");
}

void TestSurvivingPackIntegration() {
    namespace w = kh2coop::inject::recoverywarp;
    namespace p = kh2coop::inject::progresssync;
    Reset(); SeedPreparationShadows(17);
    const auto plan = NativePlan(); ReceiveResyncPlan(plan);
    ResyncSnapshot snapshot; ProducerWorldContext context;
    const bool captured = CaptureHostResync(400, snapshot, context);
    Check(captured && snapshot.livingCount == 5 && snapshot.enemies.size() == 5,
          "preparation integration source is a real five-row capture over owned native record bytes");
    if (!captured) return;
    const auto begin = NativeBegin(plan, snapshot, context.hostSourceSerial);
    auto stage = [&](bool enabled = true) {
        ResetWorld(); SeedPreparationShadows(20); g_survivingPackEnabled = enabled;
        StageNativeBootstrap(plan, encodeNativeResyncSnapshot(begin, snapshot));
        if (w::g_hostQueued) { w::IssueHostTransition(); w::CompleteLoad(); }
        g_seenLoad = w::LoadSerial(); g_seenTransition = w::TransitionSerial();
        g_host.arrived = true; TrackSpawns(CaptureNativeCensus());
    };
    auto inbox = [&](const auto& message, std::uint64_t source) {
        g_bridge.incoming.push_back(HostEnvelope(encode(message), source)); ReceiveWorldPackets();
    };
    auto unchangedNative = [&] {
        for (std::size_t i = 0; i < 5; ++i)
            if (Read<int>(status + i * 0x100) != 20) return false;
        return deathCalls == 0 && nativeCalls == 0;
    };
    stage();
    Check(PackPreparationActive() && !g_survivingPack.HasOwnAttempt(),
          "opt-in binds immutable five-record intent without an own native attempt");
    inbox(EnemyHp {9, {{1, 17, 20}}, snapshot.hpSequence + 1}, begin.snapshotCut + 1);
    Check(PackPreparationActive() && !g_survivingPack.Terminal(),
          "actual parsed equal HP heartbeat preserves original-cut preparation");
    ClientFrame(CaptureNativeCensus());
    Check(g_survivingPack.Classification() == SurvivingPackClassification::Unavailable &&
          (g_survivingPack.MissingFacts() & PackMissingMutationTicket) &&
          Read<int>(status) == 17 && !g_survivingPack.HasOwnAttempt(),
          "uninstalled native fence stays unavailable while independent exact HP reconciliation remains usable");

    stage();
    inbox(EnemyHp {9, {{1, 16, 20}}, snapshot.hpSequence + 1}, begin.snapshotCut + 1);
    Check(g_survivingPack.Terminal() && g_nativeResync->finished &&
          g_resyncWriteFence == ResyncWriteFence::Failed && unchangedNative(),
          "actual postcut positive HP change fails preparation before any native HP write");
    ClientFrame(CaptureNativeCensus());
    inbox(EnemyHp {9, {{1, 17, 20}}, snapshot.hpSequence + 2}, begin.snapshotCut + 2);
    Check(g_survivingPack.Terminal() && g_resyncWriteFence == ResyncWriteFence::Failed && unchangedNative(),
          "later HP restoration cannot revive terminal original-cut preparation");
    const auto ack = DecodeNativePacket<ResyncAck>(g_resyncOutput);
    Check(ack.status == ResyncAckStatus::Unavailable && ack.checksMask == 0,
          "changed source emits unavailable rather than forging old fingerprint convergence");
    g_nativeResync.reset();
    Check(g_survivingPack.Terminal() && g_survivingPack.Intent().has_value(),
          "transport cleanup preserves the reducer tombstone and immutable intent");

    stage();
    inbox(EnemyHp {9, {{1, 16, 20}}, snapshot.hpSequence + 1}, begin.snapshotCut);
    Check(!g_survivingPack.Terminal() && g_host.enemies[1].hp == 17,
          "actual envelope admission discards pre-cut changed HP before reducer cancellation");
    inbox(EnemyDeath {9, 1}, begin.snapshotCut + 2);
    inbox(EnemyHp {9, {{1, 17, 20}}, snapshot.hpSequence + 2}, begin.snapshotCut + 3);
    Check(g_survivingPack.Terminal() && g_resyncWriteFence == ResyncWriteFence::Failed && unchangedNative(),
          "actual death then newer HP retains failed living-only preparation without lethal work");

    stage();
    EnemyManifest identical {9, true, {}};
    for (const auto& row : snapshot.enemies) identical.entries.push_back(row.identity);
    inbox(identical, begin.snapshotCut + 1);
    Check(!g_survivingPack.Terminal(), "identical parsed full-manifest replay is not material change");
    identical.entries[0].objectId = 309;
    inbox(identical, begin.snapshotCut + 2);
    Check(g_survivingPack.Terminal() && unchangedNative(),
          "actual changed manifest member cancels before native binding or creation");

    stage();
    inbox(FullProgress(0), begin.snapshotCut + 1);
    Check(!g_survivingPack.Terminal(), "version-only full progress replay preserves identical desired bytes");
    inbox(FullProgress(0x44), begin.snapshotCut + 2);
    Check(g_survivingPack.Terminal() && p::g_desired[0x10] == 0x44 && unchangedNative(),
          "accepted changed desired progress cancels without waiting for native SAVE application");

    stage();
    inbox(EventHold {9, true, snapshot.hold.eventProgram}, begin.snapshotCut + 1);
    Check(g_survivingPack.Terminal() && unchangedNative(), "parsed postcut event hold cancels the living-only intent");

    stage(false);
    inbox(EnemyHp {9, {{1, 16, 20}}, snapshot.hpSequence + 1}, begin.snapshotCut + 1);
    ClientFrame(CaptureNativeCensus());
    Check(!g_survivingPack.Intent() && Read<int>(status) == 16 && !g_nativeResync->finished,
          "default-disabled preparation preserves existing exact resync HP behavior");
}

void TestNativeRecordWriteGuards() {
    namespace w = kh2coop::inject::recoverywarp;
    Reset(); const auto plan = NativePlan(); ReceiveResyncPlan(plan);
    Put(status, std::int32_t{777});
    ResyncSnapshot snapshot; ProducerWorldContext context;
    const bool captured = CaptureHostResync(700, snapshot, context);
    Check(captured && snapshot.enemies.size() == 1 && snapshot.enemies[0].maxHp == 1000,
          "write guard fixture captures real qualified native maximum before the source cut");
    if (!captured || snapshot.enemies.size() != 1) return;
    const auto begin = NativeBegin(plan, snapshot, context.hostSourceSerial);
    const auto packet = encodeNativeResyncSnapshot(begin, snapshot);
    auto stage = [&] {
        ResetWorld(); StageNativeBootstrap(plan, packet);
        w::IssueHostTransition(); w::CompleteLoad(); OnFrameStart(710);
    };
    auto probe = [&] {
        recordDeathFault = 0; recordDeathDelta = 0;
        recordDeathArguments = recordDeathProtected = false;
        g_applyStatDelta = RecordGuardDeath;
    };

    stage();
    EnemyHp excessive {snapshot.room.epoch, {{1, 2000, 2000}}, g_hostHpSequence + 1};
    const auto excessiveSequence = excessive.sequence;
    g_bridge.SendToDll(HostEnvelope(encode(excessive), begin.snapshotCut + 1)); ReceiveWorldPackets();
    Check(g_hostHpSequence == excessiveSequence && g_host.enemies[1].hp == 2000,
          "actual post-cut valid EnemyHp admits hp2000 max2000 into mutable host cache");
    const auto beforeExcessive = Read<int>(status);
    OnFrameStart(711);
    Check(beforeExcessive == 777 && Read<int>(status) == beforeExcessive && deathCalls == 0 &&
          g_resyncWriteFence != ResyncWriteFence::None && g_inst.spawns[0].netId < 0,
          "qualified source and local max1000 reject post-cut desired hp2000 without any native write");

    NativeEnemy sampled;
    std::vector<ResyncActorBinding> bindings;
    std::vector<NativeRecordContentDefinition> definitions;
    auto qualify = [&] {
        stage(); probe();
        const auto census = CaptureNativeCensus();
        const bool complete = g_resyncRecordAuthority &&
            CollectResyncBindings(g_resyncRecordAuthority->snapshot, g_resyncRecordAuthority->context,
                                  census, bindings, definitions);
        Check(complete && bindings.size() == 1 && census.enemies.size() == 1,
              "leaf adversary begins with actual complete whole-population collected binding");
        if (!complete || bindings.size() != 1 || census.enemies.size() != 1) return false;
        sampled = census.enemies[0];
        return true;
    };
    if (!qualify()) return;
    Put(status, std::int32_t{0});
    Check(!WriteNativeHp(sampled, 777, g_bridge.generation, &bindings[0].write) &&
          Read<int>(status) == 0 && !ApplyNativeDeath(sampled, sampled.hp, g_bridge.generation, &bindings[0].write) &&
          deathCalls == 0,
          "last native leaves reject post-binding HP0 without resurrection or stale lethal invocation");

    if (!qualify()) return;
    Put(status + 4, std::int32_t{999});
    Check(!WriteNativeHp(sampled, 777, g_bridge.generation, &bindings[0].write) &&
          !ApplyNativeDeath(sampled, sampled.hp, g_bridge.generation, &bindings[0].write) &&
          Read<int>(status) == 777 && deathCalls == 0,
          "last native leaves reject maximum-HP drift after complete binding");

    if (!qualify()) return;
    Put(object + offsets::objentry::TYPE_FLAGS, std::uint8_t{offsets::objentry::TYPE_BOSS});
    Check(!WriteNativeHp(sampled, 777, g_bridge.generation, &bindings[0].write) &&
          !ApplyNativeDeath(sampled, sampled.hp, g_bridge.generation, &bindings[0].write) &&
          Read<int>(status) == 777 && deathCalls == 0,
          "last native leaves reject changed combat object type even with identical pointers and object ID");

    if (!qualify()) return;
    Check(!WriteNativeHp(sampled, 2000, g_bridge.generation, &bindings[0].write) &&
          !WriteNativeHp(sampled, 0, g_bridge.generation, &bindings[0].write) &&
          !WriteNativeHp(sampled, -1, g_bridge.generation, &bindings[0].write) && Read<int>(status) == 777,
          "last HP leaf independently rejects over-max zero and negative desired HP");

    if (!qualify()) return;
    Put(status, std::int32_t{600});
    Check(!ApplyNativeDeath(sampled, sampled.hp, g_bridge.generation, &bindings[0].write) && deathCalls == 0 &&
          Read<int>(status) == 600,
          "last lethal leaf rejects native HP drift instead of applying the stale sampled delta");
    if (!qualify()) return;
    Check(!ApplyNativeDeath(sampled, sampled.hp - 1, g_bridge.generation, &bindings[0].write) && deathCalls == 0,
          "last lethal leaf rejects a delta argument different from the qualified sample HP");
    Check(ApplyNativeDeath(sampled, sampled.hp, g_bridge.generation, &bindings[0].write) &&
          deathCalls == 1 && recordDeathArguments && recordDeathDelta == -sampled.hp && Read<int>(status) == 0,
          "positive lethal control invokes the actual native leaf with exact sampled HP and original stat arguments");

    // The final context call is made after native content comparisons. Existing
    // transport instrumentation changes the context during that repeated read.
    if (!qualify()) return;
    // WorldSessionGeneration samples twice: leaf entry reads 1/2, initial
    // record context reads 3/4, and the repeated context begins at read 5.
    const auto qualifiedGeneration = g_bridge.generation;
    g_bridge.generationReads = 0; g_bridge.replaceGenerationOnRead = 5;
    Check(!WriteNativeHp(sampled, 600, qualifiedGeneration, &bindings[0].write) && Read<int>(status) == 777 &&
          g_bridge.generation == qualifiedGeneration + 1 && g_bridge.generationReads >= 5,
          "HP leaf rejects generation retirement at its repeated final context check");

    for (unsigned fault : {1u, 2u}) {
        stage(); probe(); recordDeathFault = fault; g_host.enemies[1].dead = true;
        const bool applied = ClientFrame(CaptureNativeCensus());
        DWORD ignored = 0;
        const bool restored = !recordDeathProtected || VirtualProtect(reinterpret_cast<void*>(status), 1,
            recordDeathOldProtection, &ignored) != 0;
        Check(!applied && deathCalls == 1 && recordDeathArguments && recordDeathDelta == -777 && restored &&
              (fault != 1 || recordDeathProtected) && g_resyncWriteFence == ResyncWriteFence::Failed,
              fault == 1 ? "post-lethal status read fault leaves a Failed write fence after the one actual call" :
                           "post-lethal active-list census fault leaves a Failed write fence after the one actual call");
        if (!restored) return;
        Put(enemy + offsets::actor::LINKED_NEXT_HANDLE, std::uint32_t{0});
        Put(status, std::int32_t{777}); // readable bytes returning never grant retry
        ClientFrame(CaptureNativeCensus());
        Check(deathCalls == 1 && Read<int>(status) == 777 && g_resyncWriteFence == ResyncWriteFence::Failed,
              "restored post-lethal bytes cannot retry a possibly applied native death");
    }

    // Two real active actors and distinct record indices expose prefix writes:
    // the first desired HP is valid, while only the second exceeds the cut max.
    auto secondActor = [&] {
        const auto actor2 = image + 0x24000, object2 = image + 0x31000, status2 = image + 0x41000;
        SeedRecordCatalog({2});
        Put(enemy + offsets::actor::LINKED_NEXT_HANDLE,
            static_cast<std::uint32_t>(actor2 & offsets::active_entity_list::HANDLE_LOW_MASK));
        Put(image + offsets::active_entity_list::TAIL, actor2);
        Put(actor2 + offsets::actor::OBJENTRY_PTR, object2);
        Put(object2 + offsets::objentry::TYPE_FLAGS, std::uint8_t{offsets::objentry::TYPE_MOB});
        Put(object2 + offsets::objentry::OBJECT_ID, std::uint32_t{309});
        Put(object2 + offsets::objentry::NAME, char{'M'});
        Put(actor2 + ACTOR_STATUS, status2); Put(status2, std::int32_t{1000}); Put(status2 + 4, std::int32_t{1000});
        Put(actor2 + 0x9E8, RecordController()); Put(actor2 + 0x9F0, RecordData(0, 1));
        if (!g_inst.byActor.count(actor2)) {
            auto spawn = g_inst.spawns[0]; spawn.actor = actor2; spawn.objentry = object2; spawn.status = status2;
            spawn.spawnIndex = 1; spawn.netId = 2;
            g_inst.byActor[actor2] = g_inst.spawns.size(); g_inst.spawns.push_back(spawn);
        }
        return status2;
    };
    Reset(); secondActor(); ReceiveResyncPlan(plan);
    ResyncSnapshot pair; ProducerWorldContext pairContext;
    const bool pairCaptured = CaptureHostResync(720, pair, pairContext);
    Check(pairCaptured && pair.enemies.size() == 2 && pair.enemies[0].record.recordIndex != pair.enemies[1].record.recordIndex,
          "whole-batch guard fixture captures two actual actors with unique full native record membership");
    if (!pairCaptured || pair.enemies.size() != 2) return;
    const auto pairBegin = NativeBegin(plan, pair, pairContext.hostSourceSerial);
    ResetWorld(); secondActor(); StageNativeBootstrap(plan, encodeNativeResyncSnapshot(pairBegin, pair));
    w::IssueHostTransition(); w::CompleteLoad(); OnFrameStart(721);
    Check(g_inst.spawns.size() == 2 && g_inst.spawns[0].netId == 1 && g_inst.spawns[1].netId == 2 &&
          g_resyncWriteFence == ResyncWriteFence::Exact,
          "two-actor batch starts with both exact record associations successfully bound");
    EnemyHp batch {pair.room.epoch, {{1, 700, 1000}, {2, 2000, 2000}}, g_hostHpSequence + 1};
    g_bridge.SendToDll(HostEnvelope(encode(batch), pairBegin.snapshotCut + 1)); ReceiveWorldPackets(); OnFrameStart(722);
    Check(g_host.enemies[1].hp == 700 && g_host.enemies[2].hp == 2000 &&
          Read<int>(status) == 1000 && Read<int>(image + 0x41000) == 1000 && deathCalls == 0 &&
          g_inst.spawns.size() == 2 && g_inst.spawns[0].netId < 0 && g_inst.spawns[1].netId < 0,
          "whole-population bounds reject invalid second desired HP before writing the valid first actor");

    ResetWorld(); secondActor(); StageNativeBootstrap(plan, encodeNativeResyncSnapshot(pairBegin, pair));
    w::IssueHostTransition(); w::CompleteLoad(); OnFrameStart(723);
    EnemyHp validBatch {pair.room.epoch, {{1, 700, 1000}, {2, 1000, 1000}}, g_hostHpSequence + 1};
    g_bridge.SendToDll(HostEnvelope(encode(validBatch), pairBegin.snapshotCut + 1)); ReceiveWorldPackets();
    Put(image + 0x41000, std::int32_t{2000});
    const auto overMaxCensus = CaptureNativeCensus();
    Check(overMaxCensus.state == CensusState::Complete && overMaxCensus.enemies.size() == 2 &&
          overMaxCensus.enemies[1].hp == 2000 && overMaxCensus.enemies[1].maxHp == 1000 &&
          g_host.enemies[1].hp == 700 && g_host.enemies[2].hp == 1000,
          "batch adversary retains complete native census with only second native HP above its maximum");
    Check(!ClientFrame(overMaxCensus) && Read<int>(status) == 1000 &&
          Read<int>(image + 0x41000) == 2000 && deathCalls == 0 &&
          g_inst.spawns[0].netId < 0 && g_inst.spawns[1].netId < 0,
          "whole-population native bounds reject invalid second actor before a valid first HP write");
}

}


namespace {
std::vector<std::string> constructionLines;
bool constructionLineOverflow = false;
void ConstructionCaptureLog(const char* format, ...) {
    char line[2048] {};
    va_list args; va_start(args, format);
    const int length = std::vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    if (length < 0 || static_cast<std::size_t>(length) >= sizeof(line)) constructionLineOverflow = true;
    constructionLines.emplace_back(line);
}
bool ConstructionHas(const std::string& text) {
    return std::any_of(constructionLines.begin(), constructionLines.end(),
        [&](const auto& line) { return line.find(text) != std::string::npos; });
}
std::string ConstructionField(const std::string& line, const std::string& name) {
    const auto at = line.find(" " + name + "=");
    if (at == std::string::npos) return {};
    const auto start = at + name.size() + 2;
    return line.substr(start, line.find(' ', start) - start);
}
template<std::size_t N> std::string ConstructionHex(const std::array<std::uint8_t, N>& bytes) {
    // Independent byte-to-text oracle, not the production TraceHex function.
    std::string result;
    for (auto byte : bytes) { char part[3] {}; std::snprintf(part, sizeof(part), "%02X", byte); result += part; }
    return result;
}
void TestConstructionSerialization() {
    using namespace kh2coop::inject::enemysync;
    namespace sc = kh2coop::inject::spawncontroller;
    const auto savedLog = g_log;
    const auto savedCounters = g_constructionLog;
    const auto legacyDrained = g_traceDrained;
    g_log = ConstructionCaptureLog; g_constructionLog = {};
    constructionLines.clear(); constructionRows.clear(); constructionPopCalls = 0; constructionLineOverflow = false;
    sc::NativeConstructionLineage e;
    e.serial = UINT64_MAX; e.coverage = UINT64_MAX - 1; e.coverageAfter = 9; e.wrapperSequence = UINT64_MAX - 2;
    e.controller = UINTPTR_MAX; e.record = UINTPTR_MAX - 1; e.callerRva = UINTPTR_MAX - 2;
    e.callerRvaAvailable = true; e.depth = UINT32_MAX; e.threadId = UINT32_MAX;
    e.enclosingThreadSerial = 88; e.tickSequence = 99; e.dispatcherSequence = 100; e.scriptSequence = 101;
    e.wrapper = sc::TraceWrapper::Generated; e.captured = true; e.candidateThreadParent = true;
    e.ambiguous = true; e.unwound = true; e.droppedBefore = 3; e.droppedAfter = 5;
    for (unsigned phase = 0; phase < 2; ++phase) {
        auto& a = e.samples[phase];
        for (unsigned i = 0; i < 64; ++i) { a.controllerBytes[i] = static_cast<std::uint8_t>(i + phase); a.recordBytes[i] = static_cast<std::uint8_t>(255 - i - phase); }
        for (unsigned i = 0; i < 44; ++i) a.headerBytes[i] = static_cast<std::uint8_t>(i * 3 + phase);
        for (unsigned r = 0; r < 5; ++r) for (unsigned i = 0; i < 64; ++i) a.records[r][i] = static_cast<std::uint8_t>(r * 64 + i + phase);
        for (unsigned r = 0; r < 64; ++r) for (unsigned i = 0; i < 16; ++i) a.table[r][i] = static_cast<std::uint8_t>(r * 16 + i + phase);
        a.controllerRead = true; a.headerRead = false; a.recordRead = true;
        a.countBeforeRead = true; a.countBefore = -1; a.countAfter = 65;
        a.tableReadMask = 0x8000000000000001ULL; a.recordReadMask = 17;
        a.nativeType = 255; a.declaredRecords = UINT16_MAX; a.recordIndex = UINT16_MAX;
        a.mutation.revision = UINT64_MAX; a.mutation.inFlight = UINT32_MAX; a.mutation.poisoned = true;
    }
    sc::TraceStats stats; stats.constructionRequested = true; stats.constructionConfigured = true;
    stats.constructionPublished = 12; stats.constructionDropped = 7; stats.constructionSerial = 14;
    constructionRows.push_back(e);
    DrainConstructionTrace(stats);
    Check(constructionLines.size() == 56 && !constructionLineOverflow, "Construction serialization fixed 55 rows plus summary fits 2048-byte logger boundary");
    Check(ConstructionHas("serial=18446744073709551615 coverage=18446744073709551614 wrapperSequence=18446744073709551613") &&
          ConstructionHas("controller=FFFFFFFFFFFFFFFF record=FFFFFFFFFFFFFFFE callerRva=FFFFFFFFFFFFFFFD"), "Construction full-width identity fields retained");
    Check(ConstructionHas("normalReturn=0 unwound=1") && ConstructionHas("ambiguous=1") && ConstructionHas("enclosingThreadSerial=88"),
          "Construction unwind and ambiguous parent evidence retained without success promotion");
    Check(ConstructionHas("countBefore=-1 countAfter=65 tableReadMask=8000000000000001 recordReadMask=17 tableComplete=0"),
          "Construction partial invalid counts and masks remain unavailable not empty");
    for (const auto& line : constructionLines) std::cout << "SERIALIZED: " << line << '\n';
    bool rawExact = true, recordsExact = true, tablesExact = true, joined = true;
    unsigned rawCount = 0, recordCount = 0, tableCount = 0;
    for (const auto& line : constructionLines) {
        if (line.find(" summary ") != std::string::npos) continue;
        joined = joined && ConstructionField(line, "serial") == std::to_string(e.serial) &&
            ConstructionField(line, "coverage") == std::to_string(e.coverage) && ConstructionField(line, "wrapperSequence") == std::to_string(e.wrapperSequence);
        const auto phaseText = ConstructionField(line, "phase");
        if (phaseText.empty()) continue;
        const auto phase = static_cast<unsigned>(std::stoul(phaseText));
        const auto& a = e.samples[phase];
        if (line.find(" raw ") != std::string::npos) {
            ++rawCount; rawExact = rawExact && ConstructionField(line, "controllerHex") == ConstructionHex(a.controllerBytes) &&
                ConstructionField(line, "recordHex") == ConstructionHex(a.recordBytes) && ConstructionField(line, "headerHex") == ConstructionHex(a.headerBytes);
        }
        if (line.find(" record ") != std::string::npos) {
            ++recordCount; const auto index = static_cast<unsigned>(std::stoul(ConstructionField(line, "index")));
            recordsExact = recordsExact && ConstructionField(line, "bytesHex") == ConstructionHex(a.records[index]) &&
                ConstructionField(line, "read") == std::to_string((a.recordReadMask >> index) & 1U);
        }
        if (line.find(" table ") != std::string::npos) {
            ++tableCount; const auto first = static_cast<unsigned>(std::stoul(ConstructionField(line, "first")));
            std::string expected; for (unsigned i = first; i < first + 4; ++i) expected += ConstructionHex(a.table[i]);
            tablesExact = tablesExact && ConstructionField(line, "bytesHex") == expected &&
                ConstructionField(line, "readMask") == std::to_string((a.tableReadMask >> first) & 15U);
        }
    }
    Check(rawExact && rawCount == 2, "Construction both full controller raw-record and unread header buffers round trip");
    Check(recordsExact && recordCount == 10, "Construction all ten full record buffers retain order and individual validity");
    Check(tablesExact && tableCount == 32, "Construction all 128 table slots round trip including unread storage and bit63");
    Check(joined && ConstructionHas("phases=2 recordsPerPhase=5 tableChunksPerPhase=16 rows=55"), "Construction every row uses exact terminal join and explicit expected extents");
    Check(ConstructionHas("mutationRevision=18446744073709551615 mutationInFlight=4294967295 mutationAvailable=0 mutationPoisoned=1 mutationCoverageComplete=0"),
          "Construction mutation ticket width and poison unavailable retained");
    Check(ConstructionHas("parentFiberAncestryUnproven=1 fiberContinuityProven=0 continuousModeProven=0 globalPendingExcluded=0 creatorExclusive=0 controllerIncarnationProven=0 atomic=0 creationAuthority=0"),
          "Construction no positive authority inferred from parent or byte samples");
    Check(ConstructionHas("producerPublished=12 producerDropped=7 consumed=1 serialized=1 suppressedBudget=0 suppressedNoLogger=0"), "Construction producer loss remains distinct from logger counters");
    constructionLines.clear();
    auto returned = e; returned.normalReturn = true; returned.unwound = false; returned.ambiguous = false;
    returned.sampledDefinitionStable = true; returned.sampledTableStable = true; returned.coverageStable = true;
    returned.samples[0].controllerRead = true; returned.samples[0].headerRead = true;
    returned.samples[0].fiveRecordLayout = true; returned.samples[0].tableComplete = true;
    returned.samples[0].countBefore = 1; returned.samples[0].countAfter = 1;
    constructionRows = {returned}; DrainConstructionTrace(stats);
    Check(ConstructionHas("normalReturn=1 unwound=0") && ConstructionHas("sampledDefinitionStable=1 sampledTableStable=1") &&
          ConstructionHas("creationAuthority=0"), "Construction normal terminal preserves sample flags without action authority");
    constructionLines.clear(); constructionRows = {sc::NativeConstructionLineage {}}; DrainConstructionTrace(stats);
    Check(ConstructionHas("controllerRead=0 recordRead=0 headerRead=0 countBeforeRead=0 countAfterRead=0 stampRead=0 countBefore=0 countAfter=0") &&
          ConstructionHas("tableReadMask=0000000000000000 recordReadMask=0 tableComplete=0") && ConstructionHas("normalReturn=0 unwound=0"),
          "Construction zero-filled unavailable terminal is not an observed empty successful return");
    constructionLines.clear(); constructionRows = {e, e, e}; constructionPopCalls = 0;
    DrainConstructionTrace(stats);
    Check(constructionRows.size() == 1 && constructionPopCalls == 2 && constructionLines.size() == 110,
          "Construction drain caps two complete terminals and leaves queued remainder");
    constructionLines.clear(); stats.constructionRequested = false; constructionPopCalls = 0;
    DrainConstructionTrace(stats);
    Check(constructionPopCalls == 0 && constructionLines.empty() && constructionRows.size() == 1,
          "Construction default disabled drain does not pop or serialize");
    stats.constructionRequested = true; g_log = nullptr;
    DrainConstructionTrace(stats);
    Check(g_constructionLog.suppressedNoLogger == 1 && constructionRows.empty(), "Construction missing logger counts lost terminal separately");
    g_log = ConstructionCaptureLog; g_constructionLog.serialized = ConstructionLogCap; g_constructionLog.summaryMs = 0;
    constructionRows = {e, e}; DrainConstructionTrace(stats);
    Check(g_constructionLog.suppressedBudget == 2 && constructionLines.size() == 1 &&
          ConstructionHas("serialized=64 suppressedBudget=2 suppressedNoLogger=1"), "Construction exhausted log budget drops whole terminals with explicit separate counts");
    constructionLines.clear(); g_constructionLog.summaryMs = 0; DrainConstructionTrace(stats);
    Check(constructionLines.size() == 1 && ConstructionHas("emptyMeansAbsent=0 parentCompletenessProven=0 creationAuthority=0") &&
          !ConstructionHas(" terminal "), "Construction missing parents and empty queue never synthesize terminal or absence");
    constructionLines.clear(); sc::TraceState legacy; legacy.controllerAvailable = true; legacy.key = 42;
    LogTraceState("[spawntrace]", 123, "before", legacy);
    Check(constructionLines.size() == 1 && constructionLines[0].starts_with("[spawntrace] state seq=123 phase=before controllerAvailable=1") &&
          ConstructionHas("key=42") && g_traceDrained == legacyDrained, "Construction drain preserves legacy trace serialization and counters");
    constructionRows.clear(); constructionLines.clear(); g_log = savedLog; g_constructionLog = savedCounters;
}
}


namespace {
void TestResourceSerialization() {
    using namespace kh2coop::inject::enemysync;
    namespace rt = kh2coop::inject::resourcetrace;
    const auto savedLog = g_log; const auto savedCounters = g_resourceLog;
    const auto constructionCounters = g_constructionLog; const auto legacyDrained = g_traceDrained;
    g_log = ConstructionCaptureLog; g_resourceLog = {};
    constructionLines.clear(); constructionLineOverflow = false; resourceRows.clear();
    resourceStats = {}; resourcePopCalls = 0; resourceStatsCalls = 0;
    DrainResourceTrace();
    Check(resourceStatsCalls == 1 && resourcePopCalls == 0 && constructionLines.empty(),
        "Resource default disabled statistics do not pop or log");
    rt::ResourceObservation e;
    e.invocation = UINT64_MAX; e.parentCallback = UINT64_MAX - 1; e.generation = UINT64_MAX - 2; e.outerBoundary = UINT64_MAX - 3;
    e.parent.serial = UINT64_MAX - 4; e.parent.coverage = UINT64_MAX - 5; e.parent.wrapperSequence = UINT64_MAX - 6;
    e.parent.controller = UINTPTR_MAX; e.parent.record = UINTPTR_MAX - 1; e.parent.threadId = UINT32_MAX; e.parent.recordIndex = UINT16_MAX;
    e.parent.available = true; e.parent.recordIndexAvailable = true; e.parent.recordBytesAvailable = false;
    for (unsigned i = 0; i < 64; ++i) e.parent.recordBytes[i] = static_cast<std::uint8_t>(255 - i);
    e.actualEnteredTarget = UINTPTR_MAX - 2; e.caller = UINTPTR_MAX - 3;
    e.package = UINTPTR_MAX - 4; e.filename = UINTPTR_MAX - 5; e.optionalRoot = UINTPTR_MAX - 6;
    e.sampledVtable = UINTPTR_MAX - 7; e.sampledSlot0 = UINTPTR_MAX - 8; e.sampledCurrentPackage = UINTPTR_MAX - 9;
    e.sampledCount = INT32_MIN; e.sampledIndex = INT32_MAX; e.sampledMode = UINT32_MAX; e.threadId = UINT32_MAX; e.depth = UINT32_MAX;
    e.rawRax = 0xABCDEF0123456780ULL; e.al = 128; e.callerKind = rt::Caller::Recursive; e.boundaryDropped = UINT64_MAX;
    e.normalReturn = true; e.outerUnwound = true; e.installationIdentityVerified = true;
    e.vtableRead = true; e.slotRead = false; e.countRead = true; e.indexRead = false; e.currentPackageRead = true; e.modeRead = false;
    for (unsigned i = 0; i < 256; ++i) { e.filenameSample.bytes[i] = static_cast<char>(i); e.rootSample.bytes[i] = static_cast<char>(255 - i); }
    e.filenameSample.bytes[0] = 'A'; e.filenameSample.bytes[1] = '\n'; e.filenameSample.bytes[2] = '%'; e.filenameSample.bytes[3] = 0;
    e.filenameSample.length = 3; e.filenameSample.pointerNonNull = true; e.filenameSample.readable = true; e.filenameSample.terminated = true;
    e.rootSample.length = 7; e.rootSample.pointerNonNull = true;
    resourceStats.status = rt::InstallStatus::Ready; resourceStats.recording = true; resourceStats.resourcesMayBeReferenced = true;
    resourceStats.modulePinned = true; resourceStats.installationIdentityVerified = true; resourceStats.generation = 42;
    resourceStats.entered = 501; resourceStats.returned = 498; resourceStats.unwound = 3; resourceStats.dropped = 13;
    resourceStats.unparented = 17; resourceStats.foreign = 19; resourceStats.published = 480;
    resourceRows.push_back(e); DrainResourceTrace();
    Check(constructionLines.size() == 8 && !constructionLineOverflow, "Resource seven full receipt rows plus summary fit 2048-byte logger");
    for (const auto& line : constructionLines) std::cout << "RESOURCE-SERIALIZED: " << line << '\n';
    bool allJoined = true;
    for (const auto& line : constructionLines) if (line.find(" summary ") == std::string::npos) {
        allJoined = allJoined && ConstructionField(line,"invocation") == std::to_string(e.invocation) &&
            ConstructionField(line,"parentCallback") == std::to_string(e.parentCallback) &&
            ConstructionField(line,"generation") == std::to_string(e.generation) &&
            ConstructionField(line,"outerBoundary") == std::to_string(e.outerBoundary) &&
            ConstructionField(line,"parentSerial") == std::to_string(e.parent.serial) &&
            ConstructionField(line,"parentCoverage") == std::to_string(e.parent.coverage) &&
            ConstructionField(line,"wrapperSequence") == std::to_string(e.parent.wrapperSequence);
    }
    Check(allJoined, "Resource every child row retains seven full-width callback and immutable parent join IDs");
    Check(ConstructionHas("actualEnteredTarget=FFFFFFFFFFFFFFFD caller=FFFFFFFFFFFFFFFC callerKind=2 package=FFFFFFFFFFFFFFFB filename=FFFFFFFFFFFFFFFA optionalRoot=FFFFFFFFFFFFFFF9") &&
        ConstructionHas("threadId=4294967295 depth=4294967295"), "Resource actual target caller classification and three pointer arguments remain distinct");
    Check(ConstructionHas("rawRax=ABCDEF0123456780 al=128 normalReturn=1 unwound=0 outerNormalReturn=0 outerUnwound=1 installationIdentityVerified=1 boundaryDropped=18446744073709551615"),
        "Resource opaque full RAX and AL preserve native return versus outer unwind");
    Check(ConstructionHas("controller=FFFFFFFFFFFFFFFF record=FFFFFFFFFFFFFFFE threadId=4294967295 recordIndex=65535 available=1 recordIndexAvailable=1 recordBytesAvailable=0 recordHex=" + ConstructionHex(e.parent.recordBytes)),
        "Resource actual parent C R index and unavailable raw64 storage round trip");
    Check(ConstructionHas("sampledVtable=FFFFFFFFFFFFFFF8 sampledSlot0=FFFFFFFFFFFFFFF7 sampledCurrentPackage=FFFFFFFFFFFFFFF6 sampledCount=-2147483648 sampledIndex=2147483647 sampledMode=4294967295") &&
        ConstructionHas("vtableRead=1 slotRead=0 countRead=1 indexRead=0 currentPackageRead=1 modeRead=0"),
        "Resource sampled pointer count index mode values preserve independent read validity");
    Check(ConstructionHas("dispatchOperandObserved=0 completeRouting=0 fiberContinuityProven=0 continuousModeProven=0 globalPendingExcluded=0 lifetimeProven=0 creatorExclusive=0 atomic=0 creationAuthority=0"),
        "Resource sampled slot and thread parent confer no operand routing fiber pending or creation proof");
    bool stringsExact = true; unsigned stringRows = 0;
    for (const auto& line : constructionLines) if (line.find(" string ") != std::string::npos) {
        ++stringRows; const auto index = std::stoul(ConstructionField(line,"index"));
        const auto& sample = index == 0 ? e.filenameSample : e.rootSample;
        std::array<std::uint8_t,256> bytes {};
        for (unsigned i = 0; i < 256; ++i) bytes[i] = static_cast<std::uint8_t>(sample.bytes[i]);
        stringsExact = stringsExact && ConstructionField(line,"bytesHex") == ConstructionHex(bytes) && ConstructionField(line,"bytesHex").size() == 512;
    }
    Check(stringsExact && stringRows == 2 && ConstructionHas("index=0 capacity=256 length=3 pointerNonNull=1 readable=1 terminated=1 truncated=0 bytesHex=410A2500"),
        "Resource all 256 string bytes preserve embedded NUL newline percent and trailing storage safely as hex");
    Check(ConstructionHas("index=1 capacity=256 length=7 pointerNonNull=1 readable=0 terminated=0 truncated=0"),
        "Resource unreadable partial string retains length and bytes without successful read inference");
    Check(ConstructionHas("rows=7 stringSamples=2 stringBytes=256 parentCompletenessProven=0"), "Resource explicit row extents cannot certify missing construction terminal");
    Check(ConstructionHas("producerEntered=501 producerReturned=498 producerUnwound=3 producerDropped=13 producerUnparented=17 producerForeign=19 producerPublished=480 consumed=1 logged=1 suppressedNoLogger=0 suppressedBudget=0"),
        "Resource producer counters and consumer logging losses remain separate");
    Check(ConstructionHas("status=1 generation=42 recording=1 resourcesMayBeReferenced=1 modulePinned=1 installationIdentityVerified=1"),
        "Resource installation identity recording and retained resource state are explicit");
    constructionLines.clear(); auto bounded = e;
    bounded.filenameSample.bytes.fill('x'); bounded.filenameSample.length = 256; bounded.filenameSample.terminated = false; bounded.filenameSample.truncated = true;
    bounded.rootSample.bytes.fill('y'); bounded.rootSample.bytes[255] = 0; bounded.rootSample.length = 255;
    bounded.rootSample.readable = true; bounded.rootSample.terminated = true;
    resourceRows = {bounded}; DrainResourceTrace();
    Check(ConstructionHas("index=0 capacity=256 length=256 pointerNonNull=1 readable=1 terminated=0 truncated=1") &&
        ConstructionHas("index=1 capacity=256 length=255 pointerNonNull=1 readable=1 terminated=1 truncated=0") && !constructionLineOverflow,
        "Resource 256-byte truncation and last-byte NUL remain different bounded outcomes");
    constructionLines.clear(); bounded.filenameSample.length = UINT16_MAX; bounded.normalReturn = false; bounded.unwound = true;
    bounded.parent.available = false; bounded.parent.recordIndexAvailable = false; bounded.rootSample = {};
    resourceRows = {bounded}; DrainResourceTrace();
    Check(ConstructionHas("length=65535") && ConstructionHas("normalReturn=0 unwound=1") &&
        ConstructionHas("available=0 recordIndexAvailable=0 recordBytesAvailable=0") && !constructionLineOverflow,
        "Resource malformed length never drives a memory extent and unwind cannot imply valid RAX or parent");
    Check(ConstructionHas("index=1 capacity=256 length=0 pointerNonNull=0 readable=0 terminated=0 truncated=0"),
        "Resource null string is explicitly unavailable rather than a readable empty string");
    constructionLines.clear(); resourceRows.assign(9,e); resourcePopCalls = 0;
    DrainResourceTrace();
    Check(resourceRows.size() == 1 && resourcePopCalls == 8 && constructionLines.size() == 56,
        "Resource drain caps eight complete receipts and retains queued duplicate identities without deduplication");
    constructionLines.clear(); g_log = nullptr; DrainResourceTrace();
    Check(resourceRows.empty() && g_resourceLog.suppressedNoLogger == 1, "Resource absent logger consumes with independent no-logger loss");
    g_log = ConstructionCaptureLog; g_resourceLog.logged = ResourceLogCap - 1;
    resourceRows = {e,e}; DrainResourceTrace();
    Check(g_resourceLog.logged == ResourceLogCap && g_resourceLog.suppressedBudget == 1 && constructionLines.size() == 7,
        "Resource last budget slot logs a whole receipt then suppresses the next whole receipt");
    constructionLines.clear(); resourceRows.clear(); resourceStats.status = rt::InstallStatus::EnableFailedRetained;
    resourceStats.recording = false; ++resourceStats.generation;
    // Actual deferred entry must not hide stats-only failures when legacy trace
    // and census are unavailable; the resource Pop/stats boundary alone is mocked.
    DrainPendingSpawnTrace(false);
    Check(ConstructionHas("status=9 generation=43 recording=0 resourcesMayBeReferenced=1 modulePinned=1") &&
        ConstructionHas("logged=512 suppressedNoLogger=1 suppressedBudget=1") && !ConstructionHas(" terminal "),
        "Resource stats-only retained installation failure persists outside legacy trace early return");
    constructionLines.clear(); resourceStats.status = rt::InstallStatus::Retired;
    DrainResourceTrace();
    Check(ConstructionHas("status=10") && ConstructionHas("emptyMeansAbsent=0 parentCompletenessProven=0 creationAuthority=0"),
        "Resource retirement summary without children does not assert routing or parent completeness");
    Check(g_constructionLog.consumed == constructionCounters.consumed && g_constructionLog.serialized == constructionCounters.serialized &&
        g_traceDrained == legacyDrained, "Resource drain leaves construction and legacy event counters unchanged");
    resourceStats = {}; resourceRows.clear(); constructionLines.clear(); g_log = savedLog; g_resourceLog = savedCounters;
}
}

int main() try {
    std::cout << std::unitbuf;
    image = reinterpret_cast<uintptr_t>(VirtualAlloc(nullptr, 0x3000000,
                                                    MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    if (!image) return 2;
    // Production registration is one-way; exercise these existing controls
    // before the first registration instead of resetting a test-only stub.
    Reset(true, false);
    Check(!RecordLocalPlayerEnemyHit(LocalHit()), "publisher rejects unregistered thread");
    Check(CapturePuppetAuthority().mode == PuppetAuthorityMode::Unavailable,
          "unregistered caller cannot read mutable puppet authority");
    Check(!CurrentPuppetActor(player, transition, load),
          "unregistered caller cannot sample native puppet membership");
    Reset();
    Check(CaptureNativeCensus().state == CensusState::Complete,
          "production census accepts owned linked actors and metadata");

    QueueTestClaim(Claim()); QueueTestClaim(Claim());
    Check(g_hitCount == 1, "duplicate queued sequence is rejected");
    Check(Process() && nativeCalls == 1 && *reinterpret_cast<int*>(status) == 993,
          "one accepted claim invokes original callback exactly once with genuine arguments");
    QueueTestClaim(Claim()); Process();
    Check(nativeCalls == 1 && g_hitCount == 0, "consumed duplicate never invokes original again");

    Reset(); QueueTestClaim(Claim(3)); QueueTestClaim(Claim(2)); Process();
    Check(nativeCalls == 1 && g_claimSequences[1].consumed == 3,
          "reordered older sequence cannot apply behind a newer consumed claim");
    g_epoch = 10; auto nextRoom = Claim(3); nextRoom.epoch = 10;
    QueueTestClaim(nextRoom); Process();
    Check(nativeCalls == 1, "sequence high-water mark survives room epoch changes");

    Reset(); QueueTestClaim(Claim()); g_bridge.connections[1] = 201; Process();
    Check(nativeCalls == 0, "queued claim from retired connection cannot apply");
    auto reconnected = Claim(); reconnected.requesterConnectionId = 201;
    QueueTestClaim(reconnected); Process();
    Check(nativeCalls == 1, "new connection may start sequence one without replaying retired work");

    Reset(); auto wrong = Claim(); wrong.epoch = 8; QueueTestClaim(wrong); Process();
    Check(nativeCalls == 0 && g_hitCount == 0, "wrong epoch rejected before native invocation");
    Reset(); QueueTestClaim(Claim()); ++g_bridge.generation; Process();
    Check(nativeCalls == 0 && g_hitCount == 0, "generation change invalidates queued work");
    Reset(); QueueTestClaim(Claim()); ++load; Process();
    Check(nativeCalls == 0, "native load change rejects queued work");
    Reset(); QueueTestClaim(Claim()); ++transition; Process();
    Check(nativeCalls == 0, "native transition change rejects queued work");
    Reset(); wrong = Claim(); wrong.objectId = 311; QueueTestClaim(wrong); Process();
    Check(nativeCalls == 0, "wrong object ID cannot select bound actor");
    Reset(); QueueTestClaim(Claim()); Put(object + offsets::objentry::TYPE_FLAGS, std::uint8_t {0}); Process();
    Check(nativeCalls == 0, "noncombat type cannot receive a claim");
    Reset(); QueueTestClaim(Claim()); Put(enemy + ACTOR_STATUS, status + 0x100); Process();
    Check(nativeCalls == 0, "reused actor with changed status cannot use old binding");
    Reset(); QueueTestClaim(Claim()); g_pendingHits[g_hitHead].receivedMs = GetTickCount64() - 501; Process();
    Check(nativeCalls == 0 && g_hitCount == 0, "expired queued claim is dropped");
    Reset(); for (std::uint32_t seq = 1; seq <= 129; ++seq) QueueTestClaim(Claim(seq));
    Check(g_hitCount == HIT_PENDING_CAP, "pending queue enforces bounded capacity");
    Process();
    Check(nativeCalls == HIT_NATIVE_PER_FRAME && g_hitCount == HIT_PENDING_CAP - HIT_NATIVE_PER_FRAME,
          "native callback budget leaves bounded remainder for later frames");

    Reset(); QueueTestClaim(Claim()); callbackMode = 1;
    Check(!Process() && nativeCalls == 1 && g_censusInterrupted,
          "native SEH is consumed once and marks census interrupted");
    callbackMode = 0; QueueTestClaim(Claim()); Process();
    Check(nativeCalls == 1, "faulted native claim is never retried");
    Reset(); QueueTestClaim(Claim()); QueueTestClaim(Claim(2)); callbackMode = 7;
    Check(Process() && nestedRejected && nativeCalls == 2 && g_hitCount == 0 && !g_hostClaimProcessing,
          "native callback reentry cannot drain queue recursively and restores processing guard");

    Reset(); QueueTestClaim(Claim()); OnFrameStart(6);
    Check(nativeCalls == 1 && HasHp(1, 993) && !HasHp(1, 1000),
          "frame publishes only fresh post-callback HP");
    Check(HasHash(hashAppliedEnemies({{1, 309, 993}})) && !HasHash(hashAppliedEnemies({{1, 309, 1000}})),
          "decoded frame StateHash uses post-callback HP 993 and available progress checksum");
    Reset(); QueueTestClaim(Claim()); callbackMode = 1; OnFrameStart(6);
    Check(nativeCalls == 1 && g_bridge.outgoing.empty(),
          "faulted native call prevents frame HP, manifest and StateHash publication");
    Reset(); QueueTestClaim(Claim()); callbackMode = 2; OnFrameStart(6);
    Check(nativeCalls == 1 && g_bridge.outgoing.empty() && g_censusInterrupted,
          "callback load change prevents frame HP, manifest and StateHash publication");
    Reset(); QueueTestClaim(Claim()); callbackMode = 6; OnFrameStart(6);
    Check(nativeCalls == 1 && g_bridge.outgoing.empty() && g_censusInterrupted,
          "callback generation change prevents frame HP, manifest and StateHash publication");
    Reset(); QueueTestClaim(Claim()); callbackMode = 4; OnFrameStart(6);
    Check(nativeCalls == 1 && g_bridge.outgoing.empty() && g_censusInterrupted,
          "unavailable callback list recapture prevents HP, manifest and StateHash publication");
    Reset(); QueueTestClaim(Claim()); callbackMode = 3; OnFrameStart(6);
    Check(nativeCalls == 1 && !g_inst.spawns[0].present && !HasHp(1, 993) && !HasHp(1, 1000),
          "callback removal recaptures membership and never publishes removed actor HP");
    Check(HasHash(hashAppliedEnemies({})) && !HasHash(hashAppliedEnemies({{1, 309, 993}})),
          "decoded StateHash after removal covers empty native enemy population");
    Reset(); QueueTestClaim(Claim()); callbackMode = 5; OnFrameStart(6);
    Check(nativeCalls == 1 && g_inst.spawns.size() == 2 && g_inst.byActor[enemy] == 1 &&
          !HasHp(1, 993) && HasHp(2, 993),
          "callback metadata replacement creates a fresh binding before HP publication");
    Check(HasHash(hashAppliedEnemies({{2, 309, 993}})) && !HasHash(hashAppliedEnemies({{1, 309, 993}})),
          "decoded StateHash after replacement covers new binding and excludes old binding");

    Reset(true); AdmitClientClaimFixture();
    Check(RecordLocalPlayerEnemyHit(LocalHit()) && g_bridge.outgoing.size() == 1,
          "actual client publisher accepts current typed binding");
    const std::uint8_t* payload = nullptr; std::size_t size = 0;
    const auto& packet = g_bridge.outgoing.front();
    const auto type = decodePacketHeader(packet.data(), packet.size(), payload, size);
    ByteReader reader(payload, size); HitClaim published; read(reader, published);
    Check(type == PacketType::HitClaim && published.seq == 1 && published.epoch == 9 &&
          published.netId == 1 && published.objectId == 309 && published.requesterConnectionId == 101 &&
          published.damage == 7 && published.attackId == 65 && published.attackerPosition.z == 3,
          "publisher encodes immutable epoch binding connection payload and sequence");
    g_bridge.sendAllowed = false;
    Check(!RecordLocalPlayerEnemyHit(LocalHit()) && g_localClaimSequence == 2,
          "transport pressure drops claim and consumes its sequence");
    g_bridge.sendAllowed = true; g_host.epoch = 10;
    arrivalTarget.epoch = 10; // controlled native-arrival adapter now observes this room epoch
    AdmitClientClaimFixture();
    Check(RecordLocalPlayerEnemyHit(LocalHit()) && g_localClaimSequence == 3 && g_bridge.outgoing.size() == 2,
          "later room sends only new detection without replaying dropped claim");
    Reset(true); AdmitClientClaimFixture(); g_bridge.connections[1] = 0;
    Check(!RecordLocalPlayerEnemyHit(LocalHit()), "publisher rejects retired local connection");
    Reset(true); AdmitClientClaimFixture(); g_activationOrderedGeneration = 0;
    Check(!RecordLocalPlayerEnemyHit(LocalHit()), "publisher requires ordered generation barrier");
    Reset(true); AdmitClientClaimFixture(); Put(enemy + offsets::actor::LINKED_NEXT_HANDLE, std::uint32_t {1});
    Check(!RecordLocalPlayerEnemyHit(LocalHit()), "publisher rejects incomplete native census");
    Reset(true); AdmitClientClaimFixture(); g_host.enemies[1].objectId = 311;
    Check(!RecordLocalPlayerEnemyHit(LocalHit()), "publisher rejects host manifest identity mismatch");
    Reset(true); AdmitClientClaimFixture(); Put(enemy + ACTOR_STATUS, status + 0x100);
    Check(!RecordLocalPlayerEnemyHit(LocalHit()), "publisher rejects changed local binding metadata");
    Reset(true); AdmitClientClaimFixture(); bool foreignAccepted = true;
    HANDLE thread = CreateThread(nullptr, 0, ForeignPublisher, &foreignAccepted, 0, nullptr);
    if (thread) { WaitForSingleObject(thread, INFINITE); CloseHandle(thread); }
    Check(thread && !foreignAccepted && g_bridge.outgoing.empty() && g_localClaimSequence == 0,
          "foreign thread cannot suppress damage, publish or consume game-thread sequence");
    // Exercise the new read-only diagnostic boundary with the real EnemySync
    // implementation; the headless bridge keeps all source memory test-owned.
    namespace ht = kh2coop::inject::nativehittrace;
    ht::Configure(true, ht::AllHooks, ht::AllHooks);
    ht::RegisterOwnerThread();
    Reset();
    constexpr std::uint64_t highHost = 0x1234567800000064ULL;
    constexpr std::uint64_t highClient = 0xFEDCBA9800000065ULL;
    g_bridge.connections[0] = highHost;
    g_inst.map = 0x345; g_inst.btl = 0x567; g_inst.evt = 0x789;
    Put(image + offsets::MAP_PROGRAM, g_inst.map);
    Put(image + offsets::BATTLE_PROGRAM, g_inst.btl);
    Put(image + offsets::EVENT_PROGRAM, g_inst.evt);
    auto context = CaptureNativeHitContext();
    Check(context.available && context.readMask == ht::ContextComplete &&
          context.connectionId == highHost && context.hostConnectionId == highHost &&
          context.location[3] == 0x345 && context.location[4] == 0x567 && context.location[5] == 0x789,
          "diagnostic context preserves full connection IDs and uint16 room programs");
    Reset(true); g_host.ackSent = true;
    g_bridge.connections[0] = highHost; g_bridge.connections[1] = highClient;
    constexpr std::uint64_t highThird = 0xABCDEF1200000066ULL;
    g_bridge.connections[2] = highThird;
    context = CaptureNativeHitContext();
    Check(context.available && context.role == 2 && context.slot == 1 &&
          context.connectionId == highClient && context.hostConnectionId == highHost && mutableArrivalCalls == 0,
          "diagnostic context identifies current client separately from host");
    std::uint64_t damageRoster[3] {};
    Check(CaptureDamageContext(context, damageRoster) && context.available &&
          damageRoster[0] == highHost && damageRoster[1] == highClient && damageRoster[2] == highThird,
          "damage context keeps all current full-width roster identities");
    g_bridge.connections[1] = 0;
    context = CaptureNativeHitContext();
    Check(!context.available && !(context.readMask & ht::ContextBridge),
          "diagnostic context exposes unavailable retired client connection");
    Check(!CaptureDamageContext(context, damageRoster) && !context.available &&
          damageRoster[0] == 0 && damageRoster[1] == 0 && damageRoster[2] == 0,
          "unavailable damage context does not publish partial roster permission");
    Reset(true); g_host.ackSent = true; g_bridge.replaceThirdOnRead = 2;
    Check(!CaptureDamageContext(context, damageRoster) && !context.available &&
          damageRoster[0] == 0 && damageRoster[1] == 0 && damageRoster[2] == 0 &&
          g_bridge.connections[2] == 0x100000066ULL,
          "damage context rejects a remote roster replacement between checked reads");
    Reset(true); g_host.ackSent = true; ++g_bridge.generation;
    const auto oldOrdered = g_activationOrderedGeneration;
    context = CaptureNativeHitContext();
    Check(!context.available && !(context.readMask & ht::ContextSession) &&
          context.generation == 2 && g_activationGeneration == 1 && g_activationOrderedGeneration == oldOrdered,
          "diagnostic context observes generation mismatch without advancing authority");
    Reset(); Put(image + offsets::MAP_PROGRAM, std::uint16_t {0x104});
    context = CaptureNativeHitContext();
    Check(!context.available && !(context.readMask & ht::ContextRoom) && context.location[3] == 0x104,
          "diagnostic context rejects full program mismatch without low-byte alias");
    Reset(true); g_host.ackSent = false;
    context = CaptureNativeHitContext();
    Check(!context.available && !(context.readMask & ht::ContextPhase),
          "diagnostic context does not promote unacknowledged arrival");
    Reset(true); g_host.ackSent = true; arrivalTarget.mapProgram = 0x104;
    context = CaptureNativeHitContext();
    Check(!context.available && !(context.readMask & ht::ContextPhase) && mutableArrivalCalls == 0,
          "diagnostic phase matches complete host target without mutable warp helper");
    Reset(); kh2coop::inject::progresssync::g_hostSampleReady = false;
    context = CaptureNativeHitContext();
    Check(!context.available && !(context.readMask & ht::ContextPhase),
          "diagnostic context does not promote host progress that is not ready");
    Reset(); bool foreignContextRead = true;
    HANDLE contextThread = CreateThread(nullptr, 0, ForeignHitContext, &foreignContextRead, 0, nullptr);
    if (contextThread) { WaitForSingleObject(contextThread, INFINITE); CloseHandle(contextThread); }
    Check(contextThread && !foreignContextRead,
          "foreign diagnostic context leaves owner state and native reads unavailable");
    // Execute the production read-only avatar binding capture with the same
    // headless transport boundary. This is authority capture, not a native
    // puppet-drive/reconnect test.
    Reset(true);
    g_bridge.connections = {highHost, highClient, highThird};
    auto puppetAuthority = CapturePuppetAuthority();
    Check(puppetAuthority.mode == PuppetAuthorityMode::Network && puppetAuthority.localSlot == 1 &&
          puppetAuthority.generation == 1 && puppetAuthority.connectionIds[2] == highThird &&
          puppetAuthority.connectionIds[0] == highHost && puppetAuthority.connectionIds[1] == highClient,
          "puppet authority capture preserves full-width current roster");
    g_bridge.puppetMode = PuppetAuthorityMode::Unavailable;
    g_bridge.slot = WORLD_SLOT_UNKNOWN; g_bridge.connections = {};
    Check(CapturePuppetAuthority().mode == PuppetAuthorityMode::Unavailable,
          "unknown zero-roster networking state is not standalone Off");
    g_bridge.puppetMode = PuppetAuthorityMode::Off; g_bridge.generation = 0;
    Check(CapturePuppetAuthority().mode == PuppetAuthorityMode::Off,
          "explicit never-armed Off allows standalone authority");
    g_bridge.generation = 1;
    Check(CapturePuppetAuthority().mode == PuppetAuthorityMode::Unavailable,
          "previously armed generation cannot grant standalone Off");
    g_bridge.generation = 0; g_bridge.connections[2] = highThird;
    Check(CapturePuppetAuthority().mode == PuppetAuthorityMode::Unavailable,
          "explicit Off rejects remaining roster identity");
    Reset(true); g_bridge.open = false;
    Check(CapturePuppetAuthority().mode == PuppetAuthorityMode::Unavailable,
          "closed bridge does not grant standalone authority");
    Reset(true); g_bridge.replaceThirdOnRead = 2;
    Check(CapturePuppetAuthority().mode == PuppetAuthorityMode::Unavailable &&
          g_bridge.connections[2] == 0x100000066ULL,
          "puppet authority rejects member replacement between reads");
    Reset(true); ++g_bridge.generation;
    Check(CapturePuppetAuthority().mode == PuppetAuthorityMode::Unavailable &&
          g_activationGeneration == 1 && g_activationOrderedGeneration == 1,
          "puppet capture never advances mismatched ordered generation");
    Reset(true); g_bridge.connections[1] = 0;
    Check(CapturePuppetAuthority().mode == PuppetAuthorityMode::Unavailable,
          "retired local connection cannot grant network puppet authority");
    Reset(true); g_bridge.connections[0] = 0;
    Check(CapturePuppetAuthority().mode == PuppetAuthorityMode::Unavailable,
          "missing host cannot grant network puppet authority");
    Reset();
    Check(CurrentPuppetActor(player, transition, load) && CurrentPuppetActor(enemy, transition, load),
          "puppet release census proves current native-list membership");
    Check(!CurrentPuppetActor(image + 0x70000, transition, load),
          "readable absent actor is not eligible for puppet release");
    Check(!CurrentPuppetActor(player, transition + 1, load) && !CurrentPuppetActor(player, transition, load + 1),
          "puppet release rejects native lifecycle mismatch");
    Put(enemy + offsets::actor::LINKED_NEXT_HANDLE, std::uint32_t {1});
    Check(!CurrentPuppetActor(player, transition, load),
          "early actor presence in an incomplete list is not release permission");
    Reset(); pendingTransition = true;
    Check(!CurrentPuppetActor(player, transition, load),
          "pending native teardown prevents puppet release");
    TestWorldRetirement();
    TestEnemyHpOrdering();
    TestFreshProgressCapture();
    TestNativeResync();
    TestNativeRecordResync();
    TestSurvivingPackIntegration();
    TestPackOccupancyProjection();
    TestNativeRecordWriteGuards();
    TestConstructionSerialization();
    TestResourceSerialization();
    ht::Shutdown();
    VirtualFree(reinterpret_cast<void*>(image), 0, MEM_RELEASE);
    return errors ? 1 : 0;
} catch (const std::exception& error) {
    std::cerr << "FAIL: unhandled nativehit test exception: " << error.what() << '\n';
    return 3;
}
