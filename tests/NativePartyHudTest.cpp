// VUH-2568: production display adapter over owned memory; no KH2 process.
#include "../inject/src/NativePartyHud.cpp"
#include "kh2coop/Codec.hpp"
#include <iostream>
#include <limits>
using namespace kh2coop;
namespace {
PuppetAuthority authority;
bool pending{};
std::uint32_t load = 9;
int failures{}, binds{}, ticks{}, visibility{};
std::array<std::uintptr_t, 3> native{};
void Check(bool b, const char* text) { std::cout << (b ? "PASS " : "FAIL ") << text << '\n'; if (!b) ++failures; }
SeatState Report(std::uint8_t slot, std::uint32_t seq = 1) {
    SeatState m;
    m.roster = {11,22,33}; m.ownerSlot = slot; m.seq = seq; m.profile = 0x54; m.worldId = 4; m.roomId = 2;
    m.vitals = {20,40,3600,10000,0,0,0x54,SeatVitals::Available};
    return m;
}
}
namespace kh2coop::inject::enemysync { PuppetAuthority CapturePuppetAuthority() noexcept { return authority; } }
namespace kh2coop::inject::warp {
bool TransitionPending() { return pending; }
std::uint32_t LoadSerial() { return load; }
}
namespace kh2coop::inject::nativehud {
namespace {
std::uintptr_t __fastcall NativeLookup(int seat) { return native[static_cast<unsigned>(seat)]; }
void __fastcall NativeBind(std::uintptr_t gauge) {
    ++binds;
    int index{}; Read(gauge + 0x38, index);
    const auto target = Current() ? Project(index + 1) : g_lookup(index + 1);
    std::memcpy(reinterpret_cast<void*>(gauge + 0x40), &target, 8);
}
void __fastcall NativeTick(std::uintptr_t) { ++ticks; }
void __fastcall NativeVisibility(std::uintptr_t) { ++visibility; }
}
void Adapter() {
    auto* image = static_cast<std::uint8_t*>(VirtualAlloc(nullptr, 0x3000000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    Check(image != nullptr, "allocate owned image"); if (!image) return;
    g_base = reinterpret_cast<std::uintptr_t>(image);
    image[offsets::WORLD_ID] = 4; image[offsets::ROOM_ID] = 2;
    g_lookup = NativeLookup; g_bind = NativeBind; g_tick = NativeTick; g_visibility = NativeVisibility; g_installed = true;
    authority = {PuppetAuthorityMode::Network, 0, 7, {11,22,33}};
    alignas(16) std::array<std::uint8_t, 0xA00> body1{}, body2{}, npc{};
    std::array<std::uint8_t, 64> obj1{}, obj2{}, npcObj{}, faces1{}, faces2{};
    const auto b1 = reinterpret_cast<std::uintptr_t>(body1.data()), b2 = reinterpret_cast<std::uintptr_t>(body2.data());
    Put(body1.data(), 0x918, reinterpret_cast<std::uintptr_t>(obj1.data())); Put(obj1.data(), 0, std::uint16_t{0x54});
    Put(body2.data(), 0x918, reinterpret_cast<std::uintptr_t>(obj2.data())); Put(obj2.data(), 0, std::uint16_t{0x54});
    Put(body1.data(), 0x928, reinterpret_cast<std::uintptr_t>(faces1.data()));
    Put(body2.data(), 0x928, reinterpret_cast<std::uintptr_t>(faces2.data()));
    Put(npc.data(), 0x918, reinterpret_cast<std::uintptr_t>(npcObj.data())); npcObj[4]=1;
    const auto before1 = body1, before2 = body2;
    native = {0x1234, b2, reinterpret_cast<std::uintptr_t>(npc.data())};
    AvatarState local; local.worldId = 4; local.roomId = 2;
    Observe(local, 0, authority, {});
    Check(Project(1)==0 && Project(2)==0,"three reserved human slots never borrow an NPC while joining");
    authority.connectionIds[2]=0; Observe(local,0,authority,{},HumanTargets(authority,{b2,native[2]}));
    Check(Project(2)==native[2],"a never-admitted player clone cannot become the spare NPC");
    obj2[4]=1; Observe(local,0,authority,{}, {b2,0});
    Check(Project(2)==native[2],"a never-admitted companion puppet is excluded independently of portrait admission");
    obj2[4]=0;
    authority.connectionIds[2]=33;
    Observe(local, 0, authority, {0,b1,b2});
    Receive(Report(1)); Receive(Report(2));
    const auto p1 = Project(1), p2 = Project(2);
    Check(p1 && p2 && p1 != b1 && p2 != b2 && p1 != p2, "three players use two distinct display-only views");
    std::uintptr_t status{}, face{}; int hp{}, mp{};
    Read(p1 + 0x5C0, status); Read(status, hp); Read(status + 0x180, mp); Read(p1 + 0x928, face);
    Check(hp == 20 && mp == 3600 && face == reinterpret_cast<std::uintptr_t>(faces1.data()), "native readers see owner vitals and the selected body's face package");
    auto roxas=Report(2,2); roxas.vitals.objectId=0x5A; Receive(roxas);
    Check(Project(2)==0,"a profile/body mismatch never borrows the wrong character portrait");
    Put(obj2.data(),0,std::uint16_t{0x5A});
    Check(Project(2)==p2,"Roxas uses the same owner-slot projection with its own native face package");
    Put(obj2.data(),0,std::uint16_t{0x54}); Receive(Report(2,3));
    Check(Lookup(0) == native[0] && Lookup(1) == native[1], "local gauge and non-HUD lookup callers retain native actors");
    Check(HudCaller(g_base + 0x182430) && !HudCaller(g_base + 0x3B57B0), "projection is restricted to checked native HUD consumers");
    alignas(16) std::array<std::uint8_t, 0x1710> gauge{};
    const auto ga = reinterpret_cast<std::uintptr_t>(gauge.data());
    Put(gauge.data(), 0x38, 0); Put(gauge.data(), 0x40, native[1]);
    const std::uint32_t faceFlags = 0x800; const float rgb[3] {1,1,1};
    Put(gauge.data(), 0x60, faceFlags); std::memcpy(gauge.data()+0x1CC, rgb, sizeof(rgb));
    std::memcpy(gauge.data()+0x1E8,rgb,sizeof(rgb));
    Tick(ga);
    std::uintptr_t bound{}; Read(ga+0x40,bound);
    Check(binds == 1 && ticks == 1 && visibility == 1 && bound == p1, "native friend tick rebinds and reveals a newly joined remote");
    float blue{}; Read(ga+0x1CC+8,blue);
    Check(blue == 1 && g_tints[0].owned, "owner blue is applied to the native portrait sprite");
    float scale{}; Read(ga+0x1EC,scale);
    Check(scale==.65f,"player portrait fits the native companion circle without resizing its gauges");
    auto damaged = Report(1,2); damaged.vitals.hp = 7; damaged.vitals.mp = 1200; Receive(damaged); Tick(ga);
    Read(status,hp); Read(status+0x180,mp);
    Check(hp == 7 && mp == 1200 && binds == 1, "owner damage and MP use update without native actor writes or needless rebinds");
    auto down = Report(1,3); down.vitals.hp = 0; down.vitals.flags |= SeatVitals::Downed; Receive(down); Tick(ga);
    std::uint32_t flags{}; Read(p1+0x9B8,flags); Read(status,hp);
    Check(hp == 0 && flags == 4, "downed renders HP zero and native downed face state without killing a gameplay clone");
    auto revived = Report(1,4); revived.vitals.hp = 30; revived.vitals.mp = 10000; Receive(revived); Tick(ga);
    Read(p1+0x9B8,flags); Read(status,hp);
    Check(hp == 30 && flags == 0, "healing and revive clear the old downed view");
    g_state.samples[1].receivedMs = GetTickCount64() - FreshMs - 1; Tick(ga); Read(ga+0x40,bound);
    Check(bound == 0, "expired owner data clears its reserved slot immediately");
    Receive(Report(1,5)); Tick(ga);
    pending = true; Tick(ga); Read(ga+0x40,bound);
    Check(bound == native[1] && !g_tints[0].owned, "room transition restores native binding and portrait color");
    std::uint32_t restored{}; Read(ga+0x60,restored); Check(restored == faceFlags, "native portrait flags restored exactly");
    Read(ga+0x1EC,scale); Check(scale==1.f,"native NPC portrait scale restored exactly");
    pending = false; ++load; Check(!Current(), "room load serial retires the old view even at the same location");
    Observe(local,0,authority,{0,b1,b2});
    authority.connectionIds[2] = 0;
    Observe(local,0,authority,{0,b1,0});
    auto two = Report(1,6); two.roster = authority.connectionIds; Receive(two);
    Check(Project(2) == native[2], "two players preserve a distinct story NPC and exclude the human clone");
    authority.connectionIds[1] = 44; Observe(local,0,authority,{0,b1,0});
    Check(Project(1) == 0, "reconnect forgets the previous connection's health");
    authority.connectionIds = {11,0,0}; Observe(local,0,authority,{},HumanTargets(authority,{b1,b2}));
    Check(Project(1) == native[2] && Project(2) == 0, "departed human clones never become NPCs and one NPC appears only once");
    native[1] = b1; native[2] = b2; ++load; Observe(local,0,authority,{},HumanTargets(authority,{b1,b2}));
    obj1[4]=obj2[4]=1;
    Check(Project(1) == b1 && Project(2) == b2, "a solo roster preserves two distinct native companions in order");
    auto* own = image + 0x2A17290;
    Put(body1.data(),0x5C0,reinterpret_cast<std::uintptr_t>(own));
    Put(own,0,19); Put(own,4,40); Put(own,0x180,2700); Put(own,0x184,10000);
    local.hp=87; local.maxHp=99; local.mp=999;
    Observe(local,b1,authority,{}); SeatState filled; Fill(filled,GetTickCount64());
    Check(filled.vitals.hp==19 && filled.vitals.mp==2700, "authoritative sample reads the owner's pool record rather than slot-zero fallback");
    Put(body1.data(),0x5C0,std::uintptr_t{0}); Observe(local,b1,authority,{}); Fill(filled,GetTickCount64());
    Check(filled.vitals.flags==0,"an invalid owner record cannot publish another actor's fallback health");
    body1=before1;
    authority={PuppetAuthorityMode::Network,0,10,{11,22,0}}; Observe(local,0,authority,{0,b1,0});
    auto retiring=Report(1,10); retiring.roster=authority.connectionIds; Receive(retiring); Tick(ga);
    Retire(); Tick(ga); Read(ga+0x40,bound);
    Check(bound==native[1]&&!g_tints[0].owned,"shutdown retirement restores HUD binding and tint on the native thread");
    Check(body1 == before1 && body2 == before2, "real actor bytes remain unchanged across damage, downed, revive, load and reconnect");
    g_installed = false; VirtualFree(image,0,MEM_RELEASE);
}
}
int main() {
    using namespace kh2coop::inject::nativehud;
    auto m = Report(1);
    const auto bytes = encode(m); const std::uint8_t* p{}; std::size_t n{};
    Check(decodePacketHeader(bytes.data(),bytes.size(),p,n)==PacketType::SeatState && n == SEAT_STATE_BYTES && PROTOCOL_VERSION == 38,
          "v29 exact SeatState framing");
    ByteReader r(p,n); SeatState back; read(r,back); Check(back.vitals == m.vitals && encode(back)==bytes,"owner vitals codec roundtrip");
    for (int kind=0;kind<7;++kind) {
        auto bad=m;
        switch(kind) { case 0:bad.vitals.hp=-1;break; case 1:bad.vitals.hp=41;break; case 2:bad.vitals.mp=10001;break;
            case 3:bad.vitals.charge=std::numeric_limits<float>::quiet_NaN();break; case 4:bad.vitals.flags=4;break;
            case 5:bad.vitals.flags|=SeatVitals::Downed;break; case 6:bad.vitals.objectId=0;break; }
        bool refused=false; try {(void)encode(bad);}catch(const std::exception&){refused=true;}
        Check(refused,"codec rejects malformed vitals");
    }
    State s; s.Rebase(7,{11,22,33},0);
    Check(s.Owners()==std::array<std::uint8_t,2>{1,2},"host sees seats one and two");
    Check(s.Receive(m,1000) && !s.Receive(m,1001),"sequence floor rejects duplicates without refreshing freshness");
    Check(s.Current(1,4,2,1500) && !s.Current(1,4,2,1501) && !s.Current(1,4,2,999) && !s.Current(1,4,3,1000),
          "freshness boundary, backward clock and location mismatch");
    auto wrong=m;wrong.roster[1]=44;wrong.seq=2;Check(!s.Receive(wrong,1001),"old or forged roster cannot populate a HUD slot");
    s.Rebase(8,{11,22,33},1); Check(s.Owners()==std::array<std::uint8_t,2>{0,2}&&!s.Current(0,4,2,1000),"friend sees host and third player, generation reset clears values");
    s.Rebase(9,{11,0,33},2);Check(s.Owners()==std::array<std::uint8_t,2>{0,255},"roster gaps compact humans without duplication");
    s.Rebase(0,{},255); Check(s.Owners()==std::array<std::uint8_t,2>{255,255},"disconnect releases every human slot");
    Adapter();
    std::cout << (failures ? "FAILURES " : "ALL CHECKS PASSED ") << failures << '\n'; return failures ? 1 : 0;
}
