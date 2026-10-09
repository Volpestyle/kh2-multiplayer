#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace kh2coop::inject::populationregistration {

using Roots = std::array<std::uintptr_t, 5>;

// Enrollment is local native history. A source occurrence does not create a
// count obligation, and a positive aggregate counter cannot assign one.
struct Scope {
    std::uint32_t load{}, transition{};
    std::uintptr_t controller{}, header{};
    std::uint64_t initialization{};
    bool operator==(const Scope&) const = default;
};

enum class Credit : std::uint8_t { Unknown, Outstanding, Occupied, Debited };
enum class Reason : std::uint8_t {
    Ready, UnknownEnrollment, InvalidInitialization, ChangedScope, CoverageLost,
    InvalidRecord, Occupied, AlreadyDebited, ChangedCount, UnknownMutation,
    ActivationUnavailable, SourceUnavailable, CensusUnavailable,
    AdmissionUnavailable, FinalCheckRefused, AlreadyAttempted, NativeFault,
    PostCallRefused
};

struct Record {
    std::uintptr_t address{};
    std::uint16_t id{};
    std::uint32_t object{};
    std::uint8_t mode{}, positionMode{}, deathMode{};
    bool operator==(const Record&) const = default;
};

struct Enrollment {
    Scope scope{};
    std::array<Record, 5> records{};
    std::uint32_t initial{}, current{};
    bool initializationReturned{}, complete{}, stable{}, cacheComplete{};
    bool cacheEmpty{}, owner{}, coverageComplete{};
};

class Ledger {
    Enrollment enrollment_{};
    std::array<Credit, 5> credits_{};
    std::array<Roots, 5> bodies_{};
    bool ready_{};
    std::uint64_t lastInitialization_{};
    Reason reason_{Reason::UnknownEnrollment};
public:
    void Refuse(Reason reason) { ready_=false; reason_=reason; }
    Reason status() const { return ready_?Reason::Ready:reason_; }
    const Enrollment& enrollment() const { return enrollment_; }
    Credit credit(std::size_t ordinal) const {
        return ready_ && ordinal<credits_.size()?credits_[ordinal]:Credit::Unknown;
    }
    bool Initialize(const Enrollment& observed) {
        Refuse(Reason::InvalidInitialization);
        if (observed.scope.initialization<=lastInitialization_) return false;
        lastInitialization_=observed.scope.initialization;
        if (!observed.initializationReturned || !observed.complete || !observed.stable ||
            !observed.cacheComplete || !observed.cacheEmpty || !observed.owner ||
            !observed.coverageComplete || !observed.scope.controller || !observed.scope.header ||
            !observed.scope.initialization || observed.initial!=5 || observed.current!=5) return false;
        for (std::size_t i=0;i<observed.records.size();++i) {
            const auto& record=observed.records[i];
            if (record.address!=observed.scope.header+44+i*64 || !record.id ||
                record.id>0x7fff || record.object!=302 || record.mode!=2 ||
                record.positionMode!=0 || record.deathMode!=0) return false;
            for (std::size_t j=0;j<i;++j)
                if (observed.records[j].id==record.id) return false;
        }
        enrollment_=observed; credits_.fill(Credit::Outstanding); bodies_={};
        ready_=true;reason_=Reason::Ready;return true;
    }
    Reason Available(const Scope& scope,std::size_t ordinal,const Record& record,
                     std::uint32_t initial,std::uint32_t current) const {
        if (!ready_) return reason_;
        if (scope!=enrollment_.scope) return Reason::ChangedScope;
        if (ordinal>=credits_.size() || record!=enrollment_.records[ordinal]) return Reason::InvalidRecord;
        if (initial!=enrollment_.initial || current!=enrollment_.current) return Reason::ChangedCount;
        if (credits_[ordinal]==Credit::Occupied) return Reason::Occupied;
        if (credits_[ordinal]==Credit::Debited) return Reason::AlreadyDebited;
        return credits_[ordinal]==Credit::Outstanding?Reason::Ready:Reason::UnknownEnrollment;
    }
    bool Birth(const Scope& scope,std::size_t ordinal,const Roots& roots) {
        if (!ready_ || scope!=enrollment_.scope || ordinal>=credits_.size() ||
            credits_[ordinal]!=Credit::Outstanding || !ValidRoots(roots) ||
            roots[3]!=scope.controller || roots[4]!=enrollment_.records[ordinal].address) {
            Refuse(Reason::UnknownMutation);return false;
        }
        credits_[ordinal]=Credit::Occupied;bodies_[ordinal]=roots;return true;
    }
    // A completed *native* count obligation for this exact occupied body.
    // No HP guess, census disappearance or anonymous aggregate count qualifies.
    bool Death(const Scope& scope,std::size_t ordinal,const Roots& roots,
               std::uint32_t before,std::uint32_t after,bool nativeCallComplete) {
        if (!ready_ || scope!=enrollment_.scope || ordinal>=credits_.size() ||
            credits_[ordinal]!=Credit::Occupied || bodies_[ordinal]!=roots ||
            !nativeCallComplete || before!=enrollment_.current || before==0 || after!=before-1) {
            Refuse(Reason::UnknownMutation);return false;
        }
        enrollment_.current=after;credits_[ordinal]=Credit::Debited;return true;
    }
    // An alive native removal can release a body without consuming its count.
    // Absence must be observed after the returned bookkeeping/disposal call.
    bool AliveRemoval(const Scope& scope,std::size_t ordinal,const Roots& roots,
                      std::uint32_t before,std::uint32_t after,bool returned,bool absent) {
        if (!ready_ || scope!=enrollment_.scope || ordinal>=credits_.size() ||
            credits_[ordinal]!=Credit::Occupied || bodies_[ordinal]!=roots ||
            !returned || !absent || before!=enrollment_.current || after!=before) {
            Refuse(Reason::UnknownMutation);return false;
        }
        bodies_[ordinal]={};credits_[ordinal]=Credit::Outstanding;return true;
    }
    static bool ValidRoots(const Roots& roots) {
        for (const auto root:roots) if (root<0x10000 || root>=0x800000000000ULL) return false;
        return true;
    }
};

