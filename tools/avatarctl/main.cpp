// avatarctl — drive and inspect a KH2 instance's AvatarBridge without a
// network (VUH-1490/1491).
//
//   avatarctl record --pid N --out FILE [--seconds S]
//       Capture the DLL's local avatar stream (every new frame) to FILE.
//   avatarctl replay --pid N --in FILE [--puppet 0|1] [--offset X,Y,Z] [--loop]
//       Play a recording into a puppet slot at its recorded pace (times are
//       re-based on now), optionally offset so it doesn't overlap the player.
//   avatarctl synth --pid N [--puppet 0|1] [--seconds S] [--center X,Y,Z]
//                   [--radius R] [--motion ID] [--world 0..255 --room 0..255]
//       Drive a puppet around a circle (run speed) — tests the puppet driver
//       before avatar capture exists.
//   avatarctl fake-local --pid N [--seconds S] [--center X,Y,Z]
//       Pretend to be the DLL: publish a local avatar running a circle, to
//       exercise record/replay or the runtime's network path without KH2.
//   avatarctl peek --pid N [--seconds S]
//       Print the bridge contents (local avatar and both puppet slots).
//   avatarctl observe --pid N [--samples 1..30] [--interval-ms 0..1000]
//       Read existing bridges only; bracket fresh pose reads with world identity.
//
// File format: "KH2AVS1\0", u32 record size, then raw AvatarState records.
// Prints one JSON object, like kh2ctl.

#include "kh2coop/AvatarBridge.hpp"
#include "kh2coop/Codec.hpp"
#include "kh2coop/WorldBridge.hpp"
#include "kh2coop/AvatarPositionFaultChannel.hpp"
#include "kh2coop/AvatarSynth.hpp"

#include <timeapi.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using namespace kh2coop;

namespace {

constexpr char kMagic[8] = {'K', 'H', '2', 'A', 'V', 'S', '1', '\0'};
constexpr std::uint64_t kFrameMs = 16;

std::uint64_t nowMs() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
}

// ~1 ms precision once main() has called timeBeginPeriod(1).
void sleepUntil(std::uint64_t due) {
    while (nowMs() < due) std::this_thread::sleep_for(std::chrono::milliseconds(1));
}

std::string jsonEscape(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '\\' || c == '"') out += '\\';
        out += c;
    }
    return out;
}

std::optional<std::string> option(std::vector<std::string>& args, const std::string& name) {
    for (std::size_t i = 0; i + 1 < args.size(); ++i) {
        if (args[i] == name) {
            std::string v = args[i + 1];
            args.erase(args.begin() + static_cast<long>(i), args.begin() + static_cast<long>(i) + 2);
            return v;
        }
    }
    return std::nullopt;
}

bool flag(std::vector<std::string>& args, const std::string& name) {
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (args[i] == name) {
            args.erase(args.begin() + static_cast<long>(i));
            return true;
        }
    }
    return false;
}

Vec3 parseVec(const std::string& s) {
    Vec3 v;
    if (std::sscanf(s.c_str(), "%f,%f,%f", &v.x, &v.y, &v.z) != 3) {
        throw std::runtime_error("expected X,Y,Z: " + s);
    }
    return v;
}

void requireEmpty(const std::vector<std::string>& args) {
    if (!args.empty()) throw std::runtime_error("unexpected argument: " + args.front());
}

void openBridge(std::vector<std::string>& args, AvatarBridge& bridge) {
    const auto pid = option(args, "--pid");
    if (!pid) throw std::runtime_error("--pid is required");
    if (!bridge.Open(static_cast<DWORD>(std::stoul(*pid)))) {
        throw std::runtime_error("could not open the avatar bridge for pid " + *pid);
    }
}

int puppetIndex(std::vector<std::string>& args) {
    const int idx = std::stoi(option(args, "--puppet").value_or("0"));
    if (idx < 0 || idx >= AVATAR_BRIDGE_PUPPETS) throw std::runtime_error("--puppet must be 0 or 1");
    return idx;
}

