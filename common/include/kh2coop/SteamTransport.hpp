#pragma once
#include "kh2coop/Transport.hpp"
#include <vector>

namespace kh2coop {
namespace steam { class BrokerLink; }
struct SteamTransports {
    std::uint64_t identity=0;
    std::unique_ptr<Transport> client; // local host endpoint OR remote joiner
    std::unique_ptr<Transport> server; // host only; SessionHost stays in runtime
};
// Explicit selection only. No Steam initialization in the runtime, no ENet
// fallback on error. IDs are public individual SteamID64s, not IP addresses.
SteamTransports makeSteamTransports(std::uint32_t gamePid, bool host,
                                   const std::vector<std::uint64_t>& allowlist);
// Same transport with an already authenticated broker link; mock boundary for
// offline tests. Production entry above always creates the OS-verified pipe.
SteamTransports makeSteamTransports(std::unique_ptr<steam::BrokerLink> link, bool host,
                                   const std::vector<std::uint64_t>& allowlist);
} // namespace kh2coop
