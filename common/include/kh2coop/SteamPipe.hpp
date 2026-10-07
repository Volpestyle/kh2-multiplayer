#pragma once
#include "kh2coop/SteamBroker.hpp"
#include <memory>

namespace kh2coop::steam {
// Local message-mode named pipe: current-user ACL, remote clients rejected,
// first-instance-only server, OS-reported peer PID and retained process handle.
// No gameplay mappings, pointers or commands cross this boundary.
class BrokerLink {
public:
    virtual ~BrokerLink()=default;
    virtual bool pump()=0;
    virtual bool connected() const=0;
    virtual bool send(const Frame&)=0;
    virtual bool receive(Frame&)=0;
    virtual std::size_t queued() const { return 0; }
    virtual void close()=0;
};
class Pipe final: public BrokerLink {
public:
    Pipe(); ~Pipe() override;
    Pipe(const Pipe&)=delete; Pipe& operator=(const Pipe&)=delete;
    bool serve();
    bool attach(std::uint32_t gamePid, std::uint32_t timeoutMs=5000);
    bool pump() override; // nonblocking, bounded work, fail closed on queue/IO/peer loss
    bool connected() const override;
    bool send(const Frame&) override;
    bool receive(Frame&) override;
    std::size_t queued() const override;
    void close() override;
private:
    struct State; std::unique_ptr<State> s_;
};
} // namespace kh2coop::steam
