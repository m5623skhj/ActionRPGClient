#pragma once

#include <cstdint>
#include <chrono>
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

    struct DungeonWorldEvent { std::string json; };
    struct DungeonCombatEvent { std::string json; };
    struct DungeonActionResultEvent { std::uint32_t sequence{}; bool accepted{}; std::uint64_t serverTick{}; };
    struct DungeonPlayerStateEvent
    {
        std::uint32_t sequence{};
        std::string mapId;
        float x{};
        float y{};
    };
    using DungeonEvent = std::variant<DungeonChallengeEvent, DungeonAuthResultEvent, DungeonWorldEvent,
        DungeonPlayerStateEvent, DungeonCombatEvent, DungeonActionResultEvent>;

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
        void RequestWorld();
        void StartCombatPolling();
        void SendAction(std::uint32_t inSequence, std::uint8_t inAction, bool inFacingLeft);
        [[nodiscard]] DungeonConnectionState GetConnectionState() const;
        void SendReliable(IPacket& inPacket);
        bool SendUnreliable(IPacket& inPacket);
        [[nodiscard]] std::vector<DungeonEvent> ConsumeEvents();

    private:
        class Impl;
        std::unique_ptr<Impl> impl;
        std::string worldJson;
        std::uint32_t worldBytes{};
        bool receivingWorld{};
        std::chrono::steady_clock::time_point worldDeadline{};
        void UpdateCombatPolling();
        std::string combatJson;
        std::uint32_t combatSnapshotId{}, combatBytes{}, combatOffset{}, lastCompletedCombatId{};
        bool combatPolling{}, combatRequestPending{};
        std::chrono::steady_clock::time_point combatNextRequest{}, combatResponseDeadline{}, combatProgressDeadline{};
    };
}
