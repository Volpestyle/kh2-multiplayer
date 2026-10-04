#pragma once
#include "kh2coop/KnownControllerMutation.hpp"
#include "kh2coop/NativeRecordContent.hpp"
#include "kh2coop/ResyncProtocol.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace kh2coop {

// Pure supplied-evidence reducer. No native call, pointer, action permit, or
// lifetime/exclusion claim. The caller owns admission and fresh native reads.
enum class SurvivingPackClassification {
    Disabled, Unsupported, Unavailable, FullSetAlreadyPresent, PartialSet,
    ListedEmptyUnqualified, Conflict, Terminal
};
enum class SurvivingPackReason {
    None, InvalidIntent, DifferentIntent, Deadline, ContextChanged, LoadChanged,
    MutationChanged, IncompleteFacts, ContentMismatch, UnsupportedPack,
    OccupancyConflict, Pending, InvalidOutcome, UnknownOutcome, MaterialChange,
    Cancelled
};
enum SurvivingPackMissing : std::uint32_t {
    PackMissingNone = 0, PackMissingFreshLoad = 1, PackMissingCatalog = 2,
    PackMissingMembers = 4, PackMissingListed = 8, PackMissingCache = 16,
    PackMissingGlobalPrelinkCoverage = 32, PackMissingMutationTicket = 64,
    PackMissingPendingCoverage = PackMissingGlobalPrelinkCoverage, // legacy name; never cleared by lists
    PackMissingKnownBoundaryCoverage = 128,
    // Always missing here: even complete supplied samples are not permission.
    PackMissingNativeExecutionAuthority = 256, PackMissingControllerState = 512
};
enum class SurvivingPackListed {
    Unavailable, NoSelectedReferences, Conflict, Pending, Unclassifiable,
    SelectedReadyReferences, Deferred
};
enum class SurvivingPackSlotState { NotAttempted, Entered, Pending, Ready, Unknown };
enum class SurvivingPackOutcome { Entry, NonNullReturn, Ready, NullReturn, Unknown };
struct SurvivingPackSlot {
    SurvivingPackSlotState state{SurvivingPackSlotState::NotAttempted};
    std::uint64_t invocation{};
};
struct SurvivingPackIntent {
    ResyncBegin begin;
    ResyncTarget target;
    ProducerWorldContext context;
    std::uint32_t loadBefore{};
    std::uint64_t deadlineMs{}; // existing absolute local deadline, never renewed
    ResyncSnapshot snapshot;
};
struct SurvivingPackMember {
    ResyncRecordReference record; // indices in Facts.catalog, not host/net IDs
    std::uint32_t objectId{}, objectType{};
    std::int32_t hp{}, maxHp{};
    bool exactMembership{};
};
// Retained sampled diagnostics only. Counts/stage/activation do not certify
// initialization history; cooldown is never decremented or modified here.
struct SurvivingPackControllerState {
    bool available{};
    std::uint32_t flags{};
    std::int32_t currentCount{}, initialCount{};
    float cooldown{};
    std::uint8_t stage{}, activation{};
};
struct SurvivingPackFacts {
    ProducerWorldContext context;
    std::uint32_t load{};
    bool arrived{};
    KnownControllerMutationTicket mutation;
    NativeRecordContentStatus catalogStatus{NativeRecordContentStatus::Unavailable};
    std::span<const NativeRecordContentCandidate> catalog;
    bool readyMembersComplete{};
    std::span<const SurvivingPackMember> readyMembers;
    SurvivingPackListed listed{SurvivingPackListed::Unavailable};
    // SelectedReadyReferences is supplied only after a complete raw active +
    // deferred sample and exact actor/controller/record-to-ready-census join.
    // A nonnull status pointer alone cannot supply it. Masks index the five
    // selected records, not native IDs, table slots, netIds or controller count.
    std::uint8_t listedReadyMask{}, cacheSelectedMask{};
    std::uint16_t cacheEntryCount{}; // all nonzero slots in the checked 256-ID bucket
    // cacheConflict denotes duplicate/unresolved/conflicting provenance, not
    // legitimate selected IDs present in the cache. Reader retains raw bytes.
    bool cacheAvailable{}, cacheConflict{};
    SurvivingPackControllerState controller;
    // No supplied flag can clear global/pre-link creator or execution limits.

};

