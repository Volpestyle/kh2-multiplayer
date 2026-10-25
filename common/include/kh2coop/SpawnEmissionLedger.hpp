#pragma once
#include "kh2coop/Protocol.hpp"
#include <vector>

namespace kh2coop {
// Owned by the game thread. Keep consumed receipts until the room ends so
// duplicate transport/cache delivery cannot grant another spawn. A failed
// native allocation leaves its receipt pending only after a qualified side-effect-free null.
// VUH-2466: Struggle orb rows share the ledger but never answer a controller lookup; the client
// takes them in arrival order with TakeOrbs(). VUH-2605 script-actor births are a separate kind.
class SpawnEmissionLedger {
public:
    enum class ScriptActorAdd : std::uint8_t { Added, Duplicate, Ended, Conflict, Refused };
    enum class ScriptActorReceiptState : std::uint8_t { Missing, Pending, Reserved, Consumed, Ended, Poisoned };
    enum class ScriptActorCancel : std::uint8_t { QualifiedNormalNull };
    struct ScriptActorReservation {
        ScriptCallEntry birth{}; // copied canonical BEGIN; never a vector pointer
        std::uint64_t attempt{}; // local one-use attempt, not wire/creation authority
    };
private:
    enum class ReceiptKind : std::uint8_t { Indexed, ScriptActor };
    struct Receipt {
        SpawnEmission emission{};
        bool consumed = false, reserved = false;
        ReceiptKind kind = ReceiptKind::Indexed;
        ScriptCallEntry birth{};
        bool ended = false, poisoned = false;
        std::uint64_t attempt{};
    };
    std::vector<Receipt> receipts_; // one shared bounded history; never evicted
    std::uint64_t floor_ = 0;
    std::uint64_t nextScriptAttempt_ = 0; // never rewinds, including Clear()
    bool scriptAdmissionFailed_ = false;
    static bool NormalizeScriptActor(const ScriptCallEntry& input, ScriptCallEntry& birth) noexcept {
        birth = {};
        if (!scriptActorBirthOperation(input.table,input.index) ||
            !validScriptActorBirthIdentity(input.netId,input.objectId,input.effect,input.args)) return false;
        birth = input;birth.index = SCRIPT_ACTOR_BIRTH_BEGIN;return true;
    }
    static std::uint64_t ScriptOccurrence(const ScriptCallEntry& birth) noexcept {
        return static_cast<std::uint32_t>(birth.args[0]) |
            (static_cast<std::uint64_t>(static_cast<std::uint32_t>(birth.args[1])) << 32);
    }
    static bool SameScriptActor(const ScriptCallEntry& a,const ScriptCallEntry& b) noexcept {
        return a.netId==b.netId && a.objectId==b.objectId && a.table==b.table && a.index==b.index &&
            a.effect==b.effect && a.args==b.args;
    }
    static bool RelatedScriptActor(const Receipt& receipt,const ScriptCallEntry& birth) noexcept {
        return receipt.kind==ReceiptKind::ScriptActor &&
            (receipt.birth.effect==birth.effect || ScriptOccurrence(receipt.birth)==ScriptOccurrence(birth));
    }
    bool AppendScriptActor(const ScriptCallEntry& birth,bool ended,bool poisoned) noexcept {
        if (scriptAdmissionFailed_ || receipts_.size()>=SPAWN_EMISSION_CAP) return false;
        Receipt receipt{};receipt.kind=ReceiptKind::ScriptActor;receipt.birth=birth;
        receipt.ended=ended;receipt.poisoned=poisoned;
        try { receipts_.push_back(receipt);return true; }
        catch (...) {
            // Without storage for a conflict alias, new script rights must stop.
            // Retained exact ends and in-flight spend bookkeeping still work.
            scriptAdmissionFailed_=true;return false;
        }
    }
    Receipt* ReservedScriptActor(const ScriptActorReservation& token) noexcept {
        ScriptCallEntry birth{};
        if (!token.attempt || token.birth.index!=SCRIPT_ACTOR_BIRTH_BEGIN ||
            !NormalizeScriptActor(token.birth,birth)) return nullptr;
        for (auto& r:receipts_) if (r.kind==ReceiptKind::ScriptActor && r.reserved && !r.consumed &&
            r.attempt==token.attempt && SameScriptActor(r.birth,birth)) return &r;
        return nullptr;
    }
public:
    void Clear() { receipts_.clear(); floor_ = 0; scriptAdmissionFailed_ = false; }
    bool Add(const SpawnEmission& e) {
        if (!e.sequence || e.sequence <= floor_ || receipts_.size() >= SPAWN_EMISSION_CAP) return false;
        // A combat occurrence grants one birth, even if the host retries it
        // under a new sequence after the original receipt was consumed.
        if (e.netId) for (const auto& r : receipts_)
            if (r.kind==ReceiptKind::Indexed && r.emission.netId == e.netId) return false;
        // An orb is born once and collected once, whatever the transport repeats.
        if (e.kind != SpawnEmissionKind::Controller) for (const auto& r : receipts_)
            if (r.kind==ReceiptKind::Indexed && r.emission.kind == e.kind && r.emission.orbId == e.orbId) return false;
        Receipt receipt{};receipt.emission=e;receipts_.push_back(receipt); floor_ = e.sequence; return true;
    }
    const SpawnEmission* Pending(std::uint32_t key, std::uint16_t header) const {
        for (const auto& r : receipts_)
            if (r.kind==ReceiptKind::Indexed && !r.consumed && r.emission.kind == SpawnEmissionKind::Controller &&
                r.emission.controllerKey == key && r.emission.headerId == header) return &r.emission;
        return nullptr;
    }
    // Exact FIFO receipt for the impending local native birth. Copy its
    // identity before another Add(), which can move vector storage.
    const SpawnEmission* Pending(std::uint32_t key, std::uint16_t header, std::uint16_t index) const {
        const auto* next = Pending(key, header);
        return next && next->recordIndex == index ? next : nullptr;
    }
    // Reserve before crossing the native allocation call. Nested calls cannot
    // borrow this receipt or skip it to the next birth from this controller.
    const SpawnEmission* Reserve(std::uint32_t key, std::uint16_t header, std::uint16_t index) {
        for (auto& r : receipts_) {
            if (r.kind!=ReceiptKind::Indexed || r.consumed || r.emission.kind != SpawnEmissionKind::Controller ||
                r.emission.controllerKey != key || r.emission.headerId != header) continue;
            if (r.reserved || r.emission.recordIndex != index) return nullptr;
            r.reserved = true;
            return &r.emission;
        }
        return nullptr;
    }
    bool Commit(std::uint64_t sequence) {
        for (auto& r : receipts_) if (r.kind==ReceiptKind::Indexed && r.emission.sequence == sequence && r.reserved && !r.consumed) {
            r.reserved = false; r.consumed = true; return true;
        }
        return false;
    }
    bool Cancel(std::uint64_t sequence) {
        for (auto& r : receipts_) if (r.kind==ReceiptKind::Indexed && r.emission.sequence == sequence && r.reserved && !r.consumed) {
            r.reserved = false; return true;
        }
        return false;
    }
    bool Permit(std::uint32_t key, std::uint16_t header, std::uint16_t index, bool consume) {
        // Preserve the host's order within each controller, including refills.
        for (auto& r : receipts_) {
            if (r.kind!=ReceiptKind::Indexed || r.consumed || r.emission.kind != SpawnEmissionKind::Controller ||
                r.emission.controllerKey != key || r.emission.headerId != header) continue;
            if (r.reserved || r.emission.recordIndex != index) return false;
            if (consume) r.consumed = true;
            return true;
        }
        return false;
    }
    // Every orb row not yet taken, in arrival order (the host's sequence order).
    std::vector<SpawnEmission> TakeOrbs() {
        std::vector<SpawnEmission> out;
        for (auto& r : receipts_)
            if (r.kind==ReceiptKind::Indexed && !r.consumed && r.emission.kind != SpawnEmissionKind::Controller) { r.consumed = true; out.push_back(r.emission); }
        return out;
    }
    // Wire admission is not native birth authority. The owner checks current
    // parent/child manifests, session/scope and native readiness before Reserve.
    ScriptActorAdd AddScriptActor(const ScriptCallEntry& input) noexcept {
        ScriptCallEntry birth{};
        if (!NormalizeScriptActor(input,birth)) return ScriptActorAdd::Refused;
        const bool end=input.index==SCRIPT_ACTOR_BIRTH_END;
        std::size_t exact=receipts_.size();bool conflict=false;
        for (std::size_t i=0;i<receipts_.size();++i) {
            const auto& r=receipts_[i];
            if (!RelatedScriptActor(r,birth)) continue;
            if (SameScriptActor(r.birth,birth)) exact=i;
            else conflict=true;
        }
        if (conflict) {
            const bool hadExact=exact<receipts_.size();
            for (auto& r:receipts_) if (RelatedScriptActor(r,birth)) r.poisoned=true;
            if (exact<receipts_.size()) {
                if (end)receipts_[exact].ended=true;
            } else {
                // At full capacity no unknown alias can enter before Clear:
                // history never shrinks. Poison all known aliases, never evict.
                (void)AppendScriptActor(birth,end,true);
            }
            return end && hadExact?ScriptActorAdd::Ended:ScriptActorAdd::Conflict;
        }
        if (exact<receipts_.size()) {
            auto& r=receipts_[exact];
            if (end){r.ended=true;return ScriptActorAdd::Ended;}
            if (r.poisoned)return ScriptActorAdd::Conflict;
            return r.ended?ScriptActorAdd::Ended:ScriptActorAdd::Duplicate;
        }
        if (!AppendScriptActor(birth,end,false))return ScriptActorAdd::Refused;
        return end?ScriptActorAdd::Ended:ScriptActorAdd::Added;
    }
    bool PendingScriptActor(ScriptCallEntry& copiedBirth) const noexcept {
        copiedBirth={};
        if (scriptAdmissionFailed_ || nextScriptAttempt_==UINT64_MAX)return false;
        for (const auto& r:receipts_)if (r.kind==ReceiptKind::ScriptActor &&
            !r.consumed && !r.reserved && !r.ended && !r.poisoned) {
            copiedBirth=r.birth;return true;
        }
        return false;
    }
    // Receipt history only; this query grants no stream/combat/adoption rights.
    ScriptActorReceiptState ScriptActorState(const ScriptCallEntry& input) const noexcept {
        ScriptCallEntry birth{};
        if (!NormalizeScriptActor(input,birth))return ScriptActorReceiptState::Missing;
        for (const auto& r:receipts_)if (r.kind==ReceiptKind::ScriptActor && SameScriptActor(r.birth,birth)) {
            if (r.poisoned)return ScriptActorReceiptState::Poisoned;
            if (r.ended)return ScriptActorReceiptState::Ended;
            if (r.consumed)return ScriptActorReceiptState::Consumed;
            return r.reserved?ScriptActorReceiptState::Reserved:ScriptActorReceiptState::Pending;
        }
        return ScriptActorReceiptState::Missing;
    }
    bool ReserveScriptActor(const ScriptCallEntry& input,ScriptActorReservation& out) noexcept {
        out={};ScriptCallEntry birth{};
        if (scriptAdmissionFailed_ || nextScriptAttempt_==UINT64_MAX || input.index!=SCRIPT_ACTOR_BIRTH_BEGIN ||
            !NormalizeScriptActor(input,birth))return false;
        for (auto& r:receipts_)if (r.kind==ReceiptKind::ScriptActor && SameScriptActor(r.birth,birth)) {
            if (r.consumed || r.reserved || r.ended || r.poisoned)return false;
            r.attempt=++nextScriptAttempt_;r.reserved=true;out={r.birth,r.attempt};return true;
        }
        return false;
    }
    // Spend is allowed after an in-flight end/poison. True only closes the
    // exact reservation; the owner must still refuse adoption/quarantine it.
    bool CommitScriptActor(const ScriptActorReservation& token) noexcept {
        auto* r=ReservedScriptActor(token);
        if (!r)return false;
        r->reserved=false;r->consumed=true;return true;
    }
    // QualifiedNormalNull is an explicit trusted-caller obligation, not native
    // proof produced by this ledger. Fault/ambiguous returns MUST be spent.
    bool CancelScriptActor(const ScriptActorReservation& token,ScriptActorCancel reason) noexcept {
        if (reason!=ScriptActorCancel::QualifiedNormalNull)return false;
        auto* r=ReservedScriptActor(token);
        if (!r)return false;
        r->reserved=false;return true;
    }

};
} // namespace kh2coop
