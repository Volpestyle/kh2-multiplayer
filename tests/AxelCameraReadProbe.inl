// Main for run_axel_camera_read_probes.py: the adapter prefix plus the actual Windows detours.
// Native/OS boundaries are controlled stand-ins; this does not execute Windows SEH.
int main() {
    constexpr uintptr_t name=0x700000,node=0x710000,thread=0x720000,vm=0x730000,other=0x740000;
    constexpr std::uint32_t nh=0x70,nodeh=0x71,th=0x72;
    std::uint64_t start[]{R::Tagged(R::kTagAddress,kAxelHandle),R::Tagged(R::kTagAddress,nh)};
    std::uint64_t watch[]{R::Tagged(R::kTagAddress,kAxelHandle)};
    std::uint64_t out[]{R::Tagged(R::kTagFloat,0x428C0000)};
    std::uint64_t in[]{R::Tagged(R::kTagFloat,0x41F00000)};
    std::uint64_t before[]{R::Tagged(R::kTagInt,303),R::Tagged(R::kTagAddress,kAxelHandle)};
    const auto running=[&](uintptr_t t,uintptr_t source) {
        Put(g_exeBase+0x2A25048,t);Put(g_exeBase+0x2A250B0,t);Put(t+0x60,source);
    };
    const auto prepare=[&](const std::function<void()>& admission={}) {
        copyFault=false;readFault=0;gameThread=true;
        Setup(1u<<R::kOvertakerGroup);Frame();
        handles[name]=nh;handles[node]=nodeh;handles[thread]=th;
        Put(name,std::uint32_t{0x3031});Put(node,th);running(vm,kAxel);
        duringCallback=[&] {
            SmOwnerCameraGlobal(start);
            if(admission)admission();
            else { const std::uint64_t after[]{R::Tagged(R::kTagAddress,nodeh)};SmAxelThreadStarted(before,after); }
        };
        (void)SmAxelComplete(Claim(),nullptr);duringCallback=nullptr;
        readFault=0;decodeFault=0;scopeOk=true;roleCurrent=true;copyFault=false;gameThread=true;running(thread,kAxel);
        reviewNativeOut=reviewNativeIn=reviewNativeReset=reviewNativeStart=reviewNativeWatch=0;
    };
    for(const auto at:{g_exeBase+0x2A250B0,thread+0x60}) {
        prepare();readFault=at;SmOwnerWhiteOut(out);SmOwnerWhiteIn(in);
        check(!reviewNativeOut && !reviewNativeIn && SmAxelFadePending(),"site 1: unread VM/source withholds without retiring");
        readFault=0;const auto count=CameraOps().size();SmOwnerWhiteIn(in);
        check(!reviewNativeIn && CameraOps().size()==count+1 && !SmAxelFadePending(),"site 1: recovered D303 completes on original owner");
    }
    for(int mode=0;mode<5;++mode) {
        prepare([&] {
            if(mode==0)readFault=kAxel+4;
            if(mode==1)scopeOk=false;
            if(mode==2)readFault=g_exeBase+0x2A250B0;
            if(mode==3)readFault=vm+0x60;
            if(mode==4)roleCurrent=false;
            const std::uint64_t after[]{R::Tagged(R::kTagAddress,nodeh)};
            SmAxelThreadStarted(before,after);
        });
        const auto count=CameraOps().size();SmOwnerWhiteOut(out);
        check(!reviewNativeOut && CameraOps().size()==count+1,
            mode==0?"site 2: unread Axel handle at adoption retains original authority":
            mode==1?"site 3: unread adoption scope retains original authority":
            mode==4?"site 9: null FriendRole at adoption recovers the captured role":
            "site 1: unread admission source recovers the captured role");
    }
    // The actor argument path has four independent reads, not a single boolean SmActorOf result.
    for(const auto at:{kAxel+4,kAxel+offsets::actor::OBJENTRY_PTR,uintptr_t{0x400000}}) {
        prepare();readFault=at;SmOwnerCameraGlobal(start);SmOwnerCameraWatch(watch);
        check(!reviewNativeStart && !reviewNativeWatch,"sites 2/6: unread handle/object chain cannot restore native start/watch");
        readFault=0;SmOwnerCameraGlobal(start);SmOwnerCameraWatch(watch);
        check(!reviewNativeStart && !reviewNativeWatch,"sites 2/6: recovered start/watch stay off host");
    }
    prepare();decodeFault=kAxelHandle;SmOwnerCameraGlobal(start);SmOwnerCameraWatch(watch);
    check(!reviewNativeStart && !reviewNativeWatch,"site 6: unread argument resolver stays off host");
    decodeFault=0;
    prepare();scopeOk=false;SmOwnerWhiteIn(in);
    check(!reviewNativeIn && SmAxelFadePending(),"site 3: unread fade scope cannot prove completion");
    scopeOk=true;SmOwnerWhiteIn(in);
    check(!reviewNativeIn && !SmAxelFadePending(),"site 3: recovered fade scope completes on original owner");
    // Interleaving: an unread pending D303 cannot be retired by another Axel thread's white-in.
    prepare([&] { decodeFault=th;const std::uint64_t after[]{R::Tagged(R::kTagAddress,nodeh)};SmAxelThreadStarted(before,after); });
    decodeFault=th;running(other,kAxel);SmOwnerWhiteIn(in);running(thread,kAxel);SmOwnerWhiteOut(out);
    check(!reviewNativeIn && !reviewNativeOut && g_axel.fade.pending,"site 4: unproven white-in preserves pending custody");
    decodeFault=0;SmOwnerWhiteIn(in);
    check(!reviewNativeIn && !SmAxelFadePending(),"site 4: only identified D303 white-in completes custody");
    // The same finite lease applies even while the scope remains unread, and frame cleanup is bounded.
    prepare();const auto until=g_axel.fade.until;scopeOk=false;g_frameCounter=until;Frame();SmOwnerWhiteOut(out);
    check(!reviewNativeOut && SmAxelFadePending(),"site 5: unread scope cannot shorten the lease");
    g_frameCounter=until+1;Frame();
    check(!SmAxelFadePending(),"site 5: the 600-frame lease retires custody without requiring another fade call");
    for(bool wrongThread:{false,true}) {
        prepare();copyFault=!wrongThread;gameThread=!wrongThread;
        SmOwnerCameraGlobal(start);SmOwnerCameraWatch(watch);SmOwnerWhiteOut(out);SmOwnerWhiteIn(in);SmOwnerCameraReset(out);
        check(!reviewNativeStart && !reviewNativeWatch && !reviewNativeOut && !reviewNativeIn && !reviewNativeReset && SmAxelFadePending(),
            wrongThread?"site 7: unavailable game thread cannot bypass custody":"site 7: unread arguments cannot bypass custody or prove completion");
        copyFault=false;gameThread=true;SmOwnerWhiteIn(in);
        check(!reviewNativeIn && !SmAxelFadePending(),"site 7: detour recovers and completes the original sequence");
    }
    // 2/20's before/after arguments can be unread after a start already reached the owner.
    for(bool lostBefore:{false,true}) {
        prepare([&] {
            std::uint64_t args[]{before[0],before[1]};
            copyFault=!lostBefore;gameThread=!lostBefore;SmOwnerThreadStart(args);
        });
        SmOwnerWhiteOut(out);SmOwnerWhiteIn(in);
        check(!reviewNativeOut && !reviewNativeIn && SmAxelFadePending(),"site 7: unread 2/20 admission is held without claiming completion");
    }
    prepare();SmAxelEndExecutor("probe");SmOwnerCameraGlobal(start);SmOwnerCameraWatch(watch);
    check(!reviewNativeStart && !reviewNativeWatch,"site 8: executor end cannot return late owner start/watch to host");
    SmAxelHostReset();SmOwnerCameraGlobal(start);SmOwnerCameraWatch(watch);
    check(!reviewNativeStart && !reviewNativeWatch,"site 8: host reset cannot erase retained camera custody");
    for(bool resolver:{false,true}) {
        // First camera has not been offered yet: unread is different from a proven bad name/send refusal.
        (void)FriendOvertaker();handles[name]=nh;Put(name,std::uint32_t{0x3031});
        if(resolver)decodeFault=nh;else readFault=name;
        reviewNativeStart=reviewNativeWatch=0;SmOwnerCameraGlobal(start);
        readFault=0;decodeFault=0;SmOwnerCameraWatch(watch);
        check(!reviewNativeStart && !reviewNativeWatch && g_axel.cameraCustody==R::AxelCameraCustody::Owner,
            "site 10: unread CameraName reserves custody, recovery cannot split the sequence");
    }
    // Controls: positive mismatches stay native without clearing another sequence's custody.
    prepare();running(other,kHostActor);SmOwnerWhiteOut(out);SmOwnerWhiteIn(in);SmOwnerCameraReset(out);
    start[0]=watch[0]=R::Tagged(R::kTagAddress,kHostHandle);SmOwnerCameraGlobal(start);SmOwnerCameraWatch(watch);
    check(reviewNativeOut==1 && reviewNativeIn==1 && reviewNativeReset==1 && reviewNativeStart==1 && reviewNativeWatch==1 && SmAxelFadePending(),
        "controls: positively unrelated source/thread/object remain native and preserve custody");
    start[0]=watch[0]=R::Tagged(R::kTagAddress,kAxelHandle);
    prepare();running(other,kAxel);readFault=g_exeBase+0x2A250B0;SmOwnerWhiteIn(in);
    check(reviewNativeIn==1 && SmAxelFadePending(),"controls: proven other thread stays native even with unread source");
    prepare();handles[thread]=0x172;SmOwnerWhiteIn(in);
    check(reviewNativeIn==1 && SmAxelFadePending(),"controls: proven replacement is native but does not retire retained custody");
    // A later callback must not overwrite an unfinished earlier D303's authority.
    prepare();const auto original=g_axel.fade.resource;
    const auto oldUntil=g_axel.fade.until;
    const auto extraThread=thread+0x10000;const std::uint32_t extraHandle=th+1;
    (void)SmOwnerBeginAxelRole(1,kFriendActor,kAxel,7,enemysync::scope);
    const auto newer=g_smRoles[1].resource;
    handles[extraThread]=extraHandle;Put(node,extraHandle);running(vm,kAxel);
    // Admission of a new RC is known; its first camera captures that new role.
    SmAxelEndExecutor("next callback");
    g_axel.executor={R::kOvertaker,7,1,0x2222,enemysync::scope.epoch,g_frameCounter,false};
    g_axel.executorActor=kFriendActor;g_axel.executorHandle=kFriendHandle;
    g_axel.inFriendCallback=true;SmOwnerCameraGlobal(start);
    const std::uint64_t after[]{R::Tagged(R::kTagAddress,nodeh)};SmAxelThreadStarted(before,after);
    g_axel.inFriendCallback=false;running(thread,kAxel);reviewNativeOut=reviewNativeIn=0;
    const auto count=CameraOps().size();SmOwnerWhiteOut(out);SmOwnerWhiteIn(in);
    check(!reviewNativeOut && !reviewNativeIn && CameraOps().size()==count && g_axel.fade.thread==extraThread &&
          original!=newer && g_axel.fade.until==oldUntil,
          "site 8: a later callback does not retire an older D303 or lend it the replacement role");
    running(extraThread,kAxel);SmOwnerWhiteIn(in);
    check(!reviewNativeIn && CameraOps().size()==count+1 && !SmAxelFadePending(),
          "site 8: each overlapping D303 completes only its own custody");
    // A continuing wire role can serve two callbacks in one frame. It is not a custody identity.
    prepare();handles[extraThread]=extraHandle;Put(node,extraHandle);running(vm,kAxel);
    SmAxelEndExecutor("continuing callback");
    g_axel.executor={R::kOvertaker,7,1,0x2222,enemysync::scope.epoch,g_frameCounter,false};
    g_axel.executorActor=kFriendActor;g_axel.executorHandle=kFriendHandle;
    g_axel.inFriendCallback=true;SmOwnerCameraGlobal(start);SmAxelThreadStarted(before,after);g_axel.inFriendCallback=false;
    running(thread,kAxel);SmOwnerWhiteIn(in);SmAxelEndExecutor("release continuation");
    reviewNativeStart=reviewNativeWatch=0;SmOwnerCameraGlobal(start);SmOwnerCameraWatch(watch);
    check(!reviewNativeStart && !reviewNativeWatch && SmAxelFadePending(),
          "site 8: old completion cannot retire a newer handoff sharing its wire role and lease frame");
    // Known invalid name and refused sends retain the existing all-native offer behavior.
    (void)FriendOvertaker();handles[name]=nh;Put(name,std::uint32_t{1});reviewNativeStart=0;SmOwnerCameraGlobal(start);
    check(reviewNativeStart==1 && g_axel.cameraCustody==R::AxelCameraCustody::Host,"controls: known invalid CameraName keeps sequence native");
    (void)FriendOvertaker();handles[name]=nh;Put(name,std::uint32_t{0x3031});emitOk=false;reviewNativeStart=0;SmOwnerCameraGlobal(start);
    check(reviewNativeStart==1 && g_axel.cameraCustody==R::AxelCameraCustody::Host,"controls: known send refusal keeps sequence native");
    return errors?1:0;
}