class SurvivingPackPreparation {
public:
    bool Begin(bool enabled, const SurvivingPackIntent& input, std::uint64_t nowMs) {
        if (!enabled || Terminal() || rejected_) return false;
        // Bound caller input before copies/serialization. Codec retains its
        // existing detailed framing, snapshot, progress and reference checks.
        if (input.snapshot.enemies.size() != 5 || input.snapshot.recordDefinitions.empty() ||
            input.snapshot.recordDefinitions.size() > NativeRecordContentMaxDefinitions)
            return RejectIntent(SurvivingPackReason::InvalidIntent);
        std::size_t total = 0;
        for (const auto& definition : input.snapshot.recordDefinitions) {
            if (definition.records.size() > NativeRecordContentMaxRecords ||
                definition.records.size() > NativeRecordContentMaxTotalRecords - total)
                return RejectIntent(SurvivingPackReason::InvalidIntent);
            total += definition.records.size();
        }
        std::vector<std::uint8_t> bytes, beginBytes;
        try {
            bytes = encodeResyncSnapshot(input.snapshot);
            beginBytes = encode(input.begin);
        } catch (const std::exception&) { return RejectIntent(SurvivingPackReason::InvalidIntent); }
        if (intent_) {
            if (bytes != snapshotBytes_ || beginBytes != beginBytes_ || input.target != intent_->target ||
                !SameContext(input.context, intent_->context) || input.loadBefore != intent_->loadBefore)
                return Stop(SurvivingPackReason::DifferentIntent);
            intent_->deadlineMs = (std::min)(intent_->deadlineMs, input.deadlineMs);
            return Live(nowMs); // exact duplicate cannot refresh the deadline
        }
        if (input.deadlineMs > nowMs && input.deadlineMs - nowMs > RESYNC_TIMEOUT_MS)
            return RejectIntent(SurvivingPackReason::InvalidIntent);
        const auto& b = input.begin;
        const auto& s = input.snapshot;
        if (!input.context.generation || !input.context.deliverySerial || !input.loadBefore ||
            input.context.deliverySerial != input.target.deliverySerial || !b.snapshotCut ||
            !b.key.hostConnectionId || !b.key.requestId || b.key.sessionId.empty() ||
            b.phase != ResyncPhase::Bootstrap || !sameResyncRoom(b.room, s.room) ||
            !b.targetCount || b.targetCount > b.targets.size() ||
            input.target.slot < 1 || input.target.slot > 2 || !input.target.connectionId ||
            std::count(b.targets.begin(), b.targets.begin() + b.targetCount, input.target) != 1 ||
            s.coverageMask != ResyncNativeComplete || s.livingCount != 5 || s.deadCount || s.hold.active ||
            bytes.size() != b.totalBytes || desyncSha256(bytes) != b.sha256 ||
            b.partCount != (bytes.size() + RESYNC_MAX_PART_BYTES - 1) / RESYNC_MAX_PART_BYTES)
            return RejectIntent(SurvivingPackReason::InvalidIntent);
        const auto selected = s.enemies.front().record.definitionIndex;
        if (selected >= s.recordDefinitions.size()) return RejectIntent(SurvivingPackReason::InvalidIntent);
        const auto& definition = s.recordDefinitions[selected];
        if (!Supported(definition)) return RejectIntent(SurvivingPackReason::UnsupportedPack);
        std::array<bool, 5> seen{};
        for (const auto& row : s.enemies) {
            if (row.record.definitionIndex != selected || row.record.recordIndex >= 5 ||
                seen[row.record.recordIndex] || row.identity.objectId != 302 || row.objectType != 4 ||
                row.life != ResyncLife::Alive || row.hp <= 0 || row.hp > row.maxHp)
                return RejectIntent(SurvivingPackReason::UnsupportedPack);
            seen[row.record.recordIndex] = true;
        }
        std::vector<NativeRecordContentCandidate> candidates;
        for (const auto& d : s.recordDefinitions)
            candidates.push_back({&d, NativeRecordContentStatus::Complete, true});
        if (ResolveNativeRecordContent(definition, candidates, NativeRecordContentStatus::Complete).status !=
            NativeRecordContentStatus::Complete) return RejectIntent(SurvivingPackReason::InvalidIntent);
        intent_ = input; selected_ = selected;
        snapshotBytes_ = std::move(bytes); beginBytes_ = std::move(beginBytes);
        classification_ = SurvivingPackClassification::Unavailable;
        missing_ = PackMissingFreshLoad | PackMissingNativeExecutionAuthority | PackMissingGlobalPrelinkCoverage;
        lastSourceSerial_ = b.snapshotCut;
        return Live(nowMs);
    }

