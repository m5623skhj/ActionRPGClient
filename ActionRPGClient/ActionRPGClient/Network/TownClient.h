#pragma once

#include "Network/TownProtocol.h"

#include "Network/AuthSettings.h"
#include <chrono>
#include <memory>

#include <cstdint>
#include <string>
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

    enum class TownConnectionState { Disconnected, Connecting, AdmissionPending, EnteringTown, Ready, Failed };
    struct TownConnectionInfo
    {
        std::uint64_t attemptId{};
        TownConnectionState state{ TownConnectionState::Disconnected };
        std::wstring message;
    };

    class TownClient final
    {
    public:
        TownClient();
        ~TownClient();

        TownClient(const TownClient&) = delete;
        TownClient& operator=(const TownClient&) = delete;

        void Start(std::uint64_t inAttempt, TownServerSettings inServer, std::filesystem::path inCa,
            std::string inTicket, std::string inName, std::uint32_t inCharacter,
            std::chrono::steady_clock::time_point inExpiry);
        void RequestStop();
        [[nodiscard]] TownConnectionInfo GetConnectionInfo() const;
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
        void QueuePacket(std::vector<std::uint8_t> inBody);
        struct Impl;
        std::unique_ptr<Impl> impl;
    };
}
