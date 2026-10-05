#define main existingForcedMain
#include "ForcedResyncTest.cpp"
#undef main
#include <fstream>
#include <sstream>
std::string field(const std::string& row,const std::string& key){
    const auto prefix=key+"=";auto at=row.find(prefix);if(at==std::string::npos)return {};
    at+=prefix.size();return row.substr(at,row.find(' ',at)-at);
}
int main(){
    if(enet_initialize()!=0)return 2;
    std::ofstream raw("causal-raw.log",std::ios::binary);
    std::vector<std::string> rows;
    const CausalSink sink=[&](const std::string& row){rows.push_back(row);raw<<row<<'\n';raw.flush();return raw.good();};
    {
        Rig r(17963);check(r.prime(),"private real loopback ready");
        auto& host=*r.clients[0];auto& friend1=*r.clients[1];
        check(host.requestDiagnostics().highWater==0,"no sink allocates no request receipts");
        host.callbacks_.onCausalDiagnostic=sink;friend1.callbacks_.onCausalDiagnostic=sink;
        host.sealRequestDiagnostics("begin");
        ResyncRequest generated;
        check(!friend1.requestWorldResync(2,&generated,ResyncRequestOrigin::OperatorMailbox)&&!generated.key.requestId&&
            field(rows.back(),"disposition")=="generator-rejected"&&field(rows.back(),"keyAvailable")=="0",
            "pre-key nonhost attempt has explicit manual origin and rejection");
        host.recordResyncCallerRejection(ResyncRequestOrigin::OperatorMailbox,2);
        check(field(rows.back(),"disposition")=="caller-rejected","mailbox rejected before generator is accounted");
        host.setLinkConditions({60000,0,0.0f,41},{});
        check(host.requestWorldResync(2,&generated,ResyncRequestOrigin::AutomaticNotice),"actual request queued by conditioner");
        const auto first=rows.back();const auto deadline=host.resyncDeadline_;
        check(field(first,"originalDeadlineMs")==std::to_string(deadline)&&
            std::stoull(field(first,"originalDeadlineMs"))-std::stoull(field(first,"startedMs"))==RESYNC_TIMEOUT_MS&&
            field(first,"request")==std::to_string(generated.key.requestId)&&field(first,"origin")=="automatic-notice",
            "exact immutable generated key and original owner clock deadline retained");
        check(!host.requestWorldResync(2,nullptr,ResyncRequestOrigin::OperatorMailbox)&&
            field(rows.back(),"keyAvailable")=="0","busy manual generator attempt has no second key");
        host.resyncDeadline_-=10; // explicit synthetic plan-shortening boundary control
        check(host.sendResyncRequest(generated)&&field(rows.back(),"origin")=="direct-request"&&
            field(rows.back(),"originalDeadlineMs")==std::to_string(deadline)&&host.resyncDeadline_==deadline-10,
            "direct retransmit receipt retains original deadline without extending current deadline");
        host.sealRequestDiagnostics();
        check(field(rows.back(),"highWater")=="4"&&field(rows.back(),"dropped")=="0",
            "flushed coverage seal accounts manual automatic and direct attempts");
        host.requestedResync_.reset();host.setLinkConditions({},{});
        auto* peer=host.enetPeer_;host.enetPeer_=nullptr;
        check(!host.requestWorldResync(2,&generated,ResyncRequestOrigin::OperatorMailbox)&&generated.key.requestId&&
            field(rows.back(),"keyAvailable")=="1"&&field(rows.back(),"disposition")=="submission-failed"&&
            field(rows.back(),"deadlineAvailable")=="1","failed real send retains allocated key and assigned deadline");
        host.enetPeer_=peer;host.sealRequestDiagnostics();
    }
    {
        CausalStream stream;
        stream.emit({},"control",[](auto& out){out<<" value=1";});
        check(!stream.highWater,"disabled diagnostic stream allocates no synthetic counters");
        stream.emit([](const auto&){return false;},"control",[](auto&){});
        check(stream.highWater==1&&stream.flushed==0&&stream.dropped==1&&stream.unavailable,"sink failure creates explicit sticky gap");
        stream.highWater=CausalStream::Limit;stream.emit(sink,"control",[](auto&){});
        check(stream.highWater==CausalStream::Limit&&stream.dropped==2,"bounded receipts never wrap or overwrite");
        stream.seal(sink,"control","interval");
        check(field(rows.back(),"unavailable")=="1","post-failure seal cannot claim complete coverage");
    }
    enet_deinitialize();std::cout<<checks-failures<<" PASS, "<<failures<<" FAIL\n";return failures?1:0;
}
