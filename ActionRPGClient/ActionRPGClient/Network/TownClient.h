#pragma once

#include "Network/TownProtocol.h"

#include "Network/AuthSettings.h"
#include <chrono>
#include <memory>
#include <optional>

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace ActionRPG
{
    struct QuestEvent
    {
        enum class Kind { StatePage, ActionResult };
        Kind kind;
        std::string payload;
    };
    struct SkillStateEvent { std::string payload; };
    struct CharacterEvent
    {
        enum class Kind { List, Create, Select };
        Kind kind;
        std::string payload;
    };
    struct InventoryEvent
    {
        enum class Kind { State, Operation, Definitions };
        Kind kind;
        std::string payload;
    };

    using TownEvent = std::variant<TownProtocol::EnterTownResponse, TownProtocol::PlayerAppear,
        TownProtocol::PlayerMove, TownProtocol::PlayerDisappear,
        TownProtocol::EnterDungeonResponse, TownProtocol::MapChanged,
        TownProtocol::DungeonSelectionOpen, TownProtocol::PartyInvitation,
        TownProtocol::PartySnapshot, TownProtocol::PartyOperationResult,
        TownProtocol::PartyDirectoryPage, TownProtocol::PartyDirectoryChanged,
        TownProtocol::DungeonCompletionResponse, TownProtocol::PartyDetailResponse,
        TownProtocol::PartyJoinRequestUpdate, TownProtocol::PartyKicked, SkillStateEvent, InventoryEvent, QuestEvent, TownProtocol::ChatResult, TownProtocol::ChatMessage>;

    enum class TownSessionStatusCode : std::uint8_t
    {
        None = 0, WaitingForPriorOwner = 1, Replaced = 2, AdmissionTimeout = 3,
        AuthUnavailable = 4, AuthorizationExpired = 5, LoggedOut = 6, ServerTransfer = 7, AccountUnavailable = 8
    };
    enum class TownConnectionState { Disconnected, Connecting, AdmissionPending, WaitingForPriorOwner, CharacterSelection, EnteringTown, Ready, Failed };
    struct TownConnectionInfo
    {
        std::uint64_t attemptId{};
        TownConnectionState state{ TownConnectionState::Disconnected };
        std::wstring message;
        TownSessionStatusCode sessionStatusCode{TownSessionStatusCode::None};
        std::uint32_t retryAfterMs{}; // Server hint only; the client never polls or reissues a ticket.
    };

    class TownClient final
    {
    public:
        TownClient();
        ~TownClient();

        TownClient(const TownClient&) = delete;
        TownClient& operator=(const TownClient&) = delete;

        void Start(std::uint64_t inAttempt, TownServerSettings inServer, std::filesystem::path inCa,
            std::string inTicket, std::chrono::steady_clock::time_point inExpiry);
        void RequestCharacterList(std::string inRequestId);
        void CreateCharacter(std::string inRequestId, std::string inName, std::uint32_t inDefinitionId);
        void SelectCharacter(std::string inRequestId, std::uint64_t inCharacterId, std::uint64_t inOwnerGeneration);
        void RequestInventoryState(std::string inRequestId);
        void ChangeInventory(std::string inRequestId, std::uint64_t inRevision, std::string inAction,
            std::string inInstanceId, std::uint32_t inCount, std::optional<bool> inFacingLeft = std::nullopt);
        void RequestUseStatus(std::string inRequestId);
        [[nodiscard]] std::vector<CharacterEvent> ConsumeCharacterEvents();
        // These two accessors belong to the game thread; network workers only publish tagged events.
        void SetSelectedCharacterId(std::uint64_t inId) { selectedCharacterId = inId; }
        [[nodiscard]] std::uint64_t GetSelectedCharacterId() const { return selectedCharacterId; }
        void RequestStop();
        [[nodiscard]] TownConnectionInfo GetConnectionInfo() const;
        void Stop();
        void SendMovement(const TownProtocol::MoveInput& inInput);
        [[nodiscard]] std::optional<std::uint32_t> SendChat(TownProtocol::ChatChannel inChannel, std::string inMessage);
        void RequestDungeon(std::string inZoneId, std::uint32_t inDungeonId);
        void RequestDungeonCompletion(std::uint64_t inRoomId, bool inRetry);
        void RequestQuestState(std::string inRequestId);
        void SendQuestAction(std::string inJson);
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
        void QueueCharacterPacket(std::vector<std::uint8_t> inBody, bool inSelecting = false);
        std::uint64_t selectedCharacterId{};
        std::uint32_t chatSequence{}; // Game thread; resets only for a new Town connection attempt.
        struct Impl;
        std::unique_ptr<Impl> impl;
    };
}
