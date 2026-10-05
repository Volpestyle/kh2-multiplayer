#include "kh2coop/CausalDiagnostics.hpp"
#include <iostream>
#include <locale>
#include <vector>
using namespace kh2coop;
int checks=0, failures=0;
void check(bool yes,const char* label){++checks;failures+=!yes;std::cout<<(yes?"PASS ":"FAIL ")<<label<<'\n';}
struct BadNumber final : std::num_put<char> {
    iter_type do_put(iter_type out,std::ios_base& io,char_type,unsigned long long) const override {
        static_cast<std::ios&>(io).setstate(std::ios::badbit);return out;
    }
    iter_type do_put(iter_type out,std::ios_base& io,char_type,unsigned long) const override {
        static_cast<std::ios&>(io).setstate(std::ios::failbit);return out;
    }
};
int main(){
    std::vector<std::string> rows;
    const CausalSink sink=[&](const std::string& row){rows.push_back(row);return true;};
    CausalStream stream;bool formatted=false;
    stream.emit({},"control",[&](auto&){formatted=true;});stream.seal({},"control","quiet");
    check(!formatted&&!stream.highWater&&!stream.flushed&&!stream.dropped&&!stream.seals,"no sink causes no formatting or diagnostic state");
    for(auto bits:{std::ios::badbit,std::ios::failbit}){
        const auto before=rows.size(),lost=stream.dropped;
        stream.emit(sink,"control",[&](auto& out){out<<" fieldA=1";out.setstate(bits);out<<" fieldB=2";});
        check(rows.size()==before&&stream.dropped==lost+1&&stream.unavailable&&!stream.flushed,"nonthrowing event stream failure never reaches sink; sticky gap recorded");
    }
    const auto lost=stream.dropped;
    stream.emit(sink,"control",[](auto& out){out<<" complete=1";});
    stream.seal(sink,"control","after-error");
    check(stream.highWater==3&&stream.flushed==3&&stream.dropped==lost&&stream.unavailable,"later success cannot erase earlier event stream loss");
    const auto count=rows.size(),seals=stream.seals;
    const auto oldLocale=std::locale();
    std::locale::global(std::locale(oldLocale,new BadNumber));
    stream.seal(sink,"control","format-failure");
    std::locale::global(oldLocale);
    check(rows.size()==count&&stream.seals==seals+1&&stream.dropped==lost+1,"nonthrowing seal numeric formatting failure never reaches sink; sticky gap recorded");
    stream.emit([](const auto&)->bool{throw 7;},"control",[](auto&){});
    check(stream.dropped==lost+2&&stream.unavailable,"throwing sink is contained with sticky loss");
    stream.emit([](const auto&){return false;},"control",[](auto&){});
    check(stream.dropped==lost+3&&stream.unavailable,"failed flush is contained with sticky loss");
    stream.emit(sink,"control",[](auto&){throw 9;});
    check(stream.dropped==lost+4&&stream.unavailable,"throwing formatter is contained with sticky loss");
    stream.highWater=CausalStream::Limit;stream.emit(sink,"control",[](auto&){});
    check(stream.highWater==CausalStream::Limit&&stream.dropped==lost+5,"event limit cannot wrap");
    stream.seals=UINT64_MAX;stream.seal(sink,"control","limit");
    check(stream.seals==UINT64_MAX&&stream.dropped==lost+6,"seal limit cannot wrap");
    std::cout<<"checks="<<checks<<" failures="<<failures<<'\n';return failures?1:0;
}
