#pragma once
#include "kh2coop/Codec.hpp"
#include <functional>
#include <sstream>
#include <string>
#include <utility>
namespace kh2coop {
// Owner-thread, opt-in diagnostics. Sink returns true only after an actual flush.
// No sink means no counters, formatting, hashing or synthetic evidence allocation.
using CausalSink = std::function<bool(const std::string&)>;
struct CausalStream {
    static constexpr std::uint64_t Limit = 65536;
    std::uint64_t highWater{}, flushed{}, dropped{}, seals{};
    bool unavailable{};
    void gap() noexcept { if(dropped!=UINT64_MAX)++dropped; unavailable=true; }
    template<class Format> void emit(const CausalSink& sink, const char* family, Format&& format) noexcept {
        if(!sink)return;
        if(highWater==Limit){gap();return;}
        const auto seq=++highWater;
        try { std::ostringstream out;out<<'['<<family<<"] schema=1 seq="<<seq;
            format(out);if(!out.good()){gap();return;}
            if(sink(out.str()))flushed=seq;else gap();
        } catch(...) { gap(); }
    }
    void seal(const CausalSink& sink,const char* family,const char* action) noexcept {
        if(!sink)return;
        if(seals==UINT64_MAX){gap();return;}
        const auto seq=++seals;
        try { std::ostringstream out;out<<'['<<family<<"-seal] schema=1 action="<<action
            <<" sealSeq="<<seq<<" highWater="<<highWater<<" flushedHighWater="<<flushed
            <<" dropped="<<dropped<<" unavailable="<<unavailable;
            if(!out.good()){gap();return;}
            if(!sink(out.str()))gap();
        } catch(...) { gap(); }
    }
};
inline void causalRoom(std::ostream& out,const RoomTransition* r) {
    out<<" roomAvailable="<<(r!=nullptr)<<" roomEpoch="<<(r?r->epoch:0)
       <<" world="<<(r?r->worldId:0)<<" room="<<(r?r->roomId:0)<<" door="<<(r?unsigned(r->door):0)
       <<" map="<<(r?r->mapProgram:0)<<" battle="<<(r?r->battleProgram:0)<<" event="<<(r?r->eventProgram:0);
}
inline std::string causalSha(const std::vector<std::uint8_t>& bytes) {
    static constexpr char hex[]="0123456789abcdef";std::string out;
    for(auto b:desyncSha256(bytes)){out+=hex[b>>4];out+=hex[b&15];}return out;
}
enum class ResyncRequestOrigin { Unspecified, AutomaticNotice, OperatorMailbox, DirectRequest };
inline const char* causalOrigin(ResyncRequestOrigin o) {
    switch(o){case ResyncRequestOrigin::AutomaticNotice:return "automatic-notice";
    case ResyncRequestOrigin::OperatorMailbox:return "operator-mailbox";
    case ResyncRequestOrigin::DirectRequest:return "direct-request";
    default:return "unspecified";}
}
struct RequestSubmissionObservation { bool deadlineAvailable{};std::uint64_t startedMs{},deadlineMs{}; };
} // namespace kh2coop
