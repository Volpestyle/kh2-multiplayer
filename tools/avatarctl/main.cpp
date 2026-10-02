// avatarctl — drive and inspect a KH2 instance's AvatarBridge without a
// network (VUH-1490/1491).
//
//   avatarctl record --pid N --out FILE [--seconds S]
//       Capture the DLL's local avatar stream (every new frame) to FILE.
//   avatarctl replay --pid N --in FILE [--puppet 0|1] [--offset X,Y,Z] [--loop]
//       Play a recording into a puppet slot at its recorded pace (times are
//       re-based on now), optionally offset so it doesn't overlap the player.
//   avatarctl synth --pid N [--puppet 0|1] [--seconds S] [--center X,Y,Z]
//                   [--radius R] [--motion ID]
//       Drive a puppet around a circle (run speed) — tests the puppet driver
//       before avatar capture exists.
//   avatarctl fake-local --pid N [--seconds S] [--center X,Y,Z]
//       Pretend to be the DLL: publish a local avatar running a circle, to
//       exercise record/replay or the runtime's network path without KH2.
//   avatarctl peek --pid N [--seconds S]
//       Print the bridge contents (local avatar and both puppet slots).
//
// File format: "KH2AVS1\0", u32 record size, then raw AvatarState records.
// Prints one JSON object, like kh2ctl.

#include "kh2coop/AvatarBridge.hpp"
#include "kh2coop/Codec.hpp"

#include <timeapi.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
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
    constexpr float kOmega = 2.0f; // rad/s -> ~600 units/s at r=300
    const float ang = kOmega * t;
    AvatarState a;
    a.position = {center.x + radius * std::cos(ang), center.y, center.z + radius * std::sin(ang)};
    a.velocity = {-radius * kOmega * std::sin(ang), 0.0f, radius * kOmega * std::cos(ang)};
    a.rotationY = std::atan2(a.velocity.x, a.velocity.z);
    a.motionId = motion;
    a.motionTime = t * 60.0f; // frames at 60 fps
    a.motionSpeed = 1.0f;
    return a;
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

int cmdSynth(std::vector<std::string> args) {
    AvatarBridge bridge;
    openBridge(args, bridge);
    const int idx = puppetIndex(args);
    const double seconds = std::stod(option(args, "--seconds").value_or("10"));
    const Vec3 center = parseVec(option(args, "--center").value_or("0,0,0"));
    const float radius = std::stof(option(args, "--radius").value_or("300"));
    const auto motion = static_cast<std::uint32_t>(std::stoul(option(args, "--motion").value_or("2")));
    requireEmpty(args);

    const auto start = nowMs();
    const auto end = start + static_cast<std::uint64_t>(seconds * 1000.0);
    std::uint64_t frames = 0;
    for (std::uint64_t due = start; due < end; due += kFrameMs) {
        sleepUntil(due);
        PuppetPose p;
        p.active = 1;
        p.pose = circlePose(static_cast<float>(nowMs() - start) / 1000.0f, center, radius, motion);
        p.pose.serverTimeMs = nowMs();
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
            std::cout << "avatarctl record|replay|synth|fake-local|peek --pid N ...\n";
            return 0;
        }
        const std::string cmd = argv[1];
        std::vector<std::string> args(argv + 2, argv + argc);
        if (cmd == "record") return cmdRecord(std::move(args));
        if (cmd == "replay") return cmdReplay(std::move(args));
        if (cmd == "synth") return cmdSynth(std::move(args));
        if (cmd == "fake-local") return cmdFakeLocal(std::move(args));
        if (cmd == "peek") return cmdPeek(std::move(args));
        throw std::runtime_error("unknown command: " + cmd);
    } catch (const std::exception& ex) {
        std::cout << "{\"ok\":false,\"error\":\"" << jsonEscape(ex.what()) << "\"}\n";
        return 1;
    }
}
