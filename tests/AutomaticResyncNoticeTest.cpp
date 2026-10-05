// Bounded host-notice policy and real generator/loopback controls; no KH2.
#include "WorldWireFixture.hpp"
#include "kh2coop/AutomaticResyncNotice.hpp"
#include "kh2coop/SessionHost.hpp"
#include <chrono>
#include <iostream>
#include <memory>
#include <thread>
using namespace kh2coop;
namespace {
int checks = 0, failures = 0;
constexpr std::uint8_t recoveryFields = DesyncEnemies | DesyncMissingEnemies;
void check(bool ok, const char* label) { ++checks; failures += !ok; std::cout << (ok ? "PASS " : "FAIL ") << label << '\n'; }
HostResyncContext context() {
    HostResyncContext c;
    c.binding = {std::string(32, 'a'), 0x100000001ULL, 0x100000001ULL, 0, 9};
    c.connections = {c.binding.hostConnectionId, 0x200000002ULL, 0x300000003ULL};
    c.room = {7, 5, 6, 0, 1, 1, 0};
    return c;
}
struct Policy {
    HostResyncContext current = context();
    bool busy = false, sendOk = true;
    unsigned sends = 0;
    std::vector<AutomaticResyncNotice::Event> events;
    AutomaticResyncNotice policy;
    explicit Policy(bool enabled = true) : policy(enabled, [&](const auto& e) { events.push_back(e); }) {}
    bool Submit(std::uint8_t mask, ResyncRequest* generated) {
        generated->key = {current.binding.sessionId, current.binding.hostConnectionId, ++sends};
        generated->room = current.room; generated->connections = current.connections; generated->targetMask = mask;
        busy = sendOk; return sendOk;
    }
    void Notice(unsigned slot = 1, std::uint8_t fields = recoveryFields, std::uint64_t now = 10) {
        policy.Notice({static_cast<SlotType>(slot), current.room.epoch, fields}, current, busy, now,
            [&](auto mask, auto* generated) { return Submit(mask, generated); });
    }
    void Pump() { policy.Pump(current, [&] { return busy; }, [&](auto mask, auto* generated) { return Submit(mask, generated); }); }
    void Finish() { busy = false; ResyncResult result; result.key = {current.binding.sessionId, current.binding.hostConnectionId, sends}; policy.Terminal(result); }
    bool Last(const char* action, const char* reason) const { return !events.empty() && std::string(events.back().action) == action && std::string(events.back().reason) == reason; }
};
void policyControls() {
    { Policy p(false); p.Notice(); p.Finish(); p.Pump(); check(p.sends == 0 && p.events.empty(), "default-off policy has no request/receipt side effects"); }
    { Policy p; p.current.binding.selfSlot = 1; p.Notice(); check(p.sends == 0 && p.Last("discard", "not-current-host-enemies-hint"), "friend role cannot auto-submit"); }
    for (unsigned invalid : {0u, 3u, 255u}) { Policy p; p.Notice(invalid); check(!p.sends, "invalid target slot rejected without indexing state"); }
    for (std::uint8_t fields : {std::uint8_t{0}, std::uint8_t{1}, std::uint8_t{2}, std::uint8_t{4}, std::uint8_t{8}, std::uint8_t{11}, std::uint8_t{16}, std::uint8_t{26}}) { Policy p; p.Notice(1, fields); check(!p.sends, "HP-only, incomplete missing hints, room changes and unknown bits cannot reload"); }
    { Policy p; p.current.connections[1] = 0; p.Notice(); check(!p.sends, "absent target connection rejected"); }
    { Policy p; auto old = context(); ++p.current.room.epoch; p.policy.Notice({SlotType::Friend1, old.room.epoch, recoveryFields}, p.current, false, 10, [&](auto m, auto* g) { return p.Submit(m, g); }); check(!p.sends, "stale epoch notice rejected"); }
    { Policy p; p.Notice(); check(p.sends == 1 && p.busy && p.events.back().key.requestId == 1 && p.events.back().candidate.context.connections[1] > UINT32_MAX, "immediate submission joins generated key and full-width incarnation"); p.Notice(); check(p.sends == 1 && p.Last("duplicate", "already-attempted"), "duplicate during request window never submits twice"); p.Finish(); p.Pump(); p.Notice(); check(p.sends == 1, "terminal failure/success does not reopen dedupe"); ++p.current.connections[1]; p.Notice(); check(p.sends == 2, "new admitted target incarnation has independent dedupe"); }
    { Policy p; p.busy = true; p.Notice(); p.Notice(1, recoveryFields, 999); check(p.sends == 0 && p.policy.PendingCount() == 1 && p.events.back().relatedReceipt == 1, "busy duplicate coalesces without replacing original receipt time"); p.Pump(); check(!p.sends, "periodic pump is not a retry timer"); p.Finish(); check(!p.sends, "terminal callback defers submission until outside receive stack"); p.Pump(); check(p.sends == 1 && p.events.back().candidate.receivedMs == 10, "terminal drain retains first candidate and submits once"); }
    { Policy p; p.busy = true; p.Notice(); p.Notice(1, recoveryFields | DesyncProgress); check(p.policy.PendingCount() == 1 && p.events[p.events.size()-2].relatedReceipt == 2, "changed fields explicitly supersede one pending slot"); p.Notice(2); check(p.policy.PendingCount() == 2, "two slots have bounded independent pending candidates"); p.Finish(); p.Pump(); check(p.sends == 1 && p.policy.PendingCount() == 1, "first drained slot occupies single global transaction"); p.Finish(); p.Pump(); check(p.sends == 2 && p.policy.PendingCount() == 0, "next terminal drains second slot without timer"); }
    for (unsigned mode = 0; mode < 6; ++mode) { Policy p; p.busy = true; p.Notice(); if (mode == 0) ++p.current.connections[1]; if (mode == 1) ++p.current.connections[2]; if (mode == 2) ++p.current.room.door; if (mode == 3) ++p.current.binding.deliverySerial; if (mode == 4) p.current.binding.sessionId[0] = 'b'; if (mode == 5) ++p.current.room.epoch; p.Finish(); p.Pump(); check(!p.sends && !p.policy.PendingCount() && p.Last("discard", "captured-context-changed"), "terminal revalidation refuses captured tuple/incarnation/roster drift"); }
    { Policy p; p.sendOk = false; p.Notice(); p.Notice(); p.Pump(); check(p.sends == 1 && !p.policy.PendingCount(), "failed send cannot become implicit repeated-notice retry"); }
    { Policy p; p.Notice(); p.policy.Pump(std::nullopt, [&] { return false; }, [&](auto m, auto* g) { return p.Submit(m,g); }); p.Finish(); p.Notice(); check(p.sends == 1, "temporary missing context does not erase consumed dedupe"); }
    { Policy p; p.busy = true; p.Notice(); p.policy.Pump(std::nullopt, [&] { return true; }, [&](auto m, auto* g) { return p.Submit(m,g); }); check(!p.policy.PendingCount() && p.Last("discard", "captured-context-changed"), "missing admission explicitly invalidates pending candidate"); }
    { Policy p; p.busy = true; p.Notice(); p.policy.Shutdown(); check(!p.policy.PendingCount() && p.Last("discard", "shutdown"), "shutdown records pending disposition"); }
    { Policy p; p.Notice(); const auto row = formatAutomaticResyncNotice(p.events.back(), 4, 99, 3); check(row.find("receipt=1") != std::string::npos && row.find("connection=8589934594") != std::string::npos && row.find("request=1") != std::string::npos && row.find("dropped=0") != std::string::npos, "actual receipt formatter preserves causal request and full-width identity"); }
}
void realGenerator() {
    SessionConfig cfg; cfg.bindAddress = "127.0.0.1"; cfg.port = 17921; cfg.gameBuild = "automatic-notice-private"; cfg.modHash = "m"; cfg.contentHash = "c";
    SessionHost relay(cfg); check(relay.start(), "owned loopback relay starts");
    std::array<std::unique_ptr<NetworkClient>, 3> clients;
    std::vector<AutomaticResyncNotice::Event> events;
    AutomaticResyncNotice policy(true, [&](const auto& e) { events.push_back(e); });
    for (unsigned i = 0; i < 3; ++i) {
        ClientCallbacks callbacks;
        if (!i) callbacks.onResyncResult = [&](const auto& r) { policy.Terminal(r); };
        clients[i] = std::make_unique<NetworkClient>("127.0.0.1", cfg.port, cfg.gameBuild, cfg.modHash, "notice" + std::to_string(i), static_cast<SlotType>(i), callbacks, RuntimeMode::CampaignCoop, cfg.contentHash);
        check(clients[i]->connect(), "owned loopback client starts");
    }
    const auto wait = [&](const std::function<bool()>& predicate) { const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(4); while (!predicate() && std::chrono::steady_clock::now() < end) { relay.tick(0); for (auto& c : clients) { c->tick(0); c->sendHeartbeat(); } std::this_thread::sleep_for(std::chrono::milliseconds(1)); } return predicate(); };
    const bool ready = wait([&] { return clients[0]->worldReady() && clients[1]->worldReady() && clients[2]->worldReady(); });
    check(ready, "real original three-peer bindings admitted");
    if (ready) {
        auto& host = *clients[0]; host.sendRoomTransition({7,5,6,0,1,1,0});
        check(host.hostResyncContext().has_value() && !clients[1]->hostResyncContext(), "actual host context accessor cannot expose a friend request context");
        const auto submit = [&](std::uint8_t mask, ResyncRequest* generated) { return host.requestWorldResync(mask, generated); };
        for (unsigned mode = 0; mode < 3; ++mode) {
            AutomaticResyncNotice retired(true, [](const auto&) {});
            const auto next = host.nextResyncRequest_; const auto beforeDeadline = host.resyncDeadline_;
            const auto guarded = [&](std::uint8_t mask, ResyncRequest* generated) {
                return automaticResyncRuntimeCurrent(mode != 0, mode != 1, mode == 2, host) &&
                    host.requestWorldResync(mask, generated);
            };
            retired.Notice({SlotType::Friend1,7,recoveryFields}, host.hostResyncContext(), false, 50, guarded);
            check(host.worldReady() && !host.resyncBusy() && host.nextResyncRequest_ == next && host.resyncDeadline_ == beforeDeadline,
                "runtime stop/unadmitted/invalidated guard blocks immediate request despite network-ready");
            AutomaticResyncNotice pending(true, [](const auto&) {});
            pending.Notice({SlotType::Friend1,7,recoveryFields}, host.hostResyncContext(), true, 60, guarded);
            ResyncResult terminal; terminal.key = {host.worldBinding()->sessionId,host.worldBinding()->hostConnectionId,99};
            pending.Terminal(terminal); pending.Pump(host.hostResyncContext(), [&] { return host.resyncBusy(); }, guarded);
            check(!pending.PendingCount() && !host.resyncBusy() && host.nextResyncRequest_ == next && host.resyncDeadline_ == beforeDeadline,
                "runtime retirement blocks terminal-drained request without ID/deadline mutation");
        }
        policy.Notice({SlotType::Friend1,7,recoveryFields}, host.hostResyncContext(), host.resyncBusy(), 100, submit);
        check(host.resyncBusy() && host.requestedResync_ && !host.pendingResync(), "combined busy includes real generated request before first plan");
        const auto request = *host.requestedResync_; const auto deadline = host.resyncDeadline_;
        const auto eventCount = events.size(); const auto nextId = host.nextResyncRequest_;
        const auto interval = formatAutomaticResyncInterval(policy, host, 1, 250, eventCount, 12, 3, request.connections);
        std::cout << interval << '\n';
        check(interval.find("action=interval sealSeq=1") != std::string::npos && interval.find("requested=1 planned=0 busy=1") != std::string::npos && interval.find("worldCauseHighWater=12") != std::string::npos,
            "interval seal reports actual request-only busy and producer highwaters");
        check(host.resyncDeadline_ == deadline && host.nextResyncRequest_ == nextId && events.size() == eventCount && policy.PendingCount() == 0,
            "interval observation cannot submit, drain or change request/deadline state");
        check(events.back().key == request.key && request.targetMask == 2 && request.connections[1] == clients[1]->worldBinding()->selfConnectionId, "production generator emits the exact logged key/current target");
        policy.Notice({SlotType::Friend1,7,recoveryFields}, host.hostResyncContext(), host.resyncBusy(), 200, submit);
        check(host.resyncDeadline_ == deadline && host.requestedResync_->key == request.key, "duplicate hint cannot renew real request deadline or ID");
        check(wait([&] { return host.pendingResync().has_value(); }), "actual relay admits generated single-target Bootstrap plan");
        check(host.resyncBusy() && !host.requestedResync_ && host.pendingResync()->request.key == request.key, "combined busy covers plan after requested state clears");
        policy.Notice({SlotType::Friend2,7,recoveryFields}, host.hostResyncContext(), host.resyncBusy(), 300, submit);
        check(policy.PendingCount() == 1 && host.resyncDeadline_ == deadline, "second target coalesces without touching active deadline");
        const auto pendingSeal = formatAutomaticResyncInterval(policy, host, 2, 300, events.size(), 13, 3, request.connections);
        std::cout << pendingSeal << '\n';
        check(pendingSeal.find("pending=1 requested=0 planned=1 busy=1") != std::string::npos && policy.PendingCount() == 1 && host.resyncDeadline_ == deadline,
            "interval seal preserves busy-plan pending candidate instead of retrying it");
        relay.pumpResync(relay.resyncDeadlineMs());
        check(wait([&] { return !host.resyncBusy(); }), "real terminal deadline releases network busy state");
        const auto terminalSeal = formatAutomaticResyncInterval(policy, host, 3, 400, events.size(), 14, 3, request.connections);
        std::cout << terminalSeal << '\n';
        check(terminalSeal.find("pending=1 requested=0 planned=0 busy=0") != std::string::npos && policy.PendingCount() == 1 && !host.requestedResync_,
            "post-terminal seal does not drain even when a candidate is ready");
        policy.Pump(host.hostResyncContext(), [&] { return host.resyncBusy(); }, submit);
        check(host.requestedResync_ && host.requestedResync_->targetMask == 4 && host.requestedResync_->key.requestId > request.key.requestId, "terminal-only drain uses generator for next original slot");
        policy.Notice({SlotType::Friend1,7,recoveryFields}, host.hostResyncContext(), host.resyncBusy(), 400, submit);
        check(policy.PendingCount() == 0, "original failed transaction's same-key notice stays consumed");
    }
    policy.Shutdown(); for (auto& c : clients) c->disconnect(); relay.stop();
}
}
int main() { if (enet_initialize() != 0) return 2; policyControls(); realGenerator(); enet_deinitialize(); std::cout << "checks=" << checks << " failures=" << failures << '\n'; return failures ? 1 : 0; }
