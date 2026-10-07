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
        while(GetTickCount64()<end){if(!p.pump())return 4;Frame r;if(p.receive(r))return r.bytes==f.bytes&&r.peer==f.peer&&r.reliable?0:5;Sleep(1);}return 6;
    }
    int failures=0;const auto check=[&](bool b,const char* t){std::cout<<(b?"PASS ":"FAIL ")<<t<<'\n';if(!b)++failures;};
    Pipe server;check(server.serve(),"current-user ACL first-instance local server");Pipe duplicate;check(!duplicate.serve(),"duplicate server cannot take endpoint");
    Pipe wrong;check(!wrong.attach(GetCurrentProcessId(),0),"self PID refused");check(!wrong.attach(0,0),"zero PID refused");
    wchar_t exe[32768]{};if(!GetModuleFileNameW(nullptr,exe,32768))return 10;
    std::wstring cmd=L"\""+std::wstring(exe)+L"\" --owned-pipe-child "+std::to_wstring(GetCurrentProcessId());
    STARTUPINFOW si{};si.cb=sizeof(si);PROCESS_INFORMATION pi{};
    if(!CreateProcessW(exe,cmd.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&si,&pi))return 11;
    CloseHandle(pi.hThread);bool echo=false,lost=false;const auto end=GetTickCount64()+5000;
    while(GetTickCount64()<end){if(!server.pump()){lost=true;break;}Frame f;if(server.receive(f)){echo=f.bytes.size()==MaxPacket&&f.bytes.front()==42&&f.bytes.back()==42&&f.channel==2;server.send(f);}Sleep(1);}
    check(echo,"full bounded binary message crossed OS PID-verified pipe both ways");
    check(WaitForSingleObject(pi.hProcess,1000)==WAIT_OBJECT_0,"owned helper finished normally");DWORD exit=99;GetExitCodeProcess(pi.hProcess,&exit);CloseHandle(pi.hProcess);
    check(exit==0,"child exact payload receipt");check(lost,"retained peer exit breaks IPC");
    server.close();check(server.serve(),"closed endpoint can be recreated with fresh sequences");server.close();
    std::cout<<"failures="<<failures<<'\n';return failures?1:0;
}
#else
int main(){return 0;}
#endif
