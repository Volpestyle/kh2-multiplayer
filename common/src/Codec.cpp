#include "kh2coop/Codec.hpp"

#include <cstdarg>
#include <cstdio>
#include <stdexcept>

namespace kh2coop {

// ===== Binary write helpers =================================================

void write(ByteWriter& w, const Vec3& v) {
    w.writeF32(v.x);
    w.writeF32(v.y);
    w.writeF32(v.z);
}

void write(ByteWriter& w, const InputButtons& b) {
    std::uint16_t bits = 0;
    if (b.attack) bits |= (1 << 0);
    if (b.jump) bits |= (1 << 1);
    if (b.guard) bits |= (1 << 2);
    if (b.dodge) bits |= (1 << 3);
    if (b.lockOn) bits |= (1 << 4);
    if (b.magic1) bits |= (1 << 5);
    if (b.magic2) bits |= (1 << 6);
    if (b.special1) bits |= (1 << 7);
    if (b.special2) bits |= (1 << 8);
    w.writeU16(bits);
}

void write(ByteWriter& w, const InputFrame& f) {
    w.writeU32(f.seq);
    w.writeU64(f.clientTimeMs);
    w.writeU32(f.ownedActorId);
    w.writeF32(f.leftStickX);
    w.writeF32(f.leftStickY);
    w.writeF32(f.rightStickX);
    w.writeF32(f.rightStickY);
    w.writeU32(f.requestedTargetId);
    write(w, f.buttons);
}

void write(ByteWriter& w, const ActorState& a) {
    w.writeU32(a.actorId);
    w.writeU8(static_cast<std::uint8_t>(a.slot));
    write(w, a.position);
    w.writeF32(a.rotationY);
    write(w, a.velocity);
    w.writeU32(a.motionId);
    w.writeU16(static_cast<std::uint16_t>(a.action));
    w.writeU32(a.comboStep);
    w.writeI32(a.hp);
    w.writeI32(a.mp);
    w.writeI32(a.drive);
    w.writeU32(a.targetId);
    // Pack 4 bools into one byte.
    std::uint8_t flags = 0;
    if (a.airborne) flags |= (1 << 0);
    if (a.invuln) flags |= (1 << 1);
    if (a.staggered) flags |= (1 << 2);
    if (a.downed) flags |= (1 << 3);
    w.writeU8(flags);
}

void write(ByteWriter& w, const EnemyState& e) {
    w.writeU32(e.netId);
    w.writeU32(e.objectId);
    w.writeU32(e.spawnGroupId);
    write(w, e.position);
    w.writeF32(e.rotationY);
    w.writeU32(e.motionId);
    w.writeI32(e.hp);
    w.writeU32(e.targetActorId);
    w.writeBool(e.alive);
}

void write(ByteWriter& w, const RoomState& r) {
    w.writeU32(r.worldId);
    w.writeU32(r.roomId);
    w.writeU32(r.mapProgram);
    w.writeU32(r.battleProgram);
    w.writeU32(r.eventProgram);
    w.writeBool(r.inTransition);
    w.writeBool(r.inCutscene);
}

void write(ByteWriter& w, const SessionActor& sa) {
    w.writeU32(sa.actorId);
    w.writeU8(static_cast<std::uint8_t>(sa.slot));
    w.writeString(sa.ownerPeerId);
    w.writeString(sa.archetype);
}

void write(ByteWriter& w, const SessionState& ss) {
    w.writeString(ss.sessionId);
    w.writeString(ss.gameBuild);
    w.writeString(ss.modHash);
    write(w, ss.room);
    auto count = static_cast<std::uint16_t>(ss.actors.size());
    w.writeU16(count);
    for (const auto& a : ss.actors) {
        write(w, a);
    }
}

void write(ByteWriter& w, const ActorSnapshot& as) {
    w.writeU32(as.snapshotId);
    write(w, as.actor);
}

void write(ByteWriter& w, const EnemySnapshot& es) {
    w.writeU32(es.snapshotId);
    write(w, es.enemy);
}

void write(ByteWriter& w, const EventMessage& em) {
    w.writeU32(em.snapshotId);
    w.writeU16(static_cast<std::uint16_t>(em.type));
    w.writeString(em.payloadJson);
}

void write(ByteWriter& w, const HelloReject& hr) {
    w.writeU8(hr.code);
    w.writeString(hr.reason);
}

void write(ByteWriter& w, const ClientHello& ch) {
    w.writeU16(ch.protocolVersion);
    w.writeString(ch.gameBuild);
    w.writeString(ch.contentHash);
    w.writeString(ch.modHash);
    w.writeString(ch.peerId);
    w.writeString(ch.peerName);
    w.writeU8(static_cast<std::uint8_t>(ch.requestedMode));
    w.writeU8(ch.requestedSlot);
}

void write(ByteWriter& w, const AvatarState& a) {
    w.writeU32(a.seq);
    w.writeU64(a.serverTimeMs);
    w.writeU8(static_cast<std::uint8_t>(a.ownerSlot));
    w.writeU8(a.character);
    w.writeU8(a.colorVariant);
    w.writeU16(a.worldId);
    w.writeU16(a.roomId);
    write(w, a.position);
    w.writeF32(a.rotationY);
    write(w, a.velocity);
    w.writeU32(a.motionId);
    w.writeF32(a.motionTime);
    w.writeF32(a.motionSpeed);
    w.writeU8(a.flags);
    w.writeI32(a.hp);
    w.writeI32(a.maxHp);
    w.writeI32(a.mp);
    w.writeI32(a.maxMp);
}

void write(ByteWriter& w, const ClockPing& p) { w.writeU64(p.clientSendMs); }

void write(ByteWriter& w, const ClockPong& p) {
    w.writeU64(p.clientSendMs);
    w.writeU64(p.serverMs);
}

void write(ByteWriter& w, const RoomTransition& m) {
    w.writeU32(m.epoch);
    w.writeU16(m.worldId);
    w.writeU16(m.roomId);
    w.writeU16(m.door);
    w.writeU16(m.mapProgram);
    w.writeU16(m.battleProgram);
    w.writeU16(m.eventProgram);
}

void write(ByteWriter& w, const TransitionAck& m) {
    w.writeU32(m.epoch);
    w.writeU16(m.worldId);
    w.writeU16(m.roomId);
    w.writeBool(m.arrived);
}

void write(ByteWriter& w, const EventHold& m) {
    w.writeU32(m.epoch);
    w.writeBool(m.active);
    w.writeU16(m.eventProgram);
}

void write(ByteWriter& w, const EnemyManifest& m) {
    if (m.entries.size() > 0xFFFF) throw std::runtime_error("EnemyManifest too large");
    w.writeU32(m.epoch);
    w.writeBool(m.replace);
    w.writeU16(static_cast<std::uint16_t>(m.entries.size()));
    for (const auto& e : m.entries) {
        w.writeU16(e.netId);
        w.writeU16(e.battleProgram);
        w.writeU16(e.spawnIndex);
        w.writeU32(e.objectId);
        write(w, e.spawnPosition);
    }
}

void write(ByteWriter& w, const EnemyHp& m) {
    if (m.entries.size() > 0xFFFF) throw std::runtime_error("EnemyHp too large");
    w.writeU32(m.epoch);
    w.writeU16(static_cast<std::uint16_t>(m.entries.size()));
    for (const auto& e : m.entries) {
        w.writeU16(e.netId);
        w.writeI32(e.hp);
        w.writeI32(e.maxHp);
    }
}

void write(ByteWriter& w, const EnemyDeath& m) {
    w.writeU32(m.epoch);
    w.writeU16(m.netId);
}

void write(ByteWriter& w, const HitClaim& m) {
    w.writeU32(m.epoch);
    w.writeU32(m.seq);
    w.writeU16(m.netId);
    w.writeU32(m.attackId);
    w.writeI32(m.damage);
    write(w, m.attackerPosition);
    w.writeU8(static_cast<std::uint8_t>(m.attackerSlot));
}

void write(ByteWriter& w, const ProgressUpdate& m) {
    if (m.spans.size() > 0xFFFF) throw std::runtime_error("ProgressUpdate: too many spans");
    w.writeU32(m.version);
    w.writeBool(m.full);
    w.writeU16(static_cast<std::uint16_t>(m.spans.size()));
    for (const auto& span : m.spans) {
        if (span.bytes.size() > 0xFFFF) throw std::runtime_error("ProgressUpdate: span too long");
        w.writeU32(span.offset);
        w.writeU16(static_cast<std::uint16_t>(span.bytes.size()));
        for (auto b : span.bytes) w.writeU8(b);
    }
}

void write(ByteWriter& w, const StateHash& m) {
    w.writeU32(m.epoch);
    w.writeU16(m.worldId);
    w.writeU16(m.roomId);
    w.writeU32(m.enemiesHash);
    w.writeU32(m.progressHash);
}

void write(ByteWriter& w, const DesyncNotice& m) {
    w.writeU8(static_cast<std::uint8_t>(m.slot));
    w.writeU32(m.epoch);
    w.writeU8(m.fields);
}

void write(ByteWriter& w, const ResyncRequest& m) { w.writeU8(m.slot); }

// ===== Binary read helpers ==================================================

void read(ByteReader& r, Vec3& v) {
    v.x = r.readF32();
    v.y = r.readF32();
    v.z = r.readF32();
}

void read(ByteReader& r, InputButtons& b) {
    auto bits = r.readU16();
    b.attack = (bits & (1 << 0)) != 0;
    b.jump = (bits & (1 << 1)) != 0;
    b.guard = (bits & (1 << 2)) != 0;
    b.dodge = (bits & (1 << 3)) != 0;
    b.lockOn = (bits & (1 << 4)) != 0;
    b.magic1 = (bits & (1 << 5)) != 0;
    b.magic2 = (bits & (1 << 6)) != 0;
    b.special1 = (bits & (1 << 7)) != 0;
    b.special2 = (bits & (1 << 8)) != 0;
}

void read(ByteReader& r, InputFrame& f) {
    f.seq = r.readU32();
    f.clientTimeMs = r.readU64();
    f.ownedActorId = r.readU32();
    f.leftStickX = r.readF32();
    f.leftStickY = r.readF32();
    f.rightStickX = r.readF32();
    f.rightStickY = r.readF32();
    f.requestedTargetId = r.readU32();
    read(r, f.buttons);
}

void read(ByteReader& r, ActorState& a) {
    a.actorId = r.readU32();
    a.slot = static_cast<SlotType>(r.readU8());
    read(r, a.position);
    a.rotationY = r.readF32();
    read(r, a.velocity);
    a.motionId = r.readU32();
    a.action = static_cast<ActionState>(r.readU16());
    a.comboStep = r.readU32();
    a.hp = r.readI32();
    a.mp = r.readI32();
    a.drive = r.readI32();
    a.targetId = r.readU32();
    auto flags = r.readU8();
    a.airborne = (flags & (1 << 0)) != 0;
    a.invuln = (flags & (1 << 1)) != 0;
    a.staggered = (flags & (1 << 2)) != 0;
    a.downed = (flags & (1 << 3)) != 0;
}

void read(ByteReader& r, EnemyState& e) {
    e.netId = r.readU32();
    e.objectId = r.readU32();
    e.spawnGroupId = r.readU32();
    read(r, e.position);
    e.rotationY = r.readF32();
    e.motionId = r.readU32();
    e.hp = r.readI32();
    e.targetActorId = r.readU32();
    e.alive = r.readBool();
}

void read(ByteReader& r, RoomState& rs) {
    rs.worldId = r.readU32();
    rs.roomId = r.readU32();
    rs.mapProgram = r.readU32();
    rs.battleProgram = r.readU32();
    rs.eventProgram = r.readU32();
    rs.inTransition = r.readBool();
    rs.inCutscene = r.readBool();
}

void read(ByteReader& r, SessionActor& sa) {
    sa.actorId = r.readU32();
    sa.slot = static_cast<SlotType>(r.readU8());
    sa.ownerPeerId = r.readString();
    sa.archetype = r.readString();
}

void read(ByteReader& r, SessionState& ss) {
    ss.sessionId = r.readString();
    ss.gameBuild = r.readString();
    ss.modHash = r.readString();
    read(r, ss.room);
    auto count = r.readU16();
    ss.actors.resize(count);
    for (auto& a : ss.actors) {
        read(r, a);
    }
}

void read(ByteReader& r, ActorSnapshot& as) {
    as.snapshotId = r.readU32();
    read(r, as.actor);
}

void read(ByteReader& r, EnemySnapshot& es) {
    es.snapshotId = r.readU32();
    read(r, es.enemy);
}

void read(ByteReader& r, EventMessage& em) {
    em.snapshotId = r.readU32();
    em.type = static_cast<EventType>(r.readU16());
    em.payloadJson = r.readString();
}

void read(ByteReader& r, HelloReject& hr) {
    hr.code = r.readU8();
    hr.reason = r.readString();
}

void read(ByteReader& r, ClientHello& ch) {
    ch.protocolVersion = r.readU16();
    ch.gameBuild = r.readString();
    ch.contentHash = r.readString();
    ch.modHash = r.readString();
    ch.peerId = r.readString();
    ch.peerName = r.readString();
    ch.requestedMode = static_cast<RuntimeMode>(r.readU8());
    ch.requestedSlot = r.readU8();
}

void read(ByteReader& r, AvatarState& a) {
    a.seq = r.readU32();
    a.serverTimeMs = r.readU64();
    a.ownerSlot = static_cast<SlotType>(r.readU8());
    a.character = r.readU8();
    a.colorVariant = r.readU8();
    a.worldId = r.readU16();
    a.roomId = r.readU16();
    read(r, a.position);
    a.rotationY = r.readF32();
    read(r, a.velocity);
    a.motionId = r.readU32();
    a.motionTime = r.readF32();
    a.motionSpeed = r.readF32();
    a.flags = r.readU8();
    a.hp = r.readI32();
    a.maxHp = r.readI32();
    a.mp = r.readI32();
    a.maxMp = r.readI32();
}

void read(ByteReader& r, ClockPing& p) { p.clientSendMs = r.readU64(); }

void read(ByteReader& r, ClockPong& p) {
    p.clientSendMs = r.readU64();
    p.serverMs = r.readU64();
}

void read(ByteReader& r, RoomTransition& m) {
    m.epoch = r.readU32();
    m.worldId = r.readU16();
    m.roomId = r.readU16();
    m.door = r.readU16();
    m.mapProgram = r.readU16();
    m.battleProgram = r.readU16();
    m.eventProgram = r.readU16();
}

void read(ByteReader& r, TransitionAck& m) {
    m.epoch = r.readU32();
    m.worldId = r.readU16();
    m.roomId = r.readU16();
    m.arrived = r.readBool();
}

void read(ByteReader& r, EventHold& m) {
    m.epoch = r.readU32();
    m.active = r.readBool();
    m.eventProgram = r.readU16();
}

void read(ByteReader& r, EnemyManifest& m) {
    m.epoch = r.readU32();
    m.replace = r.readBool();
    const auto n = r.readU16();
    m.entries.resize(n);
    for (auto& e : m.entries) {
        e.netId = r.readU16();
        e.battleProgram = r.readU16();
        e.spawnIndex = r.readU16();
        e.objectId = r.readU32();
        read(r, e.spawnPosition);
    }
}

void read(ByteReader& r, EnemyHp& m) {
    m.epoch = r.readU32();
    const auto n = r.readU16();
    m.entries.resize(n);
    for (auto& e : m.entries) {
        e.netId = r.readU16();
        e.hp = r.readI32();
        e.maxHp = r.readI32();
    }
}

void read(ByteReader& r, EnemyDeath& m) {
    m.epoch = r.readU32();
    m.netId = r.readU16();
}

void read(ByteReader& r, HitClaim& m) {
    m.epoch = r.readU32();
    m.seq = r.readU32();
    m.netId = r.readU16();
    m.attackId = r.readU32();
    m.damage = r.readI32();
    read(r, m.attackerPosition);
    m.attackerSlot = static_cast<SlotType>(r.readU8());
}

void read(ByteReader& r, ProgressUpdate& m) {
    m.version = r.readU32();
    m.full = r.readBool();
    m.spans.resize(r.readU16());
    for (auto& span : m.spans) {
        span.offset = r.readU32();
        span.bytes.resize(r.readU16());
        for (auto& b : span.bytes) b = r.readU8();
    }
}

void read(ByteReader& r, StateHash& m) {
    m.epoch = r.readU32();
    m.worldId = r.readU16();
    m.roomId = r.readU16();
    m.enemiesHash = r.readU32();
    m.progressHash = r.readU32();
}

void read(ByteReader& r, DesyncNotice& m) {
    m.slot = static_cast<SlotType>(r.readU8());
    m.epoch = r.readU32();
    m.fields = r.readU8();
}

void read(ByteReader& r, ResyncRequest& m) { m.slot = r.readU8(); }

// ===== Framed packet helpers ================================================

static constexpr std::size_t kHeaderSize = 3; // 1 type + 2 length

std::vector<std::uint8_t> encodePacket(PacketType type,
                                       const std::vector<std::uint8_t>& payload) {
    if (payload.size() > 0xFFFF) {
        throw std::runtime_error("encodePacket: payload exceeds 64 KiB limit");
    }
    std::vector<std::uint8_t> out;
    out.reserve(kHeaderSize + payload.size());
    out.push_back(static_cast<std::uint8_t>(type));
    auto len = static_cast<std::uint16_t>(payload.size());
    out.push_back(static_cast<std::uint8_t>(len & 0xFF));
    out.push_back(static_cast<std::uint8_t>((len >> 8) & 0xFF));
    out.insert(out.end(), payload.begin(), payload.end());
    return out;
}

std::vector<std::uint8_t> encode(const InputFrame& f) {
    ByteWriter w;
    write(w, f);
    return encodePacket(PacketType::InputFrame, w.data());
}

std::vector<std::uint8_t> encode(const SessionState& ss) {
    ByteWriter w;
    write(w, ss);
    return encodePacket(PacketType::SessionState, w.data());
}

std::vector<std::uint8_t> encode(const ActorSnapshot& as) {
    ByteWriter w;
    write(w, as);
    return encodePacket(PacketType::ActorSnapshot, w.data());
}

std::vector<std::uint8_t> encode(const EnemySnapshot& es) {
    ByteWriter w;
    write(w, es);
    return encodePacket(PacketType::EnemySnapshot, w.data());
}

std::vector<std::uint8_t> encode(const EventMessage& em) {
    ByteWriter w;
    write(w, em);
    return encodePacket(PacketType::EventMessage, w.data());
}

std::vector<std::uint8_t> encode(const HelloReject& hr) {
    ByteWriter w;
    write(w, hr);
    return encodePacket(PacketType::HelloReject, w.data());
}

std::vector<std::uint8_t> encode(const ClientHello& ch) {
    ByteWriter w;
    write(w, ch);
    return encodePacket(PacketType::ClientHello, w.data());
}

std::vector<std::uint8_t> encode(const ClockPing& p) {
    ByteWriter w;
    write(w, p);
    return encodePacket(PacketType::ClockPing, w.data());
}

std::vector<std::uint8_t> encode(const ClockPong& p) {
    ByteWriter w;
    write(w, p);
    return encodePacket(PacketType::ClockPong, w.data());
}

std::vector<std::uint8_t> encode(const AvatarState& a, PacketType type) {
    if (type != PacketType::AvatarState && type != PacketType::AvatarRelay) {
        throw std::invalid_argument("encode(AvatarState): bad packet type");
    }
    ByteWriter w;
    write(w, a);
    return encodePacket(type, w.data());
}

std::vector<std::uint8_t> encode(const RoomTransition& m) {
    ByteWriter w;
    write(w, m);
    return encodePacket(PacketType::RoomTransition, w.data());
}

std::vector<std::uint8_t> encode(const TransitionAck& m) {
    ByteWriter w;
    write(w, m);
    return encodePacket(PacketType::TransitionAck, w.data());
}

std::vector<std::uint8_t> encode(const EventHold& m) {
    ByteWriter w;
    write(w, m);
    return encodePacket(PacketType::EventHold, w.data());
}

std::vector<std::uint8_t> encode(const EnemyManifest& m) {
    ByteWriter w;
    write(w, m);
    return encodePacket(PacketType::EnemyManifest, w.data());
}

std::vector<std::uint8_t> encode(const EnemyHp& m) {
    ByteWriter w;
    write(w, m);
    return encodePacket(PacketType::EnemyHp, w.data());
}

std::vector<std::uint8_t> encode(const EnemyDeath& m) {
    ByteWriter w;
    write(w, m);
    return encodePacket(PacketType::EnemyDeath, w.data());
}

std::vector<std::uint8_t> encode(const HitClaim& m) {
    ByteWriter w;
    write(w, m);
    return encodePacket(PacketType::HitClaim, w.data());
}

std::vector<std::uint8_t> encode(const ProgressUpdate& m) {
    ByteWriter w;
    write(w, m);
    return encodePacket(PacketType::ProgressUpdate, w.data());
}

std::vector<std::uint8_t> encode(const StateHash& m) {
    ByteWriter w;
    write(w, m);
    return encodePacket(PacketType::StateHash, w.data());
}

std::vector<std::uint8_t> encode(const DesyncNotice& m) {
    ByteWriter w;
    write(w, m);
    return encodePacket(PacketType::DesyncNotice, w.data());
}

std::vector<std::uint8_t> encode(const ResyncRequest& m) {
    ByteWriter w;
    write(w, m);
    return encodePacket(PacketType::ResyncRequest, w.data());
}

PacketType decodePacketHeader(const std::uint8_t* data, std::size_t size,
                              const std::uint8_t*& payloadOut,
                              std::size_t& payloadSizeOut) {
    if (size < kHeaderSize) {
        throw std::runtime_error("decodePacketHeader: buffer too small for header");
    }
    auto type = static_cast<PacketType>(data[0]);
    std::uint16_t len = static_cast<std::uint16_t>(data[1])
                      | (static_cast<std::uint16_t>(data[2]) << 8);
    if (size < kHeaderSize + len) {
        throw std::runtime_error("decodePacketHeader: buffer too small for payload");
    }
    payloadOut = data + kHeaderSize;
    payloadSizeOut = len;
    return type;
}

// ===== Debug strings ========================================================

// Small helper to avoid snprintf boilerplate.
static std::string fmt(const char* format, ...) {
    char buf[512];
    va_list args;
    va_start(args, format);
    std::vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);
    return std::string(buf);
}

