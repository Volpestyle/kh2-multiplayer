// VUH-2342: the host's mission gauges, counters, timer and end, shown on clients.
// Included in EnemySync.cpp after FieldObjects.inl. Rules: MissionMirror.hpp; docs/MISSION_MIRROR.md.
// Host: read the running mission each frame and send a MissionState every 3 frames (15 without one).
// Client: while the host runs the same mission file, take its gauge and counter values through the
//   game's own setters (so the HUD and the 0/max signals behave natively), move this timer's start to
//   the host's value, and end the mission when and how the host's ended (failed, or the same end trap
//   with the same argument). The client's own script calls that change a gauge or counter, or end
//   the mission, are dropped meanwhile. Both roles hook the same table-4 trap entries; on the host
//   the hooks only record which end trap ran.
// On with the enemy mirror; KH2COOP_MISSION_MIRROR=0 parks it on that machine.
namespace MR = missionmirror::rules;
using MissionTrapFn = void(__fastcall*)(std::uint64_t* args);
using MissionSetGaugeFn = void(__fastcall*)(float value, int slot);
using MissionSetCountFn = void(__fastcall*)(int value, int slot);
using MissionFailFn = void(__fastcall*)();
using MissionEndFn = void(__fastcall*)(std::uint64_t* args);

struct MissionSlotHook { uintptr_t entry{}, native{}; bool swapped{}; };
MissionSlotHook g_missionHooks[MR::kTrapCount]{};
MissionState g_missionLatest{};
MR::Mission g_missionHost{};
std::uint64_t g_missionSequence{},g_missionReceivedMs{},g_missionHostConnection{},g_missionHostDelivery{};
std::uint32_t g_missionGeneration{};
std::atomic<bool> g_missionFollowing{false},g_missionHostFailed{false};
std::atomic<std::uint64_t> g_missionDropped{0},g_missionFollowMs{0};  // the last frame that confirmed following
bool g_missionChecked{},g_missionReady{},g_missionReplayed{},g_missionWasFollowing{};
std::atomic<std::uint8_t> g_missionRole{0};  // 1 host, 2 client: what the hooks do
// The host's end: which end trap ran on the current mission (base pointer + file), and its argument.
uintptr_t g_missionHostBase{},g_missionLocalBase{};
std::uint32_t g_missionHostLoad{},g_missionLocalLoad{};  // warp::LoadSerial(): a retry is a new mission
// Client: the mission's own end this game dropped while following, run natively if the host's end never comes.
int g_missionDroppedEnd=-1;
std::uint32_t g_missionDroppedArg{};
bool g_missionFallbackPending{},g_missionHostFailedSeen{};  // this mission: a release awaits the host's outcome; the host was seen failed
// This game's mission, read again right before a replay: the setters above may have let its own script end it.
bool LocalMissionEnded(bool failedOnly) {
    uintptr_t base=0;std::uint32_t flags=0,result=0;
    if(!ReadNative(g_exeBase+MR::kMissionPtr,base) || !base || !ReadNative(base+MR::kFlags,flags) ||
       !ReadNative(base+MR::kResult,result))return true;  // unreadable: never call into it
    return failedOnly ? result==MR::kFailedResult : (flags&MR::kEnded)==MR::kEnded;
}
char g_missionHostFile[MR::kFileBytes]{},g_missionLocalFile[MR::kFileBytes]{};
std::uint8_t g_missionHostEndTrap{};
std::uint32_t g_missionHostEndArg{};
int g_missionParked=-1;
unsigned g_missionLogs{};
struct MissionSecond { std::uint32_t gauges{},counters{},timer{},ends{}; std::uint64_t dropped{}; } g_missionSecond{};
std::uint64_t g_missionSecondMs{};

bool MissionParked() {
    if(g_missionParked<0) {
        char v[8]{};
        const DWORD n=GetEnvironmentVariableA("KH2COOP_MISSION_MIRROR",v,sizeof(v));
        g_missionParked=n>0 && n<sizeof(v) && v[0]=='0' && v[1]==0;
        if(g_log)g_log("[mission-mirror] configure requested=%u",g_missionParked?0u:1u);
    }
    return g_missionParked!=0;
}

