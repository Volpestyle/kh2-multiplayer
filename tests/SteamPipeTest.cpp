#include "kh2coop/SteamPipe.hpp"
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <iostream>
#include <string>
using namespace kh2coop::steam;
int main(int argc,char** argv){
    if(argc==3&&std::string(argv[1])=="--owned-pipe-child"){
        Pipe p;if(!p.attach(static_cast<std::uint32_t>(std::stoul(argv[2])),3000))return 2;
        Frame f{Op::Send,76561198000000002ull,0,2,true,std::vector<std::uint8_t>(MaxPacket,42)};
        if(!p.send(f))return 3;const auto end=GetTickCount64()+3000;
        bool echoed=false;
        while(GetTickCount64()<end){if(!p.pump())return 4;Frame r;if(p.receive(r)){echoed=r.bytes==f.bytes&&r.peer==f.peer&&r.reliable;break;}Sleep(1);}
        if(!echoed)return 5;
        const auto packet=[](unsigned n){return Frame{Op::Send,76561198000000002ull,0,2,true,{static_cast<std::uint8_t>(n)}};};
        unsigned sent=0,received=0;
        while(sent<MaxQueue){if(!p.send(packet(sent++)))return 6;}
        if(p.send(packet(sent))||!p.connected()||p.queued()!=MaxQueue)return 7;
        const auto burstEnd=GetTickCount64()+3000;
        while(GetTickCount64()<burstEnd&&received<192){
            if(!p.pump())return 8;
            while(sent<192&&p.queued()<MaxQueue){if(!p.send(packet(sent++)))return 9;}
            Frame r;while(p.receive(r)){if(r.bytes!=packet(received).bytes||r.peer!=f.peer||!r.reliable)return 12;++received;}
            Sleep(1);
        }
        return received==192?0:13;
    }
    int failures=0;const auto check=[&](bool b,const char* t){std::cout<<(b?"PASS ":"FAIL ")<<t<<'\n';if(!b)++failures;};
    Pipe server;check(server.serve(),"current-user ACL first-instance local server");Pipe duplicate;check(!duplicate.serve(),"duplicate server cannot take endpoint");
    Pipe wrong;check(!wrong.attach(GetCurrentProcessId(),0),"self PID refused");check(!wrong.attach(0,0),"zero PID refused");
    wchar_t exe[32768]{};if(!GetModuleFileNameW(nullptr,exe,32768))return 10;
    std::wstring cmd=L"\""+std::wstring(exe)+L"\" --owned-pipe-child "+std::to_wstring(GetCurrentProcessId());
    STARTUPINFOW si{};si.cb=sizeof(si);PROCESS_INFORMATION pi{};
    if(!CreateProcessW(exe,cmd.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&si,&pi))return 11;
    CloseHandle(pi.hThread);bool echo=false,lost=false;unsigned burst=0;bool ordered=true;const auto end=GetTickCount64()+5000;
    while(GetTickCount64()<end){if(!server.pump()){lost=true;break;}Frame f;
        for(unsigned work=0;work<16&&server.receive(f);++work){
            if(f.bytes.size()==MaxPacket)echo=f.bytes.front()==42&&f.bytes.back()==42&&f.channel==2;
            else {ordered=ordered&&f.bytes.size()==1&&f.bytes.front()==burst;++burst;}
            if(!server.send(f))return 14;
        }Sleep(1);}
    check(burst==192&&ordered,"192-frame burst survives full local queue and preserves exact sequence");
    check(echo,"full bounded binary message crossed OS PID-verified pipe both ways");
    check(WaitForSingleObject(pi.hProcess,1000)==WAIT_OBJECT_0,"owned helper finished normally");DWORD exit=99;GetExitCodeProcess(pi.hProcess,&exit);CloseHandle(pi.hProcess);
    std::cout<<"childExit="<<exit<<" burstReceived="<<burst<<'\n';
    check(exit==0,"child exact payload receipt");check(lost,"retained peer exit breaks IPC");
    server.close();check(server.serve(),"closed endpoint can be recreated with fresh sequences");server.close();
    std::cout<<"failures="<<failures<<'\n';return failures?1:0;
}
#else
int main(){return 0;}
#endif
