// VUH-2276: complete periodic field roster, kept outside enemy HP/population.
// Included after native census and world-context helpers. Owner game thread only.
struct HostFieldObject {
    NativeField identity{};
    Vec3 anchor{};
    std::uint16_t id{};
    bool present{}, announced{}, retired{};
};
struct BoundFieldObject {
    NativeField identity{};
    Vec3 anchor{};
    FieldObjectEntry host{};
    std::uint32_t seenFrame{};
    bool present{}, leaveAttempted{}, ambiguous{}, revoked{};
};
std::vector<HostFieldObject> g_fieldHost;
std::unordered_map<uintptr_t,BoundFieldObject> g_fieldLocal;
FieldObjects g_fieldLatest{};
std::uint64_t g_fieldSequence{},g_fieldReceivedMs{},g_fieldHostConnection{},g_fieldHostDelivery{};
std::uint32_t g_fieldGeneration{},g_fieldBoundFrame{},g_fieldLoad{},g_fieldTransition{};
unsigned g_fieldNext{},g_fieldLogs{};
ProducerWorldContext g_fieldContext{};

void ResetFieldObjects() {
    g_fieldHost.clear();g_fieldLocal.clear();g_fieldLatest={};
    g_fieldReceivedMs=0;g_fieldGeneration=0;g_fieldBoundFrame=0;
    g_fieldHostConnection=g_fieldHostDelivery=0;g_fieldContext={};g_fieldNext=0;
}
bool FieldSamePoint(Vec3 a,Vec3 b) {
    const float x=a.x-b.x,y=a.y-b.y,z=a.z-b.z;
    return std::isfinite(x)&&std::isfinite(y)&&std::isfinite(z)&&x*x+y*y+z*z<=64.0f;
}
bool FieldReceiveCurrent() {
    RoomTransition room;
    return g_mirrorRequested && g_fieldGeneration==WorldSessionGeneration() && g_fieldGeneration &&
        g_fieldHostConnection==g_bridge.ConnectionId(0) &&
        g_fieldHostDelivery==g_bridge.PeerDeliverySerial(0) &&
        GetTickCount64()-g_fieldReceivedMs<=500 &&
        ActivationContext(Role::Client,room) && sameResyncRoom(room,g_fieldLatest.location);
}
void ReceiveFieldObjects(const WorldScope& scope,const FieldObjects& packet) {
    enemymirror::FieldCallbackReader reader;if(!reader)return;
    if(CurrentRole()!=Role::Client || !g_lastRoomPacket ||
       !scope.sourceConnectionId || scope.sourceConnectionId!=g_bridge.ConnectionId(0) ||
       scope.sourceDeliverySerial!=g_bridge.PeerDeliverySerial(0) || !scope.hostSourceSerial ||
       !sameResyncRoom(packet.location,*g_lastRoomPacket) ||
       (g_fieldGeneration==WorldSessionGeneration() && packet.sequence<=g_fieldLatest.sequence))return;
    g_fieldLatest=packet;g_fieldReceivedMs=GetTickCount64();g_fieldGeneration=WorldSessionGeneration();
    g_fieldHostConnection=scope.sourceConnectionId;g_fieldHostDelivery=scope.sourceDeliverySerial;
}
void PublishFieldObjects(std::uint32_t frame,const NativeCensus& census) {
    enemymirror::FieldCallbackReader reader;if(!reader)return;
    if(!g_mirrorRequested || !CensusMatchesInstance(census) || census.fields.size()>FIELD_OBJECT_MAX_ENTRIES)return;
    RoomTransition room;if(!ActivationContext(Role::Host,room))return;
    for(auto& h:g_fieldHost)h.present=false;
    for(const auto& f:census.fields) {
        auto found=std::find_if(g_fieldHost.begin(),g_fieldHost.end(),[&](const auto& h){return !h.retired && SameFieldIdentity(h.identity,f);});
        if(found==g_fieldHost.end()) {
            if(g_fieldNext==0x7FFFu)return;
            g_fieldHost.push_back({f,f.value.position,static_cast<std::uint16_t>(0x8000u|++g_fieldNext),true,false});
        } else {found->identity=f;found->present=true;}
    }
    for(auto& h:g_fieldHost)if(!h.present)h.retired=true;
    if(g_fieldHost.size()>FIELD_OBJECT_MAX_ENTRIES || frame%3 || g_fieldSequence==UINT64_MAX)return;
    FieldObjects packet;packet.location=room;packet.hostFrame=frame;packet.sequence=++g_fieldSequence;
    for(const auto& h:g_fieldHost) {
        auto value=h.identity.value;value.netId=h.id;value.anchor=h.anchor;value.present=h.present;packet.entries.push_back(value);
    }
    try {
        if(!Send(encode(packet)))return;
    } catch(const std::exception& e) {
        if(g_log && g_fieldLogs++<32)g_log("[field-mirror] host-refused frame=%u reason=%s",frame,e.what());
        return;
    }
    for(auto& h:g_fieldHost)if(h.present)h.announced=true;
    if(g_log && frame%60==0)g_log("[field-mirror] host frame=%u epoch=%u count=%zu sequence=%llu",
        frame,room.epoch,packet.entries.size(),static_cast<unsigned long long>(packet.sequence));
}
void BindFieldObjects(const NativeCensus& census) {
    enemymirror::FieldCallbackReader reader;if(!reader)return;
    g_fieldBoundFrame=0;
    if(!CensusMatchesInstance(census) || !FieldReceiveCurrent() || !CaptureWorldContext(g_fieldContext))return;
    g_fieldBoundFrame=g_mirrorFrame;g_fieldLoad=census.load;g_fieldTransition=census.transition;
    for(const auto& f:census.fields) {
        auto it=g_fieldLocal.find(f.actor);
        if(it!=g_fieldLocal.end() && !SameFieldIdentity(it->second.identity,f)) {
            g_fieldLocal.erase(it);it=g_fieldLocal.end();
        }
        if(it==g_fieldLocal.end()) {
            BoundFieldObject b;b.identity=f;b.anchor=f.value.position;
            it=g_fieldLocal.emplace(f.actor,b).first;
        }
        auto& b=it->second;
        if(b.host.netId && b.seenFrame+1!=g_mirrorFrame)b.revoked=true;
        b.seenFrame=g_mirrorFrame;b.identity=f;
        const FieldObjectEntry* selected=nullptr;
        const FieldObjectEntry* retired=nullptr;unsigned liveMatches=0;
        for(const auto& h:g_fieldLatest.entries) {
            if(b.host.netId) {if(h.netId==b.host.netId)selected=&h;}
            else if(h.objectId==f.value.objectId && h.objectType==f.value.objectType && FieldSamePoint(h.anchor,b.anchor)) {
                if(h.present){selected=&h;++liveMatches;}
                else if(!retired || h.netId>retired->netId)retired=&h;
            }
        }
        if(!b.host.netId) {
            // A retained tombstone never outranks a unique living occurrence.
            // Multiple living occurrences at this point have no safe identity.
            if(liveMatches>1)continue;
            if(!liveMatches)selected=retired;
            if(!selected)continue;
            bool owned=false;
            for(const auto& [otherActor,other]:g_fieldLocal)if(otherActor!=f.actor && other.host.netId==selected->netId)
                for(const auto& live:census.fields)if(SameFieldIdentity(other.identity,live))owned=true;
            if(owned)continue;
            // Whole-census uniqueness, before granting this field identity.
            unsigned locals=0;
            for(const auto& other:census.fields)
                if(other.value.objectId==selected->objectId && other.value.objectType==selected->objectType &&
                   FieldSamePoint(other.value.position,selected->anchor)) {
                    const auto previous=g_fieldLocal.find(other.actor);
                    if(previous==g_fieldLocal.end() || !previous->second.host.netId ||
                       previous->second.host.netId==selected->netId)++locals;
                }
            if(locals!=1)continue;
            b.host=*selected;b.present=selected->present;
            if(g_log)g_log("[field-mirror] bind frame=%u netId=%u objectId=%u actor=%llX point=%.1f,%.1f,%.1f",
                g_mirrorFrame,b.host.netId,b.host.objectId,static_cast<unsigned long long>(f.actor),
                b.anchor.x,b.anchor.y,b.anchor.z);
        } else {
            b.ambiguous=selected && (selected->objectId!=b.host.objectId || selected->objectType!=b.host.objectType ||
                !FieldSamePoint(selected->anchor,b.host.anchor));
            b.present=selected && selected->present;
            if(selected && !b.ambiguous)b.host=*selected;
        }
    }
    for(auto it=g_fieldLocal.begin();it!=g_fieldLocal.end();) {
        if(it->second.seenFrame!=g_mirrorFrame)it=g_fieldLocal.erase(it);else ++it;
    }
}
bool FieldBindingCurrent(uintptr_t actor,bool present) {
    if(!FieldReceiveCurrent() || g_fieldBoundFrame!=g_mirrorFrame || !WorldContextCurrent(g_fieldContext) ||
       g_fieldLoad!=warp::LoadSerial() || g_fieldTransition!=warp::TransitionSerial())return false;
    const auto it=g_fieldLocal.find(actor);
    if(it==g_fieldLocal.end() || !it->second.host.netId || it->second.ambiguous || it->second.revoked || (present && !it->second.present))return false;
    NativeField now;bool field=false;
    return ReadNativeField(actor,now,field) && field && SameFieldIdentity(now,it->second.identity);
}
enemymirror::Gate FieldMirrorPose(uintptr_t actor,enemymirror::Pose& out) {
    enemymirror::FieldCallbackReader reader;if(!reader)return enemymirror::Gate::None;
    if(!FieldBindingCurrent(actor,false))return enemymirror::Gate::None;
    const auto& b=g_fieldLocal.at(actor);
    out={};out.field=true;out.netId=b.host.netId;out.objectId=b.host.objectId;
    if(!b.present)return enemymirror::Gate::Bound;
    out.motionId=b.host.motionId;out.motionTime=b.host.motionTime;
    out.position=b.host.position;out.rotationY=b.host.rotationY;out.cursor=g_fieldLatest.hostFrame;
    return enemymirror::Gate::Drive;
}
std::uint16_t HostFieldNetId(uintptr_t actor,std::uint32_t objectId) {
    enemymirror::FieldCallbackReader reader;if(!reader)return 0;
    for(const auto& h:g_fieldHost)if(h.present && h.announced && h.identity.actor==actor && h.identity.value.objectId==objectId) {
        NativeField now;bool field=false;
        if(ReadNativeField(actor,now,field) && field && SameFieldIdentity(now,h.identity))return h.id;
    }
    return 0;
}
