#include "Game/GameWorld.h"

#include "Graphics/D2DRenderer.h"
#include "Network/TownClient.h"
#include "Resources/AssetCatalog.h"

#include <d2d1_1helper.h>

#include <algorithm>
#include <cmath>
#include <optional>
#include <string_view>
#include <type_traits>
#include <variant>

namespace ActionRPG
{
    GameWorld::GameWorld(const float inViewportWidth, const float inViewportHeight,
        const AssetCatalog& inAssetCatalog, D2DRenderer& inRenderer, TownClient& inTownClient)
        : mapBackground(inAssetCatalog)
        , camera(inViewportWidth, inViewportHeight)
        , player(Vector2{ 640.0f, 640.0f }, inAssetCatalog, inRenderer)
        , projectileSystem(inAssetCatalog)
        , skillCommandSystem(inAssetCatalog)
        , townClient(inTownClient)
    {
        player.SetRunningEnabled(false);
        camera.Follow(player.GetGroundPosition(), gameplayMap.GetWorldLeft(), gameplayMap.GetWorldTop(),
            gameplayMap.GetWorldRight(), gameplayMap.GetWorldBottom());
    }

    void GameWorld::Update(const float inDeltaSeconds, const InputState& inInput)
    {
        ProcessNetworkEvents();
        worldTimeSeconds += inDeltaSeconds;
        projectileSystem.Update(inDeltaSeconds, gameplayMap);
        commandQueue.Record(inInput, worldTimeSeconds);

        const std::optional<SkillActivation> skillActivation = skillCommandSystem.TryActivate(commandQueue);
        if (skillActivation.has_value())
        {
            player.ActivateCommandSkill(skillActivation->effect);
        }

        player.Update(inDeltaSeconds, inInput, gameplayMap);
        SendMovementInput(inInput, inDeltaSeconds);
        UpdateRemotePlayers(inDeltaSeconds);
        while (const std::optional<PlayerProjectileRequest> request = player.ConsumeProjectileRequest())
        {
            const std::string_view definitionId = request->type == PlayerProjectileType::Straight
                ? "PlayerBullet"
                : "PlayerRock";
            projectileSystem.Spawn(
                definitionId,
                request->throwerPosition,
                request->throwerHeight,
                request->direction);
        }
        camera.Follow(player.GetGroundPosition(), gameplayMap.GetWorldLeft(), gameplayMap.GetWorldTop(),
            gameplayMap.GetWorldRight(), gameplayMap.GetWorldBottom());
    }

    void GameWorld::Render(D2DRenderer& inRenderer) const
    {
        const Vector2 worldTopLeft = camera.WorldToScreen(
            { gameplayMap.GetWorldLeft(), gameplayMap.GetWorldTop() });
        const Vector2 worldBottomRight = camera.WorldToScreen(
            { gameplayMap.GetWorldRight(), gameplayMap.GetWorldBottom() });
        const D2D1_RECT_F visibleRectangle = D2D1::RectF(
            std::clamp(worldTopLeft.x, 0.0f, camera.GetViewportWidth()),
            std::clamp(worldTopLeft.y, 0.0f, camera.GetViewportHeight()),
            std::clamp(worldBottomRight.x, 0.0f, camera.GetViewportWidth()),
            std::clamp(worldBottomRight.y, 0.0f, camera.GetViewportHeight()));
        inRenderer.PushAxisAlignedClip(visibleRectangle);
        mapBackground.Render(inRenderer, camera);
        gameplayMap.Render(inRenderer, camera);
        projectileSystem.Render(inRenderer, camera);
        for (const auto& [playerId, remotePlayer] : remotePlayers)
        {
            const Vector2 position = camera.WorldToScreen(remotePlayer.displayedPosition);
            inRenderer.FillEllipse(position.x, position.y, 27.0f, 12.0f,
                D2D1::ColorF(0.02f, 0.03f, 0.05f, 0.45f));
            inRenderer.FillRectangle(position.x - 28.0f, position.y - 96.0f,
                position.x + 28.0f, position.y, D2D1::ColorF(0.35f, 0.68f, 0.95f));
        }
        player.Render(inRenderer, camera);
        inRenderer.PopAxisAlignedClip();
    }

    void GameWorld::Resize(const float inViewportWidth, const float inViewportHeight)
    {
        camera.Resize(inViewportWidth, inViewportHeight);
        camera.Follow(player.GetGroundPosition(), gameplayMap.GetWorldLeft(), gameplayMap.GetWorldTop(),
            gameplayMap.GetWorldRight(), gameplayMap.GetWorldBottom());
    }

