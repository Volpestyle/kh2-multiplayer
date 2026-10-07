#pragma once
#include "kh2coop/NetworkClient.hpp"
#include "kh2coop/SessionHost.hpp"
#include <array>
#include <vector>
#include <limits>

// Same three-peer policy exercised by real ENet and the actual Steam broker
// stack with only Valve/OS-link mocks. No direct SessionHost private access.
template<class Pump, class Check>
void reviveNetworkChecks(kh2coop::SessionHost& server,
                         const std::array<kh2coop::NetworkClient*,3>& clients,
                         std::array<std::vector<kh2coop::ReviveRequest>,3>& received,
                         Pump pump, Check check) {
    using namespace kh2coop;
    const RoomTransition room{20,5,4,0,0,1,0};
    clients[0]->sendRoomTransition(room); pump();
    for (auto* client:clients) client->sendTransitionAck({room.epoch,5,4,true});
    pump();
    std::array<std::uint64_t,3> ids{};
    for (unsigned i=0;i<3;++i) ids[i]=server.peerBySlot(static_cast<SlotType>(i))->connectionId;
    std::array<AvatarState,3> avatars{};
    for(auto& a:avatars){a.worldId=5;a.roomId=4;a.downedEpoch=20;a.downedDelivery=1;a.hp=20;a.maxHp=20;}
    avatars[2].flags=AvatarDowned;avatars[2].hp=0;avatars[2].downedEpisode=1;
    auto publish=[&]{for(unsigned i=0;i<3;++i) clients[i]->sendAvatar(avatars[i]);pump();};
    publish();
    auto total=[&]{return received[0].size()+received[1].size()+received[2].size();};
    ReviveRequest request{room,1,ids[1],ids[2],1,0,2}; // forged requester slot must be stamped
    clients[1]->sendReviveRequest(request);pump();
    check(total()==1 && received[2].size()==1 && received[2][0].requesterSlot==1,
          "friend-to-friend revive delivered only to downed owner, slot stamped");
    clients[1]->sendReviveRequest(request);pump();
    request.seq=2;clients[1]->sendReviveRequest(request);pump();
    auto competing=request;competing.requesterConnectionId=ids[0];competing.seq=1;
    clients[0]->sendReviveRequest(competing);pump();
    check(total()==1,"duplicate/out-of-order and second teammate cannot revive one episode twice");
    avatars[2].downedEpisode=2;publish();request.targetEpisode=2;
    auto refused=[&](const char* label){const auto before=total();++request.seq;clients[1]->sendReviveRequest(request);pump();check(total()==before,label);};
    request.targetSlot=1;request.targetConnectionId=ids[1];refused("self revive refused");request.targetSlot=2;request.targetConnectionId=ids[2];
    request.targetConnectionId=ids[0];refused("wrong target connection refused");request.targetConnectionId=ids[2];
    request.requesterConnectionId=ids[0];refused("forged requester connection refused");request.requesterConnectionId=ids[1];
    request.location.epoch=19;refused("old room epoch refused");request.location=room;
    request.location.eventProgram=1;refused("same world/room with different full tuple refused");request.location=room;
    avatars[2].downedDelivery=2;publish();refused("avatar from another delivery lifetime refused");avatars[2].downedDelivery=1;
    avatars[2].position.x=REVIVE_RANGE+1;publish();refused("out-of-range request refused");
    avatars[2].position.x=0;avatars[1].flags=AvatarDowned;publish();refused("downed requester refused");
    avatars[1].flags=0;avatars[2].flags=AvatarInCutscene|AvatarDowned;publish();refused("cutscene target refused");
    avatars[2].flags=0;avatars[2].hp=20;publish();refused("standing target refused");
    avatars[2].flags=AvatarDowned;avatars[2].hp=0;avatars[2].position.x=std::numeric_limits<float>::quiet_NaN();publish();
    refused("nonfinite position refused without retaining old good authority");
    avatars[2].position.x=0;publish();clients[2]->sendTransitionAck({20,5,4,false});pump();
    refused("target not arrived refused");clients[2]->sendTransitionAck({20,5,4,true});pump();
    clients[0]->sendEventHold({20,true,0});pump();refused("active host event hold refuses revive");
    clients[0]->sendEventHold({20,false,0});pump();
    // A refused range request cannot be retried later under the same sequence.
    avatars[2].position.x=REVIVE_RANGE+1;publish();refused("outside range before replay control");
    avatars[2].position.x=0;publish();clients[1]->sendReviveRequest(request);pump();
    check(total()==1,"gameplay-refused sequence cannot become valid after moving");
    ++request.seq;clients[1]->sendReviveRequest(request);pump();
    check(total()==2 && received[2].size()==2,"new downed episode can be revived with fresh request");
    // The host is also a target owner, not a special revive authority bypass.
    avatars[2].flags=0;avatars[2].hp=20;
    avatars[0].flags=AvatarDowned;avatars[0].hp=0;avatars[0].downedEpisode=3;publish();
    request.targetSlot=0;request.targetConnectionId=ids[0];request.targetEpisode=3;++request.seq;
    clients[1]->sendReviveRequest(request);pump();
    check(total()==3 && received[0].size()==1,"friend can revive downed host through same validation");
    // No revive replay in late-join or world caches.
    check(server.currentRoom()->epoch==20,"revive does not mutate room authority");
}
