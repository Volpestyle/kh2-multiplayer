#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>

namespace kh2coop {
// Backend-owned identity. Valid only until that transport closes; never a
// session/peer/Steam identity and never serialized on the wire.
struct TransportPeer;

// Keeps the original receive buffer alive without copying. The deleter must
// remain callable after close(), including when a receive callback reconnects.
class TransportPacket {
public:
    TransportPacket() = default;
    TransportPacket(void* owner, void (*release)(void*), const std::uint8_t* bytes,
                    std::size_t count, bool reliable)
        : data(bytes), size(count), reliable(reliable), owner_(owner), release_(release) {}
    ~TransportPacket() { reset(); }
    TransportPacket(const TransportPacket&) = delete;
    TransportPacket& operator=(const TransportPacket&) = delete;
    TransportPacket(TransportPacket&& other) noexcept { swap(other); }
    TransportPacket& operator=(TransportPacket&& other) noexcept {
        if (this != &other) { reset(); swap(other); } return *this;
    }
    void reset() noexcept {
        if (owner_) release_(owner_);
        owner_ = nullptr; release_ = nullptr; data = nullptr; size = 0; reliable = false;
    }
    const std::uint8_t* data = nullptr;
    std::size_t size = 0;
    bool reliable = false;
private:
    void swap(TransportPacket& other) noexcept {
        std::swap(data, other.data); std::swap(size, other.size);
        std::swap(reliable, other.reliable); std::swap(owner_, other.owner_);
        std::swap(release_, other.release_);
    }
    void* owner_ = nullptr;
    void (*release_)(void*) = nullptr;
};

enum class TransportEventType { None, Connect, Disconnect, Receive };
struct TransportEvent {
    TransportEventType type = TransportEventType::None;
    TransportPeer* peer = nullptr;
    std::uint32_t data = 0;
    TransportPacket packet;
};
struct TransportStats {
    std::uint32_t rttMs = 0, rttVarianceMs = 0, lossPermille = 0;
    std::size_t channelCount = 0;
};
enum class TransportOpenResult { Ok, InvalidAddress, CreateFailed };

// Single-thread owner, matching the existing ENet pump. Application code owns
// initialization of ENet just as before. This seam carries bytes/events only:
// admission, cache, source/epoch fencing and resync stay in client/session code.
class Transport {
public:
    virtual ~Transport() = default;
    virtual bool createClient(std::size_t peers, std::size_t channels) = 0;
    virtual TransportOpenResult listen(const std::string& address, std::uint16_t port,
                                       std::size_t peers, std::size_t channels) = 0;
    virtual TransportPeer* connect(const std::string& address, std::uint16_t port,
                                   std::size_t channels, bool& resolved) = 0;
    virtual bool isOpen() const = 0;
    virtual void close() = 0;
    virtual int service(TransportEvent&, std::uint32_t timeoutMs) = 0;
    // Copies bytes before returning. Reliable channels ordered independently.
    virtual bool send(TransportPeer*, const std::uint8_t*, std::size_t,
                      std::uint8_t channel, bool reliable) = 0;
    virtual void disconnect(TransportPeer*, std::uint32_t reason) = 0;
    virtual void disconnectLater(TransportPeer*, std::uint32_t reason) = 0;
    virtual TransportStats stats(TransportPeer*) const = 0;
    virtual std::string pendingPeerLabel(TransportPeer*) const = 0;
};
std::unique_ptr<Transport> makeEnetTransport();
} // namespace kh2coop
