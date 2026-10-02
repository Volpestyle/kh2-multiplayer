#pragma once
#include "kh2coop/ByteBuffer.hpp"
#include "kh2coop/Protocol.hpp"
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

    // Host -> Client
    SessionState = 10,
    ActorSnapshot = 11,
    EnemySnapshot = 12,
    EventMessage = 13,
    AvatarRelay = 14,  // AvatarState with ownerSlot stamped by the host
    ClockPong = 15,

    // Host-authored world sync: host -> relay -> everyone else
    RoomTransition = 20,
    EventHold = 21,
    EnemyManifest = 22,
    EnemyHp = 23,
    EnemyDeath = 24,
};

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
void write(ByteWriter& w, const SessionActor& sa);
void write(ByteWriter& w, const SessionState& ss);
void write(ByteWriter& w, const ActorSnapshot& as);
void write(ByteWriter& w, const EnemySnapshot& es);
void write(ByteWriter& w, const EventMessage& em);
void write(ByteWriter& w, const ClientHello& ch);
void write(ByteWriter& w, const ClockPing& p);
void write(ByteWriter& w, const ClockPong& p);
void write(ByteWriter& w, const RoomTransition& m);
void write(ByteWriter& w, const TransitionAck& m);
void write(ByteWriter& w, const EventHold& m);
void write(ByteWriter& w, const EnemyManifest& m);
void write(ByteWriter& w, const EnemyHp& m);
void write(ByteWriter& w, const EnemyDeath& m);
void write(ByteWriter& w, const HitClaim& m);

void read(ByteReader& r, SessionActor& sa);
void read(ByteReader& r, SessionState& ss);
void read(ByteReader& r, ActorSnapshot& as);
void read(ByteReader& r, EnemySnapshot& es);
void read(ByteReader& r, EventMessage& em);
void read(ByteReader& r, ClientHello& ch);
void read(ByteReader& r, ClockPing& p);
void read(ByteReader& r, ClockPong& p);
void read(ByteReader& r, RoomTransition& m);
void read(ByteReader& r, TransitionAck& m);
void read(ByteReader& r, EventHold& m);
void read(ByteReader& r, EnemyManifest& m);
void read(ByteReader& r, EnemyHp& m);
void read(ByteReader& r, EnemyDeath& m);
void read(ByteReader& r, HitClaim& m);

// ---------------------------------------------------------------------------
// Framed packet helpers
//
// Wire format: [PacketType : 1 byte][payloadLen : 2 bytes][payload : N bytes]
// ---------------------------------------------------------------------------

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
std::vector<std::uint8_t> encode(const RoomTransition& m);
std::vector<std::uint8_t> encode(const TransitionAck& m);
std::vector<std::uint8_t> encode(const EventHold& m);
std::vector<std::uint8_t> encode(const EnemyManifest& m);
std::vector<std::uint8_t> encode(const EnemyHp& m);
std::vector<std::uint8_t> encode(const EnemyDeath& m);
std::vector<std::uint8_t> encode(const HitClaim& m);
// AvatarState travels as PacketType::AvatarState (client -> host) or
// PacketType::AvatarRelay (host -> clients); the payload is identical.
std::vector<std::uint8_t> encode(const AvatarState& a, PacketType type);

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
