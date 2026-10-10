#pragma once
#include "kh2coop/PopulationCut.hpp"
#include <optional>
namespace kh2coop::ordinarylifecycle {
inline const PopulationOccurrence* Find(const PopulationCut& c,unsigned id) {
    for(const auto& e:c.entries)if(e.netId==id)return &e;return nullptr;
}
inline bool AdmitsHp(const PopulationCut* c,unsigned id) {
    const auto* row=c&&c->membershipOnly?Find(*c,id):nullptr;
    return row && !row->terminal;
}
inline bool SameRecord(const PopulationCut& a,const PopulationOccurrence& x,
                       const PopulationCut& b,const PopulationOccurrence& y) {
    return x.record==y.record && x.objectId==y.objectId &&
        population_cut_detail::sameDefinition(a.definitions[x.definition],b.definitions[y.definition]);
}
inline bool SameBirth(const PopulationCut& a,const PopulationOccurrence& x,
                      const PopulationCut& b,const PopulationOccurrence& y) {
    return x.netId==y.netId && x.occurrence==y.occurrence && x.birthSequence==y.birthSequence &&
        x.activationPoint==y.activationPoint && SameRecord(a,x,b,y);
}
// Snapshot history, not an address/point matcher. No native writes or kill authority.
// The caller authenticates the original host envelope and room/connection binding.
class History {
    std::optional<PopulationCut> cut_;
public:
    const std::optional<PopulationCut>& Current()const{return cut_;}
    void Reset(){cut_.reset();}
    bool Admit(const PopulationCut& next) {
        try {ValidatePopulationCut(next);}catch(...){return false;}
        if(!next.membershipOnly || next.entries.empty())return false;
        unsigned living=0;for(const auto& row:next.entries)if(!row.terminal)++living;
        if(living>5)return false;
        if(!cut_) {
            // Bootstrap may contain history; it must already have ordered same-record
            // successors and exactly five original record identities. It grants no local roots.
            unsigned records=0;
            for(const auto& e:next.entries) {
                bool first=true;for(const auto& other:next.entries)if(other.birthSequence<e.birthSequence && SameRecord(next,e,next,other))first=false;
                if(first)++records;
            }
            if(records!=5)return false;
        } else {
            const auto& old=*cut_;
            if(next.epoch!=old.epoch || next.hostLoad!=old.hostLoad || next.hostTransition!=old.hostTransition ||
               next.location!=old.location || next.sequence<=old.sequence || next.entries.size()<old.entries.size())return false;
            for(const auto& e:old.entries) {
                const auto* n=Find(next,e.netId);
                if(!n || !SameBirth(old,e,next,*n))return false;
                if(e.terminal && (n->terminalSequence!=e.terminalSequence || n->removed!=e.removed || !n->terminal))return false;
                if(!e.terminal && n->terminal && n->terminalSequence<=old.sequence)return false;
            }
            for(const auto& e:next.entries)if(!Find(old,e.netId)) {
                if(e.birthSequence<=old.sequence)return false;
                const PopulationOccurrence* previous=nullptr;
                for(const auto& n:next.entries)if(n.netId!=e.netId && SameRecord(next,n,next,e) &&
                    n.birthSequence<e.birthSequence && (!previous || n.birthSequence>previous->birthSequence))previous=&n;
                if(!previous || !previous->terminal || !previous->removed ||
                   previous->terminalSequence>=e.birthSequence)return false;
            }
        }
        // No same-record successor may precede retirement, including bootstrap history.
        for(const auto& e:next.entries)for(const auto& other:next.entries)
            if(e.birthSequence<other.birthSequence && SameRecord(next,e,next,other) &&
               (!e.terminal || !e.removed || e.terminalSequence>=other.birthSequence))return false;
        cut_=next;return true;
    }
};
} // namespace kh2coop::ordinarylifecycle
