#pragma once
#include "kh2coop/ByteBuffer.hpp"
#include "kh2coop/Protocol.hpp"
#include "kh2coop/DesyncProtocol.hpp"
#include "kh2coop/ResyncProtocol.hpp"
#include "kh2coop/Types.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace kh2coop {

// ---------------------------------------------------------------------------
// Packet type tag — first byte of every framed message on the wire.
// ---------------------------------------------------------------------------
enum class PacketType : std::uint8_t {
    // Client -> Host
    InputFrame = 1,
    TransitionAck = 2,
    Heartbeat = 3,
    ClientHello = 4,   // Dedicated handshake (replaces SessionState-as-hello)
    AvatarState = 5,   // Owner's avatar; the host relays it to the others
    ClockPing = 6,
    HitClaim = 8,      // client -> host only (relay stamps attackerSlot)
    StateHash = 9,     // every machine -> relay (desync detection)

    // Host -> Client
    SessionState = 10,
    ActorSnapshot = 11,
    EnemySnapshot = 12,
    EventMessage = 13,
    AvatarRelay = 14,  // connection identity + AvatarState, both stamped by relay
    ClockPong = 15,
    HelloReject = 16,  // why the relay refused this client; sent before disconnect

    // Host-authored world sync: host -> relay -> everyone else
    RoomTransition = 20,
    EventHold = 21,
    EnemyManifest = 22,
    EnemyHp = 23,
    EnemyDeath = 24,
    ProgressUpdate = 25,
    ResyncRequest = 26, // host only

    // Relay -> everyone
    DesyncNotice = 27,
    ActivationRequest = 28,   // verified client -> host, ephemeral challenge
    HostActivationPoint = 29, // verified host -> requester only, never replayed
    DesyncCaptureRequest = 30, // diagnostics only; never world/bridge packets
    DesyncArtifactChunk = 31,
    DesyncCaptureDone = 32,
    WorldBinding = 33, WorldEnvelope = 34, ResyncPlan = 35,
    ResyncBegin = 36, ResyncPart = 37, ResyncEnd = 38,
    ResyncAck = 39, ResyncResult = 40,
    LocalResyncCommand = 0xF0, NativeResyncSnapshot = 0xF1,
};
inline bool isDesyncDiagnosticPacket(PacketType type) {
    return type==PacketType::DesyncCaptureRequest || type==PacketType::DesyncArtifactChunk || type==PacketType::DesyncCaptureDone;
}

// ---------------------------------------------------------------------------
// Binary codec — field-by-field write / read for every domain struct.
// ---------------------------------------------------------------------------

// Types.hpp structs
void write(ByteWriter& w, const Vec3& v);
void write(ByteWriter& w, const InputButtons& b);
void write(ByteWriter& w, const InputFrame& f);
void write(ByteWriter& w, const ActorState& a);
void write(ByteWriter& w, const EnemyState& e);
void write(ByteWriter& w, const RoomState& r);
void write(ByteWriter& w, const AvatarState& a);

void read(ByteReader& r, Vec3& v);
void read(ByteReader& r, InputButtons& b);
void read(ByteReader& r, InputFrame& f);
void read(ByteReader& r, ActorState& a);
void read(ByteReader& r, EnemyState& e);
void read(ByteReader& r, RoomState& rs);
void read(ByteReader& r, AvatarState& a);

// Protocol.hpp structs
inline constexpr std::size_t AVATAR_RELAY_PAYLOAD_SIZE = 84;
void write(ByteWriter& w, const AvatarRelay& a);
void read(ByteReader& r, AvatarRelay& a);
void write(ByteWriter& w, const SessionActor& sa);
void write(ByteWriter& w, const SessionState& ss);
void write(ByteWriter& w, const ActorSnapshot& as);
void write(ByteWriter& w, const EnemySnapshot& es);
void write(ByteWriter& w, const EventMessage& em);
void write(ByteWriter& w, const ClientHello& ch);
void write(ByteWriter& w, const ClockPing& p);
void write(ByteWriter& w, const ClockPong& p);
void write(ByteWriter& w, const HelloReject& hr);
void write(ByteWriter& w, const RoomTransition& m);
void write(ByteWriter& w, const ActivationRequest& m);
void write(ByteWriter& w, const HostActivationPoint& m);
void write(ByteWriter& w, const TransitionAck& m);
void write(ByteWriter& w, const EventHold& m);
void write(ByteWriter& w, const EnemyManifest& m);
void write(ByteWriter& w, const EnemyHp& m);
void write(ByteWriter& w, const EnemyDeath& m);
void write(ByteWriter& w, const HitClaim& m);
void write(ByteWriter& w, const ProgressUpdate& m);
void write(ByteWriter& w, const StateHash& m);
void write(ByteWriter& w, const DesyncNotice& m);
void write(ByteWriter& w, const ResyncRequest& m);

