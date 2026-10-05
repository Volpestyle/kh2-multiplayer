#pragma once
#include "kh2coop/Codec.hpp"
#include <cstdint>
#include <vector>

namespace kh2coop::inject::progresssync {
using LogFn = void (*)(const char* fmt, ...);
using SendFn = bool (*)(const std::vector<std::uint8_t>& packet, const ProducerWorldContext& context);

// Game-thread only. EnemySync owns the sole WorldBridge packet drain.
void Install(uintptr_t exeBase, LogFn log, SendFn send);
void Reset(); // clear session data, retaining last observed role
void Tick(std::uint32_t frame, bool host, bool client);
bool HandlePacket(PacketType type, ByteReader& reader);
bool HostReady(); // full snapshot enqueued, no unsent progress packet
// Fresh complete host allow-list snapshot, bracketed by native lifecycle and
// armed generation checks. False leaves both outputs unchanged. Zero-valued
// allowed bytes are valid; an unreadable/uninitialized SAVE is unavailable.
bool CaptureFull(ProgressUpdate& output, std::uint32_t& hash);
// Compare fresh actual masked bytes with a complete allowed snapshot; no write.
bool MatchesFull(const ProgressUpdate& expected, std::uint32_t& hash);
// Stage an already decoded complete snapshot. True means exact desired bytes
// and version were accepted for the captured generation, not merely recognized.
bool StageFull(const ProgressUpdate& update, std::uint32_t generation);
// Compare the accepted desired bytes without reading/applying native SAVE.
// Version-only changes are not material; malformed expected full content fails.
bool DesiredMatchesFull(const ProgressUpdate& expected, std::uint32_t generation);
bool ReadHash(std::uint32_t& hash); // actual live masked bytes
// Read-only checked equality against the current complete client desired state.
bool ClientConverged(std::uint32_t generation);
// Warp calls at its safe pre-load boundary, before native room initialization.
// False defers loading until the complete desired snapshot is applied safely.
bool ApplyAtRoomBoundary(std::uint32_t generation);
} // namespace kh2coop::inject::progresssync