    SurvivingPackClassification Observe(const SurvivingPackFacts& facts, std::uint64_t nowMs) {
        if (!Live(nowMs)) return classification_;
        missing_ = PackMissingNativeExecutionAuthority | PackMissingGlobalPrelinkCoverage;
        reason_ = SurvivingPackReason::None;
        if (!SameContext(facts.context, intent_->context)) {
            Stop(SurvivingPackReason::ContextChanged); return classification_;
        }
        if (!facts.arrived || !facts.load || facts.load == intent_->loadBefore) {
            if (mutation_) Stop(SurvivingPackReason::LoadChanged);
            else Missing(PackMissingFreshLoad);
            return classification_;
        }
        if (mutation_ && facts.load != targetLoad_) {
            Stop(SurvivingPackReason::LoadChanged); return classification_;
        }
        if (!CheckMutation(facts.mutation, false)) return classification_;
        if (!mutation_) { mutation_ = facts.mutation; targetLoad_ = facts.load; }
        controller_ = facts.controller;
        if (!facts.controller.available) missing_ |= PackMissingControllerState;
        if (!facts.mutation.coverageComplete) missing_ |= PackMissingKnownBoundaryCoverage;
        if (facts.listed == SurvivingPackListed::Unavailable) missing_ |= PackMissingListed;
        if (!facts.cacheAvailable) missing_ |= PackMissingCache;
        if ((facts.listedReadyMask & 0xe0u) || (facts.cacheSelectedMask & 0xe0u) ||
            facts.cacheEntryCount > 256 ||
            (facts.cacheAvailable && std::popcount(static_cast<unsigned>(facts.cacheSelectedMask)) > facts.cacheEntryCount)) {
            OccupancyConflict(); return classification_;
        }
        switch (facts.listed) {
        case SurvivingPackListed::Unavailable: case SurvivingPackListed::NoSelectedReferences:
        case SurvivingPackListed::SelectedReadyReferences: case SurvivingPackListed::Conflict:
        case SurvivingPackListed::Pending: case SurvivingPackListed::Deferred:
        case SurvivingPackListed::Unclassifiable: break;
        default: OccupancyConflict(); return classification_;
        }
        if (facts.listed == SurvivingPackListed::Conflict || facts.listed == SurvivingPackListed::Unclassifiable ||
            facts.listed == SurvivingPackListed::Pending || facts.listed == SurvivingPackListed::Deferred ||
            facts.cacheConflict) {
            if (facts.listed == SurvivingPackListed::Pending && !facts.cacheConflict) {
                // Delayed readiness can be an expected continuation of a known
                // nonnull return. Hold without erasing or replaying its slot.
                classification_ = SurvivingPackClassification::Conflict;
                reason_ = SurvivingPackReason::Pending;
            } else OccupancyConflict();
            return classification_;
        }
        if (facts.catalogStatus != NativeRecordContentStatus::Complete) {
            Missing(PackMissingCatalog); return classification_;
        }
        if (facts.catalog.size() != intent_->snapshot.recordDefinitions.size()) {
            Conflict(); return classification_;
        }
        std::optional<std::size_t> localSelected;
        for (std::size_t i = 0; i < intent_->snapshot.recordDefinitions.size(); ++i) {
            const auto resolved = ResolveNativeRecordContent(intent_->snapshot.recordDefinitions[i],
                facts.catalog, facts.catalogStatus);
            if (resolved.status != NativeRecordContentStatus::Complete) {
                if (resolved.status == NativeRecordContentStatus::Partial ||
                    resolved.status == NativeRecordContentStatus::Unavailable) Missing(PackMissingCatalog);
                else Conflict();
                return classification_;
            }
            if (i == selected_) localSelected = resolved.definitionIndex;
        }
        if (!facts.readyMembersComplete) { Missing(PackMissingMembers); return classification_; }
        if (facts.readyMembers.size() > 5) { Conflict(); return classification_; }
        std::array<bool, 5> seen{};
        for (const auto& member : facts.readyMembers) {
            if (!member.exactMembership) { Missing(PackMissingMembers); return classification_; }
            const auto index = member.record.recordIndex;
            if (!localSelected || member.record.definitionIndex != *localSelected || index >= 5 || seen[index] ||
                member.objectId != 302 || member.objectType != 4 || member.hp <= 0 || member.hp > member.maxHp) {
                Conflict(); return classification_;
            }
            const auto row = std::find_if(intent_->snapshot.enemies.begin(), intent_->snapshot.enemies.end(),
                [index](const auto& e) { return e.record.recordIndex == index; });
            if (row == intent_->snapshot.enemies.end() || row->maxHp != member.maxHp) {
                Conflict(); return classification_;
            }
            seen[index] = true;
        }
        std::uint8_t readyMask = 0;
        for (std::size_t i = 0; i < seen.size(); ++i)
            if (seen[i]) readyMask |= static_cast<std::uint8_t>(1u << i);
        if ((facts.listed == SurvivingPackListed::NoSelectedReferences &&
             (readyMask || facts.listedReadyMask)) ||
            (facts.listed == SurvivingPackListed::SelectedReadyReferences &&
             (!facts.listedReadyMask || facts.listedReadyMask != readyMask)) ||
            (facts.cacheAvailable && facts.cacheSelectedMask != readyMask)) {
            OccupancyConflict(); return classification_;
        }
        // A nonempty room cache with no selected actors is not a virgin empty
        // enrollment baseline, even when its entries concern other records.
        if (!readyMask && facts.cacheAvailable && facts.cacheEntryCount) {
            OccupancyConflict(); return classification_;
        }
        for (const auto& slot : slots_) {
            if (slot.state == SurvivingPackSlotState::Entered || slot.state == SurvivingPackSlotState::Pending) {
                classification_ = SurvivingPackClassification::Conflict;
                reason_ = SurvivingPackReason::Pending; return classification_;
            }
        }
        // Ready outcome receipts never substitute for current membership.
        for (std::size_t i = 0; i < slots_.size(); ++i)
            if (slots_[i].state == SurvivingPackSlotState::Ready && !seen[i]) {
                Stop(SurvivingPackReason::UnknownOutcome); return classification_;
            }
        if (facts.readyMembers.size() == 5) classification_ = SurvivingPackClassification::FullSetAlreadyPresent;
        else if (!facts.readyMembers.empty()) classification_ = SurvivingPackClassification::PartialSet;
        else if (facts.listed == SurvivingPackListed::NoSelectedReferences && facts.cacheAvailable &&
                 !facts.cacheEntryCount && !facts.cacheSelectedMask)
            classification_ = SurvivingPackClassification::ListedEmptyUnqualified;
        else Missing(PackMissingNone); // complete ready census is empty; other facts are missing
        return classification_;
    }

