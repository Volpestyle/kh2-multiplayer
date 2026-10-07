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
bool Broker::fail(const char* why){stop();failed_=true;out_.clear();Frame f{Op::Error};f.bytes.assign(why,why+std::char_traits<char>::length(why));out_.push_back(std::move(f));return false;}
void Broker::stop(){for(const auto& [id,p]:peers_){(void)id;api_.close(p.handle,false);}peers_.clear();if(listener_)api_.closeListener(listener_);listener_=0;configured_=false;allowed_.clear();target_=0;}
bool Broker::pop(Frame& f){if(out_.empty())return false;f=std::move(out_.front());out_.pop_front();return true;}
bool Broker::command(const Frame& f,std::uint64_t now){
    if(failed_)return false;
    lastCommand_=now;
    if(f.op==Op::Hello){
        if(identity_||f.peer||!f.bytes.empty())return fail("bad-hello");
        identity_=api_.readyIdentity();if(!validId(identity_))return fail("session-not-ready");
        return emit({Op::Ready,identity_});
    }
    if(!identity_||api_.readyIdentity()!=identity_)return fail("session-changed");
    if(f.op==Op::Ping)return f.bytes.empty()||fail("bad-ping");
    if(f.op==Op::Stop){stop();return true;}
    if(f.op==Op::Host){
        if(configured_||f.peer||f.bytes.empty()||f.bytes.size()>MaxPeers*8||f.bytes.size()%8)return fail("bad-host-allowlist");
        for(std::size_t i=0;i<f.bytes.size();i+=8){const auto id=get(f.bytes,i,8);if(!validId(id)||id==identity_||std::find(allowed_.begin(),allowed_.end(),id)!=allowed_.end())return fail("bad-host-allowlist");allowed_.push_back(id);}
        listener_=api_.listen();if(!listener_||!api_.iceOff(listener_,true))return fail("listen-ice-off-unverified");
        configured_=true;return emit({Op::Ready,identity_});
    }
    if(f.op==Op::Join){
        if(configured_||!validId(f.peer)||f.peer==identity_||!f.bytes.empty())return fail("bad-join");
        target_=f.peer;const auto h=api_.connect(target_);if(!h)return fail("connect-failed");
        try {peers_.emplace(target_,Peer{h,false,now});} catch(...) {api_.close(h,false);throw;}
        if(!api_.iceOff(h,false))return fail("connect-ice-off-unverified");
        configured_=true;return true;
    }
    const auto it=peers_.find(f.peer);if(it==peers_.end())return fail("unknown-peer");
    if(f.op==Op::Close){if(f.reason>9)return fail("bad-close-reason");api_.close(it->second.handle,true,f.reason);peers_.erase(it);return emit({Op::Disconnected,f.peer,f.reason});}
    if(f.op!=Op::Send||!it->second.connected||f.bytes.empty()||f.bytes.size()>MaxPacket-8||f.channel>=3)return fail("bad-send");
    std::vector<std::uint8_t> wire{0x4b,0x53,1,f.channel,static_cast<std::uint8_t>(f.reliable),0,0,0};wire.insert(wire.end(),f.bytes.begin(),f.bytes.end());
    return api_.send(it->second.handle,wire,f.reliable)||fail("steam-send-failed");
}
bool Broker::tick(std::uint64_t now){
    if(failed_)return false;
    if(!firstTick_)firstTick_=now;
    if(!identity_&&now-firstTick_>5000)return fail("ipc-hello-timeout");
    if(!api_.healthy())return fail("callback-overflow");
    if(identity_&&(api_.readyIdentity()!=identity_||now<lastCommand_||now-lastCommand_>5000))return fail("session-or-runtime-lost");
    Status s;
    for(unsigned n=0;n<64&&api_.nextStatus(s);++n){
        auto it=peers_.find(s.identity);
        if(s.state==1 && listener_ && s.listener==listener_){
            if(!s.authenticated||std::find(allowed_.begin(),allowed_.end(),s.identity)==allowed_.end()||it!=peers_.end()||peers_.size()>=MaxPeers||!api_.iceOff(s.handle,false)) {api_.close(s.handle,false);continue;}
            try {peers_.emplace(s.identity,Peer{s.handle,false,now});} catch(...) {api_.close(s.handle,false);throw;}
            if(!api_.accept(s.handle))return fail("accept-failed");
            continue;
        }
        if(it==peers_.end()||it->second.handle!=s.handle)continue; // late callback, never adopt it
        if(s.state==3){
            if(!s.authenticated||!s.relay||!api_.iceOff(s.handle,false))return fail("connection-not-authenticated-relay-only");
            if(!it->second.connected){it->second.connected=true;if(!emit({Op::Connected,s.identity}))return false;}
        } else if(s.state==0||s.state>=4){api_.close(s.handle,false);peers_.erase(it);if(!emit({Op::Disconnected,s.identity,s.reason}))return false;}
    }
    for(auto& [id,p]:peers_){
        if(!p.connected){if(now-p.started>15000)return fail("connection-timeout");continue;}
        if(now-p.lastStats>=1000){p.lastStats=now;std::uint32_t rtt=0,loss=0;
            if(api_.quality(p.handle,rtt,loss)){Frame f{Op::Stats,id};put(f.bytes,rtt,4);put(f.bytes,loss,4);if(!emit(std::move(f)))return false;}}
        Message m;
        for(unsigned n=0;n<16&&api_.receive(p.handle,m);++n){
            if(m.handle!=p.handle||m.identity!=id||m.bytes.size()<9||m.bytes.size()>MaxPacket||m.bytes[0]!=0x4b||m.bytes[1]!=0x53||m.bytes[2]!=1||m.bytes[3]>=3||m.bytes[4]!=(m.reliable?1:0)||m.bytes[5]||m.bytes[6]||m.bytes[7])return fail("invalid-steam-envelope");
            Frame f{Op::Data,id,0,m.bytes[3],m.reliable,{m.bytes.begin()+8,m.bytes.end()}};if(!emit(std::move(f)))return false;
        }
    }
    return true;
}
} // namespace kh2coop::steam