// Circle path at run speed, as a pose at time t (seconds).
AvatarState circlePose(float t, const Vec3& center, float radius, std::uint32_t motion) {
    return avatarsynth::Circle(t,center,radius,motion);
}

std::string avatarJson(const AvatarState& a) {
    std::ostringstream o;
    o << "{\"pos\":[" << a.position.x << "," << a.position.y << "," << a.position.z << "],"
      << "\"rot\":" << a.rotationY << ",\"motion\":" << a.motionId << ",\"motionTime\":"
      << a.motionTime << ",\"room\":[" << a.worldId << "," << a.roomId << "],\"flags\":"
      << static_cast<int>(a.flags) << ",\"hp\":" << a.hp << ",\"owner\":"
      << static_cast<int>(a.ownerSlot) << "}";
    return o.str();
}

int cmdRecord(std::vector<std::string> args) {
    AvatarBridge bridge;
    openBridge(args, bridge);
    const auto out = option(args, "--out");
    const double seconds = std::stod(option(args, "--seconds").value_or("10"));
    requireEmpty(args);
    if (!out) throw std::runtime_error("--out is required");

    std::ofstream file(*out, std::ios::binary);
    file.write(kMagic, sizeof(kMagic));
    const std::uint32_t recSize = sizeof(AvatarState);
    file.write(reinterpret_cast<const char*>(&recSize), sizeof(recSize));

    std::uint64_t frames = 0;
    const auto end = nowMs() + static_cast<std::uint64_t>(seconds * 1000.0);
    AvatarState a;
    while (nowMs() < end) {
        if (bridge.TryReadLocal(a)) {
            a.serverTimeMs = nowMs(); // capture time; replay re-bases it
            file.write(reinterpret_cast<const char*>(&a), sizeof(a));
            ++frames;
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    std::cout << "{\"ok\":" << (frames > 0 ? "true" : "false") << ",\"command\":\"record\","
              << "\"frames\":" << frames << ",\"seconds\":" << seconds
              << ",\"fps\":" << (frames / seconds) << ",\"out\":\"" << jsonEscape(*out)
              << "\"}\n";
    return frames > 0 ? 0 : 1;
}

std::vector<AvatarState> loadRecording(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    char magic[8] = {};
    std::uint32_t recSize = 0;
    file.read(magic, sizeof(magic));
    file.read(reinterpret_cast<char*>(&recSize), sizeof(recSize));
    if (!file || std::memcmp(magic, kMagic, sizeof(kMagic)) != 0 || recSize != sizeof(AvatarState)) {
        throw std::runtime_error("not an avatar recording (or a different AvatarState layout)");
    }
    std::vector<AvatarState> frames;
    AvatarState a;
    while (file.read(reinterpret_cast<char*>(&a), sizeof(a))) frames.push_back(a);
    return frames;
}

int cmdReplay(std::vector<std::string> args) {
    AvatarBridge bridge;
    openBridge(args, bridge);
    const int idx = puppetIndex(args);
    const auto in = option(args, "--in");
    const auto offset = option(args, "--offset");
    const bool loop = flag(args, "--loop");
    requireEmpty(args);
    if (!in) throw std::runtime_error("--in is required");

    const auto frames = loadRecording(*in);
    if (frames.empty()) throw std::runtime_error("recording is empty");
    const Vec3 shift = offset ? parseVec(*offset) : Vec3 {};
    std::uint64_t published = 0;
    do {
        const auto start = nowMs();
        const auto t0 = frames.front().serverTimeMs;
        for (const auto& f : frames) {
            sleepUntil(start + (f.serverTimeMs - t0));
            PuppetPose p;
            p.active = 1;
            p.provenance.producer = PuppetProducer::Standalone;
            p.pose = f;
            p.pose.serverTimeMs = nowMs();
            p.pose.position = {f.position.x + shift.x, f.position.y + shift.y,
                               f.position.z + shift.z};
            bridge.PublishPuppet(idx, p);
            ++published;
        }
    } while (loop);
    bridge.PublishPuppet(idx, PuppetPose {});
    std::cout << "{\"ok\":true,\"command\":\"replay\",\"puppet\":" << idx
              << ",\"frames\":" << frames.size() << ",\"published\":" << published
              << ",\"durationMs\":" << (frames.back().serverTimeMs - frames.front().serverTimeMs)
              << "}\n";
    return 0;
}

int cmdFaultArm(std::vector<std::string> args) {
    const auto pid=static_cast<DWORD>(std::stoul(option(args,"--pid").value_or("0")));
    const auto activation=parseVec(option(args,"--activation-target").value_or("nan,nan,nan"));
    requireEmpty(args);
    if (!pid || !avatarfault::FiniteTarget(activation)) throw std::runtime_error("fault-arm requires pid and finite bounded activation-target");
    wchar_t name[96] {}; avatarfault::MappingName(name,96,pid);
    const auto mapping=OpenFileMappingW(FILE_MAP_ALL_ACCESS,FALSE,name);
    if (!mapping) throw std::runtime_error("diagnostic mapping absent (default off)");
    auto* shared=static_cast<avatarfault::Shared*>(MapViewOfFile(mapping,FILE_MAP_ALL_ACCESS,0,0,sizeof(avatarfault::Shared)));
    if (!shared) { CloseHandle(mapping); throw std::runtime_error("diagnostic mapping unreadable"); }
    avatarfault::Offer offer {}; const auto now=GetTickCount64();
    if (!avatarfault::Snapshot(shared->offerSequence,shared->offer,offer) || offer.magic!=avatarfault::Magic ||
        offer.version!=1 || offer.size!=sizeof(offer) || offer.pid!=pid || !offer.creation || !offer.eligible ||
        now<offer.tick || now-offer.tick>500) {
        UnmapViewOfFile(shared);CloseHandle(mapping);throw std::runtime_error("fresh eligible native Donald offer required");
    }
    avatarfault::Request request {offer,(now<<16)^GetCurrentProcessId(),now,now+avatarfault::DormantMs,
        activation,avatarfault::Frames,avatarfault::ActiveMs};
    if (!request.nonce) request.nonce=1;
    if (InterlockedCompareExchange(&shared->requestSequence,1,0)!=0) {
        UnmapViewOfFile(shared);CloseHandle(mapping);throw std::runtime_error("fault request already consumed; renewal refused");
    }
    std::memcpy(&shared->request,&request,sizeof(request)); MemoryBarrier();InterlockedExchange(&shared->requestSequence,2);
    UnmapViewOfFile(shared);CloseHandle(mapping);
    std::cout << std::setprecision(std::numeric_limits<float>::max_digits10) << "{\"ok\":true,\"command\":\"fault-arm\",\"pid\":" << pid << ",\"nonce\":" << request.nonce
        << ",\"tick\":" << now << ",\"deadline\":" << request.deadline << ",\"actor\":" << offer.binding.actor
        << ",\"handle\":" << offer.binding.handle << ",\"transition\":" << offer.binding.transition
        << ",\"load\":" << offer.binding.load << ",\"puppetIndex\":" << unsigned(offer.binding.puppetIndex)
        << ",\"activation\":[" << activation.x << ',' << activation.y << ',' << activation.z << "]}\n";
    return 0;
}
int cmdSynth(std::vector<std::string> args) {
    AvatarBridge bridge;
    openBridge(args, bridge);
    const int idx = puppetIndex(args);
    const double seconds = std::stod(option(args, "--seconds").value_or("10"));
    const Vec3 center = parseVec(option(args, "--center").value_or("0,0,0"));
    const float radius = std::stof(option(args, "--radius").value_or("300"));
    const auto motion = static_cast<std::uint32_t>(std::stoul(option(args, "--motion").value_or("2")));
    const auto worldOption=option(args,"--world"),roomOption=option(args,"--room");
    const auto roomNumber=[](const std::optional<std::string>& value)->unsigned {
        if (!value) return 0;
        std::size_t consumed=0;const auto parsed=std::stoul(*value,&consumed,0);
        if (consumed!=value->size() || parsed>255) throw std::runtime_error("synth room IDs must be integers 0..255");
        return static_cast<unsigned>(parsed);
    };
    const auto world=roomNumber(worldOption),room=roomNumber(roomOption);
    if (!avatarsynth::ValidRoomOptions(bool(worldOption),bool(roomOption),world,room))
        throw std::runtime_error("synth --world and --room must be supplied together");
    const auto teleportAfter=option(args,"--teleport-after-ms"), teleportDelta=option(args,"--teleport-delta");
    const auto teleportMs=teleportAfter?std::stoull(*teleportAfter):0;
    const Vec3 delta=teleportDelta?parseVec(*teleportDelta):Vec3{};
    if (bool(teleportAfter)!=bool(teleportDelta) || (teleportAfter &&
        (!teleportMs || teleportMs>60000 || !avatarfault::FiniteTarget(delta) ||
         std::abs(delta.x)>100 || std::abs(delta.y)>100 || std::abs(delta.z)>100)))
        throw std::runtime_error("teleport requires paired options: delay 1..60000 ms, finite delta <=100 per axis");
    requireEmpty(args);

    const auto start = nowMs();
    const auto end = start + static_cast<std::uint64_t>(seconds * 1000.0);
    std::uint64_t frames = 0;
    for (std::uint64_t due = start; due < end; due += kFrameMs) {
        sleepUntil(due);
        const auto sample=avatarsynth::Puppet(static_cast<float>(nowMs()-start)/1000.0f,center,radius,motion,
            static_cast<std::uint16_t>(world),static_cast<std::uint16_t>(room));
        PuppetPose p; p.active=sample.active;p.pose=sample.pose;p.provenance=sample.provenance;
        p.pose.serverTimeMs = nowMs();
        if (teleportMs && p.pose.serverTimeMs-start>=teleportMs) {
            p.pose.position.x+=delta.x;p.pose.position.y+=delta.y;p.pose.position.z+=delta.z;
        }
        bridge.PublishPuppet(idx, p);
        ++frames;
    }
    bridge.PublishPuppet(idx, PuppetPose {});
    std::cout << "{\"ok\":true,\"command\":\"synth\",\"puppet\":" << idx
              << ",\"frames\":" << frames << "}\n";
    return 0;
}

int cmdFakeLocal(std::vector<std::string> args) {
    AvatarBridge bridge;
    openBridge(args, bridge);
    const double seconds = std::stod(option(args, "--seconds").value_or("10"));
    const Vec3 center = parseVec(option(args, "--center").value_or("0,0,0"));
    requireEmpty(args);
    const auto start = nowMs();
    const auto end = start + static_cast<std::uint64_t>(seconds * 1000.0);
    std::uint64_t frames = 0;
    for (std::uint64_t due = start; due < end; due += kFrameMs) {
        sleepUntil(due);
        AvatarState a = circlePose(static_cast<float>(nowMs() - start) / 1000.0f, center, 300.0f, 2);
        a.worldId = 4;
        a.roomId = 0x1A;
        a.hp = a.maxHp = 100;
        bridge.PublishLocal(a);
        ++frames;
    }
    std::cout << "{\"ok\":true,\"command\":\"fake-local\",\"frames\":" << frames << "}\n";
    return 0;
}

// A repeated bracket is a bounded observation, not a global header seqlock.
struct ObservedWorld {
    PuppetAuthority authority {};
    std::uint64_t deliverySerial {0};
    std::array<std::uint64_t, 3> peerDeliverySerials {};

    bool operator==(const ObservedWorld& other) const {
        return authority.mode == other.authority.mode &&
            authority.localSlot == other.authority.localSlot &&
            authority.generation == other.authority.generation &&
            authority.connectionIds == other.authority.connectionIds &&
            deliverySerial == other.deliverySerial &&
            peerDeliverySerials == other.peerDeliverySerials;
    }
};

ObservedWorld observeWorld(const WorldBridge& bridge) {
    ObservedWorld out;
    out.authority.mode = bridge.GetPuppetAuthorityMode();
    out.authority.localSlot = bridge.LocalSlot();
    out.authority.generation = bridge.SessionGeneration();
    out.deliverySerial = bridge.DeliverySerial();
    for (std::uint8_t i = 0; i < 3; ++i) {
        out.authority.connectionIds[i] = bridge.ConnectionId(i);
        out.peerDeliverySerials[i] = bridge.PeerDeliverySerial(i);
    }
    return out;
}

void writeObservedWorld(std::ostream& out, const ObservedWorld& world) {
    out << "{\"localSlot\":" << static_cast<unsigned>(world.authority.localSlot)
        << ",\"generation\":" << world.authority.generation
        << ",\"deliverySerial\":" << world.deliverySerial
        << ",\"authorityMode\":" << static_cast<unsigned>(world.authority.mode)
        << ",\"connectionIds\":[";
    for (unsigned i = 0; i < 3; ++i) {
        if (i) out << ',';
        out << world.authority.connectionIds[i];
    }
    out << "],\"peerDeliverySerials\":[";
    for (unsigned i = 0; i < 3; ++i) {
        if (i) out << ',';
        out << world.peerDeliverySerials[i];
    }
    out << "]}";
}

void writeObservedFloat(std::ostream& out, float value) {
    if (std::isfinite(value)) out << value;
    else out << "null";
}

void writeObservedVector(std::ostream& out, const Vec3& value) {
    out << '[';
    writeObservedFloat(out, value.x);
    out << ',';
    writeObservedFloat(out, value.y);
    out << ',';
    writeObservedFloat(out, value.z);
    out << ']';
}

void writeObservedAvatar(std::ostream& out, const AvatarState& value) {
    const bool finite = std::isfinite(value.position.x) && std::isfinite(value.position.y) &&
        std::isfinite(value.position.z) && std::isfinite(value.velocity.x) &&
        std::isfinite(value.velocity.y) && std::isfinite(value.velocity.z) &&
        std::isfinite(value.rotationY) && std::isfinite(value.motionTime) &&
        std::isfinite(value.motionSpeed);
    out << "{\"seq\":" << value.seq << ",\"serverTimeMs\":" << value.serverTimeMs
        << ",\"ownerSlot\":" << static_cast<unsigned>(value.ownerSlot)
        << ",\"character\":" << static_cast<unsigned>(value.character)
        << ",\"colorVariant\":" << static_cast<unsigned>(value.colorVariant)
        << ",\"worldId\":" << value.worldId << ",\"roomId\":" << value.roomId
        << ",\"finite\":" << (finite ? "true" : "false") << ",\"position\":";
    writeObservedVector(out, value.position);
    out << ",\"rotationY\":";
    writeObservedFloat(out, value.rotationY);
    out << ",\"velocity\":";
    writeObservedVector(out, value.velocity);
    out << ",\"motionId\":" << value.motionId << ",\"motionTime\":";
    writeObservedFloat(out, value.motionTime);
    out << ",\"motionSpeed\":";
    writeObservedFloat(out, value.motionSpeed);
    out << ",\"flags\":" << static_cast<unsigned>(value.flags)
        << ",\"hp\":" << value.hp << ",\"maxHp\":" << value.maxHp
        << ",\"mp\":" << value.mp << ",\"maxMp\":" << value.maxMp << '}';
}

std::uint32_t observeNumber(const std::string& text, const char* name,
                            std::uint32_t minimum, std::uint32_t maximum) {
    if (text.empty() || text.find_first_not_of("0123456789") != std::string::npos)
        throw std::runtime_error(std::string(name) + " must be an unsigned decimal integer");
    const auto value = std::stoull(text);
    if (value < minimum || value > maximum)
        throw std::runtime_error(std::string(name) + " is out of range");
    return static_cast<std::uint32_t>(value);
}

int cmdObserve(std::vector<std::string> args) {
    const auto pidText = option(args, "--pid");
    if (!pidText) throw std::runtime_error("observe requires explicit --pid N");
    const auto pid = observeNumber(*pidText, "--pid", 1, (std::numeric_limits<std::uint32_t>::max)());
    const auto samples = observeNumber(option(args, "--samples").value_or("1"), "--samples", 1, 30);
    const auto interval = observeNumber(option(args, "--interval-ms").value_or("250"), "--interval-ms", 0, 1000);
    requireEmpty(args);
    std::ostringstream rows;
    rows << std::setprecision(std::numeric_limits<float>::max_digits10);
    bool complete = true;
    for (std::uint32_t sample = 0; sample < samples; ++sample) {
        if (sample) {
            std::this_thread::sleep_for(std::chrono::milliseconds(interval));
            rows << ',';
        }
        // Reopen per sample: TryRead does not reuse a previously cached pose.
        WorldBridge world;
        AvatarBridge avatars;
        const bool worldOpen = world.OpenExisting(static_cast<DWORD>(pid));
        const bool avatarOpen = avatars.OpenExisting(static_cast<DWORD>(pid));
        const auto beginMs = nowMs();
        ObservedWorld before, beforeRepeat, after, afterRepeat;
        if (worldOpen) { before = observeWorld(world); beforeRepeat = observeWorld(world); }
        AvatarState local;
        PuppetPose puppets[AVATAR_BRIDGE_PUPPETS];
        bool localRead = false;
        bool puppetRead[AVATAR_BRIDGE_PUPPETS] {};
        if (worldOpen && avatarOpen) {
            localRead = avatars.TryReadLocal(local);
            for (int i = 0; i < AVATAR_BRIDGE_PUPPETS; ++i)
                puppetRead[i] = avatars.TryReadPuppet(i, puppets[i]);
        }
        if (worldOpen) { after = observeWorld(world); afterRepeat = observeWorld(world); }
        const auto endMs = nowMs();
        const bool stable = worldOpen && before == beforeRepeat && before == after && before == afterRepeat;
        const bool available = stable && avatarOpen;
        const bool sampleComplete = available && localRead && puppetRead[0] && puppetRead[1];
        complete = complete && sampleComplete;
        rows << "{\"index\":" << sample << ",\"beginMonotonicMs\":" << beginMs
             << ",\"endMonotonicMs\":" << endMs
             << ",\"worldMappingAvailable\":" << (worldOpen ? "true" : "false")
             << ",\"avatarMappingAvailable\":" << (avatarOpen ? "true" : "false")
             << ",\"identityStable\":" << (stable ? "true" : "false")
             << ",\"complete\":" << (sampleComplete ? "true" : "false")
             << ",\"worldBefore\":";
        if (worldOpen) writeObservedWorld(rows, before); else rows << "null";
        rows << ",\"worldAfter\":";
        if (worldOpen) writeObservedWorld(rows, afterRepeat); else rows << "null";
        rows << ",\"localReadAvailable\":" << (localRead ? "true" : "false") << ",\"local\":";
        if (available && localRead) writeObservedAvatar(rows, local); else rows << "null";
        rows << ",\"puppets\":[";
        for (int i = 0; i < AVATAR_BRIDGE_PUPPETS; ++i) {
            if (i) rows << ',';
            rows << "{\"index\":" << i << ",\"readAvailable\":" << (puppetRead[i] ? "true" : "false")
                 << ",\"value\":";
            if (available && puppetRead[i]) {
                const auto& p = puppets[i];
                const auto& tag = p.provenance;
                const bool matches = ValidPuppetProvenance(tag, static_cast<std::uint8_t>(p.pose.ownerSlot), i, before.authority);
                rows << "{\"active\":" << static_cast<unsigned>(p.active)
                     << ",\"provenanceMatchesWorld\":" << (matches ? "true" : "false")
                     << ",\"provenance\":{\"producer\":" << static_cast<unsigned>(tag.producer)
                     << ",\"localSlot\":" << static_cast<unsigned>(tag.localSlot)
                     << ",\"generation\":" << tag.generation
                     << ",\"ownerConnectionId\":" << tag.ownerConnectionId
                     << ",\"localConnectionId\":" << tag.localConnectionId
                     << ",\"hostConnectionId\":" << tag.hostConnectionId << "},\"pose\":";
                writeObservedAvatar(rows, p.pose);
                rows << '}';
            } else rows << "null";
            rows << '}';
        }
        rows << "]}";
    }
    std::cout << "{\"ok\":" << (complete ? "true" : "false")
              << ",\"schema\":1,\"command\":\"observe\",\"processId\":" << pid
              << ",\"readOnly\":true,\"atomicAcrossBridges\":false"
              << ",\"avatarBridgeVersion\":" << AVATAR_BRIDGE_VERSION
              << ",\"worldBridgeVersion\":" << WORLD_BRIDGE_VERSION
              << ",\"samples\":[" << rows.str() << "]}\n";
    return complete ? 0 : 1;
}

int cmdPeek(std::vector<std::string> args) {
    AvatarBridge bridge;
    openBridge(args, bridge);
    const double seconds = std::stod(option(args, "--seconds").value_or("1"));
    requireEmpty(args);
    std::optional<AvatarState> local;
    std::optional<PuppetPose> puppets[AVATAR_BRIDGE_PUPPETS];
    std::uint64_t localFrames = 0;
    const auto end = nowMs() + static_cast<std::uint64_t>(seconds * 1000.0);
    while (nowMs() < end) {
        AvatarState a;
        if (bridge.TryReadLocal(a)) {
            local = a;
            ++localFrames;
        }
        for (int i = 0; i < AVATAR_BRIDGE_PUPPETS; ++i) {
            PuppetPose p;
            if (bridge.TryReadPuppet(i, p)) puppets[i] = p;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    std::cout << "{\"ok\":true,\"command\":\"peek\",\"localFramesPerSecond\":"
              << (localFrames / seconds) << ",\"local\":"
              << (local ? avatarJson(*local) : std::string("null"));
    for (int i = 0; i < AVATAR_BRIDGE_PUPPETS; ++i) {
        std::cout << ",\"puppet" << i << "\":";
        if (puppets[i]) {
            std::cout << "{\"active\":" << (puppets[i]->active ? "true" : "false")
                      << ",\"pose\":" << avatarJson(puppets[i]->pose) << "}";
        } else {
            std::cout << "null";
        }
    }
    std::cout << "}\n";
    return 0;
}

} // namespace

int main(int argc, char* argv[]) {
    timeBeginPeriod(1); // 1 ms sleeps for 60 Hz pacing
    try {
        if (argc < 2) {
            std::cout << "avatarctl record|replay|synth|fake-local|peek|observe --pid N ...\n";
            return 0;
        }
        const std::string cmd = argv[1];
        std::vector<std::string> args(argv + 2, argv + argc);
        if (cmd == "record") return cmdRecord(std::move(args));
        if (cmd == "replay") return cmdReplay(std::move(args));
        if (cmd == "synth") return cmdSynth(std::move(args));
        if (cmd == "fault-arm") return cmdFaultArm(std::move(args));
        if (cmd == "fake-local") return cmdFakeLocal(std::move(args));
        if (cmd == "peek") return cmdPeek(std::move(args));
        if (cmd == "observe") return cmdObserve(std::move(args));
        throw std::runtime_error("unknown command: " + cmd);
    } catch (const std::exception& ex) {
        std::cout << "{\"ok\":false,\"error\":\"" << jsonEscape(ex.what()) << "\"}\n";
        return 1;
    }
}