// The running mission in this game; active=false without one or when it can't be read.
MR::Mission ReadMission(uintptr_t* baseOut=nullptr) {
    MR::Mission m;
    uintptr_t base=0;
    if(baseOut)*baseOut=0;
    if(!ReadNative(g_exeBase+MR::kMissionPtr,base) || !base)return m;
    char path[MR::kPathBytes]{};
    if(!CopyNative(base+MR::kPath,path,sizeof(path)) || !MR::FileOf(path,sizeof(path),m.file) ||
       !ReadNative(base+MR::kFlags,m.flags) || !ReadNative(base+MR::kResult,m.result) ||
       !ReadNative(base+MR::kGaugeLock,m.gaugeLock))return {};
    for(std::size_t i=0;i<MR::kSlots;++i) {
        const uintptr_t g=base+MR::kGauges+i*MR::kGaugeStride,c=base+MR::kCounters+i*MR::kCounterStride;
        auto& gs=m.gauges[i];auto& cs=m.counters[i];
        if(!ReadNative(g+MR::kSlotFlags,gs.flags) || !ReadNative(g+MR::kSlotMax,gs.max) || !ReadNative(g+MR::kSlotValue,gs.gauge) ||
           !ReadNative(c+MR::kSlotFlags,cs.flags) || !ReadNative(c+MR::kSlotMax,cs.max) || !ReadNative(c+MR::kSlotValue,cs.count))return {};
    }
    if(!ReadNative(g_exeBase+MR::kTimerState,m.timerState) || !ReadNative(g_exeBase+MR::kTimerMode,m.timerMode) ||
       !ReadNative(g_exeBase+MR::kTimerValue,m.timerValue) || !ReadNative(g_exeBase+MR::kTimerDuration,m.timerDuration))return {};
    m.active=true;
    if(baseOut)*baseOut=base;
    return m;
}
bool MissionHooksReady();

// ---- host ----------------------------------------------------------------------------------------
void PublishMission(std::uint32_t frame) {
    if(!g_mirrorRequested || MissionParked())return;
    g_missionRole.store(1,std::memory_order_release);
    (void)MissionHooksReady();  // without the hooks the end isn't known; gauges still go out
    uintptr_t base=0;
    const auto m=ReadMission(&base);
    const auto load=warp::LoadSerial();
    if(!m.active || base!=g_missionHostBase || load!=g_missionHostLoad || std::strncmp(m.file,g_missionHostFile,MR::kFileBytes)) {
        g_missionHostBase=m.active?base:0;g_missionHostLoad=load;std::memcpy(g_missionHostFile,m.file,MR::kFileBytes);
        g_missionHostEndTrap=0;g_missionHostEndArg=0;
    }
    if(frame%(m.active?3u:15u) || g_missionSequence==UINT64_MAX)return;
    RoomTransition room;if(!ActivationContext(Role::Host,room))return;
    MissionState packet;packet.location=room;packet.sequence=++g_missionSequence;packet.hostFrame=frame;
    if(m.active) {
        packet.active=true;std::memcpy(packet.file.data(),m.file,MR::kFileBytes);
        packet.flags=m.flags;packet.result=m.result;
        for(std::size_t i=0;i<MR::kSlots;++i) {
            packet.gauges[i]={m.gauges[i].flags,m.gauges[i].max,std::isfinite(m.gauges[i].gauge)?m.gauges[i].gauge:0.0f,0};
            packet.counters[i]={m.counters[i].flags,m.counters[i].max,0.0f,m.counters[i].count};
        }
        packet.timerState=m.timerState<=2?m.timerState:0;packet.timerMode=m.timerMode<=1?m.timerMode:0;
        packet.timerValue=m.timerValue;packet.timerDuration=m.timerDuration;
        packet.endTrap=g_missionHostEndTrap;packet.endArg=g_missionHostEndArg;
    }
    try {
        Send(encode(packet));
    } catch(const std::exception& e) {
        if(g_log && g_missionLogs++<16)g_log("[mission-mirror] host-refused frame=%u reason=%s",frame,e.what());
        return;
    }
    if(g_log && m.active && frame%120==0)g_log("[mission-mirror] host frame=%u file=%s flags=%X result=%u gauges=%.2f/%.2f/%.2f counts=%d/%d/%d timer=%u/%u end=%u/%u",
        frame,m.file,m.flags,m.result,m.gauges[0].gauge,m.gauges[1].gauge,m.gauges[2].gauge,
        m.counters[0].count,m.counters[1].count,m.counters[2].count,m.timerState,m.timerValue,
        g_missionHostEndTrap,g_missionHostEndArg);
}

