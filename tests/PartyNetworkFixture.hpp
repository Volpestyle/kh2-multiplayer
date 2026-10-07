#pragma once
#include "kh2coop/NetworkClient.hpp"
#include "kh2coop/SessionHost.hpp"
#include <array>

template<class Pump, class Check>
void partyNetworkChecks(kh2coop::SessionHost& server,
                        const std::array<kh2coop::NetworkClient*,3>& peers,
                        Pump pump, Check check) {
    using namespace kh2coop;
    RoomTransition room{30,8,12,0,0,1,0};
    peers[0]->sendRoomTransition(room);pump();
    std::array<std::uint64_t,3> ids{};
    for(unsigned i=0;i<3;++i)ids[i]=server.peerBySlot(static_cast<SlotType>(i))->connectionId;
    auto layout=*defaultPartyLayout(room,1,PartyApplyReason::HostChoice,PartyRule::Default,0,ids);
    check(!peers[0]->sendPartyLayout({}) && !peers[0]->requestPartyReapply({}),"unavailable/default host party input refuses without dispatch");
    check(peers[0]->sendPartyLayout(layout),"host can choose party through runtime network API");pump();
    check(server.partyLayout() && std::all_of(peers.begin(),peers.end(),[](auto* p){return p->partyLayout() && p->partyLayout()->version==1;}),
          "same versioned party reaches host and both friends");
    check(!peers[1]->sendPartyLayout(layout),"non-host convenience API refuses party authorship");
    auto forged=layout;forged.version=99;
    peers[1]->sendNativeWorld(encode(forged),{1,peers[1]->deliverySerial(),0},true);pump();
    check(server.partyLayout()->version==1,"relay refuses non-host party even through raw world producer");
    peers[0]->sendPartyLayout(layout);pump();check(peers[2]->partyLayout()->version==1,"duplicate layout cannot become a second application");
    auto wrong=layout;wrong.version=2;wrong.connections[2]+=1000;
    peers[0]->sendPartyLayout(wrong);pump();check(server.partyLayout()->version==1,"retired roster binding refused");
    wrong=layout;wrong.version=2;wrong.location.eventProgram=2;
    peers[0]->sendPartyLayout(wrong);pump();check(server.partyLayout()->version==1,"party from another full room tuple refused");
    check(peers[0]->requestPartyReapply({room,1,PartyApplyReason::StoryForced}),"host reports story-forced party change");pump();
    check(!server.partyLayout() && std::none_of(peers.begin(),peers.end(),[](auto* p){return p->partyLayout().has_value();}),
          "story reapplication retires old party everywhere until new checked layout");
    layout=*defaultPartyLayout(room,2,PartyApplyReason::StoryForced,PartyRule::WorldFixed,1234,ids,{2,1});
    peers[0]->sendPartyLayout(layout);pump();
    check(peers[1]->partyLayout() && peers[1]->partyLayout()->seats[1].kind==PartyMemberKind::WorldAlly &&
          peers[1]->partyLayout()->seats[2].playerSlot==2 && partyBenchedMask(*peers[1]->partyLayout())==2,
          "forced ally retains seat and host-selected friend priority benches other player explicitly");
    peers[0]->requestPartyReapply({room,1,PartyApplyReason::StoryForced});pump();
    check(server.partyLayout().has_value(),"old invalidation floor cannot retire newer party");
    room.epoch=31;room.roomId=13;peers[0]->sendRoomTransition(room);pump();
    check(!server.partyLayout() && std::none_of(peers.begin(),peers.end(),[](auto* p){return p->partyLayout().has_value();}),
          "room change requires fresh rule; never blindly reapplies previous ally");
    layout=*defaultPartyLayout(room,3,PartyApplyReason::RoomChanged,PartyRule::NoFriend,0,ids);
    peers[0]->sendPartyLayout(layout);pump();
    check(peers[2]->partyLayout() && partyBenchedMask(*peers[2]->partyLayout())==6,
          "Sora-only room leaves both remote players seated nowhere without changing network ownership");
}
