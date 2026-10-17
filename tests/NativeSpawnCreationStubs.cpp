// Headless controller tests do not install player/private-status game hooks.
// Refuse creation without linking those hooks or fabricating readiness.
#include "../inject/src/PlayerKit.hpp"
#include "../inject/src/NativePrivateStatus.hpp"
#include <cstdio>
#include <cstdlib>

namespace kh2coop::inject::playerkit {
bool MissingPartyReady() { return false; }
}
namespace kh2coop::inject::privatestatus {
bool Ready() { return false; }
void* SpawnMissingParty(const void*,void*,NativeWrapper,LogFn) {
    std::fputs("FAIL: headless controller test reached private-status creation\n",stderr);
    std::abort();
}
}
