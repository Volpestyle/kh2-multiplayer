// Game-thread host outbox and client receipts. No actor or controller pointer
// survives a callback. Pending announcements survive an intro hold/load arrival.
struct PendingSpawnEmission {
    SpawnEmission value;
    std::uint32_t generation = 0, transition = 0, load = 0;
};
std::vector<PendingSpawnEmission> g_emissionOutbox;
SpawnEmissionLedger g_emissionLedger;
std::uint64_t g_emissionSequence = 0;
unsigned g_emissionDeniedLogs = 0;

void ResetSpawnEmissions() {
    g_emissionOutbox.clear(); g_emissionLedger.Clear();
    g_emissionSequence = 0; g_emissionDeniedLogs = 0;
}
void FlushSpawnEmissions() {
    RoomTransition room;
    if (!ActivationContext(Role::Host, room)) return;
    while (!g_emissionOutbox.empty()) {
        auto& pending = g_emissionOutbox.front();
        if (pending.generation != WorldSessionGeneration() || pending.transition != warp::TransitionSerial() ||
            pending.load != warp::LoadSerial() || !SameLocation(pending.value.location, room)) {
            g_emissionOutbox.erase(g_emissionOutbox.begin()); continue;
        }
        pending.value.location = room;
        ProducerWorldContext context;
        if (!CaptureWorldContext(context) || !SendCapturedWorld(encode(pending.value), context)) return;
        if (g_log) g_log("[spawn-emission] sent epoch=%u seq=%llu key=%u header=%u record=%u point=(%.1f,%.1f,%.1f,%.1f)",
            room.epoch, static_cast<unsigned long long>(pending.value.sequence), pending.value.controllerKey,
            pending.value.headerId, pending.value.recordIndex, pending.value.point[0], pending.value.point[1],
            pending.value.point[2], pending.value.point[3]);
        g_emissionOutbox.erase(g_emissionOutbox.begin());
    }
}
void ReceiveSpawnEmission(const WorldScope& scope, const SpawnEmission& value) {
    // The target room packet precedes its emissions on the reliable channel.
    // Do not require native arrival: that discarded the BC intro's early adds.
    if (CurrentRole() != Role::Client || !g_lastRoomPacket ||
        !scope.sourceConnectionId || scope.sourceConnectionId != g_bridge.ConnectionId(0) ||
        scope.sourceDeliverySerial != g_bridge.PeerDeliverySerial(0) || !scope.hostSourceSerial ||
        !sameActivationLocation(value.location, *g_lastRoomPacket)) return;
    if (g_emissionLedger.Add(value) && g_log)
        g_log("[spawn-emission] received epoch=%u seq=%llu key=%u header=%u record=%u",
            value.location.epoch, static_cast<unsigned long long>(value.sequence), value.controllerKey, value.headerId, value.recordIndex);
}