static const char* slotName(SlotType s) {
    switch (s) {
        case SlotType::Player: return "PLAYER";
        case SlotType::Friend1: return "FRIEND_1";
        case SlotType::Friend2: return "FRIEND_2";
    }
    return "?";
}

static const char* actionName(ActionState a) {
    switch (a) {
        case ActionState::Unknown: return "Unknown";
        case ActionState::Idle: return "Idle";
        case ActionState::Move: return "Move";
        case ActionState::Jump: return "Jump";
        case ActionState::Attack: return "Attack";
        case ActionState::Guard: return "Guard";
        case ActionState::Dodge: return "Dodge";
        case ActionState::Stagger: return "Stagger";
        case ActionState::Downed: return "Downed";
    }
    return "?";
}

static const char* eventName(EventType t) {
    switch (t) {
        case EventType::Unknown: return "Unknown";
        case EventType::SpawnGroup: return "SpawnGroup";
        case EventType::KillEnemy: return "KillEnemy";
        case EventType::RewardGranted: return "RewardGranted";
        case EventType::RoomTransitionBegin: return "RoomTransitionBegin";
        case EventType::RoomTransitionComplete: return "RoomTransitionComplete";
        case EventType::CutsceneBegin: return "CutsceneBegin";
        case EventType::CutsceneEnd: return "CutsceneEnd";
        case EventType::PlayerKo: return "PlayerKo";
        case EventType::PlayerRevive: return "PlayerRevive";
        case EventType::ForceTeleport: return "ForceTeleport";
        case EventType::SessionResyncRequired: return "SessionResyncRequired";
    }
    return "?";
}

