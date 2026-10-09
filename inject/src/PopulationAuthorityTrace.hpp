#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace kh2coop::inject::authoritytrace {
// Allocation-free observation storage. No FLS get/set or native stack address
// is retained. Sampled identities never establish uninterrupted execution.
enum class Kind : std::uint8_t { Constructor, Initialize, Update, Fixed, Generated,
    Teardown, Removal, Disposal, Death, DeathBookkeeping, Count, Factory };
enum class Loss : unsigned { None, Context, Depth, Contention, Queue, Serial, Deadline, ReturnMismatch, Install };
struct Identity {
    std::uint32_t thread{};
    std::uintptr_t fiber{}, stackHigh{};
    bool valid{}, isFiber{};
    bool operator==(const Identity&) const = default;
};
struct Snapshot {
    std::uintptr_t controller{}, actor{}, objectEntry{}, status{}, actorController{}, actorRecord{}, header{}, record{};
    std::uint32_t load{}, transition{}, initialCount{}, currentCount{}, flags{}, validMask{};
    std::uint32_t rawObjectId{}, yawBits{};
    std::uint16_t nativeId{};
    std::array<std::uint8_t,10> location{};
    std::array<std::uint8_t,64> controllerBytes{}, recordBytes{};
    std::array<std::uint8_t,44> headerBytes{};
    std::array<std::array<std::uint8_t,64>,5> definitionRecords{};
    std::array<std::uint8_t,16> pointBytes{};
};
struct Event {
    std::uint64_t sequence{}, invocation{}, parent{}, context{}, milliseconds{}, result{};
    std::uintptr_t caller{};
    Identity identity{};
    Snapshot snapshot{};
    Kind kind{};
    std::uint32_t depth{}, observedOpen{};
    bool exit{}, returned{}, unwound{};
};
struct Token { std::uint64_t invocation{}, parent{}; unsigned context{}, depth{}; bool entered{}; };

template<std::size_t Contexts=64,std::size_t Depth=32,std::size_t Queue=4096>
class Recorder {
    struct Context {
        std::atomic<unsigned> state{};
        std::atomic_flag busy=ATOMIC_FLAG_INIT;
        Identity identity{};
        std::array<std::uint64_t,Depth> frames{};
        unsigned depth{};
    };
    std::array<Context,Contexts> contexts_{};
    std::array<Event,Queue> queue_{};
    std::atomic_flag queueBusy_=ATOMIC_FLAG_INIT;
    std::size_t read_{}, write_{}, size_{};
    std::atomic<std::uint64_t> serial_{};
    std::atomic<unsigned> open_{}, loss_{};
    std::atomic<bool> enabled_{};
    bool started_{};
    std::uint64_t start_{};
public:
    void Start(std::uint64_t milliseconds) {
        if (started_) {Stop(Loss::Install);return;}
        started_=true;start_=milliseconds;enabled_=true;
    }
    bool enabled() const { return enabled_.load(); }
    Loss loss() const { return static_cast<Loss>(loss_.load()); }
    unsigned observedOpen() const { return open_.load(); }
    void Stop(Loss reason) {
        unsigned empty=0;loss_.compare_exchange_strong(empty,static_cast<unsigned>(reason));enabled_=false;
    }
    bool Within(std::uint64_t now,std::uint32_t frames=0) {
        if (!enabled()) return false;
        if (now<start_ || now-start_>=180000 || frames>=10800) { Stop(Loss::Deadline);return false; }
        return true;
    }
    Token Enter(Kind kind,Identity identity,std::uintptr_t caller,Snapshot snapshot,std::uint64_t now) {
        if (!Within(now)) return {};
        if (!identity.valid) { Stop(Loss::Context);return {}; }
        for (unsigned i=0;i<Contexts;++i) {
            auto& context=contexts_[i];auto state=context.state.load();
            if (!state) {
                unsigned empty=0;
                if (context.state.compare_exchange_strong(empty,1)) {
                    context.identity=identity;context.state.store(2);state=2;
                } else state=empty;
            }
            if (state==1) {Stop(Loss::Contention);return {};}
            if (state!=2 || context.identity!=identity) continue;
            if (context.busy.test_and_set()) { Stop(Loss::Contention);return {}; }
            Token token{};
            if (context.depth>=Depth) Stop(Loss::Depth);
            else {
                const auto serial=serial_.fetch_add(1)+1;
                if (!serial) Stop(Loss::Serial);
                else if (open_.fetch_add(1)>=256) { open_.fetch_sub(1);Stop(Loss::Depth); }
                else {
                    token={serial,context.depth?context.frames[context.depth-1]:0,i,context.depth,true};
                    context.frames[context.depth++]=serial;
                    Push({serial,serial,token.parent,i+1,now,0,caller,identity,snapshot,kind,token.depth,open_.load(),false,false,false});
                }
            }
            context.busy.clear();return token;
        }
        Stop(Loss::Context);return {};
    }
    void Exit(Token token,Kind kind,Identity identity,Snapshot snapshot,std::uint64_t now,
              std::uint64_t result,bool returned) {
        if (!token.entered) return;
        auto& context=contexts_[token.context];
        if (context.busy.test_and_set()) { Stop(Loss::Contention);return; }
        const bool matches=context.identity==identity && context.depth==token.depth+1 &&
            context.frames[token.depth]==token.invocation;
        if (!matches) Stop(Loss::ReturnMismatch);
        else {
            --context.depth;context.frames[token.depth]=0;open_.fetch_sub(1);
            // Keep terminal receipts even when expiry/loss stopped new admission.
            (void)Within(now);
            const auto serial=serial_.fetch_add(1)+1;
            if (!serial) Stop(Loss::Serial);
            else Push({serial,token.invocation,token.parent,token.context+1,now,result,0,identity,snapshot,
                kind,token.depth,open_.load(),true,returned,!returned});
        }
        context.busy.clear();
    }
    bool Pop(Event& event) {
        if (queueBusy_.test_and_set()) { Stop(Loss::Contention);return false; }
        const bool have=size_!=0;
        if (have) { event=queue_[read_];read_=(read_+1)%Queue;--size_; }
        queueBusy_.clear();return have;
    }
private:
    void Push(const Event& event) {
        if (queueBusy_.test_and_set()) { Stop(Loss::Contention);return; }
        if (size_==Queue) Stop(Loss::Queue);
        else {queue_[write_]=event;write_=(write_+1)%Queue;++size_;}
        queueBusy_.clear();
    }
};
} // namespace kh2coop::inject::authoritytrace
