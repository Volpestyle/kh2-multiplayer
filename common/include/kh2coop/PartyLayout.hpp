#pragma once
#include "kh2coop/PlayerKits.hpp"
#include "kh2coop/Protocol.hpp"
#include <algorithm>
#include <optional>

namespace kh2coop {
// Authored MEMT/ARD values, not a guessed world-to-rule lookup. Native must
// supply the actual current program's Party/SetMember rule after story changes.
enum class PartyRule : std::uint8_t {
    NoFriend=0, Default=1, WorldOptional=2, WorldIn=3,
    WorldFixed=4, WorldOnly=5, DonaldOnly=6, Unavailable=0xFF
};
enum class PartyMemberKind : std::uint8_t { Empty, LocalPlayer, RemotePlayer, Donald, Goofy, WorldAlly };
enum class PartyApplyReason : std::uint8_t { HostChoice, RoomChanged, StoryForced, RosterChanged };
struct PartyMember {
    PartyMemberKind kind{PartyMemberKind::Empty};
    std::uint8_t playerSlot{0xFF}; // stable NETWORK slot, not local engine slot
    std::uint32_t objectId{0}; // only WorldAlly; exact current native model
    bool operator==(const PartyMember&) const = default;
};
// Host-view logical seats. Each machine's own local actor remains engine slot0;
// a future native projector must translate this, not rewrite network ownership.
struct PartyLayout {
    RoomTransition location{};
    std::uint64_t version{0}; // host-connection lifetime, never room-reset/wrapped
    std::array<std::uint64_t,3> connections{}; // includes benched peers
    PartyRule rule{PartyRule::Unavailable};
    PartyApplyReason reason{PartyApplyReason::HostChoice};
    std::array<PartyMember,3> seats{};
};
// Invalidation/reapply notification: no implicit authority to reuse an old
// rule/model in a new room. A newer complete PartyLayout must follow.
struct PartyReapply {
    RoomTransition location{};
    std::uint64_t afterVersion{0};
    PartyApplyReason reason{PartyApplyReason::RoomChanged};
};
// VUH-1786: the host's intended party for one TARGET room, published before the
// room loads (the per-load resolver runs before a room-pinned PartyLayout can
// exist). Room-independent: no epoch/door. Own version namespace per host
// connection. kits: objentry per seat (party kits): the player in each seat shows its
// owner's chosen kit. Seat 0 (the host's own player): 0 (legacy, = Sora) or a qualified kit;
// a remote-player seat: a qualified kit (kh2coop/PlayerKits.hpp); AI/empty seats: 0. Kits are
// qualified there, one live fixture each. Receivers without KH2COOP_PARTY_KITS refuse any non-Sora kit natively.
struct PartyIntentTarget {
    std::uint16_t worldId{0}, roomId{0}, eventProgram{0};
    bool operator==(const PartyIntentTarget&) const = default;
};
inline constexpr std::uint16_t PARTY_INTENT_KIT_SORA = 0x54;
inline constexpr std::uint16_t PARTY_INTENT_KIT_ROXAS = 0x5A; // P_EX110, the one qualified non-Sora kit
inline bool partyKitAllowed(std::uint16_t kit) { return qualifiedKit(kit)!=nullptr; } // the reviewed kit table (PlayerKits.hpp)
inline constexpr std::size_t PARTY_INTENT_PAYLOAD = 63;
struct PartyIntent {
    std::uint64_t version{0};
    std::array<std::uint64_t,3> connections{};
    PartyIntentTarget target{};
    PartyRule rule{PartyRule::Unavailable};
    std::array<PartyMember,3> seats{};
    std::array<std::uint16_t,3> kits{};
};
inline bool partyForcedAlly(PartyRule r) {
    return r==PartyRule::WorldIn || r==PartyRule::WorldFixed || r==PartyRule::WorldOnly;
}
inline unsigned partyFreeMask(PartyRule r) {
    if(r==PartyRule::NoFriend || r==PartyRule::WorldOnly)return 0;
    if(r==PartyRule::WorldIn || r==PartyRule::WorldFixed)return 4;
    if(r==PartyRule::DonaldOnly)return 2;
    return 6;
}
inline unsigned partyBenchedMask(const PartyLayout& p) {
    unsigned mask=0;for(unsigned i=1;i<3;++i)if(p.connections[i])mask|=1u<<i;
    for(const auto& s:p.seats)if(s.kind==PartyMemberKind::RemotePlayer && s.playerSlot<3)mask&=~(1u<<s.playerSlot);
    return mask;
}
inline bool validPartyLayout(const PartyLayout& p, const std::array<std::uint64_t,3>& roster) {
    if(!p.version || !p.location.epoch || p.connections!=roster || !roster[0] ||
       static_cast<unsigned>(p.rule)>6 || static_cast<unsigned>(p.reason)>3 ||
       p.seats[0]!=PartyMember{PartyMemberKind::LocalPlayer,0,0})return false;
    for(unsigned i=0;i<3;++i)for(unsigned j=i+1;j<3;++j)if(roster[i] && roster[i]==roster[j])return false;
    unsigned occupiedPlayers=0,seenPlayers=0,seenAi=0;
    const auto free=partyFreeMask(p.rule);
    for(unsigned i=1;i<3;++i){const auto& s=p.seats[i];
        if(i==1 && partyForcedAlly(p.rule)){
            if(s.kind!=PartyMemberKind::WorldAlly || !s.objectId || s.playerSlot!=0xFF)return false;
            continue;
        }
        if(!(free&(1u<<i))){if(s!=PartyMember{})return false;continue;}
        if(s.kind==PartyMemberKind::RemotePlayer){
            if(s.playerSlot<1 || s.playerSlot>2 || !roster[s.playerSlot] || s.objectId || (seenPlayers&(1u<<s.playerSlot)))return false;
            seenPlayers|=1u<<s.playerSlot;++occupiedPlayers;
        }else{
            if(s.playerSlot!=0xFF || s.objectId)return false;
            if(s.kind==PartyMemberKind::Empty)continue;
            if(s.kind!=PartyMemberKind::Donald && s.kind!=PartyMemberKind::Goofy)return false;
            if(p.rule==PartyRule::DonaldOnly && s.kind!=PartyMemberKind::Donald)return false;
            const auto bit=1u<<static_cast<unsigned>(s.kind);if(seenAi&bit)return false;seenAi|=bit;
        }
    }
    const unsigned players=unsigned(roster[1]!=0)+unsigned(roster[2]!=0);
    const unsigned capacity=unsigned((free&2)!=0)+unsigned((free&4)!=0);
    return occupiedPlayers==(std::min)(players,capacity); // no AI/empty displaces a legal player
}
// Same seat policy as a PartyLayout for the target room, plus the kit rule.
inline bool validPartyIntent(const PartyIntent& m, const std::array<std::uint64_t,3>& roster) {
    PartyLayout probe{};
    probe.location.epoch=1; probe.location.worldId=m.target.worldId; probe.location.roomId=m.target.roomId;
    probe.location.eventProgram=m.target.eventProgram;
    probe.version=m.version; probe.connections=m.connections; probe.rule=m.rule; probe.reason=PartyApplyReason::HostChoice;
    probe.seats=m.seats;
    if(!validPartyLayout(probe,roster) || m.target.worldId==0xFFFF || m.target.roomId==0xFFFF)return false;
    if(m.kits[0]!=0 && !partyKitAllowed(m.kits[0]))return false; // 0 = legacy Sora (VUH-1786 hosts)
    for(unsigned i=1;i<3;++i){
        const bool player=m.seats[i].kind==PartyMemberKind::RemotePlayer;
        if(player ? !partyKitAllowed(m.kits[i]) : m.kits[i]!=0)return false;
    }
    return true;
}
inline std::optional<PartyLayout> defaultPartyLayout(RoomTransition room, std::uint64_t version,
        PartyApplyReason reason, PartyRule rule, std::uint32_t forcedAllyObject,
        const std::array<std::uint64_t,3>& roster, std::array<std::uint8_t,2> priority={1,2}) {
    if(priority[0]<1 || priority[0]>2 || priority[1]<1 || priority[1]>2 || priority[0]==priority[1] ||
       partyForcedAlly(rule)!=(forcedAllyObject!=0))return std::nullopt;
    PartyLayout p{room,version,roster,rule,reason,{}};
    p.seats[0]={PartyMemberKind::LocalPlayer,0,0};
    if(partyForcedAlly(rule))p.seats[1]={PartyMemberKind::WorldAlly,0xFF,forcedAllyObject};
    unsigned next=0;
    for(unsigned i=1;i<3;++i){if(!(partyFreeMask(rule)&(1u<<i)))continue;
        while(next<2 && !roster[priority[next]])++next;
        if(next<2)p.seats[i]={PartyMemberKind::RemotePlayer,priority[next++],0};
        else p.seats[i]={i==1?PartyMemberKind::Donald:PartyMemberKind::Goofy,0xFF,0};
    }
    if(!validPartyLayout(p,roster))return std::nullopt;
    return p;
}
} // namespace kh2coop