// ---- client --------------------------------------------------------------------------------------
void ReceiveMission(const WorldScope& scope,const MissionState& packet) {
    if(CurrentRole()!=Role::Client || !g_lastRoomPacket ||
       !scope.sourceConnectionId || scope.sourceConnectionId!=g_bridge.ConnectionId(0) ||
       scope.sourceDeliverySerial!=g_bridge.PeerDeliverySerial(0) || !scope.hostSourceSerial ||
       !sameResyncRoom(packet.location,*g_lastRoomPacket) ||
       (g_missionGeneration==WorldSessionGeneration() && packet.sequence<=g_missionLatest.sequence))return;
    g_missionLatest=packet;g_missionReceivedMs=GetTickCount64();g_missionGeneration=WorldSessionGeneration();
    g_missionHostConnection=scope.sourceConnectionId;g_missionHostDelivery=scope.sourceDeliverySerial;
    MR::Mission h;
    h.active=packet.active;std::memcpy(h.file,packet.file.data(),MR::kFileBytes);h.file[MR::kFileBytes-1]=0;
    h.flags=packet.flags;h.result=packet.result;
    for(std::size_t i=0;i<MR::kSlots;++i) {
        h.gauges[i]={packet.gauges[i].flags,packet.gauges[i].max,packet.gauges[i].gauge,0};
        h.counters[i]={packet.counters[i].flags,packet.counters[i].max,0.0f,packet.counters[i].count};
    }
    h.timerState=packet.timerState;h.timerMode=packet.timerMode;h.timerValue=packet.timerValue;h.timerDuration=packet.timerDuration;
    h.endTrap=packet.endTrap;h.endArg=packet.endArg;
    g_missionHost=h;
}
bool MissionReceiveCurrent() {
    RoomTransition room;
    return g_missionGeneration && g_missionGeneration==WorldSessionGeneration() &&
        g_missionHostConnection==g_bridge.ConnectionId(0) && g_missionHostDelivery==g_bridge.PeerDeliverySerial(0) &&
        GetTickCount64()-g_missionReceivedMs<=500 && ActivationContext(Role::Client,room) &&
        sameResyncRoom(room,g_missionLatest.location);
}

template <std::size_t I> void __fastcall MissionDetour(std::uint64_t* args) {
    const auto role=g_missionRole.load(std::memory_order_acquire);
    if(role==1) {
        // Host: remember which end ran on this mission, with its argument, then end it natively.
        if(MR::kTraps[I].kind==MR::Kind::End && !g_missionHostEndTrap) {
            std::uint32_t arg=0;
            if(CopyNative(reinterpret_cast<uintptr_t>(args),&arg,sizeof(arg))) {
                g_missionHostEndTrap=static_cast<std::uint8_t>(MR::kTraps[I].index);g_missionHostEndArg=arg;
                if(g_log)g_log("[mission-mirror] host end trap=%s arg=%u",MR::kTraps[I].name,arg);
            }
        }
    } else if(role==2) {
        // A tick that stopped running (a load, a room change, the session ending) must not keep
        // dropping calls: following and the replay guard both expire with it.
        const bool fresh=GetTickCount64()-g_missionFollowMs.load(std::memory_order_acquire)<=250;
        const bool following=fresh && g_missionFollowing.load(std::memory_order_acquire);
        if(MR::DropLocalCall(I,following,g_missionHostFailed.load(std::memory_order_acquire),fresh && g_missionReplayed)) {
            g_missionDropped.fetch_add(1,std::memory_order_relaxed);
            std::uint32_t arg=0;
            if(following && !g_missionReplayed && MR::kTraps[I].kind==MR::Kind::End && g_missionDroppedEnd<0 &&
               CopyNative(reinterpret_cast<uintptr_t>(args),&arg,sizeof(arg))) {
                g_missionDroppedEnd=static_cast<int>(I);g_missionDroppedArg=arg;
            }
            return;  // none of these returns a value: args stay as the script passed them
        }
    }
    reinterpret_cast<MissionTrapFn>(g_missionHooks[I].native)(args);
}
template <std::size_t... I> constexpr std::array<MissionTrapFn,sizeof...(I)> MissionMakeDetours(std::index_sequence<I...>) {
    return {&MissionDetour<I>...};
}
constexpr auto kMissionDetours=MissionMakeDetours(std::make_index_sequence<MR::kTrapCount>{});

