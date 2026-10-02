#pragma once
// ============================================================================
// LinkConditioner — deterministic latency / jitter / loss for one direction
// of a link, so network tests are repeatable without OS-level tools.
//
// Packets enter with enqueue() and leave through popDue() once their delay
// has elapsed. Unreliable packets may be dropped; reliable ones are only
// delayed (ENet would retransmit them, so dropping them would understate
// delivery). Jitter never reorders packets: delivery times are monotonic,
// matching ENet's sequenced channels.
// ============================================================================

#include <cstdint>
#include <deque>
#include <random>
#include <vector>

namespace kh2coop {

struct LinkConditions {
    std::uint32_t latencyMs {0}; // one-way delay added to every packet
    std::uint32_t jitterMs {0};  // extra uniform delay in [0, jitterMs]
    float lossRate {0.0f};       // drop probability for unreliable packets
    std::uint32_t seed {1};

    [[nodiscard]] bool active() const {
        return latencyMs > 0 || jitterMs > 0 || lossRate > 0.0f;
    }
};

class LinkConditioner {
public:
    struct Packet {
        std::vector<std::uint8_t> bytes;
        bool reliable {false};
    };

    void configure(const LinkConditions& conditions) {
        conditions_ = conditions;
        rng_.seed(conditions.seed);
        lastDueMs_ = 0;
    }

    [[nodiscard]] const LinkConditions& conditions() const { return conditions_; }

    // Returns false if the packet was dropped.
    bool enqueue(std::uint64_t nowMs, std::vector<std::uint8_t> bytes,
                 bool reliable) {
        if (!reliable && conditions_.lossRate > 0.0f &&
            loss_(rng_) < conditions_.lossRate) {
            ++dropped_;
            return false;
        }
        std::uint64_t due = nowMs + conditions_.latencyMs;
        if (conditions_.jitterMs > 0) {
            due += std::uniform_int_distribution<std::uint32_t>(
                0, conditions_.jitterMs)(rng_);
        }
        if (due < lastDueMs_) due = lastDueMs_; // keep order
        lastDueMs_ = due;
        queue_.push_back({due, Packet {std::move(bytes), reliable}});
        return true;
    }

    std::vector<Packet> popDue(std::uint64_t nowMs) {
        std::vector<Packet> out;
        while (!queue_.empty() && queue_.front().dueMs <= nowMs) {
            out.push_back(std::move(queue_.front().packet));
            queue_.pop_front();
        }
        return out;
    }

    [[nodiscard]] std::size_t pending() const { return queue_.size(); }
    [[nodiscard]] std::uint64_t dropped() const { return dropped_; }

private:
    struct Entry {
        std::uint64_t dueMs {0};
        Packet packet;
    };

    LinkConditions conditions_ {};
    std::mt19937 rng_ {1};
    std::uniform_real_distribution<float> loss_ {0.0f, 1.0f};
    std::deque<Entry> queue_;
    std::uint64_t lastDueMs_ {0};
    std::uint64_t dropped_ {0};
};

} // namespace kh2coop
