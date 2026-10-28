// Actual-detour cross-sequence controls for Ione e6eb9287 / 5ddf906d.
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
    const auto queue=[&](std::uint32_t handle) {
        reviewThreadNode=handle;
        std::uint64_t args[]{R::Tagged(R::kTagInt,303),R::Tagged(R::kTagAddress,kAxelHandle)};
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
            begin(R::kClearLight);SmOwnerCameraGlobal(start);queue(oldNodeHandle);g_axel.inFriendCallback=false;
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
        const auto count=CameraOps().size();
        SmOwnerWhiteOut(out);SmOwnerCameraReset(out);SmOwnerWhiteIn(in);
        std::printf("CASE olderD303=%u replacedRole=%u invalidName=%u native=%u/%u/%u ownerRecords=%zu\n",
            olderD303?1u:0u,replaceRole?1u:0u,invalidName?1u:0u,reviewNativeOut,reviewNativeReset,reviewNativeIn,
            CameraOps().size()-count);
        check(reviewNativeOut==1 && reviewNativeReset==1 && reviewNativeIn==1 && CameraOps().size()==count,
              "cross-sequence: later Host D303 keeps every fade/reset native and emits nothing to friend");
        const auto oldBinding=[&](const auto& f) { return f.thread==oldThread && f.threadHandle==oldThreadHandle; };
        const bool oldStillBound=oldBinding(g_axel.fade) ||
            std::any_of(g_axel.olderFades.begin(),g_axel.olderFades.end(),oldBinding);
        check(olderD303?oldStillBound:!SmAxelFadePending(),
              "cross-sequence control: later Host callback leaves earlier D303 bindings unchanged");
        if(olderD303) {
            const auto nativeOut=reviewNativeOut,nativeReset=reviewNativeReset,nativeIn=reviewNativeIn;
            running(oldThread);SmOwnerWhiteOut(out);SmOwnerCameraReset(out);SmOwnerWhiteIn(in);
            check(reviewNativeOut==nativeOut && reviewNativeReset==nativeReset && reviewNativeIn==nativeIn && !SmAxelFadePending(),
                  "cross-sequence control: old Owner D303 still sends or drops independently and completes itself");
        }
    }
    return errors?1:0;
}
