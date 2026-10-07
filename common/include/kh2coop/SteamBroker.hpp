#pragma once
#include <cstdint>
#include <deque>
#include <map>
#include <span>
#include <string>
#include <vector>
#include <utility>

namespace kh2coop::steam {
// Private broker protocol, NOT the game protocol. All lengths, queues and
// identities are bounded before allocation or dispatch. One runtime per game.
constexpr std::uint32_t AppId = 2552430;
constexpr std::size_t MaxPacket = 65536, MaxQueue = 128, MaxPeers = 2;
constexpr std::uint16_t VirtualPort = 27795;
enum class Op : std::uint8_t { Hello=1, Host, Join, Send, Close, Stop, Ready, Connected, Disconnected, Data, Error, Ping, Stats };
struct Frame {
    Op op{};
    std::uint64_t peer=0; // authenticated SteamID, never caller-selected for incoming data
    std::uint32_t reason=0;
    std::uint8_t channel=0;
    bool reliable=false;
    std::vector<std::uint8_t> bytes;
    Frame()=default;
    Frame(Op operation,std::uint64_t id=0,std::uint32_t why=0,std::uint8_t ch=0,
          bool ordered=false,std::vector<std::uint8_t> payload={})
        :op(operation),peer(id),reason(why),channel(ch),reliable(ordered),bytes(std::move(payload)){}
};
std::vector<std::uint8_t> encode(const Frame&, std::uint64_t sequence);
bool decode(std::span<const std::uint8_t>, std::uint64_t expectedSequence, Frame&);
bool validId(std::uint64_t);
bool parseId(const std::string&, std::uint64_t&);

struct Status { std::uint32_t handle=0, listener=0; std::uint64_t identity=0; int state=0; bool authenticated=false, relay=false; std::uint32_t reason=0; };
struct Message { std::uint32_t handle=0; std::uint64_t identity=0; bool reliable=false; std::vector<std::uint8_t> bytes; };
// Native adapter owns the game's existing Steam API only. Tests supply mocks.
// listen/connect MUST pass ICE=0 in creation options. iceOff checks actual handle.
class Api {
public:
    virtual ~Api()=default;
    virtual std::uint64_t readyIdentity()=0;
    virtual std::uint32_t listen()=0;
    virtual std::uint32_t connect(std::uint64_t)=0;
    virtual bool iceOff(std::uint32_t, bool listener)=0;
    virtual bool accept(std::uint32_t)=0;
    virtual void close(std::uint32_t, bool linger, std::uint32_t reason=0)=0;
    virtual void closeListener(std::uint32_t)=0;
    virtual bool nextStatus(Status&)=0;
    virtual bool healthy() const=0; // callback overflow is fatal, never silent
    virtual bool receive(std::uint32_t, Message&)=0;
    virtual bool send(std::uint32_t, std::span<const std::uint8_t>, bool)=0;
    virtual bool quality(std::uint32_t, std::uint32_t& rttMs, std::uint32_t& lossPermille) { (void)rttMs;(void)lossPermille;return false; }
};
class Broker {
public:
    explicit Broker(Api& api):api_(api){}
    ~Broker(){stop();}
    bool command(const Frame&, std::uint64_t nowMs);
    bool tick(std::uint64_t nowMs);
    bool pop(Frame&);
    void stop();
    bool failed() const {return failed_;}
private:
    struct Peer { std::uint32_t handle; bool connected; std::uint64_t started; std::uint64_t lastStats=0; };
    Api& api_;
    std::uint64_t identity_=0, target_=0, lastCommand_=0, firstTick_=0;
    std::uint32_t listener_=0;
    bool configured_=false, failed_=false;
    std::vector<std::uint64_t> allowed_;
    std::map<std::uint64_t,Peer> peers_;
    std::deque<Frame> out_;
    bool emit(Frame);
    bool fail(const char*);
};
} // namespace kh2coop::steam
