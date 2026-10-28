// Actual friend and host Overtaker -> Clear Light completions; Ione c2dc00d1.
int main() {
 constexpr uintptr_t name=0x700000,oldNode=0x710000,oldThread=0x720000,vm=0x730000;
 constexpr uintptr_t newNode=0x810000,newThread=0x820000;
 constexpr std::uint32_t nameHandle=0x70,oldNodeHandle=0x71,oldThreadHandle=0x72,newNodeHandle=0x81,newThreadHandle=0x82;
 std::uint64_t start[]{R::Tagged(R::kTagAddress,kAxelHandle),R::Tagged(R::kTagAddress,nameHandle)};
 std::uint64_t out[]{R::Tagged(R::kTagFloat,0x428C0000)},in[]{R::Tagged(R::kTagFloat,0x41F00000)};
 const auto running=[&](uintptr_t thread) {Put(g_exeBase+0x2A25048,thread);Put(g_exeBase+0x2A250B0,thread);Put(thread+0x60,kAxel);};
 const auto queue=[&](std::uint32_t handle) {
  reviewThreadNode=handle;
  std::uint64_t args[]{R::Tagged(R::kTagInt,303),R::Tagged(R::kTagAddress,kAxelHandle)};
  SmOwnerThreadStart(args);
 };
 for(bool olderPending:{false,true})for(bool replaceRole:{false,true}) {
  copyFault=false;check(FriendOvertaker(),"control: original friend RC accepted");
  handles[name]=nameHandle;Put(name,std::uint32_t{0x3031});
  handles[oldNode]=oldNodeHandle;handles[oldThread]=oldThreadHandle;Put(oldNode,oldThreadHandle);
  handles[newNode]=newNodeHandle;handles[newThread]=newThreadHandle;Put(newNode,newThreadHandle);
  reviewNativeStart=reviewNativeOut=reviewNativeReset=reviewNativeIn=0;
  Put(kFriendActor+0x638,std::uint32_t{490});Put(kFriendActor+0x18C,std::uint32_t{1u<<21});Frame();
  duringCallback=[&] {running(vm);SmOwnerCameraGlobal(start);if(olderPending){readFault=oldNode;queue(oldNodeHandle);}};
  check(SmAxelComplete(Claim(R::kClearLight),nullptr) && callbackPlayer==kFriendActor,"control: actual friend Clear Light establishes older Owner callback");
  duringCallback=nullptr;
  if(replaceRole)(void)SmOwnerBeginAxelRole(1,kFriendActor,kAxel,7,enemysync::scope);
  // A fresh activation now belongs to the host, rather than a friend's refused first camera send.
  SmAxelEndExecutor("host's next RC");
  Put(kAxel+0x5A0,std::uint32_t{});Frame();
  g_seats[0]={0x1111,{0,0,100},true,true,true};
  Put(kAxel+0x5A0,std::uint32_t{1u<<R::kOvertakerGroup});Frame();
  check(g_axel.windows.Find(R::kOvertaker)->slot==0,"control: fresh offer belongs to host seat");
  check(SmAxelComplete(Claim(),nullptr),"control: native host Overtaker accepts");
  Put(kHostActor+0x638,std::uint32_t{490});Put(kHostActor+0x18C,std::uint32_t{1u<<21});Frame();
  check(g_axel.windows.Find(R::kClearLight) && g_axel.windows.Find(R::kClearLight)->slot==0,"control: host Overtaker opens its chain-owned Clear Light");
  bool friendFlag=true;
  duringCallback=[&] {friendFlag=g_axel.inFriendCallback;running(vm);SmOwnerCameraGlobal(start);queue(newNodeHandle);};
  check(SmAxelComplete(Claim(R::kClearLight),nullptr) && callbackPlayer==kHostActor && !friendFlag && reviewNativeStart==1,
        "control: actual seat-0 callback starts camera natively outside friend callback");
  duringCallback=nullptr;running(newThread);
  const auto before=CameraOps().size();
  SmOwnerWhiteOut(out);SmOwnerCameraReset(out);SmOwnerWhiteIn(in);
  std::printf("HOST-SEAT pending=%u replaceRole=%u native=%u/%u/%u ownerRecords=%zu\n",
   olderPending?1u:0u,replaceRole?1u:0u,reviewNativeOut,reviewNativeReset,reviewNativeIn,CameraOps().size()-before);
  check(reviewNativeOut==1 && reviewNativeReset==1 && reviewNativeIn==1 && CameraOps().size()==before,
        "host's own RC keeps fades/reset native beside an older pending friend D303");
  if(olderPending) {
   const auto outs=reviewNativeOut,resets=reviewNativeReset,ins=reviewNativeIn;
   readFault=0;running(oldThread);SmOwnerWhiteOut(out);SmOwnerCameraReset(out);SmOwnerWhiteIn(in);
   check(reviewNativeOut==outs && reviewNativeReset==resets && reviewNativeIn==ins && !SmAxelFadePending(),
         "control: older Owner completes independently after header recovers");
  }
 }

 // Reuse the real admission flow for unread observations, recovery and lifetime controls.
 const auto prepareHost=[&](bool olderPending) {
  copyFault=false;const bool overtaker=FriendOvertaker();
  handles[name]=nameHandle;Put(name,std::uint32_t{0x3031});
  handles[oldNode]=oldNodeHandle;handles[oldThread]=oldThreadHandle;Put(oldNode,oldThreadHandle);
  handles[newNode]=newNodeHandle;handles[newThread]=newThreadHandle;Put(newNode,newThreadHandle);
  reviewNativeStart=reviewNativeOut=reviewNativeReset=reviewNativeIn=0;
  Put(kFriendActor+0x638,std::uint32_t{490});Put(kFriendActor+0x18C,std::uint32_t{1u<<21});Frame();
  duringCallback=[&] {running(vm);SmOwnerCameraGlobal(start);if(olderPending){readFault=oldNode;queue(oldNodeHandle);}};
  const bool friendClear=SmAxelComplete(Claim(R::kClearLight),nullptr);duringCallback=nullptr;
  readFault=0;if(olderPending)memory.erase(oldNode); // the earlier header remains unread through later ops
  SmAxelEndExecutor("host's next chain");Put(kAxel+0x5A0,std::uint32_t{});Frame();
  g_seats[0]={0x1111,{0,0,100},true,true,true};
  Put(kAxel+0x5A0,std::uint32_t{1u<<R::kOvertakerGroup});Frame();
  const bool hostOvertaker=SmAxelComplete(Claim(),nullptr);
  Put(kHostActor+0x638,std::uint32_t{490});Put(kHostActor+0x18C,std::uint32_t{1u<<21});Frame();
  const auto* clear=g_axel.windows.Find(R::kClearLight);
  return overtaker && friendClear && hostOvertaker && clear && clear->slot==0;
 };
 for(unsigned fault=0;fault<9;++fault) {
  check(prepareHost(true),"host sequence control: both chains admitted through actual completions");
  bool observed=false,oldIsolated=false;
  duringCallback=[&] {
   observed=!g_axel.inFriendCallback && SmAxelThreadWatched();
   running(vm);SmOwnerCameraGlobal(start);
   switch(fault) {
    case 1:copyFault=true;break;
    case 2:readFault=newNode;break;
    case 3:readFault=vm+0x60;break;
    case 4:decodeFault=newThreadHandle;break;
    case 5:gameThread=false;break;
    case 6:scopeOk=false;break; // native identity needs no borrowed Owner delivery scope
    case 8:Put(vm+0x60,kHostActor);break;
   }
   if(fault==7) {
    reviewThreadNode=newNodeHandle;
    std::uint64_t other[]{R::Tagged(R::kTagInt,304),R::Tagged(R::kTagAddress,kAxelHandle)};
    SmOwnerThreadStart(other);
   } else queue(newNodeHandle);
   copyFault=false;readFault=0;decodeFault=0;gameThread=true;scopeOk=true;
   // Even inside a proven host callback, an older unread Owner never gains a native bypass.
   const auto count=CameraOps().size();running(oldThread);
   SmOwnerWhiteOut(out);SmOwnerCameraReset(out);SmOwnerWhiteIn(in);
   oldIsolated=!reviewNativeOut && !reviewNativeReset && !reviewNativeIn && CameraOps().size()==count && g_axel.fade.pending;
  };
  const bool accepted=SmAxelComplete(Claim(R::kClearLight),nullptr);duringCallback=nullptr;
  check(accepted && callbackPlayer==kHostActor && observed && !g_axel.inFriendCallback && !SmAxelThreadWatched() && reviewNativeStart==1,
        "host sequence: positive seat-0 observation is confined to a validated callback, never friend admission");
  check(oldIsolated,"host sequence: host callback provenance cannot bypass or clear older unread Owner custody");
  const auto count=CameraOps().size();
  const bool proven=fault==0 || fault==6;
  if(proven) {
   for(unsigned unread=0;unread<5;++unread) {
    running(newThread);
    switch(unread) {
     case 0:readFault=g_exeBase+0x2A25048;break;
     case 1:readFault=g_exeBase+0x2A250B0;break;
     case 2:readFault=newThread+0x60;break;
     case 3:decodeFault=newThreadHandle;break;
     case 4:handles[newThread]=0x92;break;
    }
    SmOwnerWhiteOut(out);SmOwnerCameraReset(out);SmOwnerWhiteIn(in);
    check(!reviewNativeOut && !reviewNativeReset && !reviewNativeIn && CameraOps().size()==count && g_axel.fade.pending,
          "host sequence: unread/replaced matching identity never restores native fade or retires pending Owner");
    readFault=0;decodeFault=0;handles[newThread]=newThreadHandle;
   }
  }
  running(newThread);SmAxelEndExecutor("host thread outlives executor");
  SmOwnerWhiteOut(out);SmOwnerCameraReset(out);SmOwnerWhiteIn(in);
  std::printf("HOST-ADMISSION fault=%u native=%u/%u/%u ownerRecords=%zu\n",fault,reviewNativeOut,reviewNativeReset,reviewNativeIn,CameraOps().size()-count);
  check(reviewNativeOut==(proven?1u:0u) && reviewNativeReset==(proven?1u:0u) && reviewNativeIn==(proven?1u:0u) && CameraOps().size()==count,
        "host sequence: only fully proven native admission survives executor retirement and preserves host routing");
  const auto outs=reviewNativeOut,resets=reviewNativeReset,ins=reviewNativeIn;
  Put(oldNode,oldThreadHandle);running(oldThread);SmOwnerWhiteOut(out);SmOwnerCameraReset(out);SmOwnerWhiteIn(in);
  check(reviewNativeOut==outs && reviewNativeReset==resets && reviewNativeIn==ins && !SmAxelFadePending(),
        "host sequence: recovered old Owner completes independently after positive or unread native admission");
 }
 for(unsigned refused=0;refused<3;++refused) {
  check(prepareHost(true),"host sequence control: validated windows precede refusal controls");
  auto claim=Claim(R::kClearLight);bool entered=false;
  duringCallback=[&] {entered=true;running(vm);queue(newNodeHandle);};
  if(refused==0)claim.requesterConnectionId=0x3333;
  if(refused==1)scopeOk=false;
  if(refused==2)readFault=kHostActor+4;
  const bool accepted=SmAxelComplete(claim,nullptr);duringCallback=nullptr;scopeOk=true;readFault=0;
  check(!accepted && !entered && !SmAxelThreadWatched() && !g_axel.inFriendCallback,
        "host sequence: refused connection/scope/seat-actor admission never establishes host callback provenance");
  running(vm);queue(newNodeHandle);running(newThread);const auto count=CameraOps().size();
  SmOwnerWhiteOut(out);SmOwnerCameraReset(out);SmOwnerWhiteIn(in);
  check(!reviewNativeOut && !reviewNativeReset && !reviewNativeIn && CameraOps().size()==count && g_axel.fade.pending,
        "host sequence: thread start outside a validated callback cannot manufacture native proof");
 }
 check(prepareHost(false),"host sequence control: genuine host native-only lease setup");
 duringCallback=[&] {running(vm);SmOwnerCameraGlobal(start);queue(newNodeHandle);};
 check(SmAxelComplete(Claim(R::kClearLight),nullptr),"host sequence control: host Clear Light accepted for lease test");duringCallback=nullptr;
 const auto until=g_frameCounter+R::kAxelFadeLeaseFrames;
 SmAxelEndExecutor("lease control");
 check(SmAxelFadePending() && !SmAxelThreadWatched(),"host sequence: native proof survives callback and executor retirement");
 g_frameCounter+=100;running(vm);queue(newNodeHandle); // no current callback can authorize or renew this observation
 g_frameCounter=until;SmAxelExpireCamera();
 check(SmAxelFadePending(),"host sequence: native identity remains at inclusive original lease boundary");
 ++g_frameCounter;SmAxelExpireCamera();
 check(!SmAxelFadePending(),"host sequence: outside-callback observation cannot renew the 600-frame proof");
 return errors?1:0;
}
