#pragma once
// Diagnostic-only scalar snapshots. Deliberately isolated from Protocol/Codec:
// EntityHook has its own HitClaim ABI and cannot import world message structs.
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>

namespace kh2coop::rcdiag {
enum class Stage : unsigned {
    NativeCallback, LocalHandler, DllSend, RuntimeOutgoing, NetworkSend,
    RelayEnvelope, RelayCompletion, RelayForward, NetworkReceive, RuntimeEnvelope,
    RuntimeInbox, DllEnvelope, DllReceive, HostAttack, HostRole, RoleRetirement,
    LifecycleQuery, LifecycleCancel, LifecycleCapture, Count
};
inline const char* Name(Stage s) noexcept {
    constexpr const char* names[]{"native-callback","local-handler","dll-send","runtime-outgoing",
        "network-send","relay-envelope","relay-completion","relay-forward","network-receive","runtime-envelope",
        "runtime-inbox","dll-envelope","dll-receive","host-attack","host-role","role-retirement",
        "lifecycle-query","lifecycle-cancel","lifecycle-capture"};
    const auto i=static_cast<unsigned>(s);
    return i<static_cast<unsigned>(Stage::Count)?names[i]:"invalid";
}
enum Facts : unsigned { Producer=1, Envelope=2, Native=4, Role=8, Window=16, Seat=32, Observed=64, Accepted=128 };
struct Record {
    bool tuple{};
    std::uint64_t seq{},requester{},occurrence{};
    std::uint32_t epoch{},resource{},objectId{},netId{},command{},owner{255};
    std::uint32_t bytes{},type{},facts{},generation{},kind{},frame{},load{},transition{},slot{255},mode{};
    std::uint64_t session{},source{},sourceDelivery{},target{},targetDelivery{},hostSource{};
    std::uint64_t actor{},sourceActor{},thread{},expected{},observed{},lease{};
    std::uint64_t actorStatus{},actorEntry{},sourceStatus{},sourceEntry{};
    std::uint32_t actorHandle{},sourceHandle{};
    std::uint32_t commandFrame{},openedFrame{},duration{},expires{};
    std::int32_t observedRead{-1},consumedBefore{-1},consumedAfter{-1},living{-1},fresh{-1},capable{-1};
};
// Preserve original short-circuit evaluation: these wrappers only observe the
// boolean already evaluated by the caller, and never repeat a proof.
inline bool Check(bool value,const char* why,const char*& failed) noexcept {
    if(!value)failed=why;return value;
}
inline bool Reject(bool value,const char* why,const char*& failed) noexcept {
    if(value)failed=why;return value;
}
inline Record Claim(std::uint32_t epoch,std::uint32_t resource,std::uint32_t objectId,
    std::uint32_t netId,std::uint32_t command,std::uint32_t owner,std::uint64_t requester,
    std::uint64_t occurrence,std::uint64_t seq=0) noexcept {
    Record r{};r.tuple=true;r.epoch=epoch;r.resource=resource;r.objectId=objectId;r.netId=netId;
    r.command=command;r.owner=owner;r.requester=requester;r.occurrence=occurrence;r.seq=seq;return r;
}
inline constexpr unsigned Limit(Stage stage) noexcept {
    // The getter observations are change-only. Keep enough independent room
    // for late kill blocks without spending the transport/retirement budgets.
    return stage==Stage::LifecycleQuery?2048u:stage==Stage::LifecycleCancel?1024u:
        stage==Stage::LifecycleCapture?512u:256u;
}
// Each stage has its own lifetime budget and one saturation row, without
// rollover or room-edge renewal. No diagnostic state is consulted by gameplay.
inline std::array<std::atomic<unsigned>,static_cast<unsigned>(Stage::Count)> budgets{};
template<class Sink> inline void Emit(Stage stage,const char* result,const char* reason,
                                      const Record& r,Sink&& sink) noexcept {
    const auto index=static_cast<unsigned>(stage);
    if(index>=budgets.size())return;
    const auto limit=Limit(stage);
    auto used=budgets[index].load(std::memory_order_relaxed);
    while(used<limit+1 && !budgets[index].compare_exchange_weak(used,used+1,std::memory_order_relaxed)){}
    if(used>limit)return;
    char line[2048]{};
    if(used==limit) {
        std::snprintf(line,sizeof(line),"[script-rc-hop] stage=%s result=unobserved reason=budget-exhausted limit=%u subsequent=unobserved",Name(stage),limit);
    } else {
        std::snprintf(line,sizeof(line),
            "[script-rc-hop] stage=%s result=%s reason=%s tuple=%u seq=%llu epoch=%u source=%u objectId=%u resource=%u occurrence=%llu command=%u owner=%u requester=%llu type=%u bytes=%u facts=%X generation=%u kind=%u session=%llX sourceConnection=%llu sourceDelivery=%llu targetConnection=%llu targetDelivery=%llu hostSource=%llu frame=%u load=%u transition=%u slot=%u mode=%u actor=%llX sourceActor=%llX thread=%llX commandFrame=%u commandDelta=%u openedFrame=%u duration=%u expires=%u consumedBefore=%d consumedAfter=%d living=%d fresh=%d capable=%d expected=%llX observed=%llX observedRead=%d lease=%llu actorStatus=%llX actorEntry=%llX sourceStatus=%llX sourceEntry=%llX actorHandle=%u sourceHandle=%u",
            Name(stage),result,reason,r.tuple?1u:0u,static_cast<unsigned long long>(r.seq),r.epoch,r.netId,r.objectId,r.resource,
            static_cast<unsigned long long>(r.occurrence),r.command,r.owner,static_cast<unsigned long long>(r.requester),
            r.type,r.bytes,r.facts,r.generation,r.kind,static_cast<unsigned long long>(r.session),
            static_cast<unsigned long long>(r.source),static_cast<unsigned long long>(r.sourceDelivery),
            static_cast<unsigned long long>(r.target),static_cast<unsigned long long>(r.targetDelivery),
            static_cast<unsigned long long>(r.hostSource),r.frame,r.load,r.transition,r.slot,r.mode,
            static_cast<unsigned long long>(r.actor),static_cast<unsigned long long>(r.sourceActor),static_cast<unsigned long long>(r.thread),
            r.commandFrame,r.frame-r.commandFrame,r.openedFrame,r.duration,r.expires,r.consumedBefore,r.consumedAfter,
            r.living,r.fresh,r.capable,static_cast<unsigned long long>(r.expected),static_cast<unsigned long long>(r.observed),r.observedRead,static_cast<unsigned long long>(r.lease),
            static_cast<unsigned long long>(r.actorStatus),static_cast<unsigned long long>(r.actorEntry),
            static_cast<unsigned long long>(r.sourceStatus),static_cast<unsigned long long>(r.sourceEntry),r.actorHandle,r.sourceHandle);
    }
    try {sink(line);} catch(...) {} // logging never replaces a gameplay exception
}
} // namespace kh2coop::rcdiag