    void GameWorld::ProcessNetworkEvents()
    {
        for (TownEvent& event : townClient.ConsumeEvents())
        {
            std::visit([this](auto& inEvent)
            {
                using EventType = std::decay_t<decltype(inEvent)>;
                if constexpr (std::is_same_v<EventType, TownProtocol::EnterTownResponse>)
                {
                    localPlayerId = inEvent.playerId;
                    remotePlayers.clear();
                    gameplayMap.Configure(inEvent.map);
                    mapBackground.Configure(inEvent.map);
                    player.ConfigureMovementSpeeds(inEvent.map.walkSpeed, inEvent.map.runSpeed);
                    player.SetGroundPosition(Vector2{ inEvent.map.spawnX, inEvent.map.spawnY });
                    movementSequence = 0;
                    movementSendAccumulator = 0.0f;
                    lastSentDirectionX = 0;
                    lastSentDirectionY = 0;
                    hasSentMovementInput = false;
                }
                else if constexpr (std::is_same_v<EventType, TownProtocol::PlayerAppear>)
                {
                    if (inEvent.playerId != localPlayerId)
                    {
                        remotePlayers[inEvent.playerId] = RemotePlayerState{
                            inEvent.playerName,
                            Vector2{ inEvent.position.x, inEvent.position.y },
                            Vector2{ inEvent.position.x, inEvent.position.y },
                            Vector2{ inEvent.velocity.x, inEvent.velocity.y },
                            0.0f
                        };
                    }
                }
                else if constexpr (std::is_same_v<EventType, TownProtocol::PlayerMove>)
                {
                    if (inEvent.playerId == localPlayerId)
                    {
                        const bool isAuthoritativeStopped = inEvent.velocity.x == 0.0f
                            && inEvent.velocity.y == 0.0f;
                        player.ReconcileGroundPosition(
                            Vector2{ inEvent.position.x, inEvent.position.y }, isAuthoritativeStopped);
                    }
                    else if (auto iterator = remotePlayers.find(inEvent.playerId); iterator != remotePlayers.end())
                    {
                        const Vector2 snapshotPosition{ inEvent.position.x, inEvent.position.y };
                        const Vector2 velocity{ inEvent.velocity.x, inEvent.velocity.y };
                        iterator->second.snapshotPosition = snapshotPosition;
                        iterator->second.velocity = velocity;
                        iterator->second.secondsSinceSnapshot = 0.0f;
                        if (velocity.x == 0.0f && velocity.y == 0.0f)
                        {
                            iterator->second.displayedPosition = snapshotPosition;
                        }
                    }
                }
                else if constexpr (std::is_same_v<EventType, TownProtocol::PlayerDisappear>)
                {
                    remotePlayers.erase(inEvent.playerId);
                }
            }, event);
        }
    }

    void GameWorld::UpdateRemotePlayers(const float inDeltaSeconds)
    {
        for (auto& [playerId, remotePlayer] : remotePlayers)
        {
            remotePlayer.secondsSinceSnapshot = std::min(remotePlayer.secondsSinceSnapshot + inDeltaSeconds, 0.25f);
            const Vector2 predictedPosition{
                remotePlayer.snapshotPosition.x + remotePlayer.velocity.x * remotePlayer.secondsSinceSnapshot,
                remotePlayer.snapshotPosition.y + remotePlayer.velocity.y * remotePlayer.secondsSinceSnapshot
            };
            const float blend = 1.0f - std::exp(-12.0f * inDeltaSeconds);
            remotePlayer.displayedPosition.x += (predictedPosition.x - remotePlayer.displayedPosition.x) * blend;
            remotePlayer.displayedPosition.y += (predictedPosition.y - remotePlayer.displayedPosition.y) * blend;
        }
    }

    void GameWorld::SendMovementInput(const InputState& inInput, const float inDeltaSeconds)
    {
        constexpr float MOVEMENT_HEARTBEAT_SECONDS = 0.25f;
        movementSendAccumulator += inDeltaSeconds;
        if (localPlayerId == 0)
        {
            return;
        }

        const std::int8_t directionX = static_cast<std::int8_t>(
            static_cast<int>(inInput.moveRight) - static_cast<int>(inInput.moveLeft));
        const std::int8_t directionY = static_cast<std::int8_t>(
            static_cast<int>(inInput.moveDown) - static_cast<int>(inInput.moveUp));
        const bool isMoving = directionX != 0 || directionY != 0;
        const bool stateChanged = !hasSentMovementInput
            || directionX != lastSentDirectionX
            || directionY != lastSentDirectionY;
        if (!stateChanged && (!isMoving || movementSendAccumulator < MOVEMENT_HEARTBEAT_SECONDS))
        {
            return;
        }

        movementSendAccumulator = 0.0f;
        townClient.SendMovement(TownProtocol::MoveInput{
            ++movementSequence,
            directionX,
            directionY,
            false
        });
        lastSentDirectionX = directionX;
        lastSentDirectionY = directionY;
        hasSentMovementInput = true;
    }
}
