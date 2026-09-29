#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <variant>
#include <vector>

class IPacket;

namespace ActionRPG
{
    struct DungeonChallengeEvent
    {
        std::uint64_t challenge{};
    };

    struct DungeonAuthResultEvent
    {
        bool succeeded{};
    };

    enum class DungeonConnectionState
    {
        Stopped,
        Connecting,
        Connected,
        Failed
    };

    using DungeonEvent = std::variant<DungeonChallengeEvent, DungeonAuthResultEvent>;

    // Owns the RUDP connection. Start returns immediately while the TLS session
    // broker exchange and RUDP connection are performed on a worker thread.
    class DungeonClient final
    {
    public:
        DungeonClient();
        ~DungeonClient();

        DungeonClient(const DungeonClient&) = delete;
        DungeonClient& operator=(const DungeonClient&) = delete;

        bool Start(std::string inSessionBrokerAddress, std::uint16_t inSessionBrokerPort);
        void Stop();
        [[nodiscard]] DungeonConnectionState GetConnectionState() const;
        void SendReliable(IPacket& inPacket);
        bool SendUnreliable(IPacket& inPacket);
        [[nodiscard]] std::vector<DungeonEvent> ConsumeEvents();

    private:
        class Impl;
        std::unique_ptr<Impl> impl;
    };
}
