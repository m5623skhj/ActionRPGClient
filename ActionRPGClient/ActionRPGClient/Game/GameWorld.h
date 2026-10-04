#pragma once

#include "Core/IniDocument.h"
#include "Game/Camera.h"
#include "Game/DungeonCombatBuffer.h"
#include "Game/DungeonWorld.h"
#include "Game/GameplayMap.h"
#include "Game/InputCommandQueue.h"
#include "Game/MapBackground.h"
#include "Game/Player.h"
#include "Game/ProjectileSystem.h"
#include "Game/SkillUi.h"
#include "Game/PlayerSkillPresentation.h"
#include "Input/InputState.h"
#include "Network/TownClient.h"
#include "Resources/SpriteAnimation.h"

#include <d2d1_1.h>
#include <wrl/client.h>

#include <cstddef>
#include <cstdint>
#include <chrono>
#include <deque>
#include <mutex>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace ActionRPG
{
    class AssetCatalog;
    class D2DRenderer;
    class DungeonClient;
    class TownClient;

    class GameWorld final
    {
    public:
        GameWorld(float inViewportWidth, float inViewportHeight, const AssetCatalog& inAssetCatalog,
            D2DRenderer& inRenderer, TownClient& inTownClient, DungeonClient& inDungeonClient);

        void Update(float inDeltaSeconds, const InputState& inInput);
        void Render(D2DRenderer& inRenderer) const;
        void Resize(float inViewportWidth, float inViewportHeight);
        // Collision/network producers may enqueue hits; Update applies them on the game thread.
        void QueuePlayerHit(CharacterHitType inType);
        [[nodiscard]] std::wstring_view GetDungeonStatusText() const;
        [[nodiscard]] bool ConsumeExitRequested() noexcept;
        [[nodiscard]] bool IsUiOverlayVisible() const noexcept;

    private:
        struct RemotePlayerState
        {
            RemotePlayerState(std::string inName, std::uint32_t inCharacterId,
                Vector2 inPosition, Vector2 inVelocity, const AssetCatalog& inAssetCatalog,
                D2DRenderer& inRenderer, const IniDocument& inAnimationDefinitions,
                const IniDocument& inCharacterDefinitions);

            std::string name;
            std::uint32_t characterId{};
            Vector2 displayedPosition{};
            Vector2 snapshotPosition{};
            Vector2 velocity{};
            float secondsSinceSnapshot{};
            bool facingLeft{};
            SpriteAnimation idleAnimation;
            SpriteAnimation walkAnimation;
        };

        enum class DungeonEntryState
        {
            Idle,
            WaitingRoom,
            Connecting,
            WaitingAuthentication,
            WaitingWorld,
            Entered
        };

        enum class SystemUiPage
        {
            Closed,
            Menu,
            Party,
            PartyDirectory,
            PartyDetails,
            PartyCreate,
            ExitConfirmation,
            Skills
        };

        enum class SystemMenuAction
        {
            Party,
            Exit,
            Skills
        };

        struct SystemMenuEntry
        {
            std::wstring label;
            SystemMenuAction action = SystemMenuAction::Party;
            Microsoft::WRL::ComPtr<ID2D1Bitmap1> icon;
        };

        void ProcessNetworkEvents(const InputState& inInput);
        void ProcessDungeonEvents();
        void ApplyCombatState(DungeonCombatSnapshot inSnapshot);
        void ApplyCombatSnapshot(std::string_view inJson);
        void SendCombatActions(const InputState& inInput);
        void UpdateDungeonCombat(float inDeltaSeconds);
        void RenderCombatProjectiles(D2DRenderer& inRenderer) const;
        void RenderCombatHud(D2DRenderer& inRenderer) const;
        void ProcessPlayerHits();
        void UpdateDungeonCompletion(float inDeltaSeconds, const InputState& inInput);
        void RenderDungeonCompletion(D2DRenderer& inRenderer) const;
        void ProcessDungeonCompletion();
        void UpdateDungeonSelection(const InputState& inInput);
        void UpdateSystemInterface(const InputState& inInput);
        void UpdatePartyInterface(const InputState& inInput);
        [[nodiscard]] bool IsDungeonUiRestricted() const noexcept
        { return dungeonEntryState != DungeonEntryState::Idle; }
        [[nodiscard]] static bool IsPartyUiPage(SystemUiPage inPage) noexcept
        { return inPage==SystemUiPage::Party || inPage==SystemUiPage::PartyDirectory || inPage==SystemUiPage::PartyCreate || inPage==SystemUiPage::PartyDetails; }
        void SetSystemUiPage(SystemUiPage inPage);
        void RequestPartyDirectoryPage(std::uint32_t inPage);
        void RequestSelectedPartyDetail();
        void ResetPartyRequestUi();
        void UpdatePartyNotifications(const InputState& inInput);
        void RenderPartyNotifications(D2DRenderer& inRenderer) const;
        void RenderPartyDetails(D2DRenderer& inRenderer) const;
        [[nodiscard]] bool CanRequestPartyJoin() const noexcept;
        [[nodiscard]] bool IsPartyJoinNoticeVisible() const noexcept;
        void SubmitPartyTitle();
        void SubmitPartyCreation();
        void RequestDungeon(std::uint32_t inDungeonId);
        void ApplyMap(const TownProtocol::MapInfo& inMap, Vector2 inPosition);
        void ResetDungeonEntry();
        void ApplyDungeonMap(const std::string& inMapId, Vector2 inPosition);
        void UpdateRemotePlayers(float inDeltaSeconds);
        void SendMovementInput(const InputState& inInput, float inDeltaSeconds);
        void RenderTransitionZones(D2DRenderer& inRenderer) const;
        void RenderDungeonSelection(D2DRenderer& inRenderer) const;
        void RenderSystemInterface(D2DRenderer& inRenderer) const;
        void RenderPartyInterface(D2DRenderer& inRenderer) const;
        [[nodiscard]] bool IsPartyLeader() const noexcept;

        GameplayMap gameplayMap;
        MapBackground mapBackground;
        Camera camera;
        IniDocument animationDefinitions;
        IniDocument characterDefinitions;
        IniDocument systemMenuDefinitions;
        Player player;
        std::mutex playerHitMutex;
        std::vector<CharacterHitType> pendingPlayerHits;
        ProjectileSystem projectileSystem;
        InputCommandQueue commandQueue;

        PlayerSkillPresentation playerSkillPresentation;
        SkillUi skillUi;
        std::unordered_map<std::uint32_t,std::string> skillActionIds;
        std::uint64_t lastSkillStateTick{};
        std::uint32_t lastAcceptedSkillSequence{};
        bool hasSkillStateTick{};
        const AssetCatalog& assetCatalog;
        D2DRenderer& renderer;
        TownClient& townClient;
        DungeonClient& dungeonClient;
        MonsterCatalog monsterCatalog;
        DungeonCombatBuffer combatBuffer;
        std::optional<DungeonCombatSnapshot> presentationSnapshot;
        std::uint32_t appliedMapEpoch{}, pendingMapEpoch{};
        std::optional<DungeonCombatSnapshot> combatSnapshot;
        std::unordered_map<std::uint64_t, std::unique_ptr<Player>> combatPlayers;
        std::uint64_t lastCombatTick{};
        bool hasCombatTick{}, dungeonCleared{};
        bool completionPending{}, completionStopping{};
        float completionWaitSeconds{};
        std::size_t selectedCompletionIndex{};
        std::wstring completionStatus;
        std::optional<TownProtocol::DungeonCompletionResponse> completionResponse;
        std::vector<TownEvent> deferredTownEvents;
        std::uint32_t combatActionSequence{}, lastCombatActionResult{}, combatInputsThisSecond{};
        float combatInputWindowSeconds{}, combatSnapshotAge{}, rejectedActionSeconds{};
        std::wstring combatStatus;
        std::unordered_map<std::uint64_t, RemotePlayerState> remotePlayers;
        std::vector<TownProtocol::TransitionZone> transitionZones;
        std::uint64_t localPlayerId{};
        std::uint32_t localCharacterId{};
        std::uint32_t movementSequence{};
        float movementSendAccumulator{};
        struct TownMovementSend
        {
            std::uint32_t sequence{};
            std::chrono::steady_clock::time_point sentTime{};
        };
        // Bounded, game-thread-only timestamps for first acknowledgements of older heartbeats.
        std::deque<TownMovementSend> townMovementSends;
        std::uint32_t townMovementStateSequence{};
        std::uint32_t lastTownAcknowledgedSequence{}, lastTownMovementTick{};
        float townSnapshotDelaySeconds{};
        bool hasTownMovementTick{}, hasTownDelaySample{};
        std::int8_t lastSentDirectionX{};
        std::int8_t lastSentDirectionY{};
        bool hasSentMovementInput{};
        double worldTimeSeconds{};
        DungeonEntryState dungeonEntryState = DungeonEntryState::Idle;
        std::size_t selectedDungeonIndex{};
        bool isDungeonSelectionOpen{};
        std::string activeDungeonZoneId;
        std::vector<TownProtocol::DungeonOption> dungeonOptions;
        std::uint64_t dungeonRoomId{};
        std::uint64_t combatSeed{};
        std::optional<DungeonWorld> dungeonWorld;
        std::unordered_map<std::string, std::vector<std::unique_ptr<Monster>>> dungeonMonsters;
        std::string dungeonMapId;
        std::unordered_map<std::uint32_t, std::unique_ptr<Monster>> monsterTemplates;
        std::optional<TownProtocol::MapInfo> lastTownMap;
        Vector2 lastTownPosition{};
        bool lastDungeonRun{};
        std::uint32_t dungeonMovementSequence{};
        std::uint32_t lastDungeonStateSequence{};
        std::vector<SystemMenuEntry> systemMenuEntries;
        SystemUiPage systemUiPage = SystemUiPage::Closed;
        float systemMenuScrollOffset{};
        float uiMouseX{};
        float uiMouseY{};
        bool uiClickConsumed{};
        bool exitRequested{};
        TownProtocol::PartySnapshot partySnapshot;
        TownProtocol::PartyDirectoryPage partyDirectoryPage;
        bool directoryPageRequestPending{};
        std::uint64_t wantedPartyDirectoryRevision{};
        std::uint64_t selectedDirectoryPartyId{};
        std::optional<TownProtocol::PartyDetailResponse> partyDetails;
        bool partyDetailRequestPending{}, partyDetailRefreshNeeded{}, partyJoinSendPending{};
        // Mutated only by Update/ProcessNetworkEvents on the game thread.
        std::vector<TownProtocol::PartyJoinRequestUpdate> partyJoinRequests;
        std::optional<TownProtocol::PartyJoinRequestUpdate> ownPartyJoinRequest;
        std::uint64_t answeringPartyJoinRequestId{};
        bool partyKickedNotice{};
        std::wstring partyTitleDraft;
        std::wstring newPartyTitleDraft;
        bool newPartyIsPublic{};
        bool editingPartyTitle{};
        bool partyCreationPending{};
        std::optional<TownProtocol::PartyInvitation> pendingPartyInvitation;
        bool partyInvitationAnswerPending{};
        std::uint8_t selectedPartySlot{};
        std::wstring partyStatusText;
    };
}
