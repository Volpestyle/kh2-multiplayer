// VUH-2881: actual detour wrappers, native-memory reads, Pump and TID logging over owned
// memory. No MinHook installation, process/game/bridge/Steam or save access.
#include "../inject/src/StruggleOrbsHook.cpp"
#include "kh2coop/Codec.hpp"
#include "kh2coop/SpawnEmissionLedger.hpp"
#include <cstdarg>
#include <cstdio>
#include <future>
#include <string>
#include <thread>

namespace so = kh2coop::inject::struggleorbs;
using namespace kh2coop;
namespace {
unsigned checks = 0, failed = 0;
void Check(bool ok, const char* what) {
    ++checks; if (!ok) { ++failed; std::printf("FAIL %s\n", what); }
}
constexpr DWORD kEntryError = 1234, kNativeError = 5678;
DWORD ownerTid = 0;
std::atomic<DWORD> workerTid{0};
std::atomic<unsigned> nativeCreates{0}, nativeDrops{0}, nativePicks{0}, badNativeEntry{0}, badCallbackThread{0};
std::mutex logMutex;
std::vector<std::string> logs;
void Log(const char* fmt, ...) {
    char buf[1024]; va_list args; va_start(args, fmt); vsnprintf(buf, sizeof(buf), fmt, args); va_end(args);
    { std::lock_guard lock(logMutex); logs.emplace_back(buf); }
    SetLastError(9999); // observer work must not change the native input/output LastError
}
std::array<std::byte, 0x100> orbMemory{};
std::uintptr_t Record() { return reinterpret_cast<std::uintptr_t>(orbMemory.data()); }
template<class T> void Put(std::uintptr_t at, T value) { std::memcpy(reinterpret_cast<void*>(at), &value, sizeof(value)); }
void Prepare(std::uintptr_t base) {
    orbMemory = {};
    Put(Record() + so::kRecordManager, std::uintptr_t{0xA000});
    Put(Record() + so::kRecordDescriptor, base + so::kDescriptorsRva);
    Put(Record() + so::kRecordUpdate, std::uint32_t{1});
}
std::function<void()> duringPick;
bool switchToFlight = true;
std::uintptr_t __fastcall Create(std::uintptr_t manager, std::uintptr_t descriptor, const float* point) {
    ++nativeCreates;
    if (GetLastError() != kEntryError || manager != 0xA000 || !descriptor || point[0] != 1) ++badNativeEntry;
    const float velocity[3]{4, 5, 6};
    std::memcpy(reinterpret_cast<void*>(Record() + so::kRecordVelocity), velocity, sizeof(velocity));
    SetLastError(kNativeError); return Record();
}
void __fastcall Drop(void*, void*, void*, void*) {
    ++nativeDrops; if (GetLastError() != kEntryError) ++badNativeEntry; SetLastError(kNativeError);
}
void __fastcall Pick(void*, void*, void*, void*) {
    ++nativePicks; if (GetLastError() != kEntryError) ++badNativeEntry;
    if (duringPick) duringPick();
    if (switchToFlight) {
        Put(Record() + so::kRecordUpdate, std::uint32_t{2});
        Put(Record() + so::kRecordCollector, std::uintptr_t{0x222});
    }
    SetLastError(kNativeError);
}
so::HostBook book;
SpawnEmissionLedger ledger;
so::ClientDisplay display;
std::uint64_t sequence = 0;
unsigned callbacks = 0;
void OnOwner() { ++callbacks; if (GetCurrentThreadId() != ownerTid) ++badCallbackThread; }
bool RoundTrip(const SpawnEmission& e, SpawnEmission& out) {
    const auto bytes = encode(e);
    const std::uint8_t* payload = nullptr; std::size_t size = 0;
    if (decodePacketHeader(bytes.data(), bytes.size(), payload, size) != PacketType::SpawnEmission) return false;
    ByteReader r(payload, size); read(r, out); return r.atEnd();
}
void Birth(std::uintptr_t manager, std::uintptr_t key, std::uint8_t type, const float* point, const float* velocity) {
    OnOwner();
    const auto orb = book.Birth(manager, key);
    SpawnEmission e{}; e.kind = SpawnEmissionKind::OrbBirth; e.sequence = ++sequence;
    e.location.epoch = 2; e.orbId = orb.id; e.orbMatch = orb.match; e.orbType = type;
    std::copy_n(point, 4, e.point.begin()); std::copy_n(velocity, 3, e.velocity.begin());
    Check(e.point[0] == 1 && e.point[3] == 1 && e.velocity[0] == 4 && e.velocity[2] == 6,
          "actual create wrapper copies position/velocity before worker returns");
    SpawnEmission decoded;
    Check(RoundTrip(e, decoded) && ledger.Add(decoded), "birth crosses codec and ledger once");
}
void Collect(std::uintptr_t manager, std::uintptr_t key, std::uint8_t kind, std::uint16_t id) {
    OnOwner(); const auto orb = book.Collect(manager, key);
    SpawnEmission e{}; e.kind = SpawnEmissionKind::OrbCollect; e.sequence = ++sequence;
    e.location.epoch = 2; e.orbId = orb.id; e.orbMatch = orb.match;
    e.collector = static_cast<OrbCollector>(kind); e.collectorNetId = id;
    SpawnEmission decoded;
    Check(RoundTrip(e, decoded) && ledger.Add(decoded), "worker collect crosses codec and ledger once");
}
void Collectors(std::vector<so::Collector>& out) { OnOwner(); out.push_back({0x222, so::kCollectorFriend, 0}); }
void Worker(const std::function<void()>& f) {
    std::thread t([&] { workerTid = GetCurrentThreadId(); SetLastError(kEntryError); f(); }); t.join();
}
}
int main() {
    using namespace so;
    ownerTid = GetCurrentThreadId();
    const auto imageSize = static_cast<std::size_t>(kManagerRva + 0x1000);
    const auto base = reinterpret_cast<std::uintptr_t>(VirtualAlloc(nullptr, imageSize, MEM_RESERVE, PAGE_NOACCESS));
    if (!base) return 2;
    const auto page = (base + kManagerRva) & ~std::uintptr_t{0xFFF};
    if (!VirtualAlloc(reinterpret_cast<void*>(page), 0x1000, MEM_COMMIT, PAGE_READWRITE)) return 2;
    Put(base + kManagerRva, std::uintptr_t{0xA000});
    // The private production wrappers and environment, with synthetic native targets only.
    g_exeBase = base; g_log = &Log; g_create = &Create; g_hitDrop = &Drop; g_drop = &Drop; g_pickA = g_pickB = &Pick;
    g_cb.hostBirth = &Birth; g_cb.hostCollect = &Collect; g_cb.collectors = &Collectors;
    g_env = {base, &ThreadNow, &ReadNative, &g_cb};
    g_gate.active = true;
    NoteGameThread();
    Scope scope{11, 2, 3, 4, 55, 0xA000, Mode::Host};
    Pump(scope);
    for (unsigned pick = 0; pick < 2; ++pick) {
        Prepare(base);
        const auto before = callbacks;
        Worker([&] {
            float point[4]{1, 2, 3, 1};
            Check(CreateDetour(0xA000, base + kDescriptorsRva, point) == Record() && GetLastError() == kNativeError,
                  "actual worker create returns native result and LastError");
        });
        Check(callbacks == before, "create never calls EnemySync on the worker");
        Pump(scope);
        display.Receive(ledger.TakeOrbs());
        display.Tick(0xB000, [](const CreateRequest&) { return std::uintptr_t{0x888}; }, [](const VanishRequest&) { return false; });
        Check(display.Shown() == 1, "friend sees host birth before pickup");
        const auto booked = callbacks;
        Worker([&] {
            if (pick == 0) PickADetour(nullptr, nullptr, reinterpret_cast<void*>(Record()), nullptr);
            else PickBDetour(nullptr, reinterpret_cast<void*>(Record()), nullptr, nullptr);
            Check(GetLastError() == kNativeError, "actual worker pickup preserves native LastError");
        });
        Check(callbacks == booked, "both pick detours defer every callback to owner");
        Pump(scope); display.Receive(ledger.TakeOrbs());
        unsigned flights = 0;
        display.Tick(0xB000, [](const CreateRequest&) { return std::uintptr_t{0}; }, [&](const VanishRequest& v) {
            ++flights; return v.record == 0x888 && v.collector == OrbCollector::Friend && !v.netId;
        });
        Check(flights == 1 && display.Shown() == 0 && book.LiveCount() == 0,
              "actual worker pickup makes friend's shown orb fly to its collector");
    }
    Worker([] { HitDropDetour(nullptr, nullptr, nullptr, nullptr); SetLastError(kEntryError); DropDetour(nullptr, nullptr, nullptr, nullptr); });
    Check(nativeCreates == 2 && nativePicks == 2 && nativeDrops == 2 && !badNativeEntry && !badCallbackThread,
          "host calls each original exactly once with intact LastError; all callbacks run on owner");
    // A pool record can be reused before the owner drains. Both threads' births/collects
    // share the same FIFO, so the old collect cannot take the newer orb's identity.
    Prepare(base);
    Worker([&] {
        float point[4]{1, 2, 3, 1};
        CreateDetour(0xA000, base + kDescriptorsRva, point);
        SetLastError(kEntryError);
        PickBDetour(nullptr, reinterpret_cast<void*>(Record()), nullptr, nullptr);
        Prepare(base); SetLastError(kEntryError);
        CreateDetour(0xA000, base + kDescriptorsRva, point);
    });
    Pump(scope); display.Receive(ledger.TakeOrbs());
    unsigned reuseFlights = 0;
    display.Tick(0xB000, [](const CreateRequest&) { return std::uintptr_t{0x999}; }, [&](const VanishRequest&) { ++reuseFlights; return true; });
    Check(book.LiveCount() == 1 && display.Shown() == 1 && reuseFlights == 0 && display.Stats().elided == 1,
          "same-frame record reuse: old orb elided, new orb stays live through the actual FIFO and codec");
    scope.mode = Mode::Follow; Pump(scope);
    Worker([] {
        HitDropDetour(nullptr, nullptr, nullptr, nullptr); DropDetour(nullptr, nullptr, nullptr, nullptr);
        PickADetour(nullptr, nullptr, reinterpret_cast<void*>(Record()), nullptr);
        PickBDetour(nullptr, reinterpret_cast<void*>(Record()), nullptr, nullptr);
        Check(GetLastError() == kEntryError, "suppressed worker entries retain caller LastError");
    });
    Check(nativeDrops == 2 && nativePicks == 3 && Stats().dropsSuppressed == 2 && Stats().picksSuppressed == 2,
          "both drop and pick wrappers suppress client-local native actions on a worker");
    // An owner can retire policy while the original waits. No mailbox lock spans the native call.
    scope.mode = Mode::Host; Pump(scope); Prepare(base);
    std::promise<void> entered, resume; auto resumed = resume.get_future();
    duringPick = [&] { entered.set_value(); resumed.wait(); };
    std::thread pending([&] { SetLastError(kEntryError); PickBDetour(nullptr, reinterpret_cast<void*>(Record()), nullptr, nullptr); });
    entered.get_future().wait(); ResetContext(); ++scope.load; Pump(scope); resume.set_value(); pending.join();
    duringPick = {};
    Check(Stats().stale == 1, "actual delayed native return cannot book into a replacement scope");
    for (std::size_t i = 0; i < static_cast<std::size_t>(adapter::Site::Count); ++i) {
        const auto prefix = std::string("[struggle-orbs] thread site=") + adapter::kSiteNames[i] + " tid=";
        bool found = false;
        for (const auto& line : logs) if (line.starts_with(prefix)) { found = true; std::puts(line.c_str()); }
        Check(found, "each actual detour and NoteGameThread emits thread-id evidence");
    }
    Check(g_gate.gameThread == ownerTid && workerTid != ownerTid && Stats().offThread >= 10,
          "worker entries are observed off-thread and marshalled, never relabelled as game thread");
    ResetContext(); g_gate.active = false; VirtualFree(reinterpret_cast<void*>(base), 0, MEM_RELEASE);
    std::printf("%u/%u checks PASS (synthetic native workers only)\n", checks - failed, checks);
    return failed ? 1 : 0;
}