    // Supplied original-entry/return/readiness observations only. True means the
    // receipt was recorded, NEVER permission to make a call. No native address
    // is promoted into lifetime identity. A nonnull unreadable return is Pending.
    bool ObserveOutcome(std::size_t recordIndex, std::uint64_t invocation,
                        SurvivingPackOutcome outcome, const KnownControllerMutationTicket& ticket,
                        std::uint64_t nowMs) {
        if (!Live(nowMs)) return false;
        if (!mutation_ || !CheckMutation(ticket, true)) return false;
        if (recordIndex >= slots_.size() || !invocation) return Stop(SurvivingPackReason::InvalidOutcome);
        auto& slot = slots_[recordIndex];
        if (outcome == SurvivingPackOutcome::Entry) {
            if (slot.invocation == invocation && slot.state != SurvivingPackSlotState::NotAttempted) return false;
            if (slot.state != SurvivingPackSlotState::NotAttempted || invocation <= lastInvocation_)
                return Stop(SurvivingPackReason::InvalidOutcome);
            for (const auto& old : slots_)
                if (old.state == SurvivingPackSlotState::Entered || old.state == SurvivingPackSlotState::Pending)
                    return Stop(SurvivingPackReason::InvalidOutcome);
            slot = {SurvivingPackSlotState::Entered, invocation}; lastInvocation_ = invocation;
        } else {
            if (slot.invocation != invocation || slot.state == SurvivingPackSlotState::NotAttempted)
                return Stop(SurvivingPackReason::InvalidOutcome);
            if (outcome == SurvivingPackOutcome::NonNullReturn) {
                if (slot.state == SurvivingPackSlotState::Pending || slot.state == SurvivingPackSlotState::Ready) return false;
                if (slot.state != SurvivingPackSlotState::Entered) return Stop(SurvivingPackReason::InvalidOutcome);
                slot.state = SurvivingPackSlotState::Pending;
            } else if (outcome == SurvivingPackOutcome::Ready) {
                if (slot.state == SurvivingPackSlotState::Ready) return false;
                if (slot.state != SurvivingPackSlotState::Pending) return Stop(SurvivingPackReason::InvalidOutcome);
                slot.state = SurvivingPackSlotState::Ready;
            } else {
                slot.state = SurvivingPackSlotState::Unknown;
                return Stop(SurvivingPackReason::UnknownOutcome);
            }
        }
        classification_ = SurvivingPackClassification::Unavailable;
        reason_ = SurvivingPackReason::Pending;
        return true;
    }
    // Caller has already admitted this exact parsed world packet. Material is
    // compared against immutable intent; equal heartbeat/replay is not change.
    // Reordered admitted serials >cut still cancel; max is evidence, not a floor.
    bool Continue(std::uint64_t admittedSourceSerial, bool materialChanged, std::uint64_t nowMs) {
        if (!Live(nowMs)) return false;
        if (admittedSourceSerial <= intent_->begin.snapshotCut) return true;
        lastSourceSerial_ = (std::max)(lastSourceSerial_, admittedSourceSerial);
        if (materialChanged) return Stop(SurvivingPackReason::MaterialChange);
        return true;
    }
    void Cancel() { Stop(SurvivingPackReason::Cancelled); }
    bool Terminal() const { return classification_ == SurvivingPackClassification::Terminal; }
    SurvivingPackClassification Classification() const { return classification_; }
    SurvivingPackReason Reason() const { return reason_; }
    std::uint32_t MissingFacts() const { return missing_; }
    const std::array<SurvivingPackSlot, 5>& Slots() const { return slots_; }
    const std::optional<SurvivingPackIntent>& Intent() const { return intent_; }
    bool HasOwnAttempt() const {
        return std::any_of(slots_.begin(), slots_.end(), [](const auto& s) {
            return s.state != SurvivingPackSlotState::NotAttempted;
        });
    }
    std::uint64_t LastSourceSerial() const { return lastSourceSerial_; }
    std::uint32_t TargetLoad() const { return targetLoad_; }
    const SurvivingPackControllerState& ControllerSample() const { return controller_; }

private:
    static bool SameContext(const ProducerWorldContext& a, const ProducerWorldContext& b) {
        return a.generation == b.generation && a.deliverySerial == b.deliverySerial &&
            a.hostSourceSerial == b.hostSourceSerial;
    }
    static bool Supported(const NativeRecordContentDefinition& d) {
        using native_record_detail::u16;
        constexpr std::array<std::uint16_t, 5> ids{11, 12, 13, 14, 18};
        if (d.groupKey != 808476514 || d.header[0] != 2 || u16(d.header.data() + 2) != 30 ||
            d.records.size() != 5 || u16(d.header.data() + 4) != 5) return false;
        for (std::size_t i = 0; i < ids.size(); ++i) {
            const auto& r = d.records[i];
            if (r[0] != 0x2e || r[1] != 1 || r[2] || r[3] || r[0x1c] != 2 || r[0x1d] ||
                u16(r.data() + 0x1e) != ids[i] || u16(r.data() + 0x2a) != 8 || r[0x30]) return false;
        }
        return true; // only content subset, not header/resource/count eligibility
    }
    bool CheckMutation(const KnownControllerMutationTicket& t, bool requireBaseline) {
        if (!t.available || !t.coverageComplete || t.poisoned || t.inFlight || !t.revision) {
            if (mutation_ || requireBaseline) return Stop(SurvivingPackReason::MutationChanged);
            Missing(PackMissingMutationTicket); return false;
        }
        if (mutation_ && (t.revision != mutation_->revision || t.coverageComplete != mutation_->coverageComplete))
            return Stop(SurvivingPackReason::MutationChanged);
        return true; // a negative known-boundary check, never creator exclusion
    }
    bool Live(std::uint64_t nowMs) {
        if (Terminal() || !intent_) return false;
        if (nowMs >= intent_->deadlineMs) return Stop(SurvivingPackReason::Deadline);
        return true;
    }
    bool RejectIntent(SurvivingPackReason why) {
        if (intent_) return Stop(why);
        rejected_ = true; reason_ = why;
        classification_ = why == SurvivingPackReason::UnsupportedPack ?
            SurvivingPackClassification::Unsupported : SurvivingPackClassification::Unavailable;
        return false;
    }
    bool Stop(SurvivingPackReason why) {
        if (!Terminal()) { classification_ = SurvivingPackClassification::Terminal; reason_ = why; }
        return false;
    }
    void Missing(std::uint32_t bits) {
        missing_ |= bits; classification_ = SurvivingPackClassification::Unavailable;
        reason_ = SurvivingPackReason::IncompleteFacts;
    }
    void OccupancyConflict(SurvivingPackReason why = SurvivingPackReason::OccupancyConflict) {
        if (HasOwnAttempt()) { Stop(why); return; }
        classification_ = SurvivingPackClassification::Conflict; reason_ = why;
    }
    void Conflict() {
        if (HasOwnAttempt()) { Stop(SurvivingPackReason::ContentMismatch); return; }
        classification_ = SurvivingPackClassification::Conflict;
        reason_ = SurvivingPackReason::ContentMismatch;
    }
    std::optional<SurvivingPackIntent> intent_;
    std::optional<KnownControllerMutationTicket> mutation_;
    std::vector<std::uint8_t> snapshotBytes_, beginBytes_;
    std::array<SurvivingPackSlot, 5> slots_{};
    SurvivingPackControllerState controller_;
    SurvivingPackClassification classification_{SurvivingPackClassification::Disabled};
    SurvivingPackReason reason_{SurvivingPackReason::None};
    std::uint32_t missing_{PackMissingNativeExecutionAuthority | PackMissingGlobalPrelinkCoverage}, targetLoad_{};
    std::size_t selected_{};
    bool rejected_{};
    std::uint64_t lastInvocation_{}, lastSourceSerial_{};
};
} // namespace kh2coop
