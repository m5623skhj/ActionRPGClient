#pragma once

#include "Game/Camera.h"
#include "Game/GameplayMap.h"
#include "Game/InputCommandQueue.h"
#include "Game/MapBackground.h"
#include "Game/Player.h"
#include "Game/ProjectileSystem.h"
#include "Game/SkillCommandSystem.h"
#include "Input/InputState.h"
#include "Network/TownProtocol.h"

#include <cstddef>
#include <cstdint>
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
        [[nodiscard]] std::wstring_view GetDungeonStatusText() const;

    private:
        struct RemotePlayerState
        {
            std::string name;
            Vector2 displayedPosition{};
            Vector2 snapshotPosition{};
            Vector2 velocity{};
            float secondsSinceSnapshot{};
        };

        enum class DungeonEntryState
        {
            Idle,
            WaitingRoom,
            Connecting,
            WaitingAuthentication,
            Entered
        };

        void ProcessNetworkEvents(const InputState& inInput);
        void ProcessDungeonEvents();
        void UpdateDungeonSelection(const InputState& inInput);
        void RequestDungeon(std::uint32_t inDungeonId);
        void ApplyMap(const TownProtocol::MapInfo& inMap, Vector2 inPosition);
        void ResetDungeonEntry();
        void UpdateRemotePlayers(float inDeltaSeconds);
        void SendMovementInput(const InputState& inInput, float inDeltaSeconds);
        void RenderTransitionZones(D2DRenderer& inRenderer) const;
        void RenderDungeonSelection(D2DRenderer& inRenderer) const;

        GameplayMap gameplayMap;
        MapBackground mapBackground;
        Camera camera;
        Player player;
        ProjectileSystem projectileSystem;
        InputCommandQueue commandQueue;
        SkillCommandSystem skillCommandSystem;
        TownClient& townClient;
        DungeonClient& dungeonClient;
        std::unordered_map<std::uint64_t, RemotePlayerState> remotePlayers;
        std::vector<TownProtocol::TransitionZone> transitionZones;
        std::uint64_t localPlayerId{};
        std::uint32_t movementSequence{};
        float movementSendAccumulator{};
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
    };
}
