#include "AvatarPositionDiagnostic.hpp"
#include "AvatarPositionFaultBinding.hpp"
#include "kh2coop/AvatarSynth.hpp"
#include "kh2coop/AvatarPositionFaultChannel.hpp"
#include <iostream>
#include <vector>
#include <limits>
using namespace kh2coop;
namespace af=kh2coop::avatarfault;
namespace ap=kh2coop::inject::avatarposition;
namespace {
unsigned checks{},failures{};
void Check(bool p,const char* label){++checks;if(!p)++failures;std::cout<<(p?"PASS ":"FAIL ")<<label<<'\n';}
af::Offer Offer(){af::Offer o{af::Magic,99,100,1,sizeof(af::Offer),77,1,{}};o.binding.actor=0x1234;o.binding.handle=7;o.binding.load=2;o.binding.transition=3;o.binding.world=4;o.binding.room=26;o.binding.producer=1;o.binding.localSlot=255;return o;}
af::Request Request(){return {Offer(),123,100,10100,{40,0,0},af::Frames,af::ActiveMs};}
}
int main(){
{
 auto binding=Offer().binding;af::Admission a{true,true,true,true,true,92,7,1,4,26,255,0,0};
 Check(af::Admitted(a,binding),"production admission accepts exact native Donald Friend1 field in GoA");
 for(unsigned field=0;field<14;++field){auto b=a;auto scope=binding;
 switch(field){case 0:b.readable=false;break;case 1:b.active=false;break;case 2:b.unheld=false;break;case 3:b.friend1=false;break;case 4:b.repeated=false;break;case 5:b.objectId=93;break;case 6:b.handle++;break;case 7:b.type=0;break;case 8:b.world=2;break;case 9:b.room=10;break;case 10:b.menu=10;break;case 11:b.cutscene=1;break;case 12:b.event=1;break;case 13:scope.world=2;break;}
 Check(!af::Admitted(b,scope),"production admission rejects wrong native actor/load/room/menu/event/authority scope");
 }
}
{
 af::Admission native{true,true,true,true,true,92,7,1,4,26,255,0,0};
 auto binding=[](const avatarsynth::Sample& p){return ap::FaultBinding(ap::PoseScope(p.pose,p.provenance,0x1234,7,3,2,0));};
 const auto intended=avatarsynth::Puppet(1.0f,{10,20,30},0,2,4,26);
 Check(intended.active==1&&intended.pose.position.x==10&&intended.pose.worldId==4&&intended.pose.roomId==26,
       "actual synth producer publishes explicit intended room and pose");
 Check(af::Admitted(native,binding(intended)),"actual synth -> production Scope -> FaultBinding -> native admission succeeds");
 Check(!af::Admitted(native,binding(avatarsynth::Puppet(1.0f,{10,20,30},0,2))),"legacy room0/0 producer remains unchanged and refuses GoA fault");
 for(const auto room: {std::pair<unsigned,unsigned>{2,26},{4,10}}){
  const auto wrong=avatarsynth::Puppet(1.0f,{},0,2,static_cast<std::uint16_t>(room.first),static_cast<std::uint16_t>(room.second));
  Check(!af::Admitted(native,binding(wrong)),"wrong producer room cannot be authorized by native GoA");
 }
 for(unsigned mode=0;mode<2;++mode){auto wrong=native;if(mode)wrong.room=10;else wrong.world=2;
  Check(!af::Admitted(wrong,binding(intended)),"claimed producer GoA cannot authorize wrong independently read native room");
 }
 auto network=intended;network.provenance.producer=PuppetProducer::Network;
 Check(!af::Admitted(native,binding(network)),"actual pose with network provenance refuses standalone fault");
 Check(avatarsynth::ValidRoomOptions(false,false,0,0)&&avatarsynth::ValidRoomOptions(true,true,4,26),"legacy default and explicit paired room options valid");
 Check(!avatarsynth::ValidRoomOptions(true,false,4,0)&&!avatarsynth::ValidRoomOptions(false,true,0,26)&&
       !avatarsynth::ValidRoomOptions(true,true,256,26)&&!avatarsynth::ValidRoomOptions(true,true,4,256),"partial and out-of-range room options refused");
}
const auto offer=Offer();auto q=Request();std::vector<af::Event> events;auto emit=[&](af::Event e){events.push_back(e);};
{
 af::Gate g;Check(!g.Skip(false,true,&q,offer,{40,0,0},100,1,emit)&&!g.Consumed(),"default off cannot consume or suppress");
 Check(!g.Skip(true,true,nullptr,offer,{40,0,0},100,1,emit)&&!g.Consumed(),"no request delegates");
 auto no=offer;no.eligible=0;Check(!g.Skip(true,true,&q,no,{40,0,0},100,1,emit),"no eligible native scope delegates");
 Check(!g.Skip(true,true,&q,offer,{40,0,0},100,2,emit),"refused request cannot renew");
}
for(unsigned mode=0;mode<7;++mode){
 af::Gate g;auto bad=q;
 switch(mode){case 0:bad.frames=9;break;case 1:bad.activeMs=1001;break;case 2:bad.deadline=10101;break;case 3:bad.activation.x=std::numeric_limits<float>::quiet_NaN();break;case 4:bad.offer.creation++;break;case 5:bad.offer.binding.handle++;break;case 6:bad.nonce=0;break;}
 Check(!g.Skip(true,true,&bad,offer,{40,0,0},100,1,emit)&&g.Consumed(),"malformed request burns one-shot without suppressing");
 Check(!g.Skip(true,true,&q,offer,{40,0,0},100,2,emit),"malformed request cannot be replaced");
}
{
 af::Gate g;Check(!g.Skip(true,true,&q,offer,{},100,1,emit),"arm stays dormant for old target");
 g.Tick(10100,2,emit);Check(!g.Skip(true,true,&q,offer,{40,0,0},10100,2,emit),"lost producer expires dormant request");
}
{
 af::Gate g;Check(g.Skip(true,true,&q,offer,{40,0,0},100,1,emit),"matching target activates");
 Check(!g.Skip(true,false,&q,offer,{40,0,0},101,2,emit),"unreadable control immediately delegates");
 Check(!g.Skip(true,true,&q,offer,{40,0,0},102,3,emit),"read fault cannot rearm");
}
{
 af::Gate g;Check(g.Skip(true,true,&q,offer,{40,0,0},100,1,emit),"changed-control setup");auto changed=q;changed.deadline--;
 Check(!g.Skip(true,true,&changed,offer,{40,0,0},101,2,emit),"immutable request rejects changed deadline");
}
for(unsigned mode=0;mode<4;++mode){
 af::Gate g;g.Skip(true,true,&q,offer,{40,0,0},100,1,emit);auto changed=offer;Vec3 target{40,0,0};
 switch(mode){case 0:target.x=41;break;case 1:target.x=std::numeric_limits<float>::quiet_NaN();break;case 2:changed.creation++;break;case 3:changed.pid++;break;}
 Check(!g.Skip(true,true,&q,changed,target,101,2,emit),"active unexpected target or process identity change delegates");
}
for(unsigned field=0;field<8;++field){
 af::Gate g;g.Skip(true,true,&q,offer,{40,0,0},100,1,emit);auto changed=offer;
 switch(field){case 0:changed.binding.actor++;break;case 1:changed.binding.handle++;break;case 2:changed.binding.load++;break;case 3:changed.binding.transition++;break;case 4:changed.binding.world++;break;case 5:changed.binding.room++;break;case 6:changed.binding.producer++;break;case 7:changed.eligible=0;break;}
 Check(!g.Skip(true,true,&q,changed,{40,0,0},101,2,emit),"actor/native-room/authority/held change delegates");
}
{
 af::Gate g;g.Skip(true,true,&q,offer,{40,0,0},100,1,emit);
 Check(!g.Skip(true,true,&q,offer,{40,0,0},1100,2,emit),"active wall deadline bounds a stalled producer");
}
{
 af::Gate g;g.Skip(true,true,&q,offer,{40,0,0},100,1,emit);
 Check(!g.Skip(true,true,&q,offer,{40,0,0},101,3,emit),"frame gap releases instead of extending duration");
}
{
 af::Gate g;ap::Monitor monitor;ap::Scope scope{};scope.actor=offer.binding.actor;scope.handle=7;scope.load=2;
 monitor.Bind(scope,false);Vec3 memory{};unsigned writes{},skips{};std::vector<ap::Receipt> reports;
 auto report=[&](ap::Receipt v){reports.push_back(v);};auto read=[&]{return memory;};
 for(unsigned frame=1;frame<=12;++frame){
  monitor.BeforeUpdate(frame,read,report);const Vec3 target=frame==1?Vec3{}:Vec3{40,0,0};
  monitor.Apply(frame,frame,target,read,[&](Vec3 value){if(g.Skip(true,true,&q,offer,value,100+frame,frame,emit)){++skips;return;}memory=value;++writes;},report);
 }
 Check(skips==8&&writes==4&&memory.x==40,"actual Monitor write seam suppresses eight stores then ordinary writes recover");
 for(auto phase:{ap::Phase::BetweenUpdates,ap::Phase::Writeback}){
  unsigned bad{},recovered{};for(auto v:reports)if(v.phase==phase){bad+=!v.recovered;recovered+=v.recovered;Check(v.recovered||v.streak==3,"fault report requires three distinct frames");}
  Check(bad==1&&recovered==1,"actual Monitor emits bounded real fault and recovery in both required lanes");
 }
 Check(!g.Skip(true,true,&q,offer,{40,0,0},200,13,emit),"exhausted eight-frame request cannot renew");
 bool budget=false;for(auto e:events)budget|=std::string(e.reason)=="budget"&&e.skipped==8;Check(budget,"release receipt proves exact eight-frame budget");
}
{
 af::Shared shared{};af::Offer out{};Check(!af::Snapshot(shared.offerSequence,shared.offer,out),"unpublished offer rejected");
 InterlockedIncrement(&shared.offerSequence);shared.offer=offer;Check(!af::Snapshot(shared.offerSequence,shared.offer,out),"odd in-progress offer rejected");
 InterlockedIncrement(&shared.offerSequence);Check(af::Snapshot(shared.offerSequence,shared.offer,out)&&out.binding==offer.binding,"stable owned shared offer accepted");
 Check(InterlockedCompareExchange(&shared.requestSequence,1,0)==0,"first child request admitted atomically");
 Check(InterlockedCompareExchange(&shared.requestSequence,1,0)!=0,"second child request refused atomically");
}
std::cout<<checks<<" checks, "<<failures<<" failures\n";return failures?1:0;
}
