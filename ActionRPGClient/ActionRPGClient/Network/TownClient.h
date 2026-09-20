#pragma once

#include "Network/TownProtocol.h"

#include <asio.hpp>

#include <array>
#include <atomic>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <variant>
#include <vector>

namespace ActionRPG
{
    using TownEvent = std::variant<TownProtocol::EnterTownResponse, TownProtocol::PlayerAppear,
        TownProtocol::PlayerMove, TownProtocol::PlayerDisappear>;

    class TownClient final
    {
    public:
        TownClient();
        ~TownClient();

        TownClient(const TownClient&) = delete;
        TownClient& operator=(const TownClient&) = delete;

        void Start(std::string inHost, std::uint16_t inPort, std::string inPlayerName);
        void Stop();
        void SendMovement(const TownProtocol::MoveInput& inInput);
        [[nodiscard]] std::vector<TownEvent> ConsumeEvents();
        [[nodiscard]] bool IsConnected() const noexcept;

    private:
        void Connect();
        void ScheduleReconnect();
        void ReadHeader();
        void ReadBody(std::uint32_t inBodySize);
        void HandlePacket();
        void QueuePacket(std::vector<std::uint8_t> inPacketBody);
        void WriteNext();
        void HandleDisconnect();
        void PushEvent(TownEvent inEvent);

        asio::io_context ioContext;
        asio::strand<asio::io_context::executor_type> strand;
        asio::executor_work_guard<asio::io_context::executor_type> workGuard;
        asio::ip::tcp::resolver resolver;
        asio::ip::tcp::socket socket;
        asio::steady_timer reconnectTimer;
        std::thread networkThread;
        std::string host;
        std::string port;
        std::string playerName;
        std::array<std::uint8_t, 4> receiveHeader{};
        std::vector<std::uint8_t> receiveBody;
        std::deque<std::vector<std::uint8_t>> sendQueue;
        std::mutex eventMutex;
        std::vector<TownEvent> events;
        std::atomic_bool connected = false;
        bool started = false;
        bool stopping = false;
    };
}
