#include "Network/DungeonClient.h"

#include "Network/DungeonProtocol.h"

#include <WinSock2.h>
#include <Windows.h>

#include <RUDPClientCore.h>

#include <array>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <utility>

namespace
{
    constexpr std::size_t MAX_IPV4_ADDRESS_LENGTH = 15;
    constexpr int MAX_PACKETS_PER_UPDATE = 64;

    [[nodiscard]] std::filesystem::path GetExecutableDirectory()
    {
        std::array<wchar_t, MAX_PATH> executablePath{};
        const DWORD length = GetModuleFileNameW(
            nullptr, executablePath.data(), static_cast<DWORD>(executablePath.size()));
        if (length == 0 || length == executablePath.size())
        {
            return {};
        }
        return std::filesystem::path(executablePath.data()).parent_path();
    }

    [[nodiscard]] std::optional<std::filesystem::path> WriteSessionBrokerOptions(
        const std::string& inAddress,
        const std::uint16_t inPort)
    {
        if (inAddress.empty() || inAddress.size() > MAX_IPV4_ADDRESS_LENGTH || inPort == 0)
        {
            return std::nullopt;
        }

        const std::filesystem::path path = std::filesystem::temp_directory_path()
            / (L"ActionRPGClient-SessionBroker-" + std::to_wstring(GetCurrentProcessId()) + L"-"
                + std::to_wstring(std::chrono::steady_clock::now().time_since_epoch().count()) + L".txt");
        const std::wstring address(inAddress.begin(), inAddress.end());
        const std::wstring contents = L"\xFEFF:SESSION_BROKER\r\n{\r\n\tIP = \"" + address
            + L"\"\r\n\tPORT = " + std::to_wstring(inPort)
            + L"\r\n}\r\n\r\n:SERIALIZEBUF\r\n{\r\n\tPACKET_CODE = 119\r\n"
              L"\tPACKET_KEY = 50\r\n}\r\n";

        static_assert(sizeof(wchar_t) == 2);
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        if (!output)
        {
            return std::nullopt;
        }
        output.write(reinterpret_cast<const char*>(contents.data()),
            static_cast<std::streamsize>(contents.size() * sizeof(wchar_t)));
        if (!output)
        {
            std::error_code ignoredError;
            std::filesystem::remove(path, ignoredError);
            return std::nullopt;
        }
        return path;
    }
}

namespace ActionRPG
{
    class DungeonClient::Impl final : public RUDPClientCore
    {
    public:
        ~Impl()
        {
            StopClient();
        }

        bool StartClient(std::string inSessionBrokerAddress, const std::uint16_t inSessionBrokerPort)
        {
            const DungeonConnectionState currentState = GetConnectionState();
            if (currentState != DungeonConnectionState::Stopped
                && currentState != DungeonConnectionState::Failed)
            {
                return false;
            }

            if (connectionThread.joinable())
            {
                connectionThread.join();
            }
            if (!IsStopped())
            {
                RUDPClientCore::Stop();
            }

            startCompleted.store(false, std::memory_order_release);
            state.store(DungeonConnectionState::Connecting, std::memory_order_release);
            try
            {
                connectionThread = std::thread(
                    [this, address = std::move(inSessionBrokerAddress), inSessionBrokerPort]()
                    {
                        std::optional<std::filesystem::path> brokerOptions;
                        bool started = false;
                        try
                        {
                            const std::filesystem::path executableDirectory = GetExecutableDirectory();
                            const std::filesystem::path coreOptions = executableDirectory
                                / L"ClientOptionFile" / L"CoreOption.txt";
                            brokerOptions = WriteSessionBrokerOptions(address, inSessionBrokerPort);
                            if (!executableDirectory.empty() && brokerOptions.has_value())
                            {
                                started = RUDPClientCore::Start(
                                    coreOptions.wstring(), brokerOptions->wstring(), false);
                            }
                        }
                        catch (...)
                        {
                            started = false;
                        }
                        if (brokerOptions.has_value())
                        {
                            std::error_code ignoredError;
                            std::filesystem::remove(*brokerOptions, ignoredError);
                        }
                        if (!started)
                        {
                            state.store(DungeonConnectionState::Failed, std::memory_order_release);
                        }
                        startCompleted.store(true, std::memory_order_release);
                    });
            }
            catch (...)
            {
                state.store(DungeonConnectionState::Failed, std::memory_order_release);
                startCompleted.store(true, std::memory_order_release);
                return false;
            }
            return true;
        }