bool MissionPinned(uintptr_t at,const std::uint8_t* bytes,std::size_t n) {
    std::uint8_t live[128]{};
    return n<=sizeof(live) && CopyNative(at,live,n) && std::memcmp(live,bytes,n)==0;
}
// Once, either role: every entry and helper must be this build's before any entry is swapped.
bool MissionHooksReady() {
    if(g_missionChecked)return g_missionReady;
    g_missionChecked=true;
    uintptr_t table=0;
    if(!ReadNative(g_exeBase+MR::kTrapTables+8u*MR::kTable,table) || !table) {
        if(g_log)g_log("[mission-mirror] client-refused table");
        return false;
    }
    if(!MissionPinned(g_exeBase+MR::kSetGaugeRva,MR::kSetGaugeBody,sizeof(MR::kSetGaugeBody)) ||
       !MissionPinned(g_exeBase+MR::kSetCountRva,MR::kSetCountBody,sizeof(MR::kSetCountBody)) ||
       !MissionPinned(g_exeBase+MR::kFailRva,MR::kFailHead,sizeof(MR::kFailHead))) {
        if(g_log)g_log("[mission-mirror] client-refused helpers");
        return false;
    }
    for(std::size_t i=0;i<MR::kTrapCount;++i) {
        const auto& t=MR::kTraps[i];
        const uintptr_t entry=table+16u*t.index,native=g_exeBase+t.fnRva;
        uintptr_t fn=0;std::uint32_t flags=0;MEMORY_BASIC_INFORMATION mbi{};
        if(!ReadNative(entry,fn) || !ReadNative(entry+8,flags) || fn!=native || flags!=t.flags ||
           !MissionPinned(native,t.body,t.bodySize) ||
           VirtualQuery(reinterpret_cast<void*>(entry),&mbi,sizeof(mbi))!=sizeof(mbi) || mbi.State!=MEM_COMMIT ||
           mbi.Protect!=PAGE_READWRITE) {
            if(g_log)g_log("[mission-mirror] client-refused trap=%s fn=%llX flags=%X protect=%lX",t.name,
                static_cast<unsigned long long>(fn?fn-g_exeBase:0),flags,static_cast<unsigned long>(mbi.Protect));
            return false;
        }
        g_missionHooks[i].entry=entry;g_missionHooks[i].native=native;
    }
    // The detours stay callable for the process lifetime even if a restore races a script call.
    HMODULE pinned=nullptr;
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
          reinterpret_cast<LPCWSTR>(kMissionDetours[0]),&pinned))return false;
    for(std::size_t i=0;i<MR::kTrapCount;++i) {
        auto& h=g_missionHooks[i];
        const void* prior=InterlockedCompareExchangePointer(reinterpret_cast<PVOID volatile*>(h.entry),
            reinterpret_cast<void*>(kMissionDetours[i]),reinterpret_cast<void*>(h.native));
        h.swapped=prior==reinterpret_cast<void*>(h.native);
        if(!h.swapped) {
            if(g_log)g_log("[mission-mirror] swap-raced trap=%s",MR::kTraps[i].name);
            for(auto& r:g_missionHooks)if(r.swapped) {
                InterlockedCompareExchangePointer(reinterpret_cast<PVOID volatile*>(r.entry),
                    reinterpret_cast<void*>(r.native),reinterpret_cast<void*>(kMissionDetours[&r-g_missionHooks]));
                r.swapped=false;
            }
            return false;
        }
    }
    g_missionReady=true;
    if(g_log)g_log("[mission-mirror] %s ready traps=%zu",g_missionRole.load()==1?"host":"client",MR::kTrapCount);
    return true;
}

