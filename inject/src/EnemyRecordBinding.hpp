#pragma once
#include "EnemyRecordPins.hpp"
#include "EnemyRecordExclusions.hpp"
#include "OrdinaryEnemyBinding.hpp"
#include <span>
#include <vector>

namespace kh2coop::inject::recordbinding {
struct Scope {
    std::uint32_t generation{}, epoch{}, load{}, transition{}, frame{};
    std::uint64_t hostConnection{};
    NativeRecordLocation location {};
};
inline std::size_t Population(const NativeRecordLocation& l);
inline bool AuthorityScopeCurrent(const Scope& a,const Scope& b,bool context,bool safe) noexcept {
    return context && safe && a.generation && a.epoch && a.load && a.transition && a.hostConnection &&
        Population(a.location)!=0 && a.generation==b.generation && a.epoch==b.epoch && a.load==b.load &&
        a.transition==b.transition && a.frame==b.frame && a.hostConnection==b.hostConnection && a.location==b.location;
}
struct Local { ordinarybinding::Identity roots; EnemyRecordKey key; int id{}; bool living{}; };
struct Host { EnemyRecordKey key; int id{}; std::uint32_t objectId{}; bool dead{}; };
struct Result { bool admitted{}; const char* reason="unavailable"; std::vector<int> ids; };
struct Selection {
    bool complete=false;
    const char* reason="unavailable";
    std::vector<Local> local;
    std::vector<Host> host;
    std::size_t localExcluded=0, hostExcluded=0;
};
// Adapter supplies fresh native membership/catalog proof for EVERY local row,
// and validates each peer key against the same complete native catalog. Unknown
// rows are never reduced to a negative classification by this selector.
inline Selection SelectPopulation(const Scope& scope,std::span<const Local> locals,std::span<const Host> hosts) {
    Selection out;
    const auto fail=[&](const char* reason) { out.reason=reason;return out; };
    if (!Population(scope.location)) return fail("wrong-population-scope");
    for (std::size_t i=0;i<locals.size();++i) {
        const auto& l=locals[i];
        if (l.key.location!=scope.location || !l.roots.actor || !l.roots.objentry || !l.roots.status ||
            !l.roots.controller || !l.roots.record || l.id<=0) return fail("unavailable-local-identity");
        const bool admitted=Admitted(l.roots.objectId,l.key);
        if (!admitted && !ExclusionKnown(l.roots.objectId,l.key)) return fail("unknown-local-record");
        for (std::size_t j=0;j<i;++j)
            if (locals[j].key==l.key || locals[j].id==l.id || locals[j].roots.actor==l.roots.actor ||
                locals[j].roots.status==l.roots.status || locals[j].roots.record==l.roots.record)
                return fail("duplicate-local-identity");
        if (admitted) out.local.push_back(l); else ++out.localExcluded;
    }
    for (const auto& l:locals) if (ExclusionKnown(l.roots.objectId,l.key))
        for (const auto& a:out.local)
            if (l.roots.controller==a.roots.controller) return fail("excluded-controller-alias");
    for (std::size_t i=0;i<hosts.size();++i) {
        const auto& h=hosts[i];
        if (h.key.location!=scope.location || h.id<=0 || h.id>65535) return fail("unavailable-peer-key");
        const bool admitted=Admitted(h.objectId,h.key);
        if (!admitted && !ExclusionKnown(h.objectId,h.key)) return fail("unknown-peer-record");
        for (std::size_t j=0;j<i;++j)
            if (hosts[j].key==h.key || hosts[j].id==h.id) return fail("duplicate-peer-identity");
        if (admitted) out.host.push_back(h); else ++out.hostExcluded;
    }
    if (out.local.size()!=Population(scope.location) || out.host.size()!=Population(scope.location))
        return fail("incomplete-admitted-population");
    out.complete=true;out.reason="positively-classified-population";return out;
}
inline std::size_t Population(const NativeRecordLocation& l) {
    if (l == NativeRecordLocation{18,6,0,0,2,32767}) return 3;
    if (l == NativeRecordLocation{7,0,0,0,2,32767}) return 5;
    return 0;
}
// A key is consumed for this complete local load and shared epoch. There is no
// same-record reincarnation algorithm: any discontinuity permanently holds it.
class Lease {
    Scope scope_ {};
    std::vector<Local> local_;
    std::vector<Host> host_;
    bool installed_=false, poisoned_=false, terminalIssued_=false;
    std::uint32_t terminalFrame_{};
public:
    void Interrupt() noexcept { if (installed_) poisoned_=true; }
    Result Resolve(Scope s, std::span<const Local> local, std::span<const Host> host, bool complete, bool terminalAuthority=false) {
        const auto fail=[&](const char* reason, bool poison=false) {
            if (poison && installed_) poisoned_=true;
            return Result{false,reason,{}};
        };
        if (!s.generation || !s.epoch || !s.load || !s.transition || !s.hostConnection || !Population(s.location))
            return fail("wrong-scope",true);
        const auto ahead=[](std::uint32_t a,std::uint32_t b) { const auto delta=a-b;return delta && delta<0x80000000u; };
        const bool newLoad=installed_ && ahead(s.load,scope_.load) && ahead(s.transition,scope_.transition) &&
            (ahead(s.epoch,scope_.epoch) || (s.generation!=scope_.generation && s.hostConnection!=scope_.hostConnection));
        if (newLoad) { installed_=false; poisoned_=false; terminalIssued_=false; local_.clear(); host_.clear(); }
        if (installed_ && (s.generation!=scope_.generation || s.epoch!=scope_.epoch || s.load!=scope_.load ||
            s.transition!=scope_.transition || s.hostConnection!=scope_.hostConnection || s.location!=scope_.location))
            return fail("scope-changed-without-fresh-load",true);
        if (terminalIssued_ && s.frame!=terminalFrame_) return fail("terminal-population-retired",true);
        if (poisoned_) return fail("record-reuse-held");
        if (!complete) return fail("incomplete-census-or-content",true);
        const auto n=Population(s.location);
        if (local.size()!=n || host.size()!=n) return fail("incomplete-population",true);
        const auto deaths=std::count_if(host.begin(),host.end(),[](const auto& h){return h.dead;});
        // Exactly one final handoff from a previously complete living population.
        // No terminal first admission, no second death, no recovery after removal.
        if (deaths && (!terminalAuthority || !installed_ || deaths!=1)) return fail("terminal-not-authorized",true);
        Result result{true,deaths?"terminal-death-handoff":"exact-record-population",{}};
        for (std::size_t i=0;i<n;++i) {
            const auto& l=local[i];
            if (!l.living || !l.roots.actor || !l.roots.objentry || !l.roots.status || !l.roots.controller || !l.roots.record ||
                l.key.location!=s.location || !Admitted(l.roots.objectId,l.key)) return fail("unavailable-or-unreviewed-record",true);
            for (std::size_t j=0;j<i;++j)
                if (local[j].key==l.key || local[j].roots.actor==l.roots.actor) return fail("duplicate-local-record",true);
            int id=0;
            for (std::size_t j=0;j<n;++j) {
                const auto& h=host[j];
                if ((!terminalAuthority && h.dead) || h.id<=0 || h.id>65535 || h.key.location!=s.location || !Admitted(h.objectId,h.key)) return fail("unavailable-host-record",true);
                for (std::size_t k=0;k<j;++k)
                    if (host[k].key==h.key || host[k].id==h.id) return fail("duplicate-host-record",true);
                if (h.key==l.key && h.objectId==l.roots.objectId) id=h.id;
            }
            if (!id) return fail("no-exact-peer-record",true);
            result.ids.push_back(id);
        }
        if (installed_) {
            if (s.frame<scope_.frame || s.frame-scope_.frame>1) return fail("observation-gap",true);
            for (std::size_t i=0;i<n;++i) {
                const auto old=std::find_if(local_.begin(),local_.end(),[&](const auto& l){return l.key==local[i].key;});
                const auto oldHost=std::find_if(host_.begin(),host_.end(),[&](const auto& h){return h.key==local[i].key;});
                if (old==local_.end() || oldHost==host_.end() || old->roots!=local[i].roots || old->id!=local[i].id ||
                    oldHost->id!=result.ids[i]) return fail("incarnation-or-controller-reuse",true);
            }
        }
        if (deaths) { terminalIssued_=true;terminalFrame_=s.frame; }
        local_.assign(local.begin(),local.end()); host_.assign(host.begin(),host.end()); scope_=s; installed_=true;
        return result;
    }
};
// Binding-only baseline remains unchanged. Full authority is a separate opt-in
// with a fresh frame-local population/identity proof at each native boundary.
inline bool ScopedAuthorityAllowed(bool binding,bool authority,std::uint32_t id,bool current) {
    return !RecordFamily(id) || (binding && authority && current);
}
// Mapping qualification must not silently acquire HP/death, claim or pose rights.
inline bool AuthorityAllowed(bool requested,std::uint32_t id) { return !requested || !RecordFamily(id); }
} // namespace kh2coop::inject::recordbinding
