#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class IPacket;

namespace ActionRPG
{
    struct DungeonPacket
    {
        bool reliable{};
        std::vector<std::uint8_t> bytes;
    };

    // Owns the RUDP connection. Start is called when entering a dungeon.
    class DungeonClient final
    {
    public:
        DungeonClient();
        ~DungeonClient();

        DungeonClient(const DungeonClient&) = delete;
        DungeonClient& operator=(const DungeonClient&) = delete;

        bool Start(const std::wstring& inCoreOptions, const std::wstring& inBrokerOptions);
        void Stop();
        [[nodiscard]] bool IsConnected() const;
        void SendReliable(IPacket& inPacket);
        bool SendUnreliable(IPacket& inPacket);
        [[nodiscard]] std::vector<DungeonPacket> ConsumePackets();

    private:
        class Impl;
        std::unique_ptr<Impl> impl;
    };
}
