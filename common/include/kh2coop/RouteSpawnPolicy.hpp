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
    auto digit=[](char c) { return c>='0' && c<='9' ? c-'0' : c>='A' && c<='F' ? c-'A'+10 : c-'a'+10; };
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
// VUH-2464: the Roxas prologue's first field, NOW 02/07 evt95 (TT07_MS101). Package b_40,
// group88 holds the player record raw566 and the two absent friend seats raw567/568 (row
// 00/12/12/12 leaves them unbuilt). Probe 20261018-165208, receipt [prologue-player-spawn].
inline constexpr std::array<std::uint8_t,10> TramCommon95Location{2,7,0,0,0x5F,0,0x5F,0,0x5F,0};
inline constexpr auto TramCommon95Header=Bytes("0100580003000000000000000000000000000000000000000000000000000000000000000000000000000000");
inline constexpr auto TramCommon95Records=Bytes("3602000075190fc5000000801f09cac4000000001bd63b3f0000000000000d000000000000000000000000000000000000000000000000000000000000000000370200009ca40ec500000080aa61b8c4000000001bd63b3f0000000000000e000000000000000000000000000000000000000000000000000000000000000000380200002bed17c5000000806d1fc9c4000000001bd63b3f0000000000000f000000000000000000000000000000000000000000000000000000000000000000");
// A checked missing-party site: the exact NOW, package, fixed-wrapper caller, header and complete
// group whose first record is the authored raw566. The added clone copies that record with only its
// objectId changed; the authored call still builds the canonical local (`localObject`, status key
// `localKey`). `prologue` sites take the clone from the remote seat's latched profile; Bailey keeps Sora.
struct MissingPartySite {
    std::array<std::uint8_t,10> location;
    std::uint32_t key;
    std::span<const std::uint8_t> header,records;
    std::uint16_t localObject,localKey;
    bool prologue;
    const char* name;
};
// The same-room event reloads that follow: event 104 (Armor Shop) reloads tt07 as evt97 (TT07_MS103,
// b_42 group132), event 106 (Accessory Shop) as evt99 (TT07_MS105, b_44 group176). Both seats logged
// identical receipts after native and host-led loads (run 20261018-185319).
inline constexpr std::array<std::uint8_t,10> TramCommon97Location{2,7,0,0,0x61,0,0x61,0,0x61,0};
inline constexpr auto TramCommon97Header=Bytes("0100840003000000000000000000000000000000000000000000000000000000000000000000000000000000");
inline constexpr auto TramCommon97Records=Bytes("360200007b360ec502000036ac80b8c4000000002f2916c0000000000000130000000000000000000000000000000000000000000000000000000000000000003702000000200ec502000036ec2dcac4000000002f2916c00000000000001400000000000000000000000000000000000000000000000000000000000000000038020000d75f05c502000036be53b8c4000000002f2916c000000000000015000000000000000000000000000000000000000000000000000000000000000000");
inline constexpr std::array<std::uint8_t,10> TramCommon99Location{2,7,0,0,0x63,0,0x63,0,0x63,0};
inline constexpr auto TramCommon99Header=Bytes("0100b00003000000000000000000000000000000000000000000000000000000000000000000000000000000");
inline constexpr auto TramCommon99Records=Bytes("36020000e5c845c525bd73b8121512c500000000d0c2aa3d0000000000001900000000000000000000000000000000000000000000000000000000000000000037020000b21340c542fa80b860550bc500000000d0c2aa3d0000000000001a0000000000000000000000000000000000000000000000000000000000000000003802000093884cc506b684b8e35f0cc500000000d0c2aa3d0000000000001b000000000000000000000000000000000000000000000000000000000000000000");
// The first room change: the cat lock-on (event 108) loads the Sandlot as evt76 (b_40 group49, still
// raw566 plus the absent raw567/568). Both seats logged identical receipts (run 20261018-204359).
inline constexpr std::array<std::uint8_t,10> Sandlot76Location{2,4,0,0,0x4C,0,0x4C,0,0x4C,0};
inline constexpr auto Sandlot76Header=Bytes("0100310003000000000000000000000000000000000000000000000000000000000000000000000000000000");
inline constexpr auto Sandlot76Records=Bytes("36020000991624c400000080e7792244000000001e3611c00000000000000b00000000000000000000000000000000000000000000000000000000000000000037020000c30121c400000080c182fe43000000001e3611c00000000000000c0000000000000000000000000000000000000000000000000000000000000000003802000013de00c400000080bd8e2544000000001e3611c00000000000000d000000000000000000000000000000000000000000000000000000000000000000");
// VUH-2466: the Day-6 Mansion Dusk ambush, Basement Hall evt134 (TT19_MS602, b_40 group50: raw566
// plus the absent raw567/568). 74 byte-identical receipts from both seats (runs 20261019-1*).
inline constexpr std::array<std::uint8_t,10> BasementHall134Location{2,0x13,0,0,0x86,0,0x86,0,0x86,0};
inline constexpr auto BasementHall134Header=Bytes("0100320003000000000000000000000000000000000000000000000000000000000000000000000000000000");
inline constexpr auto BasementHall134Records=Bytes("360200000000000000000080000048c200000000000000000000000000000a000000000000000000000000000000000000000000000000000000000000000000370200000000c842000000800000484200000000000000000000000000000b000000000000000000000000000000000000000000000000000000000000000000380200000000c8c2000000800000484200000000000000000000000000000c000000000000000000000000000000000000000000000000000000000000000000");
// VUH-2466: the Day-6 Back Alley Dusk ambush, evt73 (TT03_MS601, headerId 90: raw566 plus the absent
// raw567/568). Both seats logged byte-identical receipts from FM-01 (run 20261020-043937).
inline constexpr std::array<std::uint8_t,10> BackAlley73Location{2,3,0,0,0x49,0,0x49,0,0x49,0};
inline constexpr auto BackAlley73Header=Bytes("01005a0003000000000000000000000000000000000000000000000000000000000000000000000000000000");
inline constexpr auto BackAlley73Records=Bytes("36020000f474fec3ff35d8385917dfc2000000001672fabf0000000000000e0000000000000000000000000000000000000000000000000000000000000000003702000099f2e2c3580633bd75d371c3000000001672fabf0000000000000f000000000000000000000000000000000000000000000000000000000000000000380200001051bdc3fa3fdb38e01b62c2000000001672fabf00000000000010000000000000000000000000000000000000000000000000000000000000000000");
// VUH-2466: the Day-1 Sandlot bat duel, evt77 (TT04_MS107: raw566, the absent raw567/568 and the opponent
// N_EX570_BTL 1656). Both seats logged byte-identical receipts after the bat choice (run 20261020-071814).
inline constexpr std::array<std::uint8_t,10> Sandlot77Location{2,4,0,0,0x4D,0,0x4D,0,0x4D,0};
inline constexpr auto Sandlot77Header=Bytes("0100470004000000000000000000000000000000000000000000000000000000000000000000000000000000");
inline constexpr auto Sandlot77Records=Bytes("36020000de6b0dc200000080de6b0d4200000000e4cb16c00000000000000e00000000000000000000000000000000000000000000000000000000000000000037020000e16b0dc200000080cb21d4c200000000e4cb16c00000000000000f00000000000000000000000000000000000000000000000000000000000000000038020000cb21d44200000080dc6b0d4200000000e4cb16c00000000000001000000000000000000000000000000000000000000000000000000000000000000078060000cb2154c300000080cb21544300000000db0f493f00000000030057000100000000000000000000000000000000000000000000000000000000000000");
// VUH-2466: the bat duel's second stage, evt78 (TT04_MS108; the same four-record shape under its own key).
// Both seats logged byte-identical receipts (run 20261020-093747).
inline constexpr std::array<std::uint8_t,10> Sandlot78Location{2,4,0,0,0x4E,0,0x4E,0,0x4E,0};
inline constexpr auto Sandlot78Header=Bytes("01005d0004000000000000000000000000000000000000000000000000000000000000000000000000000000");
inline constexpr auto Sandlot78Records=Bytes("36020000de6b0dc200000080de6b0d4200000000e4cb16c00000000000001100000000000000000000000000000000000000000000000000000000000000000037020000e16b0dc200000080cb21d4c200000000e4cb16c00000000000001200000000000000000000000000000000000000000000000000000000000000000038020000cb21d44200000080dc6b0d4200000000e4cb16c000000000000013000000000000000000000000000000000000000000000000000000000000000000780600009b4216c4000000809c42164400000000db0f493f00000000030014000200000000000000000000000000000000000000000000000000000000000000");
// VUH-2570: the Dive's Station of Calling, btl1 evt0 (b_00, headerId 23: raw566 alone, no 567/568 rows;
// the clone is built from the raw566 copy as everywhere). Unpinned, it stayed native solo and neither seat
// saw the other. Both seats logged byte-identical receipts from FM-00 (runs 20261019-225122 to -231804).
inline constexpr std::array<std::uint8_t,10> Calling33Location{2,0x21,0,0,0,0,1,0,0,0};
inline constexpr auto Calling33Header=Bytes("0100170001000000000000000000000000000000000000000000000021000000000000000000000000000000");
inline constexpr auto Calling33Records=Bytes("360200000080fc4400007a440000204300000000368da7bf00000000010001000000000000000000000000000000000000000000000000000000000000000000");
// VUH-2572: the Old Mansion's front on Day 1, evt1 (raw566 plus the absent raw567/568, headerId 40). Both
// seats logged byte-identical receipts (run 20261020-155646). The Woods before it (door 50, evt22) is not
// pinned: its walk-in entrance resets both players to the tunnel mouth when the clone is built there
// (runs 20261020-171912, -184451; docs/PARTY_SETUP.md).
// VUH-2578: the Woods, entered from the Back Alley's hole (door 50, evt22, headerId 57: raw566 plus the
// absent raw567/568). Nine byte-identical receipts from both seats (runs 20261020-100758, -151129, -155646).
inline constexpr std::array<std::uint8_t,10> Woods13Location{2,0x0D,0x32,0,1,0,1,0,0x16,0};
inline constexpr auto Woods13Header=Bytes("0100390003000000000000000000000000000000000000000000000000000000000000000000000000000000");
inline constexpr auto Woods13Records=Bytes("36020000b89ef6c4bbd306b748819fc400000000db0fc93f00000000013207000000000000000000000000000000000000000000000000000000000000000000370200005c8f01c5bbd306b7480193c400000000db0fc93f00000000013208000000000000000000000000000000000000000000000000000000000000000000380200005c8f01c5bbd306b74801acc400000000db0fc93f00000000013209000000000000000000000000000000000000000000000000000000000000000000");
inline constexpr std::array<std::uint8_t,10> Mansion14Location{2,0x0E,0,0,1,0,0,0,1,0};
inline constexpr auto Mansion14Header=Bytes("010028000300000000000000000000000000000000000000000000000e000000000000000000000000000000");
inline constexpr auto Mansion14Records=Bytes("36020000a7a8dcc397c11fb829ec44c5000000004ee43d400000000001000100000000000000000000000000000000000000000000000000000000000000000037020000724a0bc42025f9b700fe49c5000000004ee43d400000000001000200000000000000000000000000000000000000000000000000000000000000000038020000ce19b4c338cfafb7ae294cc5000000004ee43d4000000000010003000000000000000000000000000000000000000000000000000000000000000000");
// VUH-2572: the Back Alley after the Day 6 ambush, door 52 evt4 (headerId 156, 18 records: raw566, the
// absent raw567/568 and the alley's people). Both seats logged byte-identical receipts (run 20261020-191402).
inline constexpr std::array<std::uint8_t,10> BackAlley4Location{2,3,0x34,0,5,0,0,0,4,0};
inline constexpr auto BackAlley4Header=Bytes("01009c0012000000000000000000000000000000000000000000000000000000000000000000000000000000");
inline constexpr auto BackAlley4Records=Bytes("36020000e920adc306a184be4d61d5c100000000fc222d4000000000013212000000000000000000000000000000000000000000000000000000000000000000370200002993efc306a184be3c1596c200000000fc222d400000000001321300000000000000000000000000000000000000000000000000000000000000000038020000adf194c306a184beaa901fc300000000fc222d400000000001321400000000000000000000000000000000000000000000000000000000000000000036020000d1311fc3dee414383c858dc3000000001e36913f000000000133150000000000000000000000000000000000000000000000000000000000000000003702000048904fc3dee41438f72516c3000000001e36913f0000000001331600000000000000000000000000000000000000000000000000000000000000000038020000290b92c3dee4143878b4a5c3000000001e36913f00000000013317000000000000000000000000000000000000000000000000000000000000000000360200001c0cc9c326342e380b75d2c200000000f366df3f00000000013418000000000000000000000000000000000000000000000000000000000000000000370200002c7c01c426342e38b0e6c0c100000000f366df3f0000000001341900000000000000000000000000000000000000000000000000000000000000000038020000f29af1c326342e38ff125dc300000000f366df3f0000000001341a0000000000000000000000000000000000000000000000000000000000000000003602000064bbaac375c716bf4bf2ccc000000000d90fc9bf0000000001351b00000000000000000000000000000000000000000000000000000000000000000037020000c97671c375c716bf28cfd4c200000000d90fc9bf0000000001351c00000000000000000000000000000000000000000000000000000000000000000038020000c97671c375c716bfde30bb4200000000d90fc9bf0000000001351d000000000000000000000000000000000000000000000000000000000000000000360200005b890644dee41438b2a982c4000000001e3611c00000000001367900000000000000000000000000000000000000000000000000000000000000000037020000349e0944dee41438f44594c4000000001e3611c00000000001367a00000000000000000000000000000000000000000000000000000000000000000038020000e3c12944dee41438441f81c4000000001e3611c00000000001367b000000000000000000000000000000000000000000000000000000000000000000360200006ef4a043dee41438dd3829c400000000aa611cc00000000001378700000000000000000000000000000000000000000000000000000000000000000037020000bdca9a43dee4143865714cc400000000aa611cc000000000013788000000000000000000000000000000000000000000000000000000000000000000380200007a65e743dee41438b54d2cc400000000aa611cc000000000013789000000000000000000000000000000000000000000000000000000000000000000");
// VUH-2572: the Basement Hall after the post-Axel chain, door 50 evt2 (headerId 94, 6 records). Both seats
// logged byte-identical receipts (run 20261020-191946, FM-02).
inline constexpr std::array<std::uint8_t,10> BasementHall2Location{2,0x13,0x32,0,1,0,0,0,2,0};
inline constexpr auto BasementHall2Header=Bytes("01005e0006000000000000000000000000000000000000000000000000000000000000000000000000000000");
inline constexpr auto BasementHall2Records=Bytes("36020000de050ec2d304ddb8c69c87c100000000a9619cbf0000000001320e00000000000000000000000000000000000000000000000000000000000000000037020000c454b942d304ddb8047099c200000000a9619cbf0000000001320f000000000000000000000000000000000000000000000000000000000000000000380200008d17c241d304ddb87d70de4200000000a9619cbf0000000001321000000000000000000000000000000000000000000000000000000000000000000036020000f8e621443b09ed37817cc7c300000000db0f493f0000000001331100000000000000000000000000000000000000000000000000000000000000000037020000f8e6214468b6223894c680c300000000db0f493f00000000013312000000000000000000000000000000000000000000000000000000000000000000380200000318fd433b09ed37817cc7c300000000db0f493f00000000013313000000000000000000000000000000000000000000000000000000000000000000");
inline constexpr MissingPartySite MissingPartySites[]{
    {BaileyLocation,808738658,BaileyHeader,BaileyRecords,84,1,false,"hb08-bailey"},
    {TramCommon95Location,808738658,TramCommon95Header,TramCommon95Records,90,14,true,"tt07-95"},
    {TramCommon97Location,842293090,TramCommon97Header,TramCommon97Records,90,14,true,"tt07-97"},
    {TramCommon99Location,875847522,TramCommon99Header,TramCommon99Records,90,14,true,"tt07-99"},
    {Sandlot76Location,808738658,Sandlot76Header,Sandlot76Records,90,14,true,"tt04-76"},
    {BasementHall134Location,808738658,BasementHall134Header,BasementHall134Records,90,14,true,"tt19-134"},
    {BackAlley73Location,808738658,BackAlley73Header,BackAlley73Records,90,14,true,"tt03-73"},
    {Sandlot77Location,825515874,Sandlot77Header,Sandlot77Records,90,14,true,"tt04-77"},
    {Sandlot78Location,842293090,Sandlot78Header,Sandlot78Records,90,14,true,"tt04-78"},
    {Calling33Location,808476525,Calling33Header,Calling33Records,90,14,true,"tt33-calling"},
    {Mansion14Location,808476525,Mansion14Header,Mansion14Records,90,14,true,"tt14-1"},
    {Woods13Location,808804197,Woods13Header,Woods13Records,90,14,true,"tt13-22"},
    {BackAlley4Location,808804197,BackAlley4Header,BackAlley4Records,90,14,true,"tt03-4"},
    {BasementHall2Location,808804197,BasementHall2Header,BasementHall2Records,90,14,true,"tt19-2"},
};
// VUH-2578: at these sites both seats' players stand on the same raw566 point, so each seat's clone is
// driven exactly onto the local player. In the Woods the push-out sent both players to the tunnel mouth
// and held them there for about 40 s (5 of 8 early releases); seat 1 steps its own player aside by
// StepAsideZ once per load at its release, and 3 of 3 runs then walked on (runs 20261020-214917 to -215400).
inline constexpr float StepAsideZ=150.0f;
inline constexpr std::array<std::array<std::uint8_t,10>,1> StepAsideSites{{Woods13Location}};
inline bool StepAsideAt(const std::array<std::uint8_t,10>& location) {
    for(const auto& l:StepAsideSites)if(l==location)return true;
    return false;
}
// Header +4 is the group's record count; every record of the group is pinned.
inline const MissingPartySite* MissingPartySiteFor(const std::array<std::uint8_t,10>& location,std::uint32_t key,
        std::uintptr_t caller,std::span<const std::uint8_t> header,std::span<const std::uint8_t> records) {
    if(caller!=0x3FF727)return nullptr;
    for(const auto& site:MissingPartySites)
        if(site.location==location && site.key==key && Equal(header,site.header) && Equal(records,site.records))return &site;
    return nullptr;
}
// VUH-2572: the pinned group's raw566 record at a 64-byte index. A group can hold one 566/567/568 triple
// per entrance (Back Alley evt4: six), and the game builds the one for the entrance taken.
inline bool SitePlayerRecordAt(const MissingPartySite& site,std::size_t index) {
    return index<site.records.size()/64 && site.records[index*64]==0x36 && site.records[index*64+1]==0x02 &&
           site.records[index*64+2]==0 && site.records[index*64+3]==0;
}
inline bool SiteHasPlayerRecord(const MissingPartySite& site,std::span<const std::uint8_t> record) {
    if(record.size()!=64)return false;
    for(std::size_t i=0;i<site.records.size()/64;++i)
        if(SitePlayerRecordAt(site,i) && Equal(record,site.records.subspan(i*64,64)))return true;
    return false;
}
inline const MissingPartySite* MissingPartySiteAt(const std::array<std::uint8_t,10>& location) {
    for(const auto& site:MissingPartySites)if(site.location==location)return &site;
    return nullptr;
}
// Only this observed base-constructor allocation has the fatal fallback contract: Sora (key1) and,
// for the prologue's Roxas (key14, the same player-class base constructor), form0 only.
inline bool MissingPartyAbort(std::uintptr_t caller,int key,int form) {
    return caller==0x3A7B1B && (key==1 || key==14) && form==0;
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