void read(ByteReader& r, SessionActor& sa);
void read(ByteReader& r, SessionState& ss);
void read(ByteReader& r, ActorSnapshot& as);
void read(ByteReader& r, EnemySnapshot& es);
void read(ByteReader& r, EventMessage& em);
void read(ByteReader& r, ClientHello& ch);
void read(ByteReader& r, ClockPing& p);
void read(ByteReader& r, ClockPong& p);
void read(ByteReader& r, HelloReject& hr);
void read(ByteReader& r, RoomTransition& m);
void read(ByteReader& r, ActivationRequest& m);
void read(ByteReader& r, HostActivationPoint& m);
void read(ByteReader& r, TransitionAck& m);
void read(ByteReader& r, EventHold& m);
void read(ByteReader& r, EnemyManifest& m);
// Requires a complete payload and nonzero source sequence; no partial assignment.
void read(ByteReader& r, EnemyHp& m);
void read(ByteReader& r, EnemyDeath& m);
void read(ByteReader& r, HitClaim& m);
void read(ByteReader& r, ProgressUpdate& m);
void read(ByteReader& r, StateHash& m);
void read(ByteReader& r, DesyncNotice& m);
void read(ByteReader& r, ResyncRequest& m);
void write(ByteWriter&, const DesyncCaptureRequest&);
void write(ByteWriter&, const DesyncArtifactChunk&);
void write(ByteWriter&, const DesyncCaptureDone&);
void read(ByteReader&, DesyncCaptureRequest&);
void read(ByteReader&, DesyncArtifactChunk&);
void read(ByteReader&, DesyncCaptureDone&);

// ---------------------------------------------------------------------------
// Framed packet helpers
//
// Wire format: [PacketType : 1 byte][payloadLen : 2 bytes][payload : N bytes]
// ---------------------------------------------------------------------------

void write(ByteWriter&, const WorldBinding&);
void read(ByteReader&, WorldBinding&);
void write(ByteWriter&, const WorldEnvelope&);
void read(ByteReader&, WorldEnvelope&);
void write(ByteWriter&, const ResyncPlan&);
void read(ByteReader&, ResyncPlan&);
void write(ByteWriter&, const ResyncBegin&);
void read(ByteReader&, ResyncBegin&);
void write(ByteWriter&, const ResyncPart&);
void read(ByteReader&, ResyncPart&);
void write(ByteWriter&, const ResyncEnd&);
void read(ByteReader&, ResyncEnd&);
void write(ByteWriter&, const ResyncAck&);
void read(ByteReader&, ResyncAck&);
void write(ByteWriter&, const ResyncResult&);
void read(ByteReader&, ResyncResult&);
void write(ByteWriter&, const ResyncSnapshot&);
void read(ByteReader&, ResyncSnapshot&);
std::vector<std::uint8_t> encode(const WorldBinding&);
std::vector<std::uint8_t> encode(const WorldEnvelope&);
std::vector<std::uint8_t> encode(const ResyncPlan&);
std::vector<std::uint8_t> encode(const ResyncBegin&);
std::vector<std::uint8_t> encode(const ResyncPart&);
std::vector<std::uint8_t> encode(const ResyncEnd&);
std::vector<std::uint8_t> encode(const ResyncAck&);
std::vector<std::uint8_t> encode(const ResyncResult&);
std::vector<std::uint8_t> encodeResyncSnapshot(const ResyncSnapshot&);
ResyncSnapshot decodeResyncSnapshot(const std::vector<std::uint8_t>&);
std::vector<std::uint8_t> encodeNativeResyncSnapshot(const ResyncBegin&, const ResyncSnapshot&);
void decodeNativeResyncSnapshot(const std::vector<std::uint8_t>&, ResyncBegin&, ResyncSnapshot&);
std::vector<std::uint8_t> encodeLocalResyncCommand(std::uint8_t targetMask);
bool isScopedWorldPacket(PacketType);
bool isMaterialWorldPacket(PacketType);
PacketType validateScopedWorldPacket(const std::vector<std::uint8_t>&);

// Build a framed packet from a pre-serialized payload.
std::vector<std::uint8_t> encodePacket(PacketType type,
                                       const std::vector<std::uint8_t>& payload);

