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

#include <cstdint>
#include <string>
#include <unordered_map>

namespace ActionRPG
{
    class AssetCatalog;
    class D2DRenderer;
    class TownClient;

    class GameWorld final
    {
    public:
        GameWorld(float inViewportWidth, float inViewportHeight, const AssetCatalog& inAssetCatalog,
            D2DRenderer& inRenderer, TownClient& inTownClient);

        void Update(float inDeltaSeconds, const InputState& inInput);
        void Render(D2DRenderer& inRenderer) const;
        void Resize(float inViewportWidth, float inViewportHeight);

    private:
        struct RemotePlayerState
        {
            std::string name;
            Vector2 displayedPosition{};
            Vector2 snapshotPosition{};
            Vector2 velocity{};
            float secondsSinceSnapshot{};
        };

        void ProcessNetworkEvents();
        void UpdateRemotePlayers(float inDeltaSeconds);
        void SendMovementInput(const InputState& inInput, float inDeltaSeconds);

        GameplayMap gameplayMap;
        MapBackground mapBackground;
        Camera camera;
        Player player;
        ProjectileSystem projectileSystem;
        InputCommandQueue commandQueue;
        SkillCommandSystem skillCommandSystem;
        TownClient& townClient;
        std::unordered_map<std::uint64_t, RemotePlayerState> remotePlayers;
        std::uint64_t localPlayerId{};
        std::uint32_t movementSequence{};
        float movementSendAccumulator{};
        std::int8_t lastSentDirectionX{};
        std::int8_t lastSentDirectionY{};
        bool hasSentMovementInput{};
        double worldTimeSeconds{};
    };
}