struct Request {
    Scope scope{};
    Record record{};
    std::uint16_t ordinal{}, netId{};
    std::uint64_t birth{}, cut{}, generation{}, delivery{}, epoch{};
    std::array<std::uint8_t,16> session{};
    std::uint64_t hostConnection{}, selfConnection{};
    std::array<std::uint16_t,6> location{};
    bool operator==(const Request&) const = default;
};

struct Check {
    Request request{};
    std::uint32_t initial{}, current{};
    bool source{}, census{}, admission{}, ownerActivation{};
    bool fiberContinuity{}, creatorExclusive{}, globalPendingExcluded{};
    bool noNativeCopy{}, noPendingDisposal{}, loadedFamily{}, recordBookends{};
};

struct PostCheck {
    Check state{};
    Roots roots{};
    // These are independent post-call bookends, not the pre-call snapshot.
    bool rootsStable{}, subtypeMatches{}, physicalCensusMember{};
};

// Consumer boundary for native integration and tests. The probe must capture
// actual immutable source + local native state; there are no default true gates.
// Attempt poison survives reauthorization and ledger reinitialization.
class Consumer {
    std::array<Request,64> attempts_{};
    std::size_t count_{};
public:
    template<class Probe,class Invoke,class Recapture>
    Reason Dispatch(Ledger& ledger,const Request& request,Probe&& probe,
                    Invoke&& invoke,Recapture&& recapture) {
        if (!request.netId || !request.birth || request.birth>request.cut ||
            !request.generation || !request.delivery || !request.epoch ||
            !request.hostConnection || !request.selfConnection ||
            request.hostConnection==request.selfConnection) return Reason::SourceUnavailable;
        bool sessionPresent=false;
        for (const auto byte:request.session) sessionPresent=sessionPresent || byte!=0;
        if (!sessionPresent) return Reason::SourceUnavailable;
        for (std::size_t i=0;i<count_;++i)
            if (attempts_[i].scope==request.scope && attempts_[i].ordinal==request.ordinal &&
                attempts_[i].birth==request.birth && attempts_[i].session==request.session &&
                attempts_[i].hostConnection==request.hostConnection)
                return Reason::AlreadyAttempted;
        if (count_==attempts_.size()) return Reason::CoverageLost;
        auto first=probe();
        auto reason=Validate(ledger,request,first);
        if (reason!=Reason::Ready) return reason;
        const auto final=probe();
        reason=Validate(ledger,request,final);
        if (reason!=Reason::Ready || final.request!=first.request)
            return reason==Reason::Ready?Reason::FinalCheckRefused:reason;
        attempts_[count_++]=request; // BEFORE call; faults cannot reauthorize.
        const auto result=invoke(request);
        // Invoke owns the native exception boundary. Always recapture after a
        // mutation/fault; a failed post-check never permits a blind retry.
        const auto post=recapture(request,result);
        if (!result) {
            ledger.Refuse(Reason::NativeFault);
            return Reason::NativeFault;
        }
        // The native return must be the very actor in the fresh complete census.
        // A valid-looking different body is not evidence of this call's success.
        // Post-call noNativeCopy is deliberately NOT required: this call created it.
        const auto& state=post.state;
        if (state.request!=request || !state.source || !state.census ||
            !state.ownerActivation || !state.fiberContinuity || !state.creatorExclusive ||
            !state.globalPendingExcluded || !state.noPendingDisposal || !state.loadedFamily ||
            !state.recordBookends || !post.rootsStable || !post.subtypeMatches ||
            !post.physicalCensusMember || post.roots[0]!=result ||
            ledger.Available(request.scope,request.ordinal,request.record,state.initial,state.current)!=Reason::Ready ||
            !ledger.Birth(request.scope,request.ordinal,post.roots)) {
            ledger.Refuse(Reason::PostCallRefused);
            return Reason::PostCallRefused;
        }
        return Reason::Ready;
    }
private:
    static Reason Validate(const Ledger& ledger,const Request& request,const Check& state) {
        if (state.request!=request || !state.source) return Reason::SourceUnavailable;
        if (!state.ownerActivation || !state.fiberContinuity || !state.creatorExclusive ||
            !state.globalPendingExcluded) return Reason::ActivationUnavailable;
        if (!state.census || !state.noNativeCopy || !state.noPendingDisposal ||
            !state.recordBookends) return Reason::CensusUnavailable;
        if (!state.admission || !state.loadedFamily) return Reason::AdmissionUnavailable;
        return ledger.Available(request.scope,request.ordinal,request.record,state.initial,state.current);
    }
};
} // namespace kh2coop::inject::populationregistration