// Convenience: serialize a domain struct and frame it in one call.
std::vector<std::uint8_t> encode(const InputFrame& f);
std::vector<std::uint8_t> encode(const SessionState& ss);
std::vector<std::uint8_t> encode(const ActorSnapshot& as);
std::vector<std::uint8_t> encode(const EnemySnapshot& es);
std::vector<std::uint8_t> encode(const EventMessage& em);
std::vector<std::uint8_t> encode(const ClientHello& ch);
std::vector<std::uint8_t> encode(const ClockPing& p);
std::vector<std::uint8_t> encode(const ClockPong& p);
std::vector<std::uint8_t> encode(const HelloReject& hr);
std::vector<std::uint8_t> encode(const RoomTransition& m);
std::vector<std::uint8_t> encode(const ActivationRequest& m);
std::vector<std::uint8_t> encode(const HostActivationPoint& m);
std::vector<std::uint8_t> encode(const TransitionAck& m);
std::vector<std::uint8_t> encode(const EventHold& m);
std::vector<std::uint8_t> encode(const EnemyManifest& m);
std::vector<std::uint8_t> encode(const EnemyHp& m);
std::vector<std::uint8_t> encode(const EnemyDeath& m);
std::vector<std::uint8_t> encode(const HitClaim& m);
std::vector<std::uint8_t> encode(const ProgressUpdate& m);
std::vector<std::uint8_t> encode(const StateHash& m);
std::vector<std::uint8_t> encode(const DesyncNotice& m);
std::vector<std::uint8_t> encode(const ResyncRequest& m);
std::vector<std::uint8_t> encode(const AvatarRelay& a);
std::vector<std::uint8_t> encode(const DesyncCaptureRequest&);
std::vector<std::uint8_t> encode(const DesyncArtifactChunk&);
std::vector<std::uint8_t> encode(const DesyncCaptureDone&);
// Client -> host only; passing another packet type throws.
std::vector<std::uint8_t> encode(const AvatarState& a, PacketType type);

// World sync packets (host-authored state, hit claims, acks): the ones the
// runtime shuttles between the relay and the DLL's WorldBridge.
inline bool isWorldPacket(PacketType t) {
    switch (t) {
        case PacketType::RoomTransition:
        case PacketType::EventHold:
        case PacketType::EnemyManifest:
        case PacketType::EnemyHp:
        case PacketType::EnemyDeath:
        case PacketType::ProgressUpdate:
        case PacketType::HitClaim:
        case PacketType::TransitionAck:
        case PacketType::StateHash:
        case PacketType::ResyncRequest:
        case PacketType::DesyncNotice:
        case PacketType::ActivationRequest:
        case PacketType::HostActivationPoint:
            return true;
        default:
            return false;
    }
}

inline bool isEphemeralWorldPacket(PacketType t) {
    return t == PacketType::ActivationRequest || t == PacketType::HostActivationPoint;
}

// Checks the complete framed packet, including exact length and finite float4.
// Throws on a malformed activation packet; other types are not accepted here.
void validateActivationPacket(const std::vector<std::uint8_t>& packet);

// Bridge-local only: ordered reset carrying the generation already published
// in WorldBridge, followed by its captured delivery serial (12 payload bytes).
// Zero generation/serial (mapping not attached yet) cannot arm native authority.
std::vector<std::uint8_t> encodeWorldSessionReset(std::uint32_t generation, std::uint64_t deliverySerial = 0);

// Read the framed header. Returns the PacketType and sets payloadOut /
// payloadSizeOut to point into the original buffer (no copy).
// Throws on truncated header.
PacketType decodePacketHeader(const std::uint8_t* data, std::size_t size,
                              const std::uint8_t*& payloadOut,
                              std::size_t& payloadSizeOut);

// ---------------------------------------------------------------------------
// Debug strings — human-readable one-liners for logging.
// ---------------------------------------------------------------------------
std::string toDebugString(const Vec3& v);
std::string toDebugString(const ActorState& a);
std::string toDebugString(const EnemyState& e);
std::string toDebugString(const RoomState& r);
std::string toDebugString(const InputFrame& f);
std::string toDebugString(const SessionState& ss);
std::string toDebugString(const ActorSnapshot& as);
std::string toDebugString(const EnemySnapshot& es);
std::string toDebugString(const EventMessage& em);
std::string toDebugString(const ClientHello& ch);
std::string toDebugString(const AvatarState& a);

} // namespace kh2coop
