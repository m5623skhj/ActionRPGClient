#include "Game/GameWorld.h"

#include "Graphics/D2DRenderer.h"
#include "Network/DungeonClient.h"
#include "Network/TownClient.h"
#include "Resources/AssetCatalog.h"

#include <d2d1_1helper.h>

#include <algorithm>
#include <cmath>
#include <optional>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

namespace ActionRPG
{
    namespace
    {
        std::wstring Utf8ToWide(const std::string_view inText)
        {
            if (inText.empty())
            {
                return {};
            }
            const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                inText.data(), static_cast<int>(inText.size()), nullptr, 0);
            if (size <= 0)
            {
                return L"?";
            }
            std::wstring result(static_cast<std::size_t>(size), L'\0');
            if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, inText.data(),
                static_cast<int>(inText.size()), result.data(), size) != size)
            {
                return L"?";
            }
            return result;
        }
    }

    GameWorld::GameWorld(const float inViewportWidth, const float inViewportHeight,
        const AssetCatalog& inAssetCatalog, D2DRenderer& inRenderer, TownClient& inTownClient,
        DungeonClient& inDungeonClient)
        : mapBackground(inAssetCatalog)
        , camera(inViewportWidth, inViewportHeight)
        , player(Vector2{ 640.0f, 640.0f }, inAssetCatalog, inRenderer)
        , projectileSystem(inAssetCatalog)
        , skillCommandSystem(inAssetCatalog)
        , townClient(inTownClient)
        , dungeonClient(inDungeonClient)
    {
        player.SetRunningEnabled(false);
        camera.Follow(player.GetGroundPosition(), gameplayMap.GetWorldLeft(), gameplayMap.GetWorldTop(),
            gameplayMap.GetWorldRight(), gameplayMap.GetWorldBottom());
    }

    void GameWorld::Update(const float inDeltaSeconds, const InputState& inInput)
    {
        ProcessNetworkEvents();
        ProcessDungeonEvents();
        UpdateDungeonSelection(inInput);

        InputState gameplayInput = inInput;
        if (isDungeonSelectionOpen || dungeonEntryState != DungeonEntryState::Idle)
        {
            gameplayInput = InputState{};
        }
        worldTimeSeconds += inDeltaSeconds;
        projectileSystem.Update(inDeltaSeconds, gameplayMap);
        commandQueue.Record(gameplayInput, worldTimeSeconds);

        const std::optional<SkillActivation> skillActivation = skillCommandSystem.TryActivate(commandQueue);
        if (skillActivation.has_value())
        {
            player.ActivateCommandSkill(skillActivation->effect);
        }

        player.Update(inDeltaSeconds, gameplayInput, gameplayMap);
        SendMovementInput(gameplayInput, inDeltaSeconds);
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
        RenderTransitionZones(inRenderer);
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
        RenderDungeonSelection(inRenderer);
    }

    void GameWorld::Resize(const float inViewportWidth, const float inViewportHeight)
    {
        camera.Resize(inViewportWidth, inViewportHeight);
        camera.Follow(player.GetGroundPosition(), gameplayMap.GetWorldLeft(), gameplayMap.GetWorldTop(),
            gameplayMap.GetWorldRight(), gameplayMap.GetWorldBottom());
    }

    std::wstring_view GameWorld::GetDungeonStatusText() const
    {
        switch (dungeonEntryState)
        {
        case DungeonEntryState::Idle:
            return isDungeonSelectionOpen
                ? L"Dungeon: choose a destination"
                : L"Dungeon: approach a dungeon transition zone";
        case DungeonEntryState::WaitingRoom:
            return L"Dungeon: creating room";
        case DungeonEntryState::Connecting:
            return L"Dungeon: connecting";
        case DungeonEntryState::WaitingAuthentication:
            return L"Dungeon: authenticating";
        case DungeonEntryState::Entered:
            return L"Dungeon: entered";
        default:
            return L"Dungeon: unknown";
        }
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
                    movementSequence = 0;
                    ApplyMap(inEvent.map, Vector2{ inEvent.map.spawnX, inEvent.map.spawnY });
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
                        if (inEvent.lastProcessedInput == movementSequence)
                        {
                            player.ReconcileGroundPosition(
                                Vector2{ inEvent.position.x, inEvent.position.y });
                        }
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
                else if constexpr (std::is_same_v<EventType, TownProtocol::EnterDungeonResponse>)
                {
                    if (dungeonEntryState != DungeonEntryState::WaitingRoom)
                    {
                        return;
                    }
                    if (!inEvent.succeeded)
                    {
                        ResetDungeonEntry();
                        return;
                    }

                    dungeonRoomId = inEvent.roomId;
                    combatSeed = inEvent.combatSeed;
                    if (!dungeonClient.Start(
                        std::move(inEvent.sessionBrokerAddress), inEvent.sessionBrokerPort))
                    {
                        ResetDungeonEntry();
                        return;
                    }
                    dungeonEntryState = DungeonEntryState::Connecting;
                }
                else if constexpr (std::is_same_v<EventType, TownProtocol::MapChanged>)
                {
                    ApplyMap(inEvent.map, Vector2{ inEvent.position.x, inEvent.position.y });
                }
                else if constexpr (std::is_same_v<EventType, TownProtocol::DungeonSelectionOpen>)
                {
                    if (dungeonEntryState == DungeonEntryState::Idle && !inEvent.dungeons.empty())
                    {
                        activeDungeonZoneId = std::move(inEvent.zoneId);
                        dungeonOptions = std::move(inEvent.dungeons);
                        selectedDungeonIndex = 0;
                        isDungeonSelectionOpen = true;
                    }
                }
            }, event);
        }
    }

    void GameWorld::ProcessDungeonEvents()
    {
        const DungeonConnectionState connectionState = dungeonClient.GetConnectionState();
        if (dungeonEntryState != DungeonEntryState::Idle
            && dungeonEntryState != DungeonEntryState::WaitingRoom
            && (connectionState == DungeonConnectionState::Failed
                || connectionState == DungeonConnectionState::Stopped))
        {
            ResetDungeonEntry();
            return;
        }

        for (DungeonEvent& event : dungeonClient.ConsumeEvents())
        {
            std::visit([this](auto& inEvent)
            {
                using EventType = std::decay_t<decltype(inEvent)>;
                if constexpr (std::is_same_v<EventType, DungeonChallengeEvent>)
                {
                    if (dungeonEntryState != DungeonEntryState::Connecting
                        || dungeonRoomId == 0 || inEvent.challenge == 0)
                    {
                        ResetDungeonEntry();
                        return;
                    }
                    townClient.ConfirmDungeonJoin(dungeonRoomId, inEvent.challenge);
                    dungeonEntryState = DungeonEntryState::WaitingAuthentication;
                }
                else if constexpr (std::is_same_v<EventType, DungeonAuthResultEvent>)
                {
                    if (dungeonEntryState != DungeonEntryState::WaitingAuthentication
                        || !inEvent.succeeded)
                    {
                        ResetDungeonEntry();
                        return;
                    }
                    dungeonEntryState = DungeonEntryState::Entered;
                }
            }, event);
        }
    }

    void GameWorld::UpdateDungeonSelection(const InputState& inInput)
    {
        if (dungeonEntryState != DungeonEntryState::Idle || localPlayerId == 0
            || !isDungeonSelectionOpen || dungeonOptions.empty())
        {
            return;
        }

        if (inInput.WasPressed(InputKey::ActionC))
        {
            isDungeonSelectionOpen = false;
            activeDungeonZoneId.clear();
            dungeonOptions.clear();
            return;
        }
        if (inInput.WasPressed(InputKey::MoveUp))
        {
            selectedDungeonIndex = selectedDungeonIndex == 0
                ? dungeonOptions.size() - 1
                : selectedDungeonIndex - 1;
        }
        if (inInput.WasPressed(InputKey::MoveDown))
        {
            selectedDungeonIndex = (selectedDungeonIndex + 1) % dungeonOptions.size();
        }
        if (inInput.WasPressed(InputKey::ConfirmSelection))
        {
            const std::uint32_t dungeonId = dungeonOptions[selectedDungeonIndex].dungeonId;
            isDungeonSelectionOpen = false;
            RequestDungeon(dungeonId);
        }
    }

    void GameWorld::RequestDungeon(const std::uint32_t inDungeonId)
    {
        townClient.RequestDungeon(activeDungeonZoneId, inDungeonId);
        dungeonEntryState = DungeonEntryState::WaitingRoom;
    }

    void GameWorld::ApplyMap(const TownProtocol::MapInfo& inMap, const Vector2 inPosition)
    {
        remotePlayers.clear();
        gameplayMap.Configure(inMap);
        mapBackground.Configure(inMap);
        transitionZones = inMap.transitionZones;
        player.ConfigureMovementSpeeds(inMap.walkSpeed, inMap.runSpeed);
        player.SetGroundPosition(inPosition);
        movementSendAccumulator = 0.0f;
        lastSentDirectionX = 0;
        lastSentDirectionY = 0;
        hasSentMovementInput = false;
        selectedDungeonIndex = 0;
        isDungeonSelectionOpen = false;
        activeDungeonZoneId.clear();
        dungeonOptions.clear();
        camera.Follow(player.GetGroundPosition(), gameplayMap.GetWorldLeft(), gameplayMap.GetWorldTop(),
            gameplayMap.GetWorldRight(), gameplayMap.GetWorldBottom());
    }

    void GameWorld::ResetDungeonEntry()
    {
        dungeonClient.Stop();
        dungeonEntryState = DungeonEntryState::Idle;
        dungeonRoomId = 0;
        combatSeed = 0;
        activeDungeonZoneId.clear();
        dungeonOptions.clear();
    }

    void GameWorld::RenderTransitionZones(D2DRenderer& inRenderer) const
    {
        if (localPlayerId == 0)
        {
            return;
        }

        for (const TownProtocol::TransitionZone& zone : transitionZones)
        {
            if (zone.polygon.size() < 3)
            {
                continue;
            }
            const D2D1_COLOR_F color = zone.actionType == TownProtocol::TransitionActionType::MapTransfer
                ? D2D1::ColorF(0.30f, 0.82f, 0.96f)
                : D2D1::ColorF(0.98f, 0.72f, 0.24f);
            Vector2 center{};
            for (std::size_t index = 0; index < zone.polygon.size(); ++index)
            {
                const TownProtocol::Vector2& startWorld = zone.polygon[index];
                const TownProtocol::Vector2& endWorld = zone.polygon[(index + 1) % zone.polygon.size()];
                const Vector2 start = camera.WorldToScreen({ startWorld.x, startWorld.y });
                const Vector2 end = camera.WorldToScreen({ endWorld.x, endWorld.y });
                inRenderer.DrawLine(start.x, start.y, end.x, end.y, color, 3.0f);
                center.x += start.x;
                center.y += start.y;
            }
            center.x /= static_cast<float>(zone.polygon.size());
            center.y /= static_cast<float>(zone.polygon.size());
            const std::wstring label = zone.actionType == TownProtocol::TransitionActionType::MapTransfer
                ? L"MAP EXIT"
                : L"DUNGEON GATE";
            inRenderer.DrawText(label, center.x - 80.0f, center.y - 14.0f,
                center.x + 90.0f, center.y + 18.0f, color);
        }
    }

    void GameWorld::RenderDungeonSelection(D2DRenderer& inRenderer) const
    {
        if (!isDungeonSelectionOpen)
        {
            return;
        }

        const float viewportWidth = camera.GetViewportWidth();
        const float viewportHeight = camera.GetViewportHeight();
        const float panelLeft = std::max(32.0f, viewportWidth * 0.10f);
        const float panelRight = std::max(panelLeft + 480.0f, viewportWidth - panelLeft);
        const float panelTop = 56.0f;
        const float panelBottom = std::max(panelTop + 430.0f, viewportHeight - 56.0f);

        inRenderer.FillRectangle(0.0f, 0.0f, viewportWidth, viewportHeight,
            D2D1::ColorF(0.01f, 0.015f, 0.025f, 0.78f));
        inRenderer.FillRectangle(panelLeft, panelTop, panelRight, panelBottom,
            D2D1::ColorF(0.10f, 0.08f, 0.055f, 0.97f));
        inRenderer.DrawRectangle(panelLeft, panelTop, panelRight, panelBottom,
            D2D1::ColorF(0.82f, 0.62f, 0.24f), 3.0f);
        inRenderer.DrawText(L"SELECT DUNGEON", panelLeft + 28.0f, panelTop + 20.0f,
            panelRight - 28.0f, panelTop + 54.0f, D2D1::ColorF(1.0f, 0.86f, 0.48f));

        constexpr float ENTRY_HEIGHT = 62.0f;
        const float listTop = panelTop + 76.0f;
        const float listRight = panelLeft + (panelRight - panelLeft) * 0.46f;
        constexpr std::size_t MAX_VISIBLE_DUNGEONS = 4;
        const std::size_t firstVisibleIndex = selectedDungeonIndex < MAX_VISIBLE_DUNGEONS
            ? 0
            : selectedDungeonIndex - MAX_VISIBLE_DUNGEONS + 1;
        const std::size_t lastVisibleIndex = std::min(
            dungeonOptions.size(), firstVisibleIndex + MAX_VISIBLE_DUNGEONS);
        for (std::size_t index = firstVisibleIndex; index < lastVisibleIndex; ++index)
        {
            const float entryTop = listTop
                + static_cast<float>(index - firstVisibleIndex) * (ENTRY_HEIGHT + 10.0f);
            const bool selected = index == selectedDungeonIndex;
            inRenderer.FillRectangle(panelLeft + 24.0f, entryTop, listRight, entryTop + ENTRY_HEIGHT,
                selected
                    ? D2D1::ColorF(0.48f, 0.18f, 0.08f, 0.96f)
                    : D2D1::ColorF(0.15f, 0.14f, 0.12f, 0.92f));
            inRenderer.DrawRectangle(panelLeft + 24.0f, entryTop, listRight, entryTop + ENTRY_HEIGHT,
                selected
                    ? D2D1::ColorF(1.0f, 0.72f, 0.25f)
                    : D2D1::ColorF(0.40f, 0.35f, 0.28f),
                selected ? 3.0f : 1.0f);
            const std::wstring name = std::wstring(selected ? L">  " : L"   ")
                + Utf8ToWide(dungeonOptions[index].name);
            inRenderer.DrawText(name, panelLeft + 38.0f, entryTop + 8.0f,
                listRight - 12.0f, entryTop + 35.0f, D2D1::ColorF(0.96f, 0.94f, 0.86f));
            const std::wstring levelRange = Utf8ToWide(dungeonOptions[index].levelRange);
            inRenderer.DrawText(levelRange, panelLeft + 66.0f, entryTop + 34.0f,
                listRight - 12.0f, entryTop + 58.0f, D2D1::ColorF(0.70f, 0.75f, 0.82f));
        }

        const TownProtocol::DungeonOption& selectedDungeon = dungeonOptions[selectedDungeonIndex];
        const std::wstring selectedName = Utf8ToWide(selectedDungeon.name);
        const std::wstring selectedLevel = Utf8ToWide(selectedDungeon.levelRange);
        const std::wstring selectedDescription = Utf8ToWide(selectedDungeon.description);
        const float detailLeft = listRight + 26.0f;
        inRenderer.FillRectangle(detailLeft, listTop, panelRight - 24.0f, listTop + 278.0f,
            D2D1::ColorF(0.07f, 0.09f, 0.12f, 0.95f));
        inRenderer.DrawRectangle(detailLeft, listTop, panelRight - 24.0f, listTop + 278.0f,
            D2D1::ColorF(0.46f, 0.60f, 0.72f), 2.0f);
        inRenderer.DrawText(selectedName, detailLeft + 22.0f, listTop + 20.0f,
            panelRight - 42.0f, listTop + 52.0f, D2D1::ColorF(1.0f, 0.78f, 0.32f));
        inRenderer.DrawText(selectedLevel, detailLeft + 22.0f, listTop + 58.0f,
            panelRight - 42.0f, listTop + 88.0f, D2D1::ColorF(0.72f, 0.83f, 0.96f));
        inRenderer.DrawText(selectedDescription, detailLeft + 22.0f, listTop + 108.0f,
            panelRight - 42.0f, listTop + 142.0f, D2D1::ColorF(0.90f, 0.91f, 0.88f));
        inRenderer.DrawText(L"ENTER  Enter dungeon", detailLeft + 22.0f, listTop + 204.0f,
            panelRight - 42.0f, listTop + 232.0f, D2D1::ColorF(0.98f, 0.82f, 0.42f));
        inRenderer.DrawText(L"C      Return to town", detailLeft + 22.0f, listTop + 238.0f,
            panelRight - 42.0f, listTop + 266.0f, D2D1::ColorF(0.78f, 0.82f, 0.86f));
        inRenderer.DrawText(L"UP / DOWN: Select", panelLeft + 28.0f, panelBottom - 42.0f,
            panelRight - 28.0f, panelBottom - 14.0f, D2D1::ColorF(0.72f, 0.72f, 0.68f));
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
        if (localPlayerId == 0 || dungeonEntryState != DungeonEntryState::Idle)
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