std::string toDebugString(const Vec3& v) {
    return fmt("(%.2f, %.2f, %.2f)", v.x, v.y, v.z);
}

std::string toDebugString(const ActorState& a) {
    return fmt("Actor{id=%u slot=%s pos=%s rotY=%.2f action=%s hp=%d mp=%d}",
               a.actorId, slotName(a.slot), toDebugString(a.position).c_str(),
               a.rotationY, actionName(a.action), a.hp, a.mp);
}

std::string toDebugString(const EnemyState& e) {
    return fmt("Enemy{net=%u obj=%u pos=%s hp=%d alive=%s}",
               e.netId, e.objectId, toDebugString(e.position).c_str(),
               e.hp, e.alive ? "yes" : "no");
}

std::string toDebugString(const RoomState& r) {
    return fmt("Room{world=%u room=%u map=%u battle=%u event=%u transition=%s cutscene=%s}",
               r.worldId, r.roomId, r.mapProgram, r.battleProgram, r.eventProgram,
               r.inTransition ? "yes" : "no", r.inCutscene ? "yes" : "no");
}

std::string toDebugString(const InputFrame& f) {
    return fmt("Input{seq=%u actor=%u stick=(%.2f,%.2f) atk=%d jmp=%d grd=%d}",
               f.seq, f.ownedActorId, f.leftStickX, f.leftStickY,
               f.buttons.attack, f.buttons.jump, f.buttons.guard);
}

