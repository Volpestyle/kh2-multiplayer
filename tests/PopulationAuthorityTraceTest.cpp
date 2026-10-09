#include "PopulationAuthorityTrace.hpp"
#include <cstdlib>
#include <iostream>

namespace a=kh2coop::inject::authoritytrace;
static void Check(bool value,const char* name) {if (!value) {std::cerr<<name<<'\n';std::abort();}}
static a::Identity Owner() {return {42,0,0x100000,true,false};}
int main() {
    const a::Snapshot snapshot{};
    {
        a::Recorder<2,3,16> r;
        Check(!r.Enter(a::Kind::Update,Owner(),0,snapshot,100).entered,"default off produces no event");
        a::Event e{};Check(!r.Pop(e),"default off queue empty");
        r.Start(100);
        const auto outer=r.Enter(a::Kind::Update,Owner(),11,snapshot,101);
        const auto inner=r.Enter(a::Kind::Fixed,Owner(),12,snapshot,102);
        Check(outer.entered && inner.entered && inner.parent==outer.invocation,"actual nested parent retained");
        r.Exit(inner,a::Kind::Fixed,Owner(),snapshot,103,0xdead,true);
        r.Exit(outer,a::Kind::Update,Owner(),snapshot,104,0,true);
        Check(r.observedOpen()==0 && r.loss()==a::Loss::None,"native return closes observed spans only");
        Check(r.Pop(e) && !e.exit && e.invocation==outer.invocation,"entry1");
        Check(r.Pop(e) && !e.exit && e.parent==outer.invocation,"entry2");
        Check(r.Pop(e) && e.exit && e.result==0xdead && e.returned,"raw pointer result retained");
        Check(r.Pop(e) && e.exit && e.invocation==outer.invocation,"outer terminal");
    }
    {
        a::Recorder<2,3,16> r;r.Start(100);
        const auto first=r.Enter(a::Kind::Update,Owner(),1,snapshot,101);
        auto fiber=Owner();fiber.isFiber=true;fiber.fiber=0x200000;
        const auto other=r.Enter(a::Kind::Fixed,fiber,2,snapshot,102);
        Check(other.entered && other.parent==0 && other.context!=first.context,"same thread foreign fiber cannot inherit parent");
        r.Exit(other,a::Kind::Fixed,fiber,snapshot,103,0,false);
        a::Event e{};bool unwind=false;
        while(r.Pop(e)) if(e.exit && e.invocation==other.invocation) unwind=e.unwound && !e.returned;
        Check(unwind,"native unwind observation retained");
        r.Exit(first,a::Kind::Update,Owner(),snapshot,104,0,true);
    }
    {
        a::Recorder<2,3,16> r;r.Start(100);
        const auto token=r.Enter(a::Kind::Update,Owner(),0,snapshot,101);
        auto changed=Owner();changed.stackHigh+=4096;
        r.Exit(token,a::Kind::Update,changed,snapshot,102,0,true);
        Check(r.loss()==a::Loss::ReturnMismatch && !r.enabled(),"changed return context stops coverage");
        r.Start(103);Check(!r.enabled(),"stopped recorder cannot be rearmed");
    }
    {
        a::Recorder<1,1,16> r;r.Start(100);
        const auto token=r.Enter(a::Kind::Update,Owner(),0,snapshot,101);
        Check(!r.Enter(a::Kind::Fixed,Owner(),0,snapshot,102).entered && r.loss()==a::Loss::Depth,"depth exhaustion stops admission");
        r.Exit(token,a::Kind::Update,Owner(),snapshot,103,0,true);
        Check(r.observedOpen()==0,"terminal after loss still closes existing span");
    }
    {
        a::Recorder<1,3,16> r;r.Start(100);
        Check(r.Enter(a::Kind::Update,Owner(),0,snapshot,101).entered,"owner context");
        auto other=Owner();++other.thread;
        Check(!r.Enter(a::Kind::Fixed,other,0,snapshot,102).entered && r.loss()==a::Loss::Context,"context exhaustion does not evict live identity");
    }
    {
        a::Recorder<2,3,1> r;r.Start(100);
        const auto token=r.Enter(a::Kind::Update,Owner(),0,snapshot,101);
        r.Exit(token,a::Kind::Update,Owner(),snapshot,102,0,true);
        Check(r.loss()==a::Loss::Queue && !r.enabled(),"queue overflow remains visible");
    }
    {
        a::Recorder<2,3,16> r;r.Start(100);
        const auto token=r.Enter(a::Kind::Update,Owner(),0,snapshot,101);
        r.Exit(token,a::Kind::Update,Owner(),snapshot,180100,0,true);
        Check(r.loss()==a::Loss::Deadline && !r.enabled(),"return crossing deadline stops coverage");
        a::Event e{};Check(r.Pop(e) && r.Pop(e) && e.exit,"deadline retains terminal receipt");
    }
    std::cout<<"Population authority recorder controls PASS (authority remains UNKNOWN)\n";
}