        void StopClient()
        {
            if (connectionThread.joinable())
            {
                connectionThread.join();
            }
            RUDPClientCore::Stop();
            startCompleted.store(false, std::memory_order_release);
            state.store(DungeonConnectionState::Stopped, std::memory_order_release);
        }

        [[nodiscard]] DungeonConnectionState GetConnectionState() const
        {
            const DungeonConnectionState currentState = state.load(std::memory_order_acquire);
            if (currentState == DungeonConnectionState::Stopped
                || currentState == DungeonConnectionState::Failed)
            {
                return currentState;
            }
            if (IsConnected())
            {
                return DungeonConnectionState::Connected;
            }
            if (startCompleted.load(std::memory_order_acquire) && IsStopped())
            {
                return DungeonConnectionState::Failed;
            }
            return DungeonConnectionState::Connecting;
        }

    private:
        std::thread connectionThread;
        std::atomic<DungeonConnectionState> state = DungeonConnectionState::Stopped;
        std::atomic_bool startCompleted{};
    };

    DungeonClient::DungeonClient()
        : impl(std::make_unique<Impl>())
    {
    }

    DungeonClient::~DungeonClient()
    {
        Stop();
    }

    bool DungeonClient::Start(
        std::string inSessionBrokerAddress,
        const std::uint16_t inSessionBrokerPort)
    {
        const DungeonConnectionState connectionState = impl->GetConnectionState();
        if (connectionState != DungeonConnectionState::Stopped
            && connectionState != DungeonConnectionState::Failed)
        {
            return false;
        }
        impl = std::make_unique<Impl>();
        return impl->StartClient(std::move(inSessionBrokerAddress), inSessionBrokerPort);
    }

    void DungeonClient::Stop()
    {
        impl->StopClient();
    }

    DungeonConnectionState DungeonClient::GetConnectionState() const
    {
        return impl->GetConnectionState();
    }

    void DungeonClient::SendReliable(IPacket& inPacket)
    {
        if (GetConnectionState() == DungeonConnectionState::Connected)
        {
            impl->SendPacket(inPacket);
        }
    }

    bool DungeonClient::SendUnreliable(IPacket& inPacket)
    {
        return GetConnectionState() == DungeonConnectionState::Connected
            && impl->SendUnreliablePacket(inPacket);
    }

    std::vector<DungeonEvent> DungeonClient::ConsumeEvents()
    {
        std::vector<DungeonEvent> events;
        bool invalidPacket = false;

        for (int packetCount = 0; packetCount < MAX_PACKETS_PER_UPDATE; ++packetCount)
        {
            NetBuffer* const receivedBuffer = impl->GetReceivedPacket();
            if (receivedBuffer == nullptr)
            {
                break;
            }
            const std::unique_ptr<NetBuffer, decltype(&NetBuffer::Free)> buffer(
                receivedBuffer, &NetBuffer::Free);

            ::PacketId packetId{};
            *buffer >> packetId;
            switch (static_cast<DungeonProtocol::PacketType>(packetId))
            {
            case DungeonProtocol::PacketType::DUNGEON_CHALLENGE:
            {
                DungeonProtocol::DungeonChallenge packet;
                packet.BufferToPacket(*buffer);
                if (packet.challenge == 0)
                {
                    invalidPacket = true;
                }
                else
                {
                    events.emplace_back(DungeonChallengeEvent{ packet.challenge });
                }
                break;
            }
            case DungeonProtocol::PacketType::DUNGEON_AUTH_RESULT:
            {
                DungeonProtocol::DungeonAuthResult packet;
                packet.BufferToPacket(*buffer);
                if (packet.succeeded > 1)
                {
                    invalidPacket = true;
                }
                else
                {
                    events.emplace_back(DungeonAuthResultEvent{ packet.succeeded != 0 });
                }
                break;
            }
            default:
                invalidPacket = true;
                break;
            }

            if (buffer->GetBufferError() != 0 || buffer->GetUseSize() != 0 || invalidPacket)
            {
                invalidPacket = true;
                break;
            }
        }

        for (int packetCount = 0; packetCount < MAX_PACKETS_PER_UPDATE; ++packetCount)
        {
            NetBuffer* const buffer = impl->GetReceivedUnreliablePacket();
            if (buffer == nullptr)
            {
                break;
            }
            NetBuffer::Free(buffer);
        }

        if (invalidPacket)
        {
            Stop();
            events.clear();
        }
        return events;
    }
}
