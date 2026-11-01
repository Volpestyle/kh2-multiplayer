// VUH-2916: the exact-boss victory registry (kh2coop/BossVictories.hpp). Thorn's row is pinned to the
// values the mission mirror and codec key on today, and to the rows it shares with bossarena and eventscenes.
#include "kh2coop/BossArenas.hpp"
#include "kh2coop/BossVictories.hpp"
#include "kh2coop/EventScenes.hpp"
#include <cstdio>
#include <string_view>

namespace V = kh2coop::bossvictory;
unsigned failures=0,checks=0;
void check(bool value,const char* name) { ++checks;if(!value){++failures;std::printf("FAIL %s\n",name);} }

// The mission mirror compares a row's missionFile as a C string (SameFile), so each must end in a NUL.
static_assert(V::TwilightThorn.missionFile.data()[V::TwilightThorn.missionFile.size()]=='\0');
static_assert(V::ByMission("TT34_MS304.bar")==&V::TwilightThorn);

int main() {
    const auto& t=V::TwilightThorn;
    check(V::Victories.size()==1 && &t==&V::Victories[0],"Thorn is the only victory row");
    check(t.location==std::array<std::uint16_t,6>{2,34,0,157,157,157},"Thorn's location is ThornLocation's");
    check(t.bossObjectId==307 && t.bossObjectType==12,"Thorn's boss is object 307 type 12 (ThornDeathFocusReady)");
    check(t.missionFile=="TT34_MS304.bar" && t.missionId==181,"Thorn's mission is TT34_MS304, id 181");
    check(t.endTable==4 && t.endTrap==16 && t.endArg==0xFFFFFFFFu,"Thorn's mission ends with 4/16 and argument -1");
    check(t.eventId==0x353133 && t.eventFlags==0x12,"Thorn's victory event is 315, flags 0x12");
    check(t.name=="tt34-twilight-thorn","Thorn's row name");

    const auto* arena=kh2coop::bossarena::ById(1);
    check(arena && arena->location==t.location && arena->bossObjectId==t.bossObjectId &&
          arena->bossObjectType==t.bossObjectType && arena->missionFile==t.missionFile,
          "Thorn's victory row agrees with its sit-out arena row");
    const auto& event=kh2coop::eventscenes::TwilightThornVictory;
    check(event.world==t.location[0] && event.room==t.location[1] && event.eventProgram==t.location[5] &&
          event.id==t.eventId && event.flags==t.eventFlags,"Thorn's victory row agrees with its in-field event row");

    check(V::ByMission(t.missionFile)==&t,"Thorn's mission file finds Thorn's row");
    for(std::string_view unknown : {"","TT34_MS304","TT34_MS304.ba","TT34_MS304.barX","tt34_ms304.bar",
                                    "msn/us/TT34_MS304.bar","TT05_MS404.bar","TT20_MS603.bar","MU01_MS102.bar"})
        check(!V::ByMission(unknown),"an unknown mission file finds no victory");
    for(std::size_t i=0;i<V::Victories.size();++i)
        for(std::size_t j=i+1;j<V::Victories.size();++j)
            check(V::Victories[i].missionFile!=V::Victories[j].missionFile && V::Victories[i].name!=V::Victories[j].name,
                  "victory rows have distinct mission files and names");

    std::printf("%s (%u/%u)\n",failures?"FAILED":"ALL PASSED",checks-failures,checks);
    return failures?1:0;
}
