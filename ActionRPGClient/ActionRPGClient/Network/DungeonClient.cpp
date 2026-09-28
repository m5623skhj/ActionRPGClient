#include "Network/DungeonClient.h"

#include <WinSock2.h>
#include <Windows.h>
#include <string>
#include <RUDPClientCore.h>

#include <memory>

namespace
{
    void AppendPacket(std::vector<ActionRPG::DungeonPacket>& inPackets, NetBuffer* inBuffer,
        const bool inReliable)
    {
        const std::unique_ptr<NetBuffer, decltype(&NetBuffer::Free)> buffer(inBuffer, &NetBuffer::Free);
        const auto* firstByte = reinterpret_cast<const std::uint8_t*>(buffer->GetReadBufferPtr());
        const int byteCount = buffer->GetUseSize();
        inPackets.push_back({ inReliable, { firstByte, firstByte + byteCount } });
    }
}

namespace ActionRPG
{
    class DungeonClient::Impl final : public RUDPClientCore
    {
    public:
        bool Start(const std::wstring& inCoreOptions, const std::wstring& inBrokerOptions)
        {
            if (!IsStopped())
            {
                return true;
            }

            return RUDPClientCore::Start(inCoreOptions, inBrokerOptions, false);
        }

        void Stop() override
        {
            RUDPClientCore::Stop();
        }
    };

    DungeonClient::DungeonClient()
        : impl(std::make_unique<Impl>())
    {
    }

    DungeonClient::~DungeonClient()
    {
        Stop();
    }

    bool DungeonClient::Start(const std::wstring& inCoreOptions, const std::wstring& inBrokerOptions)
    {
        return impl->Start(inCoreOptions, inBrokerOptions);
    }

    void DungeonClient::Stop()
    {
        impl->Stop();
    }

    bool DungeonClient::IsConnected() const
    {
        return impl->IsConnected();
    }

    void DungeonClient::SendReliable(IPacket& inPacket)
    {
        if (IsConnected())
        {
            impl->SendPacket(inPacket);
        }
    }

    bool DungeonClient::SendUnreliable(IPacket& inPacket)
    {
        return impl->SendUnreliablePacket(inPacket);
    }

    std::vector<DungeonPacket> DungeonClient::ConsumePackets()
    {
        constexpr int MAX_PACKETS_PER_CHANNEL = 64;
        std::vector<DungeonPacket> packets;

        for (int packetCount = 0; packetCount < MAX_PACKETS_PER_CHANNEL; ++packetCount)
        {
            NetBuffer* buffer = impl->GetReceivedPacket();
            if (buffer == nullptr)
            {
                break;
            }
            AppendPacket(packets, buffer, true);
        }

        for (int packetCount = 0; packetCount < MAX_PACKETS_PER_CHANNEL; ++packetCount)
        {
            NetBuffer* buffer = impl->GetReceivedUnreliablePacket();
            if (buffer == nullptr)
            {
                break;
            }
            AppendPacket(packets, buffer, false);
        }

        return packets;
    }

}
