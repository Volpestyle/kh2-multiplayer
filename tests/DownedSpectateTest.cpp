#include "DownedSpectate.hpp"
#include <cstdio>
using namespace kh2coop::inject::spectate;
static unsigned checks{}, failures{};
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); } } while (0)
static Facts Good(unsigned local = 0) {
    Facts f; f.eligible = true; f.localSlot = static_cast<std::uint8_t>(local);
    f.generation = 4; f.transition = 8; f.load = 9;
    for (unsigned i = 0; i < 3; ++i) f.targets[i] = {100+i, 1000+i, 200+i, 300+i, 400+i, true};
    return f;
}
int main() {
    { const RemoteFacts good{true,true,true,true,30,0,10}; CHECK(EligibleRemote(good));
      for (unsigned bit=0;bit<8;++bit) { auto f=good; f.flags=static_cast<std::uint8_t>(1U<<bit); CHECK(EligibleRemote(f)==(bit!=1 && bit!=2 && bit!=3)); }
      {auto f=good;f.age=31;CHECK(!EligibleRemote(f));}
      {auto f=good;f.hp=0;CHECK(!EligibleRemote(f));}
      {auto f=good;f.hp=-1;CHECK(!EligibleRemote(f));}
      {auto f=good;f.active=false;CHECK(!EligibleRemote(f));}
      {auto f=good;f.sameRoom=false;CHECK(!EligibleRemote(f));}
      {auto f=good;f.finite=false;CHECK(!EligibleRemote(f));}
      {auto f=good;f.nativeBound=false;CHECK(!EligibleRemote(f));} }
    for (unsigned local = 0; local < 3; ++local) {
        auto f = Good(local); State s;
        CHECK(Step(s,f) == (local+1)%3);
        for (unsigned n = 0; n < 9; ++n) {
            const auto before = s.slot;
            Input(s,true,false); Input(s,true,true);
            CHECK(Step(s,f) == (before+1)%3);
            for (unsigned hold = 0; hold < 6; ++hold) { Input(s,true,true); CHECK(Step(s,f) == (before+1)%3); }
        }
        // Entry with the button already held does not cycle. Neutral re-arms it.
        Release(s); Input(s,true,true); const auto first = Step(s,f);
        CHECK(Step(s,f) == first); Input(s,true,true); CHECK(Step(s,f) == first);
        Input(s,true,false); Input(s,true,true); CHECK(Step(s,f) == (first+1)%3);
    }
    for (unsigned mask = 0; mask < 8; ++mask) {
        auto f = Good(); for (unsigned i=0;i<3;++i) f.targets[i].valid = (mask & (1U<<i)) != 0;
        State s; const auto slot = Step(s,f); CHECK(slot == 255 || f.targets[slot].valid);
        CHECK((slot == 255) == (mask == 0));
        for (unsigned n=0;n<6;++n) { Input(s,true,false); Input(s,true,true); const auto next=Step(s,f); CHECK(next==255 || f.targets[next].valid); }
    }
    // Death, leave, stale pose, held pose and failed native identity all project to invalid.
    for (unsigned reason=0;reason<5;++reason) { State s; auto f=Good(); CHECK(Step(s,f)==1); f.targets[1].valid=false; CHECK(Step(s,f)==2); }
    // Recycled pointer / reconnect / changed objentry / status / handle cannot retain selection.
    for (unsigned field=0;field<5;++field) {
        State s; auto f=Good(); CHECK(Step(s,f)==1);
        if(field==0) ++f.targets[1].actor;
        if(field==1) ++f.targets[1].connection;
        if(field==2) ++f.targets[1].objentry;
        if(field==3) ++f.targets[1].status;
        if(field==4) ++f.targets[1].handle;
        CHECK(Step(s,f)==2);
    }
    // Alive, refused, event/menu, wrong thread/camera/mode, disabled and lease loss all release.
    for(unsigned reason=0;reason<10;++reason) { State s; auto f=Good(); CHECK(Step(s,f)==1); Input(s,true,false); Input(s,true,true); f.eligible=false; CHECK(Step(s,f)==255); CHECK(s.slot==255 && !s.pending && !s.armed); }
    for(unsigned field=0;field<3;++field) { State s; auto f=Good(); CHECK(Step(s,f)==1); if(field==0)++f.generation; if(field==1)++f.transition; if(field==2)++f.load; CHECK(Step(s,f)==255); CHECK(s.slot==255); }
    { State s; auto f=Good(); CHECK(Step(s,f)==1); /* episode remint is deliberately not an input */ CHECK(Step(s,f)==1); }
    { State s; auto f=Good(); f.localSlot=255; CHECK(Step(s,f)==255); f=Good(); f.generation=0; CHECK(Step(s,f)==255); }
    { State s; Input(s,true,false); Input(s,true,true); Input(s,false,true); CHECK(!s.pending && !s.armed); Input(s,true,true); CHECK(!s.pending); Input(s,true,false); Input(s,true,true); CHECK(s.pending); }
    CHECK(RestoreOwned(10,10,true,true)); CHECK(!RestoreOwned(11,10,true,true));
    CHECK(!RestoreOwned(10,10,false,true)); CHECK(!RestoreOwned(10,10,true,false)); CHECK(!RestoreOwned(0,0,true,true));
    std::printf("downed spectate: %u/%u controls PASS\n",checks-failures,checks); return failures ? 1 : 0;
}
