#pragma once
#include "kh2coop/Types.hpp"
#include <cmath>
#include <cstdint>

namespace kh2coop::avatarfault {
inline constexpr std::uint64_t Magic = 0x314654534F504156ULL;
inline constexpr unsigned Frames = 8, ActiveMs = 1000, DormantMs = 10000;
struct Binding {
    std::uint64_t actor {}, ownerConnection {}, localConnection {}, hostConnection {};
    std::uint32_t handle {}, transition {}, load {}, generation {};
    std::uint16_t world {}, room {};
    std::uint8_t owner {}, character {}, puppetIndex {}, producer {}, localSlot {};
    bool operator==(const Binding&) const = default;
};
struct Admission {
    bool readable {}, active {}, unheld {}, friend1 {}, repeated {};
    std::uint32_t objectId {}, handle {};
    std::uint8_t type {}, world {}, room {}, menu {};
    std::int32_t cutscene {};
    std::uint64_t event {};
};
inline bool Admitted(const Admission& a, const Binding& b) noexcept {
    return a.readable && a.active && a.unheld && a.friend1 && a.repeated &&
        a.objectId==92 && a.type==1 && a.handle && a.handle==b.handle &&
        a.world==4 && a.room==26 && a.menu==255 && !a.cutscene && !a.event &&
        b.actor && b.world==4 && b.room==26 && b.puppetIndex==0 && b.producer==1 &&
        b.localSlot==255 && !b.generation && !b.ownerConnection && !b.localConnection && !b.hostConnection;
}
struct Offer {
    std::uint64_t magic {}, creation {}, tick {};
    std::uint32_t version {}, size {}, pid {}, eligible {};
    Binding binding {};
};
struct Request {
    Offer offer {};
    std::uint64_t nonce {}, tick {}, deadline {};
    Vec3 activation {};
    std::uint32_t frames {}, activeMs {};
};
inline bool FiniteTarget(Vec3 p) noexcept {
    return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z) &&
        std::abs(p.x) <= 100000.0f && std::abs(p.y) <= 100000.0f && std::abs(p.z) <= 100000.0f;
}
inline bool SameRequest(const Request& a, const Request& b) noexcept {
    return a.offer.magic == b.offer.magic && a.offer.creation == b.offer.creation && a.offer.tick == b.offer.tick &&
        a.offer.version == b.offer.version && a.offer.size == b.offer.size && a.offer.pid == b.offer.pid &&
        a.offer.eligible == b.offer.eligible && a.offer.binding == b.offer.binding && a.nonce == b.nonce &&
        a.tick == b.tick && a.deadline == b.deadline && a.activation.x == b.activation.x &&
        a.activation.y == b.activation.y && a.activation.z == b.activation.z && a.frames == b.frames && a.activeMs == b.activeMs;
}
struct Event { const char* state; const char* reason; std::uint64_t tick; std::uint32_t frame, skipped; Request request; };
// Single-use process-lifetime gate. Never writes actor memory; production calls
// this only inside its existing xyz write lambda. All refusals delegate writes.
class Gate {
    enum class State { Fresh, Armed, Active, Done } state_ {State::Fresh};
    Request request_ {};
    std::uint64_t start_ {};
    std::uint32_t lastFrame_ {}, skipped_ {};
public:
    void Cancel() noexcept { state_ = State::Done; }
    bool Consumed() const noexcept { return state_ != State::Fresh; }
    template<class Emit> void Stop(const char* reason, std::uint64_t now, std::uint32_t frame, Emit emit) {
        if (state_ == State::Done) return;
        state_ = State::Done;
        emit(Event{"release",reason,now,frame,skipped_,request_});
    }
    template<class Emit> void Tick(std::uint64_t now, std::uint32_t frame, Emit emit) {
        if (state_ == State::Armed && now >= request_.deadline) Stop("deadline",now,frame,emit);
        if (state_ == State::Active && (now >= request_.deadline || now < start_ || now-start_ >= ActiveMs))
            Stop("active-deadline",now,frame,emit);
    }
    template<class Emit>
    bool Skip(bool enabled, bool readable, const Request* incoming, const Offer& current,
              Vec3 target, std::uint64_t now, std::uint32_t frame, Emit emit) {
        if (!enabled || state_ == State::Done) return false;
        if (!readable) { if (Consumed()) Stop("unreadable",now,frame,emit); return false; }
        if (state_ == State::Fresh) {
            if (!incoming) return false;
            request_ = *incoming;
            const auto& q = request_; const auto& o = q.offer;
            const bool valid = q.nonce && o.magic == Magic && o.version == 1 && o.size == sizeof(Offer) &&
                o.pid == current.pid && o.creation == current.creation && o.eligible && current.eligible &&
                o.binding == current.binding && q.frames == Frames && q.activeMs == ActiveMs &&
                FiniteTarget(q.activation) && now >= o.tick && now - o.tick <= 500 &&
                q.tick >= o.tick && q.tick <= now && q.deadline > now &&
                q.deadline >= q.tick && q.deadline - q.tick <= DormantMs;
            if (!valid) { Stop("malformed",now,frame,emit); return false; }
            state_ = State::Armed; emit(Event{"arm","accepted",now,frame,0,request_});
        } else if (!incoming || !SameRequest(request_,*incoming)) {
            Stop("changed",now,frame,emit); return false;
        }
        if (!current.eligible || current.pid != request_.offer.pid || current.creation != request_.offer.creation || !(current.binding == request_.offer.binding)) { Stop("scope",now,frame,emit); return false; }
        if (now >= request_.deadline) { Stop("deadline",now,frame,emit); return false; }
        if (state_ == State::Armed) {
            if (!FiniteTarget(target) || std::abs(target.x-request_.activation.x)>0.01f ||
                std::abs(target.y-request_.activation.y)>0.01f || std::abs(target.z-request_.activation.z)>0.01f) return false;
            state_ = State::Active; start_ = now; lastFrame_ = frame; skipped_ = 1;
            emit(Event{"firstskip","target",now,frame,skipped_,request_}); return true;
        }
        if (!FiniteTarget(target) || std::abs(target.x-request_.activation.x)>0.01f ||
            std::abs(target.y-request_.activation.y)>0.01f || std::abs(target.z-request_.activation.z)>0.01f) {
            Stop("target",now,frame,emit); return false;
        }
        if (now < start_ || now - start_ >= ActiveMs) { Stop("active-deadline",now,frame,emit); return false; }
        if (frame != lastFrame_) {
            if (frame-lastFrame_ != 1) { Stop("frame-gap",now,frame,emit); return false; }
            if (skipped_ >= Frames) { Stop("budget",now,frame,emit); return false; }
            lastFrame_ = frame; ++skipped_;
        }
        return true;
    }
};
}
