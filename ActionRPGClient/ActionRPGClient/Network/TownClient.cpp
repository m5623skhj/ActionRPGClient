#include "Network/TownClient.h"

#include <algorithm>
#include <chrono>
#include <optional>
#include <utility>

namespace
{
    constexpr std::uint32_t MAX_PACKET_BODY_SIZE = 1024 * 1024;
    constexpr std::size_t MAX_QUEUED_SEND_BYTES = 1024 * 1024;

    std::uint32_t DecodeBodySize(const std::array<std::uint8_t, 4>& inHeader)
    {
        return (static_cast<std::uint32_t>(inHeader[0]) << 24)
            | (static_cast<std::uint32_t>(inHeader[1]) << 16)
            | (static_cast<std::uint32_t>(inHeader[2]) << 8)
            | static_cast<std::uint32_t>(inHeader[3]);
    }

    std::vector<std::uint8_t> FramePacket(std::vector<std::uint8_t> inBody)
    {
        const std::uint32_t size = static_cast<std::uint32_t>(inBody.size());
        std::vector<std::uint8_t> result(4 + inBody.size());
        result[0] = static_cast<std::uint8_t>((size >> 24) & 0xFF);
        result[1] = static_cast<std::uint8_t>((size >> 16) & 0xFF);
        result[2] = static_cast<std::uint8_t>((size >> 8) & 0xFF);
        result[3] = static_cast<std::uint8_t>(size & 0xFF);
        std::copy(inBody.begin(), inBody.end(), result.begin() + 4);
        return result;
    }
}

namespace ActionRPG
{
    TownClient::TownClient()
        : strand(asio::make_strand(ioContext)),
          workGuard(asio::make_work_guard(ioContext)),
          resolver(strand),
          socket(strand),
          reconnectTimer(strand)
    {
    }

    TownClient::~TownClient()
    {
        Stop();
    }

    void TownClient::Start(std::string inHost, const std::uint16_t inPort, std::string inPlayerName)
    {
        if (started)
        {
            return;
        }
        started = true;
        host = std::move(inHost);
        port = std::to_string(inPort);
        playerName = std::move(inPlayerName);
        networkThread = std::thread([this]() { ioContext.run(); });
        asio::post(strand, [this]() { Connect(); });
    }

    void TownClient::Stop()
    {
        if (!started)
        {
            return;
        }

        asio::post(strand, [this]()
        {
            stopping = true;
            connected.store(false);
            asio::error_code ignoredError;
            reconnectTimer.cancel(ignoredError);
            resolver.cancel();
            socket.close(ignoredError);
            workGuard.reset();
        });
        if (networkThread.joinable())
        {
            networkThread.join();
        }
        started = false;
    }

    void TownClient::SendMovement(const TownProtocol::MoveInput& inInput)
    {
        asio::post(strand, [this, packet = TownProtocol::Encode(inInput)]() mutable
        {
            if (connected.load())
            {
                QueuePacket(std::move(packet));
            }
        });
    }

    std::vector<TownEvent> TownClient::ConsumeEvents()
    {
        std::scoped_lock lock(eventMutex);
        std::vector<TownEvent> result;
        result.swap(events);
        return result;
    }

    bool TownClient::IsConnected() const noexcept
    {
        return connected.load();
    }

    void TownClient::Connect()
    {
        if (stopping)
        {
            return;
        }

        resolver.async_resolve(host, port, [this](const asio::error_code& inError,
            const asio::ip::tcp::resolver::results_type& inResults)
        {
            if (inError)
            {
                ScheduleReconnect();
                return;
            }

            asio::async_connect(socket, inResults, [this](const asio::error_code& inConnectError,
                const asio::ip::tcp::endpoint&)
            {
                if (inConnectError)
                {
                    HandleDisconnect();
                    return;
                }

                socket.set_option(asio::ip::tcp::no_delay(true));
                connected.store(true);
                QueuePacket(TownProtocol::Encode(TownProtocol::EnterTownRequest{ playerName }));
                ReadHeader();
            });
        });
    }

