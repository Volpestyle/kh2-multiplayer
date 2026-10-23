#pragma once
#include <cstdint>
#include <array>
#include <cstring>
namespace kh2coop::inject::scriptmirror::rules {
// Kind11 is an entity camera, not a story scene. Exact installed evtp record:
// 0B 00 6C 00 00 00 00 00. The adapter additionally validates the admitted role.
struct EntityCameraIdentity {
    std::uintptr_t context{},sourceAnchor{},asset{};
    std::uint64_t kindRecord{};
};
inline bool SameEntityCamera(const EntityCameraIdentity& opened,
                             const EntityCameraIdentity& current) noexcept {
    return opened.context && opened.sourceAnchor && opened.asset &&
        opened.kindRecord==0x6C000Bull && opened.context==current.context &&
        opened.sourceAnchor==current.sourceAnchor && opened.asset==current.asset &&
        current.kindRecord==opened.kindRecord;
}

// VUH-2707: diagnostics only. Operands are evaluated by the unchanged caller;
// these wrappers return that exact result and never invoke a reader/getter.
struct CameraObservation {
    enum Field : unsigned { Context, Asset, Anchor, Metadata, Kind, InField, Menu, Frozen,
                           Active, State, ActorStatus, SourceStatus, Thread, VmSource, ResidentAsset, Count };
    bool enabled{},watch{true};
    const char* reason{"complete"};
    std::uint32_t observed{};
    std::array<std::uint64_t,Count> values{};
    bool Bad(bool rejected,const char* why) noexcept {
        if(enabled && watch && rejected && !std::strcmp(reason,"complete"))reason=why;
        return rejected;
    }
    bool Good(bool accepted,const char* why) noexcept {
        (void)Bad(!accepted,why);return accepted;
    }
    template<class T> bool Read(bool read,const char* why,Field field,const T& value) noexcept {
        // Reference binding does not sample `value` before the reader operand.
        // A failed/partial read is explicitly unobserved, even if it wrote bytes.
        if(enabled && watch && read) {observed|=1u<<field;values[field]=static_cast<std::uint64_t>(value);}
        (void)Bad(!read,why);
        return read;
    }
};
struct CameraObservationKey {
    unsigned site{},nativeCalls{};
    std::uint32_t role{};
    std::uint64_t connection{},context{};
    bool result{};
    const char* reason{};
};
class CameraObservationBudget {
public:
    static constexpr unsigned Capacity=64;
    bool Take(bool enabled,const CameraObservationKey& key) noexcept {
        if(!enabled)return false;
        for(unsigned i=0;i<used_;++i) {
            const auto& old=seen_[i];
            if(old.site==key.site && old.nativeCalls==key.nativeCalls && old.role==key.role &&
               old.connection==key.connection && old.context==key.context && old.result==key.result &&
               !std::strcmp(old.reason,key.reason))return false;
        }
        if(used_==Capacity)return false;
        seen_[used_++]=key;return true;
    }
    unsigned Used() const noexcept {return used_;}
private:
    unsigned used_{};
    std::array<CameraObservationKey,Capacity> seen_{};
};
}
