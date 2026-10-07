#include "kh2coop/SteamPipe.hpp"
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <sddl.h>
#include <array>
#include <string>

namespace kh2coop::steam {
namespace {
std::uint64_t creation(HANDLE h){FILETIME c{},e{},k{},u{};if(!GetProcessTimes(h,&c,&e,&k,&u))return 0;return (std::uint64_t(c.dwHighDateTime)<<32)|c.dwLowDateTime;}
std::wstring name(DWORD pid,std::uint64_t born){return L"\\\\.\\pipe\\kh2coop-steam-v1-"+std::to_wstring(pid)+L"-"+std::to_wstring(born);}
std::wstring ownerAcl(){
    HANDLE t=nullptr;if(!OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&t))return {};
    DWORD n=0;GetTokenInformation(t,TokenUser,nullptr,0,&n);std::vector<std::uint8_t>b(n);
    const bool ok=n&&GetTokenInformation(t,TokenUser,b.data(),n,&n);CloseHandle(t);if(!ok)return {};
    LPWSTR sid=nullptr;if(!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(b.data())->User.Sid,&sid))return {};
    std::wstring acl=L"D:P(A;;GA;;;";acl+=sid;acl+=L")";LocalFree(sid);return acl;
}
}
struct Pipe::State {
    HANDLE pipe=INVALID_HANDLE_VALUE,peer=nullptr;
    OVERLAPPED accept{},read{},write{};
    bool accepting=false,reading=false,writing=false,connected=false;
    std::uint64_t tx=0,rx=0;
    std::array<std::uint8_t,MaxPacket+32> readBuffer{};
    std::deque<std::vector<std::uint8_t>> outgoing;
    std::deque<Frame> incoming;
    State(){accept.hEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr);read.hEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr);write.hEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr);}
    ~State(){
        if(pipe!=INVALID_HANDLE_VALUE){CancelIoEx(pipe,nullptr);DWORD n=0;
            // Completion owns OVERLAPPED/buffers until cancellation is reaped.
            if(accepting)GetOverlappedResult(pipe,&accept,&n,TRUE);
            if(reading)GetOverlappedResult(pipe,&read,&n,TRUE);
            if(writing)GetOverlappedResult(pipe,&write,&n,TRUE);
            CloseHandle(pipe);
        }
        if(peer)CloseHandle(peer);
        for(auto h:{accept.hEvent,read.hEvent,write.hEvent})if(h)CloseHandle(h);
    }
    bool events() const{return accept.hEvent&&read.hEvent&&write.hEvent;}
    bool adoptClient(){ULONG pid=0;if(!GetNamedPipeClientProcessId(pipe,&pid)||!pid||pid==GetCurrentProcessId())return false;
        peer=OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);
        if(!peer||!creation(peer)||WaitForSingleObject(peer,0)!=WAIT_TIMEOUT)return false;
        connected=true;return true;
    }
};
Pipe::Pipe()=default;Pipe::~Pipe()=default;
void Pipe::close(){s_.reset();}
bool Pipe::connected() const{return s_&&s_->connected;}
bool Pipe::serve(){
    close();auto s=std::make_unique<State>();if(!s->events())return false;
    const auto acl=ownerAcl();PSECURITY_DESCRIPTOR sd=nullptr;
    if(acl.empty()||!ConvertStringSecurityDescriptorToSecurityDescriptorW(acl.c_str(),SDDL_REVISION_1,&sd,nullptr))return false;
    SECURITY_ATTRIBUTES sa{sizeof(sa),sd,FALSE};const auto born=creation(GetCurrentProcess());
    if(born)s->pipe=CreateNamedPipeW(name(GetCurrentProcessId(),born).c_str(),PIPE_ACCESS_DUPLEX|FILE_FLAG_OVERLAPPED|FILE_FLAG_FIRST_PIPE_INSTANCE,
        PIPE_TYPE_MESSAGE|PIPE_READMODE_MESSAGE|PIPE_WAIT|PIPE_REJECT_REMOTE_CLIENTS,1,static_cast<DWORD>(MaxPacket+32),static_cast<DWORD>(MaxPacket+32),0,&sa);
    LocalFree(sd);if(s->pipe==INVALID_HANDLE_VALUE)return false;
    const bool done=ConnectNamedPipe(s->pipe,&s->accept)!=FALSE;const auto err=done?ERROR_SUCCESS:GetLastError();
    if(err==ERROR_IO_PENDING)s->accepting=true;
    else if(err==ERROR_PIPE_CONNECTED||done){if(!s->adoptClient())return false;}
    else return false;
    s_=std::move(s);return true;
}
bool Pipe::attach(std::uint32_t pid,std::uint32_t timeoutMs){
    close();if(!pid||pid==GetCurrentProcessId()||timeoutMs>5000)return false;
    auto s=std::make_unique<State>();if(!s->events())return false;
    s->peer=OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);if(!s->peer)return false;
    const auto born=creation(s->peer);if(!born)return false;
    const auto path=name(pid,born);const auto end=GetTickCount64()+timeoutMs;
    do {s->pipe=CreateFileW(path.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,FILE_FLAG_OVERLAPPED,nullptr);
        if(s->pipe!=INVALID_HANDLE_VALUE)break;
        if(WaitForSingleObject(s->peer,0)!=WAIT_TIMEOUT)return false;
        if(GetTickCount64()>=end)return false;
        Sleep(10);
    }while(true);
    ULONG actual=0;DWORD mode=PIPE_READMODE_MESSAGE;
    if(!GetNamedPipeServerProcessId(s->pipe,&actual)||actual!=pid||WaitForSingleObject(s->peer,0)!=WAIT_TIMEOUT||!SetNamedPipeHandleState(s->pipe,&mode,nullptr,nullptr))return false;
    s->connected=true;s_=std::move(s);return true;
}
bool Pipe::send(const Frame& f){
    if(!connected()||s_->outgoing.size()>=MaxQueue||s_->tx==UINT64_MAX){close();return false;}
    auto b=encode(f,++s_->tx);if(b.empty()){close();return false;}s_->outgoing.push_back(std::move(b));return true;
}
bool Pipe::receive(Frame& f){if(!s_||s_->incoming.empty())return false;f=std::move(s_->incoming.front());s_->incoming.pop_front();return true;}
bool Pipe::pump(){
    if(!s_)return false;auto& s=*s_;
    const auto fail=[&]{close();return false;};DWORD n=0;
    if(s.accepting){if(!GetOverlappedResult(s.pipe,&s.accept,&n,FALSE)){if(GetLastError()==ERROR_IO_INCOMPLETE)return true;return fail();}s.accepting=false;if(!s.adoptClient())return fail();}
    if(!s.connected||WaitForSingleObject(s.peer,0)!=WAIT_TIMEOUT)return fail();
    for(unsigned work=0;work<16;++work){
        bool done=false;
        if(s.reading){if(!GetOverlappedResult(s.pipe,&s.read,&n,FALSE)){if(GetLastError()!=ERROR_IO_INCOMPLETE)return fail();break;}s.reading=false;done=true;}
        else {ResetEvent(s.read.hEvent);if(ReadFile(s.pipe,s.readBuffer.data(),static_cast<DWORD>(s.readBuffer.size()),&n,&s.read))done=true;
            else {if(GetLastError()!=ERROR_IO_PENDING)return fail();s.reading=true;break;}}
        if(done){Frame f;if(s.rx==UINT64_MAX||!decode(std::span(s.readBuffer.data(),n),++s.rx,f)||s.incoming.size()>=MaxQueue)return fail();s.incoming.push_back(std::move(f));}
    }
    for(unsigned work=0;work<16&&!s.outgoing.empty();++work){
        if(s.writing){if(!GetOverlappedResult(s.pipe,&s.write,&n,FALSE)){if(GetLastError()!=ERROR_IO_INCOMPLETE)return fail();break;}s.writing=false;}
        else {ResetEvent(s.write.hEvent);const auto& b=s.outgoing.front();if(!WriteFile(s.pipe,b.data(),static_cast<DWORD>(b.size()),&n,&s.write)){
                if(GetLastError()!=ERROR_IO_PENDING)return fail();s.writing=true;break;}}
        if(n!=s.outgoing.front().size())return fail();s.outgoing.pop_front();
    }
    return true;
}
} // namespace kh2coop::steam
#else
namespace kh2coop::steam {
struct Pipe::State{};Pipe::Pipe()=default;Pipe::~Pipe()=default;
bool Pipe::serve(){return false;}bool Pipe::attach(std::uint32_t,std::uint32_t){return false;}
bool Pipe::pump(){return false;}bool Pipe::connected()const{return false;}
bool Pipe::send(const Frame&){return false;}bool Pipe::receive(Frame&){return false;}void Pipe::close(){}
}
#endif