// kind 0: set_gauge(f, slot); 1: set_count(n, slot); 2: mission_failed().
bool MissionCall(uintptr_t fn,int kind,float f,int n,int slot) {
    __try {
        if(kind==0)reinterpret_cast<MissionSetGaugeFn>(fn)(f,slot);
        else if(kind==1)reinterpret_cast<MissionSetCountFn>(fn)(n,slot);
        else reinterpret_cast<MissionFailFn>(fn)();
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
bool MissionEnd(uintptr_t fn,std::uint32_t arg) {
    __try {
        std::uint64_t args[2]={(static_cast<std::uint64_t>(MR::kTagInt)<<32)|arg,0};
        reinterpret_cast<MissionEndFn>(fn)(args);
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
bool MissionShiftTimer(std::int32_t delta) {
    __try {
        auto* start=reinterpret_cast<volatile std::uint32_t*>(g_exeBase+MR::kTimerStart);
        *start=static_cast<std::uint32_t>(static_cast<std::int64_t>(*start)+delta);
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void LogMissionSecond(const MR::Mission& local,bool following) {
    const auto now=GetTickCount64();
    if(now-g_missionSecondMs<1000)return;
    g_missionSecondMs=now;
    const auto dropped=g_missionDropped.load(std::memory_order_relaxed);
    if(g_log && (following || g_missionSecond.gauges || g_missionSecond.counters || g_missionSecond.ends || dropped!=g_missionSecond.dropped))
        g_log("[mission-mirror] client sec following=%u file=%s host=%.2f/%.2f/%.2f local=%.2f/%.2f/%.2f counts=%d/%d gaugeSets=%u countSets=%u timerShifts=%u ends=%u dropped=%llu",
            following?1u:0u,local.file,g_missionHost.gauges[0].gauge,g_missionHost.gauges[1].gauge,g_missionHost.gauges[2].gauge,
            local.gauges[0].gauge,local.gauges[1].gauge,local.gauges[2].gauge,g_missionHost.counters[0].count,local.counters[0].count,
            g_missionSecond.gauges,g_missionSecond.counters,g_missionSecond.timer,g_missionSecond.ends,
            static_cast<unsigned long long>(dropped));
    const auto keep=dropped;
    g_missionSecond={};g_missionSecond.dropped=keep;
}

// VUH-2343: HB08's final death and mission end can arrive in the same drain.
// Replaying the end first enters a native hold, losing the final body's
// continuous death binding. Let ClientFrame reconcile it through the existing
// guarded native lethal before replaying the end on a later owner frame.
// This grants no death/write authority and never treats an unreadable roster
// as empty. Other missions and the failure path keep their existing policy.
bool MissionEndRosterReady(const MR::Mission& local, uintptr_t expectedMission) {
    if(!MR::SameFile(local.file,"HB08_MS102.bar"))return true;
    uintptr_t before=0,after=0;
    if(!BaileyMissionCurrent(true,&before) || before!=expectedMission)return false;
    const auto census=CaptureNativeCensus();
    if(!CensusMatchesInstance(census) ||
       std::any_of(census.enemies.begin(),census.enemies.end(),[](const auto& enemy){return enemy.hp>0;}))return false;
    return BaileyMissionCurrent(true,&after) && after==before && CensusMatchesInstance(census);
}

void ApplyMission(std::uint32_t frame) {
    (void)frame;
    g_missionRole.store(2,std::memory_order_release);
    if(!g_mirrorRequested || MissionParked() || !MissionHooksReady()) { g_missionFollowing.store(false); return; }
    uintptr_t base=0;
    const auto local=ReadMission(&base);
    const auto load=warp::LoadSerial();
    if(base!=g_missionLocalBase || load!=g_missionLocalLoad || std::strncmp(local.file,g_missionLocalFile,MR::kFileBytes)) {
        // A new mission here (or a retry after a reload): nothing of it has been replayed or dropped yet.
        g_missionLocalBase=base;g_missionLocalLoad=load;std::memcpy(g_missionLocalFile,local.file,MR::kFileBytes);
        g_missionReplayed=false;g_missionDroppedEnd=-1;g_missionFallbackPending=false;g_missionHostFailedSeen=false;
    }
    const bool current=MissionReceiveCurrent();
    const bool following=current && MR::Follows(g_missionHost,local) && !MR::HostEndedUnknown(g_missionHost);
    // Publish to the detours before calling anything that could run a script.
    g_missionHostFailed.store(following && MR::Failed(g_missionHost),std::memory_order_release);
    g_missionFollowMs.store(GetTickCount64(),std::memory_order_release);
    g_missionFollowing.store(following,std::memory_order_release);
    if(following!=g_missionWasFollowing && g_log)
        g_log("[mission-mirror] client %s file=%s host=%s",following?"follow":"release",local.active?local.file:"-",
            g_missionHost.active?g_missionHost.file:"-");
    if(current && MR::Failed(g_missionHost) && MR::SameFile(g_missionHost.file,local.file))g_missionHostFailedSeen=true;
    if(g_missionWasFollowing && !following && g_missionDroppedEnd>=0 && !g_missionReplayed)g_missionFallbackPending=true;
    if(following)g_missionFallbackPending=false;
    g_missionWasFollowing=following;
    if(!following) {
        // The host's mission is over (or gone) but its end can't be replayed, and this game had ended its own:
        // end it as its own script did. A stale packet alone never does this (the host may still be fighting).
        const bool hostDone=current && (MR::HostEndedUnknown(g_missionHost) || !g_missionHost.active ||
                                        !MR::SameFile(g_missionHost.file,local.file));
        // Checked on every fresh frame after the release, not only the first: packets may be lost meanwhile.
        if(g_missionFallbackPending && hostDone && !g_missionHostFailedSeen && g_missionDroppedEnd>=0 &&
           !g_missionReplayed && local.active && !MR::Ended(local) && !LocalMissionEnded(false)) {
            g_missionFallbackPending=false;
            g_missionReplayed=true;
            const bool ok=MissionEnd(g_missionHooks[g_missionDroppedEnd].native,g_missionDroppedArg);
            ++g_missionSecond.ends;
            if(g_log)g_log("[mission-mirror] client end of its own after release file=%s trap=%s arg=%u ok=%u",local.file,
                MR::kTraps[g_missionDroppedEnd].name,g_missionDroppedArg,ok?1u:0u);
        }
        LogMissionSecond(local,false);
        return;
    }
    // The host's last values first, so an end below leaves the friend's HUD where the host's ended.
    if(!(local.flags&MR::kSettersRefused)) {
        for(std::size_t i=0;i<MR::kSlots;++i) {
            if(!local.gaugeLock && MR::GaugeDiffers(g_missionHost.gauges[i],local.gauges[i]) &&
               MissionCall(g_exeBase+MR::kSetGaugeRva,0,g_missionHost.gauges[i].gauge,0,static_cast<int>(i)))++g_missionSecond.gauges;
            if(MR::CountDiffers(g_missionHost.counters[i],local.counters[i]) &&
               MissionCall(g_exeBase+MR::kSetCountRva,1,0,g_missionHost.counters[i].count,static_cast<int>(i)))++g_missionSecond.counters;
        }
    }
    if(MR::Failed(g_missionHost)) {
        if(!g_missionReplayed && (MR::Failed(local) || LocalMissionEnded(true)))
            g_missionReplayed=true;  // its own script failed it (the gauge reached 0 here too): guard later calls
        if(!g_missionReplayed) {
            g_missionReplayed=true;
            const bool ok=MissionCall(g_exeBase+MR::kFailRva,2,0,0,0);
            ++g_missionSecond.ends;
            if(g_log)g_log("[mission-mirror] client fail like the host file=%s ok=%u",local.file,ok?1u:0u);
        }
        LogMissionSecond(local,true);
        return;
    }
    if(const int end=MR::EndTrapIndex(g_missionHost.endTrap);end>=0) {
        // The host's mission ended this way: end ours the same way, once.
        if(!g_missionReplayed && LocalMissionEnded(false))g_missionReplayed=true;  // already ended here: guard only
        if(!g_missionReplayed) {
            if(!MissionEndRosterReady(local,base)) {
                if(g_log && g_missionLogs++<64)g_log("[mission-mirror] client end deferred file=%s reason=Bailey-roster-live-or-unavailable",local.file);
                LogMissionSecond(local,true);
                return; // do not consume the replay one-shot; normal death reconciliation runs next
            }
            g_missionReplayed=true;
            const bool ok=MissionEnd(g_missionHooks[end].native,g_missionHost.endArg);
            ++g_missionSecond.ends;
            if(g_log)g_log("[mission-mirror] client end like the host file=%s trap=%s arg=%u ok=%u",local.file,
                MR::kTraps[end].name,g_missionHost.endArg,ok?1u:0u);
        }
        LogMissionSecond(local,true);
        return;
    }
    if(const auto delta=MR::TimerStartDelta(g_missionHost,local);delta && MissionShiftTimer(delta)) {
        ++g_missionSecond.timer;
        if(g_log && g_missionLogs++<64)g_log("[mission-mirror] client timer shift=%d host=%u local=%u",delta,g_missionHost.timerValue,local.timerValue);
    }
    LogMissionSecond(local,true);
}

void ShutdownMissionMirror() {
    g_missionFollowing.store(false);
    for(std::size_t i=0;i<MR::kTrapCount;++i) {
        auto& h=g_missionHooks[i];
        if(!h.swapped)continue;
        InterlockedCompareExchangePointer(reinterpret_cast<PVOID volatile*>(h.entry),
            reinterpret_cast<void*>(h.native),reinterpret_cast<void*>(kMissionDetours[i]));
        h.swapped=false;
    }
}