std::string toDebugString(const SessionState& ss) {
    return fmt("Session{id=%s build=%s mod=%s room=%s actors=%zu}",
               ss.sessionId.c_str(), ss.gameBuild.c_str(), ss.modHash.c_str(),
               toDebugString(ss.room).c_str(), ss.actors.size());
}

std::string toDebugString(const ActorSnapshot& as) {
    return fmt("ActorSnap{snap=%u %s}", as.snapshotId,
               toDebugString(as.actor).c_str());
}

std::string toDebugString(const EnemySnapshot& es) {
    return fmt("EnemySnap{snap=%u %s}", es.snapshotId,
               toDebugString(es.enemy).c_str());
}

std::string toDebugString(const EventMessage& em) {
    return fmt("Event{snap=%u type=%s payload=%s}", em.snapshotId,
               eventName(em.type), em.payloadJson.c_str());
}

static const char* runtimeModeName(RuntimeMode m) {
    switch (m) {
        case RuntimeMode::CampaignCoop: return "CampaignCoop";
        case RuntimeMode::PublicRealm: return "PublicRealm";
    }
    return "?";
}

std::string toDebugString(const ClientHello& ch) {
    return fmt("ClientHello{proto=%u build=%s peer=%s name=%s mode=%s slot=%u}",
               ch.protocolVersion, ch.gameBuild.c_str(), ch.peerId.c_str(),
               ch.peerName.c_str(), runtimeModeName(ch.requestedMode),
               ch.requestedSlot);
}

std::string toDebugString(const AvatarState& a) {
    return fmt("Avatar{seq=%u t=%llu slot=%u room=%u/%u pos=%s rot=%.2f motion=%u@%.2f hp=%d/%d}",
               a.seq, static_cast<unsigned long long>(a.serverTimeMs),
               static_cast<unsigned>(a.ownerSlot), a.worldId, a.roomId,
               toDebugString(a.position).c_str(), a.rotationY, a.motionId,
               a.motionTime, a.hp, a.maxHp);
}

} // namespace kh2coop
