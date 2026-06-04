#pragma once

#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

#include <asio.hpp>
#include <asio/posix/stream_descriptor.hpp>

#include "network/BinaryProtocol.hpp"

struct IPCEnvelope {
    uint32_t correlationId = 0;
    uint64_t sessionId = 0;
    uint16_t messageType = 0;
    std::vector<uint8_t> payload;
};

class IPCChannel : public std::enable_shared_from_this<IPCChannel> {
public:
    IPCChannel(asio::io_context& io, int fd);
    ~IPCChannel();

    IPCChannel(const IPCChannel&) = delete;
    IPCChannel& operator=(const IPCChannel&) = delete;

    void Start(std::function<void(const IPCEnvelope&)> onMessage);
    void Stop();

    void SendAsync(const IPCEnvelope& envelope);
    void SendSync(const IPCEnvelope& envelope);

    bool IsOpen() const;
    int GetFd();

private:
    asio::io_context& io_;
    asio::posix::stream_descriptor stream_;
    std::function<void(const IPCEnvelope&)> onMessage_;
    std::atomic<bool> stopped_{false};

    std::deque<std::vector<uint8_t>> writeQueue_;
    std::mutex writeMutex_;
    bool writing_ = false;

    void doRead();
    void doWrite();
    static std::vector<uint8_t> encodeFrame(const IPCEnvelope& env);
    static IPCEnvelope decodeFrame(const uint8_t* data, size_t len);
};
