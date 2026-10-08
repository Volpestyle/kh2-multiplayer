#include "kh2coop/SteamBroker.hpp"
#include <algorithm>
#include <charconv>
#include <limits>

namespace kh2coop::steam {
namespace {
void put(std::vector<std::uint8_t>& b,std::uint64_t v,unsigned n){for(unsigned i=0;i<n;++i)b.push_back(static_cast<std::uint8_t>(v>>(8*i)));}
std::uint64_t get(std::span<const std::uint8_t> b,std::size_t p,unsigned n){std::uint64_t v=0;for(unsigned i=0;i<n;++i)v|=std::uint64_t(b[p+i])<<(8*i);return v;}
constexpr std::uint32_t Magic=0x31504253;
}
bool validId(std::uint64_t id) { // public-universe, individual desktop account
    return (id>>56)==1 && ((id>>52)&15)==1 && ((id>>32)&0xfffff)==1 && (id&0xffffffff)!=0;
}
bool parseId(const std::string& s,std::uint64_t& id){
    id=0;if(s.size()!=17)return false;const auto r=std::from_chars(s.data(),s.data()+s.size(),id);
    return r.ec==std::errc{}&&r.ptr==s.data()+s.size()&&validId(id);
}
std::vector<std::uint8_t> encode(const Frame& f,std::uint64_t seq){
    if(!seq||f.bytes.size()>MaxPacket||f.channel>=3)return {};
    std::vector<std::uint8_t>b;b.reserve(32+f.bytes.size());
    put(b,Magic,4);put(b,1,1);put(b,static_cast<unsigned>(f.op),1);put(b,f.channel,1);put(b,f.reliable?1:0,1);
    put(b,seq,8);put(b,f.peer,8);put(b,f.reason,4);put(b,f.bytes.size(),4);b.insert(b.end(),f.bytes.begin(),f.bytes.end());return b;
}
bool decode(std::span<const std::uint8_t>b,std::uint64_t seq,Frame& f){
    if(!seq||b.size()<32||b.size()>MaxPacket+32||get(b,0,4)!=Magic||b[4]!=1||b[5]<1||b[5]>static_cast<unsigned>(Op::Stats)||b[6]>=3||b[7]>1||get(b,8,8)!=seq||get(b,28,4)!=b.size()-32)return false;
    f={static_cast<Op>(b[5]),get(b,16,8),static_cast<std::uint32_t>(get(b,24,4)),b[6],b[7]!=0,{b.begin()+32,b.end()}};return true;
}
bool Broker::emit(Frame f){if(out_.size()>=MaxQueue)return fail("ipc-output-overflow");out_.push_back(std::move(f));return true;}
bool Broker::fail(const char* why){stopOwned(why);failed_=true;out_.clear();Frame f{Op::Error};f.bytes.assign(why,why+std::char_traits<char>::length(why));out_.push_back(std::move(f));return false;}
void Broker::closePeer(std::uint32_t handle,std::uint64_t identity,bool linger,
                       std::uint32_t reason,const char* why,int flags){
    api_.decision(handle,identity,"close",why,allowed_.size(),flags);
    api_.close(handle,linger,reason);
}
void Broker::stopOwned(const char* why){
    for(const auto& [id,p]:peers_)closePeer(p.handle,id,p.closing,p.closing?p.closeReason:0,why,p.flags);
    peers_.clear();
    if(listener_){api_.decision(listener_,identity_,"close-listener",why,allowed_.size(),0);api_.closeListener(listener_);}
    listener_=0;configured_=false;allowed_.clear();admitted_.clear();target_=0;
}
void Broker::stop(){stopOwned("broker-stop");}

bool Broker::pop(Frame& f){if(out_.empty())return false;f=std::move(out_.front());out_.pop_front();return true;}
bool Broker::retire(std::uint64_t id,std::uint32_t reason,const char* why){
    const auto it=peers_.find(id);if(it==peers_.end())return true;
    closePeer(it->second.handle,id,false,reason,why,it->second.flags);peers_.erase(it);
    return emit({Op::Disconnected,id,reason});
}
bool Broker::identityCurrent(std::uint64_t now){
    if(!identity_||now<lastIdentityCheck_)return false;
    if(now-lastIdentityCheck_<1000)return true;
    lastIdentityCheck_=now;return api_.currentIdentity()==identity_;
}
bool Broker::canCommand(std::size_t backlog)const{
    // Continue consuming bounded IPC commands even when gameplay is congested:
    // Close/Ping behind those sends must reach the broker. Send retains its hard
    // MaxQueue limit; terminal state and its envelope need no gameplay slot.
    return !failed_&&out_.size()+backlog<MaxQueue-32;
}
bool Broker::command(const Frame& f,std::uint64_t now){
    if(failed_)return false;
    lastCommand_=now;
    if(f.op==Op::Hello){
        if(identity_||f.peer||!f.bytes.empty())return fail("bad-hello");
        identity_=api_.readyIdentity();lastIdentityCheck_=now;if(!validId(identity_))return fail("session-not-ready");
        return emit({Op::Ready,identity_});
    }
    if(!identityCurrent(now))return fail("session-changed");
    if(f.op==Op::Ping)return f.bytes.empty()||fail("bad-ping");
    if(f.op==Op::Stop){stop();return true;}
    if(f.op==Op::Host){
        // An empty allowlist (runtime --steam-listener-probe) still listens and verifies ICE, but the
        // Connecting check below then refuses every identity before accept.
        if(configured_||f.peer||f.bytes.size()>MaxPeers*8||f.bytes.size()%8)return fail("bad-host-allowlist");
        for(std::size_t i=0;i<f.bytes.size();i+=8){const auto id=get(f.bytes,i,8);if(!validId(id)||id==identity_||std::find(allowed_.begin(),allowed_.end(),id)!=allowed_.end())return fail("bad-host-allowlist");allowed_.push_back(id);}
        listener_=api_.listen();if(!listener_||!api_.iceOff(listener_,true))return fail("listen-ice-off-unverified");
        api_.decision(listener_,identity_,"host","allowlist-configured",allowed_.size(),0);
        configured_=true;return emit({Op::Ready,identity_});
    }
    if(f.op==Op::Join){
        // VUH-1493 G8: a joiner may Join its SAME host again once its previous connection is gone.
        const bool rejoin=configured_&&target_&&f.peer==target_&&peers_.empty()&&!listener_;
        if((configured_&&!rejoin)||!validId(f.peer)||f.peer==identity_||!f.bytes.empty())return fail("bad-join");
        target_=f.peer;if(!rejoin)admitted_.push_back(f.peer);const auto h=api_.connect(target_);if(!h)return fail("connect-failed");
        try {peers_.emplace(target_,Peer{h,false,now});} catch(...) {closePeer(h,target_,false,0,"peer-allocation-failed");throw;}
        if(!api_.iceOff(h,false))return fail("connect-ice-off-unverified");
        configured_=true;return true;
    }
    const auto it=peers_.find(f.peer);
    if(it==peers_.end()){
        // In-flight runtime sends/closes may overtake our Disconnected receipt.
        if(f.op==Op::Close&&hostLeftReason(f.reason)&&std::find(admitted_.begin(),admitted_.end(),f.peer)!=admitted_.end())return emit({Op::Disconnected,f.peer,0});
        if((f.op==Op::Send||f.op==Op::Close)&&std::find(admitted_.begin(),admitted_.end(),f.peer)!=admitted_.end())return true;
        return fail("unknown-peer");
    }
    if(f.op==Op::Close && hostLeftReason(f.reason) && it->second.connected){
        auto& p=it->second;
        if(!p.closing){p.closing=true;p.closeReason=f.reason;p.closeStarted=now;
            // The end envelope has its own bounded slot outside the gameplay
            // queue. Send it only after FIFO gameplay drains; a full queue must
            // never turn this terminal request into stopOwned(reason=0).
            api_.decision(p.handle,f.peer,"drain","host-left-reliable-drain",allowed_.size(),p.flags);}
        return true; // Disconnected is the receipt AFTER broker/Steam drain and native close.
    }
    if(f.op==Op::Close){if(f.reason>9)return fail("bad-close-reason");closePeer(it->second.handle,f.peer,true,f.reason,"runtime-close",it->second.flags);peers_.erase(it);return emit({Op::Disconnected,f.peer,f.reason});}
    if((it->second.closing||it->second.remoteClosed)&&f.op==Op::Send)return true; // no new gameplay after terminal leave
    if(f.op!=Op::Send||!it->second.connected||f.bytes.empty()||f.bytes.size()>MaxPacket-8||f.channel>=3)return fail("bad-send");
    std::vector<std::uint8_t> wire{0x4b,0x53,1,f.channel,static_cast<std::uint8_t>(f.reliable),0,0,0};wire.insert(wire.end(),f.bytes.begin(),f.bytes.end());
    auto& peer=it->second;
    if(peer.pending.empty()){
        if(api_.send(peer.handle,wire,f.reliable))return true;
        if(!api_.congested())return retire(f.peer,0,"send-failed");
    }
    if(peer.pending.size()>=MaxQueue)return retire(f.peer,0,"send-queue-overflow"); // hard bounded backstop
    peer.pending.push_back({std::move(wire),f.reliable});return true;
}
bool Broker::tick(std::uint64_t now,std::size_t ipcBacklog){
    if(failed_)return false;
    if(!firstTick_)firstTick_=now;
    if(!identity_&&now-firstTick_>5000)return fail("ipc-hello-timeout");
    if(!api_.healthy())return fail("callback-overflow");
    if(identity_&&(!identityCurrent(now)||now<lastCommand_||now-lastCommand_>5000))return fail("session-or-runtime-lost");
    const auto outputRoom=[&]{return out_.size()+ipcBacklog<MaxQueue-32;};
    Status s;
    for(unsigned n=0;n<64&&outputRoom()&&api_.nextStatus(s);++n){
        auto it=peers_.find(s.identity);
        if(s.state==1 && listener_ && s.listener==listener_){
            // Unencrypted (bit 2) on Connecting precedes AcceptConnection. Do
            // not confuse it with unauthenticated identity (bit 1). Connected
            // admission and every data path still require encrypted relay.
            const char* refusal=nullptr;
            if(!validId(s.identity))refusal="incoming-invalid-identity";
            else if(!s.authenticated||(s.flags&1))refusal="incoming-unauthenticated";
            else if(std::find(allowed_.begin(),allowed_.end(),s.identity)==allowed_.end())refusal="incoming-not-allowlisted";
            else if(it!=peers_.end())refusal="incoming-duplicate-identity";
            else if(peers_.size()>=MaxPeers)refusal="incoming-peer-limit";
            else if(!api_.iceOff(s.handle,false))refusal="incoming-ice-off-unverified";
            if(refusal){closePeer(s.handle,s.identity,false,0,refusal,s.flags);continue;}
            try {peers_.emplace(s.identity,Peer{s.handle,false,now});} catch(...) {closePeer(s.handle,s.identity,false,0,"peer-allocation-failed",s.flags);throw;}
            peers_.at(s.identity).flags=s.flags;
            if(std::find(admitted_.begin(),admitted_.end(),s.identity)==admitted_.end())admitted_.push_back(s.identity);
            api_.decision(s.handle,s.identity,"accept","allowlisted-authenticated-connecting",allowed_.size(),s.flags);
            if(!api_.accept(s.handle)&&!retire(s.identity,0,"accept-failed"))return false;
            continue;
        }
        if(it==peers_.end()||it->second.handle!=s.handle)continue; // late callback, never adopt it
        it->second.flags=s.flags;
        if(s.state==3){
            if(!s.authenticated||(s.flags&3)||!s.relay||!api_.iceOff(s.handle,false))return fail("connection-not-authenticated-relay-only");
            if(!it->second.connected){it->second.connected=true;if(!emit({Op::Connected,s.identity}))return false;}
        } else if(s.state==0||s.state>=4){
            auto& p=it->second;
            if(p.connected&&(p.remoteClosed||hostLeftReason(s.reason))){
                if(!p.remoteClosed){p.remoteClosed=true;p.remoteReason=s.reason;p.remoteClosedAt=now;}
                else if(hostLeftReason(s.reason))p.remoteReason=s.reason;
                // Retain the previously admitted handle until SDK receive is
                // empty. CloseConnection would discard those reliable messages.
            }else{closePeer(s.handle,s.identity,false,0,"terminal-status",s.flags);peers_.erase(it);if(!emit({Op::Disconnected,s.identity,s.reason}))return false;}
        }
    }
    for(auto it=peers_.begin();it!=peers_.end();){
        const auto id=it->first;auto& p=it->second;++it; // retire erases only this peer
        if(!p.connected){if(now-p.started>ConnectTimeoutMs&&outputRoom()&&!retire(id,0,"pending-connect-timeout"))return false;continue;}
        bool closed=false;
        for(unsigned n=0;n<16&&!p.remoteClosed&&!p.pending.empty();++n){
            const auto& f=p.pending.front();
            if(!api_.send(p.handle,f.bytes,f.reliable)){
                if(!api_.congested()){
                    if(p.closing){const auto reason=p.closeReason;closePeer(p.handle,id,true,reason,"host-left-send-failed",p.flags);peers_.erase(id);if(!emit({Op::Disconnected,id,reason}))return false;}
                    else if(!retire(id,0,"queued-send-failed"))return false;
                    closed=true;
                }
                break;
            }
            p.pending.pop_front();
        }
        if(closed)continue;
        if(p.closing&&!p.remoteClosed){
            if(!p.terminalSent&&p.pending.empty()){
                const std::uint8_t end[]={0x4b,0x53,1,0,1,1,0,0,static_cast<std::uint8_t>(p.closeReason)};
                // Joiner-side terminal closes have no host end envelope.
                p.terminalSent=!listener_||api_.send(p.handle,end,true);
            }
            const bool delivered=p.pending.empty()&&p.terminalSent&&api_.reliableDrained(p.handle);
            if(delivered || now-p.closeStarted>=TerminalDrainMs){
                const auto reason=p.closeReason;
                if(!delivered){
                    std::size_t bytes=p.terminalSent?0:9;for(const auto& queued:p.pending)bytes+=queued.bytes.size();
                    const auto detail="undelivered-pendingMessages="+std::to_string(p.pending.size())+"-pendingBytes="+std::to_string(bytes);
                    api_.decision(p.handle,id,"drain-abandoned",detail.c_str(),allowed_.size(),p.flags);
                }
                closePeer(p.handle,id,true,reason,delivered?"host-left-delivered":"host-left-drain-timeout",p.flags);
                peers_.erase(id);if(!emit({Op::Disconnected,id,reason}))return false;
            }
            continue;
        }
        if(outputRoom()&&now-p.lastStats>=1000){p.lastStats=now;std::uint32_t rtt=0,loss=0;
            if(api_.quality(p.handle,rtt,loss)){Frame f{Op::Stats,id};put(f.bytes,rtt,4);put(f.bytes,loss,4);if(!emit(std::move(f)))return false;}}
        Message m;
        // Leave data in Steam until the combined broker/pipe backlog drains.
        bool receiveEmpty=false;
        for(unsigned n=0;n<16&&outputRoom();++n){
            if(!api_.receive(p.handle,m)){receiveEmpty=true;break;}
            if(m.handle!=p.handle||m.identity!=id||m.bytes.size()<9||m.bytes.size()>MaxPacket||m.bytes[0]!=0x4b||m.bytes[1]!=0x53||m.bytes[2]!=1||m.bytes[3]>=3||m.bytes[4]!=(m.reliable?1:0)||m.bytes[5]>1||m.bytes[6]||m.bytes[7])return fail("invalid-steam-envelope");
            if(m.bytes[5]==1){
                if(listener_ || id!=target_ || m.bytes.size()!=9 || m.bytes[3]!=0 || !m.reliable || !hostLeftReason(m.bytes[8]))return fail("invalid-session-end");
                if(!p.terminalReceived){p.terminalReceived=true;
                    api_.decision(p.handle,id,"session-end","host-left-received",allowed_.size(),p.flags);
                    if(!emit({Op::Disconnected,id,m.bytes[8]}))return false;}
                continue;
            }
            if(p.terminalReceived)continue;
            Frame f{Op::Data,id,0,m.bytes[3],m.reliable,{m.bytes.begin()+8,m.bytes.end()}};if(!emit(std::move(f)))return false;
        }
        if(p.remoteClosed&&outputRoom()&&(receiveEmpty||now-p.remoteClosedAt>=TerminalDrainMs)){
            const auto reason=p.closing?p.closeReason:p.remoteReason;
            closePeer(p.handle,id,p.closing,reason,receiveEmpty?"terminal-status-drained":"terminal-receive-drain-timeout",p.flags);
            const bool notified=p.terminalReceived;peers_.erase(id);
            if(!notified&&!emit({Op::Disconnected,id,reason}))return false;
        }
    }
    return true;
}
} // namespace kh2coop::steam
