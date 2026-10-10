#pragma once
// Passive one-shot selection only. No native memory, mutation or admission.
#include "CombatCausalTrace.hpp"

namespace kh2coop::inject::combatcausal {
class DisappearanceTracker {
    Scope original_{};
    std::array<Target,TargetCount> originals_{};
    std::array<bool,TargetCount> emitted_{};
    bool armed_{};
public:
    bool Arm(const Scope& scope,const Target* rows,unsigned count) noexcept {
        if(armed_ || !ScopeEligible(scope) || !rows || count!=TargetCount)return false;
        for(unsigned i=0;i<count;++i) {
            if(!SameTarget(rows[i],rows[i]))return false;
            for(unsigned j=0;j<i;++j)if(rows[i].netId==rows[j].netId || rows[i].actor==rows[j].actor)return false;
        }
        original_=scope;
        for(unsigned i=0;i<count;++i)originals_[i]=rows[i];
        armed_=true;return true;
    }
    // Incomplete/foreign censuses never become evidence of absence.
    int Next(const Scope& scope,bool complete,const Target* rows,unsigned count) noexcept {
        if(!armed_ || !complete || !SameScope(original_,scope) || count>TargetCount || (!rows&&count))return -1;
        for(unsigned i=0;i<TargetCount;++i) {
            if(emitted_[i])continue;
            bool found=false;
            for(unsigned j=0;j<count;++j)if(rows[j].actor==originals_[i].actor)found=true;
            if(!found){emitted_[i]=true;return static_cast<int>(i);}
        }
        return -1;
    }
    const Target& Original(unsigned index) const noexcept {return originals_[index];}
};
} // namespace kh2coop::inject::combatcausal
