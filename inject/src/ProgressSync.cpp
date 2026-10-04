#include "ProgressSync.hpp"
#include "EnemySync.hpp"
#include "Warp.hpp"
#include "kh2coop/KH2Offsets.hpp"
#include "kh2coop/ProgressAllowList.hpp"

#include <Windows.h>
#include <array>
#include <cstring>
#include <exception>
#include <utility>

namespace kh2coop::inject::progresssync {
namespace {
constexpr uintptr_t SAVE_RVA = 0x09A98B0;
constexpr std::size_t SAVE_END = 0x23E0;
constexpr std::size_t PROGRESS_BYTES = 0x1C80 + 0x260 + 0x98 + 0x34;
using Save = std::array<std::uint8_t, SAVE_END>;
using Personal = std::array<std::uint8_t, 0xE04 + 0x140 + 4 + 4>;
enum class Role { Off, Host, Client };

uintptr_t g_exeBase = 0;
LogFn g_log = nullptr;
SendFn g_send = nullptr;
Role g_role = Role::Off;
const auto g_allow = verifiedProgressAllowList();
const ProgressMirror g_policy(g_allow);
Save g_baseline {}, g_queuedSave {}, g_desired {};
std::vector<std::uint8_t> g_packet;
ProducerWorldContext g_packetContext {};
std::uint32_t g_version = 0, g_queuedVersion = 0;
std::uint32_t g_desiredGeneration = 0;
bool g_hostFull = false, g_queuedFull = false, g_clientFull = false;
bool g_hostSampleReady = false;
bool g_desiredApplied = false, g_sendBlocked = false, g_personalFailure = false;
std::size_t g_queuedSpans = 0, g_queuedBytes = 0;

// SEH stays in POD-only helpers to avoid MSVC /EHsc C2712. Failed reads never
// substitute zeros for live data. None of the STL-owning callers use __try.
bool ReadMemory(uintptr_t address, void* output, std::size_t size) {
    __try {
        std::memcpy(output, reinterpret_cast<const void*>(address), size);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool WriteMasked(uintptr_t address, std::uint8_t value, std::uint8_t mask, std::uint32_t generation) {
    __try {
        if (!generation || enemysync::WorldSessionGeneration() != generation) return false;
        auto* target = reinterpret_cast<volatile std::uint8_t*>(address);
        *target = static_cast<std::uint8_t>((*target & ~mask) | (value & mask));
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool ValidSave() {
    char magic[4] {};
    return g_exeBase != 0 && ReadMemory(g_exeBase + SAVE_RVA, magic, sizeof(magic)) &&
           std::memcmp(magic, "KH2J", sizeof(magic)) == 0;
}

bool SafeGameplay() {
    // TransitionPending also reads native IN_FIELD. Keep that inherited read
    // inside the same POD-only fault boundary as the remaining checked fields.
    __try {
    if (!g_exeBase || warp::TransitionPending() || warp::LoadSerial() == 0) return false;
    std::int32_t controllable = -1, cutsceneState = -1;
    uintptr_t eventContext = 0;
    std::uint8_t inField = 0, menu = 0;
    return ReadMemory(g_exeBase + offsets::CONTROLLABLE, &controllable, sizeof(controllable)) &&
           ReadMemory(g_exeBase + offsets::CUTSCENE_STATE, &cutsceneState, sizeof(cutsceneState)) &&
           ReadMemory(g_exeBase + offsets::EVENT_CONTEXT, &eventContext, sizeof(eventContext)) &&
           ReadMemory(g_exeBase + offsets::IN_FIELD, &inField, sizeof(inField)) &&
           ReadMemory(g_exeBase + offsets::OPEN_MENU, &menu, sizeof(menu)) &&
           controllable == 0 && cutsceneState == 0 && eventContext == 0 &&
           inField != 0 && menu == 0xFF;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool ReadSave(Save& save) {
    if (!ValidSave()) return false;
    save.fill(0);
    for (const auto& range : g_allow) {
        if (!ReadMemory(g_exeBase + SAVE_RVA + range.offset,
                        save.data() + range.offset, range.length)) return false;
        for (auto o = range.offset; o < range.offset + range.length; ++o)
            save[o] &= verifiedProgressByteMask(o);
    }
    return true;
}

void HashValue(std::uint32_t& hash, std::uint32_t value, unsigned bytes) {
    for (unsigned i = 0; i < bytes; ++i) {
        hash = (hash ^ (value & 0xFFu)) * 16777619u;
        value >>= 8;
    }
}

std::uint32_t HashSave(const Save& save) {
    std::uint32_t hash = 2166136261u;
    HashValue(hash, 0x3150484Bu, 4); // "KHP1", little-endian fixed schema
    HashValue(hash, static_cast<std::uint32_t>(PROGRESS_BYTES), 4);
    for (const auto& range : g_allow) {
        for (auto o = range.offset; o < range.offset + range.length; ++o) {
            const auto mask = verifiedProgressByteMask(o);
            HashValue(hash, o, 4);
            HashValue(hash, mask, 1);
            HashValue(hash, save[o] & mask, 1);
        }
    }
    return hash;
}

bool ReadPersonal(Personal& bytes) {
    constexpr ProgressRange ranges[] = {{0x24F0, 0xE04}, {0x3580, 0x140},
                                         {0x2440, 4}, {0x36E0, 4}};
    std::size_t pos = 0;
    for (const auto& range : ranges) {
        if (!ReadMemory(g_exeBase + SAVE_RVA + range.offset,
                        bytes.data() + pos, range.length)) return false;
        pos += range.length;
    }
    return true;
}

std::uint32_t HashPersonal(const Personal& bytes) {
    std::uint32_t hash = 2166136261u;
    for (const auto value : bytes) HashValue(hash, value, 1);
    return hash;
}

std::size_t SpanBytes(const std::vector<ProgressSpan>& spans) {
    std::size_t count = 0;
    for (const auto& span : spans) count += span.bytes.size();
    return count;
}

bool Newer(std::uint32_t version, std::uint32_t previous) {
    const auto advance = version - previous;
    return advance != 0 && advance < 0x80000000u;
}

bool FlushHostPacket() {
    if (g_packet.empty()) return true;
    if (!enemysync::WorldContextCurrent(g_packetContext)) {
        g_packet.clear(); g_packetContext = {}; g_hostFull = false;
        g_hostSampleReady = false;
        return false;
    }
    if (!g_send || !g_send(g_packet, g_packetContext)) {
        if (!g_sendBlocked && g_log)
            g_log("[progresssync] send deferred version=%u full=%u", g_queuedVersion,
                  static_cast<unsigned>(g_queuedFull));
        g_sendBlocked = true;
        return false;
    }
    g_version = g_queuedVersion;
    g_baseline = g_queuedSave;
    g_hostFull = g_hostFull || g_queuedFull;
    if (g_log)
        g_log("[progresssync] host %s version=%u spans=%zu bytes=%zu hash=%08X",
              g_queuedFull ? "full" : "delta", g_version, g_queuedSpans,
              g_queuedBytes, HashSave(g_baseline));
    g_packet.clear();
    g_packetContext = {};
    g_sendBlocked = false;
    return true;
}

bool ApplyFailure(const char* reason) {
    if (g_log) g_log("[progresssync] apply failed version=%u reason=%s", g_version, reason);
    return false;
}
} // namespace

void Install(uintptr_t exeBase, LogFn log, SendFn send) {
    g_exeBase = exeBase;
    g_log = log;
    g_send = send;
    g_role = Role::Off;
    Reset();
    if (g_log) g_log("[progresssync] installed save_rva=%llX bytes=%zu schema=KHP1",
                     static_cast<unsigned long long>(SAVE_RVA), PROGRESS_BYTES);
}

void Reset() {
    // A reset marker may precede a full snapshot in the same FIFO drain,
    // before Tick observes the role again. Retain the last role here.
    g_baseline.fill(0);
    g_queuedSave.fill(0);
    g_desired.fill(0);
    g_packet.clear();
    g_packetContext = {};
    g_version = g_queuedVersion = 0;
    g_desiredGeneration = 0;
    g_hostFull = g_queuedFull = g_clientFull = false;
    g_hostSampleReady = false;
    g_desiredApplied = g_sendBlocked = g_personalFailure = false;
    g_queuedSpans = g_queuedBytes = 0;
}

void Tick(std::uint32_t frame, bool host, bool client) {
    (void)frame;
    const Role role = host ? Role::Host : client ? Role::Client : Role::Off;
    if (role != g_role) {
        Reset();
        g_role = role;
    }
    g_hostSampleReady = false;
    if (role != Role::Host || !SafeGameplay()) return;
    Save live {};
    if (!ReadSave(live)) return;
    if (!FlushHostPacket()) return;
    const bool full = !g_hostFull;
    auto spans = full ? g_policy.snapshot(live.data(), live.size())
                      : g_policy.diff(g_baseline.data(), live.data(), live.size());
    if (spans.empty()) {
        g_hostSampleReady = true;
        return;
    }
    auto version = g_version + 1;
    if (version == 0) ++version;
    // 8108 allowed bytes: even a maximally fragmented diff fits one 16-bit
    // packet. The full snapshot is atomic; no partial-full assembly is needed.
    ProducerWorldContext context;
    if (!enemysync::CaptureWorldContext(context)) return;
    g_packet = encode(ProgressUpdate {version, full, spans});
    g_packetContext = context;
    g_queuedSave = live;
    g_queuedVersion = version;
    g_queuedFull = full;
    g_queuedSpans = spans.size();
    g_queuedBytes = SpanBytes(spans);
    g_hostSampleReady = FlushHostPacket();
}

bool HandlePacket(PacketType type, ByteReader& reader) {
    if (type != PacketType::ProgressUpdate) return false;
    const auto generation = enemysync::WorldSessionGeneration();
    if (!generation) return true;
    // Only the active-client drain calls this. Establish role before Tick so
    // it cannot reset away the first full packet of a freshly joined session.
    if (g_role != Role::Client) {
        Reset();
        g_role = Role::Client;
    }
    ProgressUpdate update;
    try {
        read(reader, update);
    } catch (const std::exception&) {
        if (g_log) g_log("[progresssync] rejected malformed packet");
        return true;
    }
    if (!reader.atEnd() || update.version == 0 ||
        (g_clientFull && update.version != g_version && !Newer(update.version, g_version)) ||
        (!update.full && (!g_clientFull || !Newer(update.version, g_version)))) return true;

    Save desired = update.full ? Save {} : g_desired;
    std::array<bool, SAVE_END> covered {};
    std::size_t count = 0;
    for (const auto& span : update.spans) {
        if (span.offset >= SAVE_END || span.bytes.size() > SAVE_END - span.offset) return true;
        for (std::size_t i = 0; i < span.bytes.size(); ++i) {
            const auto o = span.offset + static_cast<std::uint32_t>(i);
            const auto mask = verifiedProgressByteMask(o);
            if (!mask || covered[o]) return true;
            covered[o] = true;
            ++count;
            desired[o] = span.bytes[i] & mask;
        }
    }
    if (update.full && count != PROGRESS_BYTES) {
        if (g_log) g_log("[progresssync] rejected incomplete full version=%u bytes=%zu expected=%zu",
                         update.version, count, PROGRESS_BYTES);
        return true;
    }
    // Same-version full relay resyncs are idempotent. Preserve applied status
    // only if both version and all desired bytes really remained unchanged.
    const bool unchanged = g_clientFull && update.version == g_version && desired == g_desired;
    if (enemysync::WorldSessionGeneration() != generation) return true;
    g_desired = desired;
    g_desiredGeneration = generation;
    g_version = update.version;
    g_clientFull = true;
    if (!unchanged) g_desiredApplied = false;
    if (g_log)
        g_log("[progresssync] client %s version=%u spans=%zu bytes=%zu complete=1",
              update.full ? "full" : "delta", g_version, update.spans.size(), count);
    return true;
}

bool HostReady() {
    return g_role == Role::Host && g_hostFull && g_hostSampleReady && g_packet.empty();
}

bool DesiredMatchesFull(const ProgressUpdate& expected, std::uint32_t generation) {
    if (!generation || generation != enemysync::WorldSessionGeneration() ||
        g_desiredGeneration != generation || !g_clientFull || !expected.full || !expected.version) return false;
    std::array<bool, SAVE_END> covered {};
    std::size_t count = 0;
    for (const auto& span : expected.spans) {
        if (span.offset >= SAVE_END || span.bytes.size() > SAVE_END - span.offset) return false;
        for (std::size_t i = 0; i < span.bytes.size(); ++i) {
            const auto offset = span.offset + static_cast<std::uint32_t>(i);
            const auto mask = verifiedProgressByteMask(offset);
            if (!mask || covered[offset] || (span.bytes[i] & ~mask) || g_desired[offset] != span.bytes[i]) return false;
            covered[offset] = true; ++count;
        }
    }
    return count == PROGRESS_BYTES && generation == enemysync::WorldSessionGeneration();
}

bool StageFull(const ProgressUpdate& update, std::uint32_t generation) {
    if (!generation || generation != enemysync::WorldSessionGeneration() ||
        !update.full || !update.version) return false;
    Save expected {};
    std::array<bool, SAVE_END> covered {};
    std::size_t count = 0;
    for (const auto& span : update.spans) {
        if (span.offset >= SAVE_END || span.bytes.size() > SAVE_END - span.offset) return false;
        for (std::size_t i = 0; i < span.bytes.size(); ++i) {
            const auto offset = span.offset + static_cast<std::uint32_t>(i);
            const auto mask = verifiedProgressByteMask(offset);
            if (!mask || covered[offset] || (span.bytes[i] & ~mask)) return false;
            covered[offset] = true; ++count; expected[offset] = span.bytes[i];
        }
    }
    if (count != PROGRESS_BYTES) return false;
    const auto packet = encode(update);
    const std::uint8_t* payload = nullptr; std::size_t size = 0;
    decodePacketHeader(packet.data(), packet.size(), payload, size);
    ByteReader reader(payload, size);
    HandlePacket(PacketType::ProgressUpdate, reader);
    return generation == enemysync::WorldSessionGeneration() && g_role == Role::Client &&
           g_clientFull && g_desiredGeneration == generation && g_version == update.version &&
           g_desired == expected;
}

bool CaptureFull(ProgressUpdate& output, std::uint32_t& hash) {
    const auto generation = enemysync::WorldSessionGeneration();
    if (!generation || !HostReady() || !g_version || g_personalFailure || !SafeGameplay()) return false;
    const auto version = g_version;
    const auto transition = warp::TransitionSerial();
    const auto load = warp::LoadSerial();
    Save before {}, after {};
    // Read the native allow-list twice, not the cached baseline or desired
    // mirror. Masking excludes personal/unverified bits from this contract.
    if (!ReadSave(before) || enemysync::WorldSessionGeneration() != generation ||
        !ReadSave(after) || before != after) return false;
    ProgressUpdate captured {version, true, g_policy.snapshot(after.data(), after.size())};
    if (SpanBytes(captured.spans) != PROGRESS_BYTES || !HostReady() || g_version != version ||
        warp::TransitionSerial() != transition || warp::LoadSerial() != load ||
        !SafeGameplay() || enemysync::WorldSessionGeneration() != generation) return false;
    const auto capturedHash = HashSave(after);
    output = std::move(captured);
    hash = capturedHash;
    return true;
}

bool ReadHash(std::uint32_t& hash) {
    if (!SafeGameplay() || g_personalFailure || g_role == Role::Off ||
        (g_role == Role::Host && !HostReady()) ||
        (g_role == Role::Client && !g_clientFull)) return false;
    Save live {};
    if (!ReadSave(live)) return false;
    if (g_role == Role::Client && !g_desiredApplied) {
        if (live != g_desired) return false; // new version waits for a boundary
        g_desiredApplied = true; // already identical without any writes
    }
    // Once applied, subsequent native drift/tampering MUST change the hash.
    // Do not hide it by checking desired equality on every hash publication.
    hash = HashSave(live);
    return true;
}

bool MatchesFull(const ProgressUpdate& expected, std::uint32_t& hash) {
    const auto generation = enemysync::WorldSessionGeneration();
    if (!generation || !expected.full || !expected.version || !SafeGameplay() || g_personalFailure) return false;
    Save desired {}, before {}, after {};
    std::array<bool, SAVE_END> covered {};
    std::size_t count = 0;
    for (const auto& span : expected.spans) {
        if (span.offset >= SAVE_END || span.bytes.size() > SAVE_END - span.offset) return false;
        for (std::size_t i = 0; i < span.bytes.size(); ++i) {
            const auto offset = span.offset + static_cast<std::uint32_t>(i);
            const auto mask = verifiedProgressByteMask(offset);
            if (!mask || covered[offset] || (span.bytes[i] & ~mask)) return false;
            covered[offset] = true; ++count; desired[offset] = span.bytes[i];
        }
    }
    const auto transition = warp::TransitionSerial(), load = warp::LoadSerial();
    if (count != PROGRESS_BYTES || !ReadSave(before) || before != desired ||
        !ReadSave(after) || after != before || !SafeGameplay() ||
        transition != warp::TransitionSerial() || load != warp::LoadSerial() ||
        enemysync::WorldSessionGeneration() != generation) return false;
    hash = HashSave(after);
    return true;
}

bool ApplyAtRoomBoundary(std::uint32_t generation) {
    if (!generation || generation != g_desiredGeneration ||
        enemysync::WorldSessionGeneration() != generation ||
        g_role != Role::Client || !g_clientFull || g_personalFailure || !SafeGameplay()) return false;
    Save live {};
    if (!ReadSave(live)) return ApplyFailure("save-read");
    const auto spans = g_policy.diff(live.data(), g_desired.data(), live.size());
    Personal before {}, after {};
    if (!ReadPersonal(before)) return ApplyFailure("personal-before-read");
    bool wrote = true;
    for (const auto& span : spans) {
        for (std::size_t i = 0; i < span.bytes.size(); ++i) {
            const auto o = span.offset + static_cast<std::uint32_t>(i);
            if (!WriteMasked(g_exeBase + SAVE_RVA + o, span.bytes[i], verifiedProgressByteMask(o), generation)) {
                wrote = false;
                break;
            }
        }
        if (!wrote) break;
    }
    // Bracket ONLY our writes. Native room init has not yet run, so its own
    // stat/reward changes cannot contaminate this D8 negative control.
    if (!ReadPersonal(after)) {
        g_personalFailure = true;
        return ApplyFailure("personal-after-read");
    }
    const bool unchanged = before == after; // exact comparison, not just hash
    if (!unchanged) g_personalFailure = true;
    const bool readBack = ReadSave(live);
    const bool applied = wrote && readBack && live == g_desired &&
        enemysync::WorldSessionGeneration() == generation;
    if (g_log)
        g_log("[progresssync] apply version=%u spans=%zu bytes=%zu hash=%08X personal_before=%08X personal_after=%08X personal_unchanged=%u",
              g_version, spans.size(), SpanBytes(spans), readBack ? HashSave(live) : 0,
              HashPersonal(before), HashPersonal(after), static_cast<unsigned>(unchanged));
    if (!unchanged) return ApplyFailure("personal-changed");
    if (!applied) return ApplyFailure(wrote ? "save-verification" : "save-write");
    g_desiredApplied = true;
    return true;
}
} // namespace kh2coop::inject::progresssync
