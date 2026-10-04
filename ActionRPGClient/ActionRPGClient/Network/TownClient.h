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
    struct SkillStateEvent { std::string payload; };

    using TownEvent = std::variant<TownProtocol::EnterTownResponse, TownProtocol::PlayerAppear,
        TownProtocol::PlayerMove, TownProtocol::PlayerDisappear,
        TownProtocol::EnterDungeonResponse, TownProtocol::MapChanged,
        TownProtocol::DungeonSelectionOpen, TownProtocol::PartyInvitation,
        TownProtocol::PartySnapshot, TownProtocol::PartyOperationResult,
        TownProtocol::PartyDirectoryPage, TownProtocol::PartyDirectoryChanged,
        TownProtocol::DungeonCompletionResponse, TownProtocol::PartyDetailResponse,
        TownProtocol::PartyJoinRequestUpdate, TownProtocol::PartyKicked, SkillStateEvent>;

    class TownClient final
    {
    public:
        TownClient();
        ~TownClient();

        TownClient(const TownClient&) = delete;
        TownClient& operator=(const TownClient&) = delete;

        void Start(std::string inHost, std::uint16_t inPort, std::string inPlayerName,
            std::uint32_t inCharacterId);
        void Stop();
        void SendMovement(const TownProtocol::MoveInput& inInput);
        void RequestDungeon(std::string inZoneId, std::uint32_t inDungeonId);
        void RequestDungeonCompletion(std::uint64_t inRoomId, bool inRetry);
        void RequestSkillState();
        void LearnSkill(std::string inSkillId, std::uint32_t inExpectedSkillLevel);
        void ConfirmDungeonJoin(std::uint64_t inRoomId, std::uint64_t inChallenge);
        void InviteToParty(std::uint64_t inTargetPlayerId);
        void AnswerPartyInvitation(std::uint64_t inInvitationId, bool inAccepted);
        void LeaveParty();
        void KickPartyMember(std::uint64_t inTargetPlayerId);
        void CreateParty(std::string inTitle, bool inIsPublic);
        void UpdatePartySettings(std::string inTitle, bool inIsPublic);
        void RequestPartyDirectoryPage(std::uint32_t inPage);
        void UnsubscribePartyDirectory();
        void RequestPartyDetail(std::uint64_t inPartyId);
        void RequestPartyJoin(std::uint64_t inPartyId);
        void AnswerPartyJoin(std::uint64_t inRequestId, bool inAccepted);
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
        std::uint32_t characterId{ 1 };
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
