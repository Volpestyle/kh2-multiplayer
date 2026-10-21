#include "ScriptCamera.hpp"
#include <cstdio>
using namespace kh2coop::inject::scriptmirror::rules;
int main() {
    const EntityCameraIdentity opened{0x1000,0x2080,0x3000,0x6C000B};
    unsigned failed=0;
    const auto check=[&](bool ok,const char* label){std::printf("%s: %s\n",ok?"PASS":"FAIL",label);if(!ok)++failed;};
    check(SameEntityCamera(opened,opened),"exact registered native kind11 source and camera payload remain one occurrence");
    for(unsigned field=0;field<4;++field) {
        auto changed=opened;
        if(field==0)++changed.context;
        if(field==1)++changed.sourceAnchor;
        if(field==2)++changed.asset;
        if(field==3)changed.kindRecord=0x6C000A; // another native task using the same storage
        check(!SameEntityCamera(opened,changed),"context reuse, source, asset or native task replacement ends camera identity");
    }
    auto story=opened;story.kindRecord=0x00000001;
    check(!SameEntityCamera(opened,story),"a story event cannot inherit an entity camera exception");
    check(!SameEntityCamera({},opened) && !SameEntityCamera(opened,{}),"unregistered or unavailable task never grants gameplay");
    auto malformed=opened;malformed.asset=0;
    check(!SameEntityCamera(malformed,malformed),"identical missing payloads are not a registered camera");
    return failed?1:0;
}
