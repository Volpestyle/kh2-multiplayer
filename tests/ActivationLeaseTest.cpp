#include "kh2coop/ActivationLease.hpp"

#include <iostream>
#include <limits>

using namespace kh2coop;

namespace {
int errors = 0;
void check(bool valid, const char* what) {
    std::cout << (valid ? "PASS: " : "FAIL: ") << what << '\n';
    if (!valid) ++errors;
}
HostActivationPoint reply(const ActivationRequest& request, std::uint64_t sequence) {
    HostActivationPoint result;
    result.request = request;
    result.sourceSeq = sequence;
    result.position = {1.f, 2.f, 3.f, 1.f};
    return result;
}
} // namespace

int main() {
    ActivationLease lease;
    RoomTransition location;
    location.epoch = 9;
    location.worldId = 5;
    location.roomId = 6;
    const std::array<std::uint64_t, 2> incarnation {17, 19};
    float point[4] {};
    const auto first = lease.Request(location, incarnation, 1, 1000);
    check(first.has_value() && !lease.Copy(1000, point), "request grants no authority before a response");
    check(!lease.Request(location, incarnation, 1, 1099), "requests are rate limited");
    auto response = reply(*first, 1);
    auto forged = response;
    forged.request.requesterSlot = 2;
    check(!lease.Accept(forged, 1100), "wrong requester cannot grant a lease");
    forged = response;
    ++forged.request.location.door;
    check(!lease.Accept(forged, 1100), "all six native location fields bind a response");
    forged = response;
    ++forged.request.location.epoch;
    check(!lease.Accept(forged, 1100), "same-room different epoch cannot grant a lease");
    forged = response;
    ++forged.request.incarnation[0];
    check(!lease.Accept(forged, 1100), "another DLL incarnation cannot grant a lease");
    forged = response;
    forged.position[3] = std::numeric_limits<float>::quiet_NaN();
    check(!lease.Accept(forged, 1100), "nonfinite fourth component is rejected");
    check(lease.Accept(response, 1200) && lease.Copy(1499, point) && point[3] == 1.f,
          "finite exact float4 is usable only within the original request deadline");
    check(!lease.Accept(response, 1499), "duplicate receipt cannot renew a lease");
    check(!lease.Copy(1500, point), "receipt delay does not extend the fixed 500ms deadline");
    const auto second = lease.Request(location, incarnation, 1, 1600);
    check(!lease.Accept(reply(*second, 2), 2100), "a reply aged in any queue expires before receipt");
    const auto third = lease.Request(location, incarnation, 1, 2200);
    const auto fourth = lease.Request(location, incarnation, 1, 2300);
    check(lease.Accept(reply(*fourth, 4), 2400) && !lease.Accept(reply(*third, 3), 2401),
          "reordered old native captures cannot roll authority back");
    lease.Clear();
    check(!lease.Copy(2402, point) && !lease.Accept(reply(*fourth, 5), 2402),
          "session/transition reset immediately clears points and outstanding challenges");
    const auto fifth = lease.Request(location, incarnation, 1, 2402);
    check(fifth && fifth->requestSeq > fourth->requestSeq,
          "reset preserves monotonic request identity when an epoch is reused");
    check(!lease.Accept(reply(*fifth, 6), 2401), "a regressed monotonic clock cannot grant authority");
    ActivationLease bound;
    bool bounded = true;
    for (std::uint64_t tick = 0; tick < 20; ++tick)
        bounded = bounded && bound.Request(location, incarnation, 2, tick * 100).has_value();
    check(bounded, "expired requests are retired in the bounded outstanding array");
    check(!bound.Request(location, {}, 2, 2500) && !bound.Request(location, incarnation, 0, 2500),
          "unset incarnation and host requester are rejected");
    return errors ? 1 : 0;
}
