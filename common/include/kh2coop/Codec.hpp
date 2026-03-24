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

    // Host -> Client
    SessionState = 10,
    ActorSnapshot = 11,
    EnemySnapshot = 12,
    EventMessage = 13,
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

void read(ByteReader& r, Vec3& v);
void read(ByteReader& r, InputButtons& b);
void read(ByteReader& r, InputFrame& f);
void read(ByteReader& r, ActorState& a);
void read(ByteReader& r, EnemyState& e);
void read(ByteReader& r, RoomState& rs);

// Protocol.hpp structs
void write(ByteWriter& w, const SessionActor& sa);
void write(ByteWriter& w, const SessionState& ss);
void write(ByteWriter& w, const ActorSnapshot& as);
void write(ByteWriter& w, const EnemySnapshot& es);
void write(ByteWriter& w, const EventMessage& em);

void read(ByteReader& r, SessionActor& sa);
void read(ByteReader& r, SessionState& ss);
void read(ByteReader& r, ActorSnapshot& as);
void read(ByteReader& r, EnemySnapshot& es);
void read(ByteReader& r, EventMessage& em);

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

} // namespace kh2coop
