#pragma once
#include "kh2coop/Codec.hpp"
#include <cstdint>
#include <vector>

namespace kh2coop::inject::progresssync {
using LogFn = void (*)(const char* fmt, ...);
using SendFn = bool (*)(const std::vector<std::uint8_t>& packet);

// Game-thread only. EnemySync owns the sole WorldBridge packet drain.
void Install(uintptr_t exeBase, LogFn log, SendFn send);
void Reset(); // clear session data, retaining last observed role
void Tick(std::uint32_t frame, bool host, bool client);
bool HandlePacket(PacketType type, ByteReader& reader);
bool HostReady(); // full snapshot enqueued, no unsent progress packet
bool ReadHash(std::uint32_t& hash); // actual live masked bytes
// Warp calls at its safe pre-load boundary, before native room initialization.
// False defers loading until the complete desired snapshot is applied safely.
bool ApplyAtRoomBoundary();
} // namespace kh2coop::inject::progresssync