    void TownClient::ScheduleReconnect()
    {
        if (stopping)
        {
            return;
        }
        reconnectTimer.expires_after(std::chrono::seconds(2));
        reconnectTimer.async_wait([this](const asio::error_code& inError)
        {
            if (!inError)
            {
                Connect();
            }
        });
    }

    void TownClient::ReadHeader()
    {
        asio::async_read(socket, asio::buffer(receiveHeader), [this](const asio::error_code& inError, const std::size_t)
        {
            if (inError)
            {
                HandleDisconnect();
                return;
            }
            const std::uint32_t bodySize = DecodeBodySize(receiveHeader);
            if (bodySize == 0 || bodySize > MAX_PACKET_BODY_SIZE)
            {
                HandleDisconnect();
                return;
            }
            ReadBody(bodySize);
        });
    }

    void TownClient::ReadBody(const std::uint32_t inBodySize)
    {
        receiveBody.resize(inBodySize);
        asio::async_read(socket, asio::buffer(receiveBody), [this](const asio::error_code& inError, const std::size_t)
        {
            if (inError)
            {
                HandleDisconnect();
                return;
            }
            HandlePacket();
            if (connected.load())
            {
                ReadHeader();
            }
        });
    }

    void TownClient::HandlePacket()
    {
        const std::optional<TownProtocol::PacketType> type = TownProtocol::ReadPacketType(receiveBody);
        if (!type.has_value())
        {
            HandleDisconnect();
            return;
        }

        switch (*type)
        {
        case TownProtocol::PacketType::EnterTownResponse:
            if (auto packet = TownProtocol::DecodeEnterTownResponse(receiveBody)) PushEvent(std::move(*packet));
            else HandleDisconnect();
            break;
        case TownProtocol::PacketType::PlayerAppear:
            if (auto packet = TownProtocol::DecodePlayerAppear(receiveBody)) PushEvent(std::move(*packet));
            else HandleDisconnect();
            break;
        case TownProtocol::PacketType::PlayerMove:
            if (auto packet = TownProtocol::DecodePlayerMove(receiveBody)) PushEvent(std::move(*packet));
            else HandleDisconnect();
            break;
        case TownProtocol::PacketType::PlayerDisappear:
            if (auto packet = TownProtocol::DecodePlayerDisappear(receiveBody)) PushEvent(std::move(*packet));
            else HandleDisconnect();
            break;
        default:
            HandleDisconnect();
            break;
        }
    }

    void TownClient::QueuePacket(std::vector<std::uint8_t> inPacketBody)
    {
        std::vector<std::uint8_t> framedPacket = FramePacket(std::move(inPacketBody));
        std::size_t queuedBytes{};
        for (const auto& packet : sendQueue) queuedBytes += packet.size();
        if (queuedBytes + framedPacket.size() > MAX_QUEUED_SEND_BYTES)
        {
            HandleDisconnect();
            return;
        }

        const bool writeInProgress = !sendQueue.empty();
        sendQueue.push_back(std::move(framedPacket));
        if (!writeInProgress) WriteNext();
    }

    void TownClient::WriteNext()
    {
        if (sendQueue.empty() || !connected.load()) return;
        asio::async_write(socket, asio::buffer(sendQueue.front()), [this](const asio::error_code& inError, const std::size_t)
        {
            if (inError)
            {
                HandleDisconnect();
                return;
            }
            sendQueue.pop_front();
            WriteNext();
        });
    }

    void TownClient::HandleDisconnect()
    {
        connected.store(false);
        sendQueue.clear();
        asio::error_code ignoredError;
        socket.close(ignoredError);
        if (!stopping)
        {
            socket = asio::ip::tcp::socket(strand);
            ScheduleReconnect();
        }
    }

    void TownClient::PushEvent(TownEvent inEvent)
    {
        std::scoped_lock lock(eventMutex);
        events.push_back(std::move(inEvent));
    }
}
