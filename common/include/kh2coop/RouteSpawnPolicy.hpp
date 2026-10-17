#pragma once
#include <array>
#include <cstdint>
#include <span>
#include <string_view>
#include "kh2coop/MissionFlags.hpp"

namespace kh2coop::routespawn {
// Exact installed ARD definitions; see docs/ROUTE_SPAWNS.md for provenance.
template<std::size_t N> constexpr auto Bytes(const char (&hex)[N]) {
    static_assert(N%2==1); std::array<std::uint8_t,(N-1)/2> out{};
    auto digit=[](char c) { return c>='0' && c<='9' ? c-'0' : c-'a'+10; };
    for(std::size_t i=0;i<out.size();++i) out[i]=static_cast<std::uint8_t>((digit(hex[2*i])<<4)|digit(hex[2*i+1]));
    return out;
}
inline bool Equal(std::span<const std::uint8_t> a,std::span<const std::uint8_t> b) {
    if(a.size()!=b.size())return false;
    for(std::size_t i=0;i<a.size();++i)if(a[i]!=b[i])return false;
    return true;
}
inline constexpr auto MoraleWrapperBody=Bytes("48895c24084889742410574883ec20f30f100511552200498bd8f30f5c4114488bf2488bf9e8a688fbff8b0f0f28d0488bd3e8a912feff488bd84885c074478b08e8daeb0a004c8bcf4c8bc6488bd3488bc84c8b1041ff52688b0fe80018feff0fb6480480f917771ab81020a0000fa3c873100fb7571e488d0d92776e02e80d75ffff488bc3488b5c2430488b7424384883c4205fc3");
inline constexpr auto MoraleCaller=Bytes("488b47084c8d4424404883c02c4963ce48c1e106488bd74803c8e8c7fcffff4885c07415");
inline constexpr std::array<std::uint8_t,10> MoraleLocation{8,1,0,0,68,0,68,0,0,0};
inline constexpr std::array<std::uint8_t,10> BaileyLocation{4,8,0,0,52,0,52,0,0,0};
inline constexpr auto MoraleHeader978=Bytes("0200d20301000100000000000000000000000000000000000000000000000000000000000000000000000000");
inline constexpr auto MoraleRecords978=Bytes("2e010000e0c51544963affb7bca485c4000000000000000000000000020102020000000000000000000000000000000000000000000000000000000000000000");
inline constexpr auto MoraleHeader999=Bytes("0200e70301000100000000000000000000000000000000000000000000000000000000000000000000000000");
inline constexpr auto MoraleRecords999=Bytes("2e01000054a78944963affb79e6f7dc400000000000000000000000002011b020000000000000000000000000000000000000000000000000000000000000000");
inline constexpr auto MoraleHeader1007=Bytes("0200ef0302000100000000000000000000000000000000000000000000000000000000000000000000000000");
inline constexpr auto MoraleRecords1007=Bytes("0a000000e0c51544963affb7bca485c40000000000000000000000000201320200000000000000000000000000000000000000000000000000000000000000000a000000f2628944963affb7744927c4000000000000000000000000020133020000000000000000000000000000000000000000000000000000000000000000");
inline constexpr auto BaileyHeader=Bytes("0100370004000000000000000000000000000000000000000000000000000000000000000000000000000000");
inline constexpr auto BaileyRecords=Bytes("360200000000afc3000061c400007a4400000000db0fc9bf0000000000000a000000000000000000000000000000000000000000000000000000000000000000f80700000040b5c4000061c40040834400000000db0fc93f0000000000001f000000000000000000000000000000000000000000000000000000000000000000f0050000a08699c4000061c4b66f834400000000db0fc93f00000000000071000a000000000000000000000000000000000000000000000000000000000000001c0600000000c842000061c400007a4400000000db0fc93f0000000003001e000000000000000000000000000000000000000000000000000000000000000000");
inline constexpr auto BaileyCreeperHeader=Bytes("02013a000a00010000000300010000000000000000000000000000001e000000000000000000000000000000");
inline constexpr auto BaileyCreeperRecords=Bytes("3d01000032f7bb41000096c47be4814400000000db0fc93f0000000002006700000000000000000000000f0000000000000000000000000000000000000000003d010000cafd9242000096c47ba4944400000000db0fc93f000000000200210000000000000000000000000000000000000000000000000000000000000000003d010000cefd9242000096c4f6485e4400000000db0fc93f0000000002008100000000000000000000000f0000000000000000000000000000000000000000003d010000ccfdf642000096c47ba4ad4400000000db0fc93f000000000200660000000000000000000000000000000000000000000000000000000000000000003d010000ccfdf642000096c4f6c8384400000000db0fc93f000000000200680000000000000000000000000000000000000000000000000000000000000000003e010000aa51e0c3070061c46d03934400000000db0fc93f0000000002006c0000000000000000000000000000000000010000000000000000000000000000003e010000aa51e0c3050061c4db065b4400000000db0fc93f000000000200830000000000000000000000000000000000010000000000000000000000000000003e010000aa51c7c3050061c46d03ac4400000000db0fc93f0000000002006d0000000000000000000000000000000000010000000000000000000000000000003e010000aa51c7c3050061c46d43804400000000db0fc93f0000000002006e0000000000000000000000000000000000010000000000000000000000000000003e010000aa51c7c3050061c4db86354400000000db0fc93f0000000002007c000000000000000000000000000000000001000000000000000000000000000000");
inline constexpr auto BaileyCaller=Bytes("488bd3488d4fe4e869eeffff488b4308");
inline bool MoraleGenerated(const std::array<std::uint8_t,10>& location,std::uint32_t key,
                            std::span<const std::uint8_t> header,std::span<const std::uint8_t> records) {
    if(location!=MoraleLocation)return false;
    // Probe03: native type2 activation changes only header+0xE from0 to1.
    // This bounded state is not definition identity; every other byte stays pinned.
    const auto headerMatches=[&header](const auto& pin) {
        if(header.size()!=pin.size() || header[0xE]>1)return false;
        for(std::size_t i=0;i<header.size();++i)
            if(i!=0xE && header[i]!=pin[i])return false;
        return true;
    };
    return (key==0x70345F62 && ((headerMatches(MoraleHeader978) && Equal(records,MoraleRecords978)) ||
                              (headerMatches(MoraleHeader999) && Equal(records,MoraleRecords999)))) ||
           (key==0x73345F62 && headerMatches(MoraleHeader1007) && Equal(records,MoraleRecords1007));
}
inline bool Bailey(const std::array<std::uint8_t,10>& location,std::uint32_t key,std::uintptr_t caller,
                   std::span<const std::uint8_t> header,std::span<const std::uint8_t> records) {
    return location==BaileyLocation && key==808738658 && caller==0x3FF727 &&
        Equal(header,BaileyHeader) && Equal(records,BaileyRecords);
}
// VUH-2343: definition admission does not supply a birth or action permission.
// 3FE320's type2 branch sets header+0xE to1 before 3FE6F0 emits. Bailey-08
// measured that transition: retain the raw byte in native bookends, but exclude
// only this bounded runtime state from the installed definition comparison.
inline bool BaileyCreeperHeaderMatches(std::span<const std::uint8_t> header) {
    if(header.size()!=BaileyCreeperHeader.size() || header[0xE]>1)return false;
    for(std::size_t i=0;i<header.size();++i)
        if(i!=0xE && header[i]!=BaileyCreeperHeader[i])return false;
    return true;
}
inline bool BaileyCreeperGroup(const std::array<std::uint8_t,10>& location,std::uint32_t key,
                               std::span<const std::uint8_t> header,std::span<const std::uint8_t> records,unsigned ordinal) {
    return location==BaileyLocation && key==808738658 && ordinal<5 &&
        BaileyCreeperHeaderMatches(header) && Equal(records,BaileyCreeperRecords);
}
inline bool BaileyCreeperMission(const std::array<std::uint8_t,10>& location,std::string_view file,
                                 bool active,std::uint32_t flags,std::uint32_t result,bool terminal) {
    namespace mr=kh2coop::inject::missionmirror::rules;
    return location==BaileyLocation && file=="HB08_MS102.bar" && active && (flags&mr::kInUse) && result!=mr::kFailedResult &&
        (terminal || (flags&mr::kEnded)!=mr::kEnded);
}
inline bool MoraleGeneratedMission(const std::array<std::uint8_t,10>& location,std::string_view file,
                                   bool active,std::uint32_t flags,std::uint32_t result,bool terminal) {
    namespace mr=kh2coop::inject::missionmirror::rules;
    return location==MoraleLocation && file=="MU01_MS102.bar" && active && (flags&mr::kInUse) &&
        result!=mr::kFailedResult && (terminal || (flags&mr::kEnded)!=mr::kEnded);
}
// The full current native definition is still revalidated by EmissionBirthCurrent.
inline bool MoraleGeneratedBirth(std::uint32_t key,unsigned header,unsigned record,unsigned count) {
    return (key==0x70345F62 && (header==978 || header==999) && record==0 && count==1) ||
        (key==0x73345F62 && header==1007 && record<2 && count==2);
}
inline bool MoraleGeneratedBirth(std::uint32_t key,unsigned header,unsigned record,unsigned count,
                                 std::uint32_t objectId,std::uint32_t type) {
    return MoraleGeneratedBirth(key,header,record,count) && type==4 && objectId==(header==1007?10U:302U);
}
// A birth's sampled native roots must be observed on continuous owner frames.
// A failed observation consumes this occurrence; a later same address cannot revive it.
struct BaileyCreeperLease {
    std::uint32_t seen{};bool retired{};
    bool Observe(std::uint32_t frame,bool current) {
        if(retired || !seen || !current || frame<seen || frame-seen>1){retired=true;return false;}
        seen=frame;return true;
    }
};
// Same fail-closed owner-frame continuity as Bailey; no generic adopted receipt gains authority.
using MoraleGeneratedLease=BaileyCreeperLease;
// Process-lifetime record storage is never recycled while native handlers retain it.
struct MissingPartyBudget {
    static constexpr unsigned Capacity=16;
    unsigned count{}; std::uint32_t lastLoad{};
    bool Claim(std::uint32_t load) {
        if(!load || load<=lastLoad || count>=Capacity)return false;
        lastLoad=load;++count;return true;
    }
};
// Bailey's wrapper runs inside the load finalizer, before CompleteLoad increments serial.
inline bool MissingPartyLoadCurrent(std::uint32_t beforeLoad,std::uint32_t transition,
                                    std::uint32_t load,std::uint32_t currentTransition,bool pending) {
    return beforeLoad && beforeLoad!=UINT32_MAX && transition && transition==currentTransition &&
        load==beforeLoad+1 && !pending;
}
// Negative fence only: same native stack, including a failed stack sample.
inline bool SameConstructionStack(std::uint32_t ownerThread,std::uintptr_t ownerHigh,
                                  std::uint32_t thread,std::uintptr_t low,std::uintptr_t high) {
    return ownerThread && ownerHigh && ownerThread==thread && (ownerHigh==high || high<=low);
}
// A later private allocator selection can reuse the same actor/status addresses.
// Its new owned serial retires an old quarantine claim; pointers alone cannot.
inline bool NewPrivateBirth(std::uint64_t previous,std::uint64_t observed,bool owned,bool ready) {
    return previous && observed && previous!=observed && owned && ready;
}
// Only this observed base-constructor allocation has the fatal fallback contract.
inline bool MissingPartyAbort(std::uintptr_t caller,int key,int form) {
    return caller==0x3A7B1B && key==1 && form==0;
}
struct MissingPartyBinding {
    std::uintptr_t clone{},local{},cloneStatus{},localStatus{},canonical{};
    bool privateOwned{},localOwned{},cloneAttached{},localAttached{},current{};
};
inline bool Bound(const MissingPartyBinding& b) {
    return b.clone && b.local && b.clone!=b.local && b.cloneStatus && b.localStatus &&
        b.cloneStatus!=b.localStatus && b.privateOwned && !b.localOwned &&
        b.canonical==b.local && b.cloneAttached && b.localAttached && b.current;
}
}
