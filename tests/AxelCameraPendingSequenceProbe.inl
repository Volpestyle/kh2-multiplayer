// Actual-detour pending-header sequence for Ione 0e6aed3f / bf8f6adf.
// Callbacks/roles are admitted stand-ins; every camera, 2/20, fade and reset goes through its real detour.
int main() {
    constexpr uintptr_t name=0x700000,oldNode=0x710000,oldThread=0x720000,vm=0x730000;
    constexpr uintptr_t newNode=0x810000,newThread=0x820000;
    constexpr std::uint32_t nameHandle=0x70,oldNodeHandle=0x71,oldThreadHandle=0x72;
    constexpr std::uint32_t newNodeHandle=0x81,newThreadHandle=0x82;
    std::uint64_t start[]{R::Tagged(R::kTagAddress,kAxelHandle),R::Tagged(R::kTagAddress,nameHandle)};
    std::uint64_t out[]{R::Tagged(R::kTagFloat,0x428C0000)};
    std::uint64_t in[]{R::Tagged(R::kTagFloat,0x41F00000)};
    const auto running=[&](uintptr_t thread) {
        Put(g_exeBase+0x2A25048,thread);Put(g_exeBase+0x2A250B0,thread);Put(thread+0x60,kAxel);
    };
    const auto begin=[&](std::uint16_t command) {
        SmAxelEndExecutor("next admitted callback");
        g_axel.executor={command,7,1,0x2222,enemysync::scope.epoch,g_frameCounter,false};
        g_axel.executorActor=kFriendActor;g_axel.executorHandle=kFriendHandle;
        g_axel.inFriendCallback=true;running(vm);
    };
    std::uint32_t label=303;
    const auto queue=[&](std::uint32_t handle) {
        reviewThreadNode=handle;
        std::uint64_t args[]{R::Tagged(R::kTagInt,label),R::Tagged(R::kTagAddress,kAxelHandle)};
        SmOwnerThreadStart(args);
    };
    for(bool olderD303:{false,true})for(bool replaceRole:{false,true})for(bool invalidName:{false,true}) {
        copyFault=false;reviewThreadNode=oldNodeHandle;
        (void)FriendOvertaker();
        handles[name]=nameHandle;Put(name,std::uint32_t{0x3031});
        handles[oldNode]=oldNodeHandle;handles[oldThread]=oldThreadHandle;Put(oldNode,oldThreadHandle);
        handles[newNode]=newNodeHandle;handles[newThread]=newThreadHandle;Put(newNode,newThreadHandle);
        reviewNativeStart=reviewNativeOut=reviewNativeReset=reviewNativeIn=0;
        // A still-running Clear Light may precede the Overtaker whose snapshot is retained.
        if(olderD303) {
            begin(R::kClearLight);SmOwnerCameraGlobal(start);readFault=oldNode;queue(oldNodeHandle);g_axel.inFriendCallback=false;
        }
        begin(R::kOvertaker);SmOwnerCameraGlobal(start);g_axel.inFriendCallback=false;
        const auto earlier=g_axel.cameraOwner;
        check(earlier.axel==kAxel && earlier.resource && g_axel.cameraCustody==R::AxelCameraCustody::Owner && !reviewNativeStart,
              "cross-sequence control: earlier Overtaker received its own Owner handoff");
        begin(R::kClearLight);
        if(replaceRole)(void)SmOwnerBeginAxelRole(1,kFriendActor,kAxel,7,enemysync::scope);
        if(invalidName)Put(name,std::uint32_t{1});else emitOk=false;
        SmOwnerCameraGlobal(start);emitOk=true;
        check(reviewNativeStart==1 && g_axel.cameraCustody==R::AxelCameraCustody::Host,
              "cross-sequence control: later Clear Light's first start is positively Host");
        queue(newNodeHandle);g_axel.inFriendCallback=false;running(newThread);
        if(olderD303) {
            // The original header stays unread while other independent observations are faulted below.
            memory.erase(oldNode);readFault=0;
            const auto records=CameraOps().size();
            running(oldThread);SmOwnerWhiteOut(out);SmOwnerCameraReset(out);SmOwnerWhiteIn(in);
            check(!reviewNativeOut && !reviewNativeReset && !reviewNativeIn && CameraOps().size()==records && g_axel.fade.pending,
                  "pending sequence: current Host decision never bypasses the older unread Owner thread");
            for(unsigned fault=0;fault<7;++fault) {
                running(newThread);
                switch(fault) {
                    case 0:readFault=g_exeBase+0x2A25048;break;
                    case 1:readFault=g_exeBase+0x2A250B0;break;
                    case 2:readFault=newThread+0x60;break;
                    case 3:decodeFault=newThreadHandle;break;
                    case 4:handles[newThread]=0x92;break; // pointer reused under another incarnation
                    case 5:gameThread=false;break;
                    case 6:running(vm);break; // positive Host proof belongs to another thread
                }
                SmOwnerWhiteOut(out);SmOwnerCameraReset(out);SmOwnerWhiteIn(in);
                std::printf("IDENTITY fault=%u native=%u/%u/%u\n",fault,reviewNativeOut,reviewNativeReset,reviewNativeIn);
                check(!reviewNativeOut && !reviewNativeReset && !reviewNativeIn && CameraOps().size()==records && g_axel.fade.pending,
                      "pending sequence: unread or different native incarnation cannot bypass pending Owner custody");
                readFault=0;decodeFault=0;handles[newThread]=newThreadHandle;gameThread=true;
            }
            running(newThread);
            // The native identity survives executor retirement, and does not borrow role/scope delivery authority.
            SmAxelEndExecutor("native D303 outlives executor");scopeOk=false;
            SmOwnerWhiteOut(out);SmOwnerCameraReset(out);
            check(reviewNativeOut==1 && reviewNativeReset==1 && !reviewNativeIn && CameraOps().size()==records,
                  "pending sequence: proven native identity survives executor end and unread owner scope");
            scopeOk=true;reviewNativeOut=reviewNativeReset=0;
        }
        running(newThread);
        const auto count=CameraOps().size();
        SmOwnerWhiteOut(out);SmOwnerCameraReset(out);SmOwnerWhiteIn(in);
        std::printf("CASE olderD303=%u replacedRole=%u invalidName=%u native=%u/%u/%u ownerRecords=%zu\n",
            olderD303?1u:0u,replaceRole?1u:0u,invalidName?1u:0u,reviewNativeOut,reviewNativeReset,reviewNativeIn,
            CameraOps().size()-count);
        check(reviewNativeOut==1 && reviewNativeReset==1 && reviewNativeIn==1 && CameraOps().size()==count,
              "cross-sequence: later Host D303 keeps every fade/reset native and emits nothing to friend");
        if(olderD303) {
            const auto nativeOut=reviewNativeOut,nativeReset=reviewNativeReset,nativeIn=reviewNativeIn;
            SmOwnerWhiteOut(out);SmOwnerCameraReset(out);SmOwnerWhiteIn(in);
            check(reviewNativeOut==nativeOut && reviewNativeReset==nativeReset && reviewNativeIn==nativeIn && CameraOps().size()==count,
                  "pending sequence: positively completed native proof retires without clearing pending Owner");
        }
        const auto oldBinding=[&](const auto& f) { return (f.thread==oldThread && f.threadHandle==oldThreadHandle) || (f.pending && f.nodeHandle==oldNodeHandle); };
        const bool oldStillBound=oldBinding(g_axel.fade) ||
            std::any_of(g_axel.olderFades.begin(),g_axel.olderFades.end(),oldBinding);
        check(olderD303?oldStillBound:!SmAxelFadePending(),
              "cross-sequence control: later Host callback leaves earlier D303 bindings unchanged");
        if(olderD303) {
            const auto nativeOut=reviewNativeOut,nativeReset=reviewNativeReset,nativeIn=reviewNativeIn;
            readFault=0;Put(oldNode,oldThreadHandle);running(oldThread);SmOwnerWhiteOut(out);SmOwnerCameraReset(out);SmOwnerWhiteIn(in);
            check(reviewNativeOut==nativeOut && reviewNativeReset==nativeReset && reviewNativeIn==nativeIn && !SmAxelFadePending(),
                  "cross-sequence control: old Owner D303 still sends or drops independently and completes itself");
        }
    }
    // Admission must prove every part of native identity, including the current callback decision.
    for(unsigned fault=0;fault<13;++fault) {
        (void)FriendOvertaker();label=303;copyFault=false;
        handles[name]=nameHandle;Put(name,std::uint32_t{0x3031});
        handles[oldNode]=oldNodeHandle;handles[oldThread]=oldThreadHandle;Put(oldNode,oldThreadHandle);
        handles[newNode]=newNodeHandle;handles[newThread]=newThreadHandle;Put(newNode,newThreadHandle);
        reviewNativeStart=reviewNativeOut=reviewNativeReset=reviewNativeIn=0;
        begin(R::kClearLight);SmOwnerCameraGlobal(start);readFault=oldNode;queue(oldNodeHandle);
        memory.erase(oldNode);readFault=0;
        begin(R::kClearLight);emitOk=false;SmOwnerCameraGlobal(start);emitOk=true;
        switch(fault) {
            case 0:copyFault=true;break;
            case 1:readFault=newNode;break;
            case 2:decodeFault=newNodeHandle;break;
            case 3:decodeFault=newThreadHandle;break;
            case 4:readFault=g_exeBase+0x2A250B0;break;
            case 5:readFault=vm+0x60;break;
            case 6:readFault=kAxel+4;break;
            case 7:label=304;break;
            case 8:g_axel.cameraCustody=R::AxelCameraCustody::Undecided;break;
            case 9:Put(vm+0x60,kHostActor);break;
            case 10:gameThread=false;break;
            case 11:Put(newNode,std::uint32_t{});break;
            case 12:handles[newThread]=0x92;break;
        }
        queue(newNodeHandle);g_axel.inFriendCallback=false;
        copyFault=false;readFault=0;decodeFault=0;gameThread=true;label=303;running(newThread);
        const auto count=CameraOps().size();
        SmOwnerWhiteOut(out);SmOwnerCameraReset(out);SmOwnerWhiteIn(in);
        std::printf("ADMISSION fault=%u native=%u/%u/%u\n",fault,reviewNativeOut,reviewNativeReset,reviewNativeIn);
        check(!reviewNativeOut && !reviewNativeReset && !reviewNativeIn && CameraOps().size()==count && g_axel.fade.pending,
              "pending sequence: unproven Host admission cannot authorize native execution through older Owner custody");
        Put(oldNode,oldThreadHandle);running(oldThread);SmOwnerWhiteIn(in);
        check(!reviewNativeIn && !SmAxelFadePending(),
              "pending sequence: failed native proof preserves earlier Owner completion");
    }
    // Contradictory observation of the same node/handle cannot overwrite an Owner binding.
    (void)FriendOvertaker();label=303;
    handles[name]=nameHandle;Put(name,std::uint32_t{0x3031});
    handles[oldNode]=oldNodeHandle;handles[oldThread]=oldThreadHandle;Put(oldNode,oldThreadHandle);
    reviewNativeStart=reviewNativeOut=reviewNativeReset=reviewNativeIn=0;
    begin(R::kClearLight);SmOwnerCameraGlobal(start);queue(oldNodeHandle);
    begin(R::kClearLight);emitOk=false;SmOwnerCameraGlobal(start);emitOk=true;queue(oldNodeHandle);
    g_axel.inFriendCallback=false;running(oldThread);
    const auto records=CameraOps().size();SmOwnerWhiteOut(out);SmOwnerCameraReset(out);SmOwnerWhiteIn(in);
    check(!reviewNativeOut && !reviewNativeReset && !reviewNativeIn && CameraOps().size()==records+3 && !SmAxelFadePending(),
          "pending sequence: contradictory same-incarnation Host observation never overrides known Owner");
    // A later positively Owner binding also outranks an older, contradictory native observation.
    (void)FriendOvertaker();label=303;
    handles[name]=nameHandle;Put(name,std::uint32_t{0x3031});
    handles[newNode]=newNodeHandle;handles[newThread]=newThreadHandle;Put(newNode,newThreadHandle);
    reviewNativeStart=reviewNativeOut=reviewNativeReset=reviewNativeIn=0;
    begin(R::kClearLight);emitOk=false;SmOwnerCameraGlobal(start);emitOk=true;queue(newNodeHandle);
    begin(R::kClearLight);SmOwnerCameraGlobal(start);queue(newNodeHandle);
    g_axel.inFriendCallback=false;running(newThread);
    const auto laterRecords=CameraOps().size();SmOwnerWhiteOut(out);SmOwnerCameraReset(out);SmOwnerWhiteIn(in);
    check(!reviewNativeOut && !reviewNativeReset && !reviewNativeIn && CameraOps().size()==laterRecords+3,
          "pending sequence: later known Owner is not overridden by an earlier contradictory native proof");
    // A native proof has the same finite lifetime, even when there is no Owner candidate to force hook entry.
    (void)FriendOvertaker();label=303;
    handles[name]=nameHandle;Put(name,std::uint32_t{0x3031});
    handles[newNode]=newNodeHandle;handles[newThread]=newThreadHandle;Put(newNode,newThreadHandle);
    begin(R::kClearLight);emitOk=false;SmOwnerCameraGlobal(start);emitOk=true;queue(newNodeHandle);
    check(SmAxelFadePending(),"native proof: retained even without an Owner candidate, so completion can retire it");
    g_frameCounter=20;queue(newNodeHandle);g_axel.inFriendCallback=false;running(newThread);
    copyFault=true;SmOwnerWhiteIn(in);copyFault=false;
    check(SmAxelFadePending(),"native proof: unread white-in arguments do not clear identity");
    g_frameCounter=610;SmAxelExpireCamera();
    check(SmAxelFadePending(),"native proof: inclusive last frame of the original 600-frame lease");
    g_frameCounter=611;SmAxelExpireCamera();
    check(!SmAxelFadePending(),"native proof: duplicate observation cannot renew the original lease");
    return errors?1:0;
}
