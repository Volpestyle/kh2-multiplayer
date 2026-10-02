#pragma once
// ============================================================================
// ProgressMirror — host story/world flags mirrored onto clients (plan D6,
// VUH-1495/1497), the Archipelago way: an idempotent log, re-asserted.
//
// Only bytes inside the allow list ever travel or get written; character
// stats and inventory stay per player (D8) by keeping them off the list.
//
//   host:   diff(previous save region, current save region) -> spans
//   client: accept(spans) records the host's values;
//           pending(live save region) -> spans still to write.
// pending() is meant to be re-run after every room load and death: synced
// flags don't update objects that are already loaded, so the runtime applies
// at room boundaries and keeps re-asserting until live matches desired.
// ============================================================================

#include "kh2coop/Protocol.hpp"

#include <algorithm>
#include <cstdint>
#include <map>
#include <vector>

namespace kh2coop {

struct ProgressRange {
    std::uint32_t offset {0}; // from the save body base
    std::uint32_t length {0};
};

class ProgressMirror {
public:
    explicit ProgressMirror(std::vector<ProgressRange> allow)
        : allow_(std::move(allow)) {
        std::sort(allow_.begin(), allow_.end(),
                  [](const ProgressRange& a, const ProgressRange& b) {
                      return a.offset < b.offset;
                  });
    }

    [[nodiscard]] bool allowed(std::uint32_t offset) const {
        for (const auto& r : allow_) {
            if (offset >= r.offset && offset < r.offset + r.length) return true;
        }
        return false;
    }

    // Host: changed bytes inside the allow list, coalesced into spans.
    [[nodiscard]] std::vector<ProgressSpan> diff(const std::uint8_t* before,
                                                 const std::uint8_t* after,
                                                 std::size_t size) const {
        std::vector<ProgressSpan> spans;
        for (const auto& r : allow_) {
            for (std::uint32_t o = r.offset; o < r.offset + r.length && o < size; ++o) {
                if (before[o] == after[o]) continue;
                if (!spans.empty() &&
                    spans.back().offset + spans.back().bytes.size() == o) {
                    spans.back().bytes.push_back(after[o]);
                } else {
                    spans.push_back({o, {after[o]}});
                }
            }
        }
        return spans;
    }

    // Host: every allowed byte, for a full snapshot.
    [[nodiscard]] std::vector<ProgressSpan> snapshot(const std::uint8_t* save,
                                                     std::size_t size) const {
        std::vector<ProgressSpan> spans;
        for (const auto& r : allow_) {
            if (r.offset >= size) continue;
            const auto end = std::min<std::size_t>(r.offset + r.length, size);
            spans.push_back({r.offset, std::vector<std::uint8_t>(save + r.offset, save + end)});
        }
        return spans;
    }

    // Client: record the host's values. Bytes outside the allow list are
    // dropped (a malformed or hostile update can't touch stats/inventory).
    // Returns the number of bytes rejected.
    std::size_t accept(const ProgressUpdate& u) {
        if (u.full) desired_.clear();
        std::size_t rejected = 0;
        for (const auto& s : u.spans) {
            for (std::size_t i = 0; i < s.bytes.size(); ++i) {
                const auto o = static_cast<std::uint32_t>(s.offset + i);
                if (allowed(o)) desired_[o] = s.bytes[i];
                else ++rejected;
            }
        }
        return rejected;
    }

    // Client: spans where the live save differs from the host's values.
    [[nodiscard]] std::vector<ProgressSpan> pending(const std::uint8_t* live,
                                                    std::size_t size) const {
        std::vector<ProgressSpan> spans;
        for (const auto& [o, v] : desired_) {
            if (o >= size || live[o] == v) continue;
            if (!spans.empty() && spans.back().offset + spans.back().bytes.size() == o) {
                spans.back().bytes.push_back(v);
            } else {
                spans.push_back({o, {v}});
            }
        }
        return spans;
    }

    [[nodiscard]] std::size_t desiredSize() const { return desired_.size(); }

private:
    std::vector<ProgressRange> allow_;
    std::map<std::uint32_t, std::uint8_t> desired_;
};

// Splits spans into updates whose encoded payload stays under the 16-bit
// packet length limit. The first update carries `full`.
inline std::vector<ProgressUpdate> splitProgressUpdate(
    std::uint32_t version, bool full, const std::vector<ProgressSpan>& spans,
    std::size_t maxBytes = 48000) {
    std::vector<ProgressUpdate> out;
    ProgressUpdate cur {version, full, {}};
    std::size_t used = 0;
    for (const auto& span : spans) {
        std::size_t pos = 0;
        while (pos < span.bytes.size()) {
            if (used >= maxBytes) {
                out.push_back(std::move(cur));
                cur = ProgressUpdate {version, false, {}};
                used = 0;
            }
            const auto take = std::min(span.bytes.size() - pos, maxBytes - used);
            cur.spans.push_back({static_cast<std::uint32_t>(span.offset + pos),
                                 std::vector<std::uint8_t>(span.bytes.begin() + pos,
                                                           span.bytes.begin() + pos + take)});
            used += take + 8;
            pos += take;
        }
    }
    if (!cur.spans.empty() || out.empty()) out.push_back(std::move(cur));
    return out;
}

} // namespace kh2coop
