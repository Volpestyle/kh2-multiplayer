// Headless controller tests do not install player/private-status game hooks.
// Refuse creation without linking those hooks or fabricating readiness.
#include "../inject/src/PlayerKit.hpp"
#include "../inject/src/NativePrivateStatus.hpp"
#include "../inject/src/PrologueParty.hpp"
#include <cstdio>
#include <cstdlib>

namespace kh2coop::inject::playerkit {
bool MissingPartyReady() { return false; }
}
namespace kh2coop::inject::privatestatus {
bool Ready() { return false; }
void* SpawnMissingParty(const void*,void*,NativeWrapper,LogFn,const routespawn::MissingPartySite&,std::uint16_t,bool*,std::uintptr_t*) {
    std::fputs("FAIL: headless controller test reached private-status creation\n",stderr);
    std::abort();
}
}
// VUH-2464: no prologue seats headless; a prologue site refuses before any creation.
namespace kh2coop::inject::prologueparty {
bool CloneProfile(std::uint16_t& object, const char*& reason) { object = 0; reason = "headless"; return false; }
bool LocalBody(std::uint16_t& object, std::uint16_t& statusKey) { object = 0; statusKey = 0; return false; }
void ColorDuplicate(std::uintptr_t, std::uint16_t, std::uint16_t) {}
bool ProfilesSettled(std::uint32_t) { return true; }
void NoteConstruction(const std::array<std::uint8_t, 10>&, bool, const char*, std::uintptr_t) {}
bool Tick(std::uint32_t, const std::array<std::uint64_t, 3>&, std::uint8_t, std::uint64_t, SeatState&) { return false; }
void NoteSent(const SeatState&, std::uint64_t) {}
void Receive(const SeatState&) {}
}
