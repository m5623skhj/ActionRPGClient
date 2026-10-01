#include "Game/GameWorld.h"

#include "Graphics/D2DRenderer.h"
#include "Network/DungeonClient.h"
#include "Network/DungeonProtocol.h"
#include "Network/TownClient.h"
#include "Resources/AssetCatalog.h"

#include <d2d1_1helper.h>

#include <algorithm>
#include <cmath>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

namespace ActionRPG
{
    namespace
    {
        constexpr float MENU_TILE_WIDTH = 166.0f;
        constexpr float MENU_TILE_HEIGHT = 194.0f;
        constexpr float MENU_ICON_SIZE = 128.0f;
        constexpr float MENU_TILE_GAP = 18.0f;

        struct SystemMenuLayout
        {
            float left{};
            float top{};
            float right{};
            float bottom{};
            std::size_t columns{ 1 };
            float maxScrollOffset{};
        };

        struct PartyLayout
        {
            D2D1_RECT_F panel{};
            float listTop{};
            D2D1_RECT_F titleField{};
            D2D1_RECT_F saveButton{};
            D2D1_RECT_F publicButton{};
            D2D1_RECT_F kickButton{};
            D2D1_RECT_F leaveButton{};
        };

        bool ContainsPoint(const D2D1_RECT_F& inRectangle, const float inX, const float inY)
        {
            return inX >= inRectangle.left && inX <= inRectangle.right
                && inY >= inRectangle.top && inY <= inRectangle.bottom;
        }

        SystemMenuLayout CalculateSystemMenuLayout(const float inViewportWidth,
            const float inViewportHeight, const std::size_t inEntryCount)
        {
            constexpr float HORIZONTAL_MARGIN = 52.0f;
            constexpr float VIEW_TOP = 96.0f;
            constexpr float BOTTOM_MARGIN = 48.0f;
            const float availableWidth = std::max(
                MENU_TILE_WIDTH, inViewportWidth - HORIZONTAL_MARGIN * 2.0f);
            std::size_t columns = std::max<std::size_t>(1, static_cast<std::size_t>(
                (availableWidth + MENU_TILE_GAP) / (MENU_TILE_WIDTH + MENU_TILE_GAP)));
            if (inEntryCount > 0)
            {
                columns = std::min(columns, inEntryCount);
            }
            const std::size_t rows = inEntryCount == 0 ? 0
                : (inEntryCount + columns - 1) / columns;
            const float contentWidth = static_cast<float>(columns) * MENU_TILE_WIDTH
                + static_cast<float>(columns - 1) * MENU_TILE_GAP;
            const float viewBottom = std::max(VIEW_TOP + MENU_TILE_HEIGHT,
                inViewportHeight - BOTTOM_MARGIN);
            const float contentHeight = rows == 0 ? 0.0f
                : static_cast<float>(rows) * MENU_TILE_HEIGHT
                    + static_cast<float>(rows - 1) * MENU_TILE_GAP;
            return SystemMenuLayout{
                std::max(20.0f, (inViewportWidth - contentWidth) * 0.5f),
                VIEW_TOP,
                std::min(inViewportWidth - 20.0f,
                    std::max(20.0f, (inViewportWidth - contentWidth) * 0.5f) + contentWidth),
                viewBottom,
                columns,
                std::max(0.0f, contentHeight - (viewBottom - VIEW_TOP))
            };
        }

        D2D1_RECT_F GetSystemMenuTileRectangle(const SystemMenuLayout& inLayout,
            const std::size_t inIndex, const float inScrollOffset)
        {
            const std::size_t column = inIndex % inLayout.columns;
            const std::size_t row = inIndex / inLayout.columns;
            const float left = inLayout.left
                + static_cast<float>(column) * (MENU_TILE_WIDTH + MENU_TILE_GAP);
            const float top = inLayout.top
                + static_cast<float>(row) * (MENU_TILE_HEIGHT + MENU_TILE_GAP)
                - inScrollOffset;
            return D2D1::RectF(left, top, left + MENU_TILE_WIDTH, top + MENU_TILE_HEIGHT);
        }

        PartyLayout CalculatePartyLayout(const float inViewportWidth, const float inViewportHeight)
        {
            const float panelWidth = std::max(560.0f, std::min(820.0f, inViewportWidth - 40.0f));
            const float panelHeight = std::max(560.0f, std::min(600.0f, inViewportHeight - 40.0f));
            const float left = std::max(20.0f, (inViewportWidth - panelWidth) * 0.5f);
            const float top = std::max(20.0f, (inViewportHeight - panelHeight) * 0.5f);
            const D2D1_RECT_F panel = D2D1::RectF(left, top, left + panelWidth, top + panelHeight);
            const float buttonTop = panel.bottom - 76.0f;
            return PartyLayout{
                panel,
                top + 164.0f,
                D2D1::RectF(left + 22.0f, top + 58.0f, panel.right - 150.0f, top + 96.0f),
                D2D1::RectF(panel.right - 138.0f, top + 58.0f, panel.right - 22.0f, top + 96.0f),
                D2D1::RectF(left + 22.0f, top + 108.0f, left + 212.0f, top + 144.0f),
                D2D1::RectF(left + 22.0f, buttonTop, left + 136.0f, buttonTop + 36.0f),
                D2D1::RectF(left + 148.0f, buttonTop, left + 262.0f, buttonTop + 36.0f)
            };
        }

        D2D1_RECT_F GetPartyCreateDialog(const float inViewportWidth,
            const float inViewportHeight)
        {
            const float width = std::min(520.0f, inViewportWidth - 40.0f);
            const float height = 274.0f;
            const float left = (inViewportWidth - width) * 0.5f;
            const float top = (inViewportHeight - height) * 0.5f;
            return D2D1::RectF(left, top, left + width, top + height);
        }

        void AppendPartyTitleInput(std::wstring& outTitle,
            const std::wstring_view inInput)
        {
            for (const wchar_t character : inInput)
            {
                if (character == 8)
                {
                    if (!outTitle.empty())
                    {
                        outTitle.pop_back();
                        if (!outTitle.empty() && outTitle.back() >= 0xD800
                            && outTitle.back() <= 0xDBFF)
                        {
                            outTitle.pop_back();
                        }
                    }
                }
                else if (character >= L' ' && outTitle.size() < 48)
                {
                    outTitle.push_back(character);
                }
            }
        }

        D2D1_RECT_F GetInvitationDialog(const float inViewportWidth)
        {
            const float width = std::min(500.0f, inViewportWidth - 40.0f);
            const float left = (inViewportWidth - width) * 0.5f;
            return D2D1::RectF(left, 72.0f, left + width, 242.0f);
        }

        D2D1_RECT_F GetExitDialog(const float inViewportWidth, const float inViewportHeight)
        {
            const float width = std::min(480.0f, inViewportWidth - 40.0f);
            const float height = 220.0f;
            const float left = (inViewportWidth - width) * 0.5f;
            const float top = (inViewportHeight - height) * 0.5f;
            return D2D1::RectF(left, top, left + width, top + height);
        }

        std::string ResolveCharacterAnimation(const IniDocument& inDefinitions,
            const std::uint32_t inCharacterId, const std::string_view inAnimation)
        {
            const std::string characterSection = "Character" + std::to_string(inCharacterId);
            if (inDefinitions.HasValue(characterSection, inAnimation))
            {
                return inDefinitions.GetValue(characterSection, inAnimation);
            }
            return inDefinitions.GetValue("Default", inAnimation);
        }

        bool IsMoving(const Vector2 inVelocity)
        {
            return inVelocity.x * inVelocity.x + inVelocity.y * inVelocity.y > 1.0f;
        }

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

        std::optional<std::string> WideToUtf8(const std::wstring_view inText)
        {
            if (inText.empty())
            {
                return std::nullopt;
            }
            const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
                inText.data(), static_cast<int>(inText.size()), nullptr, 0, nullptr, nullptr);
            if (size <= 0)
            {
                return std::nullopt;
            }
            std::string result(static_cast<std::size_t>(size), '\0');
            if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
                inText.data(), static_cast<int>(inText.size()), result.data(), size,
                nullptr, nullptr) != size)
            {
                return std::nullopt;
            }
            return result;
        }

        std::wstring_view GetPartyResultText(const TownProtocol::PartyResultCode inResult)
        {
            switch (inResult)
            {
            case TownProtocol::PartyResultCode::Succeeded:
                return L"Party action completed.";
            case TownProtocol::PartyResultCode::PlayerNotFound:
                return L"The selected player is no longer nearby.";
            case TownProtocol::PartyResultCode::AlreadyInParty:
                return L"That player already belongs to a party.";
            case TownProtocol::PartyResultCode::NotInParty:
                return L"You are not in a party.";
            case TownProtocol::PartyResultCode::NotLeader:
                return L"Only the party leader can do that.";
            case TownProtocol::PartyResultCode::PartyFull:
                return L"The party is full.";
            case TownProtocol::PartyResultCode::AlreadyInvited:
                return L"That player already has a pending invitation.";
            case TownProtocol::PartyResultCode::InvitationNotFound:
                return L"The invitation is no longer valid.";
            case TownProtocol::PartyResultCode::InvalidTarget:
                return L"The selected party target is invalid.";
            case TownProtocol::PartyResultCode::Busy:
                return L"The party is entering a dungeon.";
            case TownProtocol::PartyResultCode::InvalidTitle:
                return L"Enter a valid party title (up to 96 UTF-8 bytes).";
            default:
                return L"Party action failed.";
            }
        }
    }

    GameWorld::RemotePlayerState::RemotePlayerState(std::string inName,
        const std::uint32_t inCharacterId, const Vector2 inPosition, const Vector2 inVelocity,
        const AssetCatalog& inAssetCatalog, D2DRenderer& inRenderer,
        const IniDocument& inAnimationDefinitions, const IniDocument& inCharacterDefinitions)
        : name(std::move(inName))
        , characterId(inCharacterId)
        , displayedPosition(inPosition)
        , snapshotPosition(inPosition)
        , velocity(inVelocity)
        , facingLeft(inVelocity.x < 0.0f)
        , idleAnimation(inRenderer, inAssetCatalog, inAnimationDefinitions,
            ResolveCharacterAnimation(inCharacterDefinitions, inCharacterId, "idle_animation"))
        , walkAnimation(inRenderer, inAssetCatalog, inAnimationDefinitions,
            ResolveCharacterAnimation(inCharacterDefinitions, inCharacterId, "walk_animation"))
    {
    }

    GameWorld::GameWorld(const float inViewportWidth, const float inViewportHeight,
        const AssetCatalog& inAssetCatalog, D2DRenderer& inRenderer, TownClient& inTownClient,
        DungeonClient& inDungeonClient)
        : mapBackground(inAssetCatalog)
        , camera(inViewportWidth, inViewportHeight)
        , animationDefinitions(inAssetCatalog.GetDataPath("Animations"))
        , characterDefinitions(inAssetCatalog.GetDataPath("Characters"))
        , systemMenuDefinitions(inAssetCatalog.GetDataPath("SystemMenu"))
        , player(Vector2{ 640.0f, 640.0f }, inAssetCatalog, inRenderer)
        , projectileSystem(inAssetCatalog)
        , skillCommandSystem(inAssetCatalog)
        , assetCatalog(inAssetCatalog)
        , renderer(inRenderer)
        , townClient(inTownClient)
        , dungeonClient(inDungeonClient)
    {
        for (const std::string& section : systemMenuDefinitions.GetSectionNames())
        {
            const std::string actionName = systemMenuDefinitions.GetValue(section, "Action");
            SystemMenuAction action{};
            if (actionName == "Party")
            {
                action = SystemMenuAction::Party;
            }
            else if (actionName == "Exit")
            {
                action = SystemMenuAction::Exit;
            }
            else
            {
                throw std::runtime_error("Unknown system menu action: " + actionName);
            }
            const std::string iconAssetId = systemMenuDefinitions.GetValue(section, "Icon");
            systemMenuEntries.push_back(SystemMenuEntry{
                Utf8ToWide(systemMenuDefinitions.GetValue(section, "Label")),
                action,
                inRenderer.LoadBitmap(inAssetCatalog.GetImagePath(iconAssetId))
            });
        }
        player.SetRunningEnabled(false);
        camera.Follow(player.GetGroundPosition(), gameplayMap.GetWorldLeft(), gameplayMap.GetWorldTop(),
            gameplayMap.GetWorldRight(), gameplayMap.GetWorldBottom());
    }

    void GameWorld::Update(const float inDeltaSeconds, const InputState& inInput)
    {
        uiMouseX = inInput.mouseX;
        uiMouseY = inInput.mouseY;
        uiClickConsumed = false;
        ProcessNetworkEvents(inInput);
        ProcessDungeonEvents();
        ProcessPlayerHits();
        UpdateSystemInterface(inInput);
        UpdatePartyInterface(inInput);
        UpdateDungeonSelection(inInput);

        InputState gameplayInput = inInput;
        if (isDungeonSelectionOpen || systemUiPage != SystemUiPage::Closed
            || pendingPartyInvitation.has_value()
            || (dungeonEntryState != DungeonEntryState::Idle && dungeonEntryState != DungeonEntryState::Entered))
        {
            gameplayInput = InputState{};
        }
        if (player.IsHitReacting())
        {
            gameplayInput = InputState{};
        }
        worldTimeSeconds += inDeltaSeconds;
        projectileSystem.Update(inDeltaSeconds, gameplayMap);
        commandQueue.Record(gameplayInput, worldTimeSeconds);

        if (!player.IsHitReacting())
        {
            const std::optional<SkillActivation> skillActivation = skillCommandSystem.TryActivate(commandQueue);
            if (skillActivation.has_value())
            {
                player.ActivateCommandSkill(skillActivation->effect);
            }
        }

        player.Update(inDeltaSeconds, gameplayInput, gameplayMap);
        SendMovementInput(gameplayInput, inDeltaSeconds);
        UpdateRemotePlayers(inDeltaSeconds);
        while (const std::optional<CharacterProjectileRequest> request = player.ConsumeProjectileRequest())
        {
            const std::string_view definitionId = request->type == CharacterProjectileType::AirStraight
                ? "PlayerAirBullet"
                : request->type == CharacterProjectileType::Straight ? "PlayerBullet" : "PlayerRock";
            projectileSystem.Spawn(
                definitionId,
                request->throwerPosition,
                request->throwerHeight,
                request->direction);
        }
        camera.Follow(player.GetGroundPosition(), gameplayMap.GetWorldLeft(), gameplayMap.GetWorldTop(),
            gameplayMap.GetWorldRight(), gameplayMap.GetWorldBottom());
    }

    void GameWorld::QueuePlayerHit(const CharacterHitType inType)
    {
        std::scoped_lock lock(playerHitMutex);
        pendingPlayerHits.push_back(inType);
    }

    void GameWorld::ProcessPlayerHits()
    {
        std::vector<CharacterHitType> hits;
        {
            std::scoped_lock lock(playerHitMutex);
            hits.swap(pendingPlayerHits);
        }
        for (const CharacterHitType type : hits)
        {
            player.ApplyHit(type);
        }
        if (!hits.empty())
        {
            commandQueue.Clear();
            // Send a stop immediately instead of waiting for the next movement heartbeat.
            SendMovementInput(InputState{}, 0.0f);
        }
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
            if (IsMoving(remotePlayer.velocity))
            {
                remotePlayer.walkAnimation.Draw(
                    inRenderer, position.x, position.y, remotePlayer.facingLeft);
            }
            else
            {
                remotePlayer.idleAnimation.Draw(
                    inRenderer, position.x, position.y, remotePlayer.facingLeft);
            }
        }
        const auto monsters = dungeonMonsters.find(dungeonMapId);
        if (monsters != dungeonMonsters.end())
            for (const auto& monster : monsters->second)
                if (monster->GetGroundPosition().y <= player.GetGroundPosition().y) monster->Render(inRenderer, camera);
        player.Render(inRenderer, camera);
        if (monsters != dungeonMonsters.end())
            for (const auto& monster : monsters->second)
                if (monster->GetGroundPosition().y > player.GetGroundPosition().y) monster->Render(inRenderer, camera);
        inRenderer.PopAxisAlignedClip();
        RenderDungeonSelection(inRenderer);
        RenderSystemInterface(inRenderer);
        RenderPartyInterface(inRenderer);
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
        case DungeonEntryState::WaitingWorld:
            return L"Loading dungeon...";
        case DungeonEntryState::Entered:
            return L"Dungeon: entered";
        default:
            return L"Dungeon: unknown";
        }
    }

    bool GameWorld::ConsumeExitRequested() noexcept
    {
        const bool result = exitRequested;
        exitRequested = false;
        return result;
    }

    bool GameWorld::IsUiOverlayVisible() const noexcept
    {
        return isDungeonSelectionOpen || systemUiPage != SystemUiPage::Closed
            || pendingPartyInvitation.has_value();
    }

    void GameWorld::ProcessNetworkEvents(const InputState& inInput)
    {
        for (TownEvent& event : townClient.ConsumeEvents())
        {
            std::visit([this, &inInput](auto& inEvent)
            {
                using EventType = std::decay_t<decltype(inEvent)>;
                if constexpr (std::is_same_v<EventType, TownProtocol::EnterTownResponse>)
                {
                    localPlayerId = inEvent.playerId;
                    partySnapshot = {};
                    pendingPartyInvitation.reset();
                    partyInvitationAnswerPending = false;
                    SetSystemUiPage(SystemUiPage::Closed);
                    partyStatusText.clear();
                    movementSequence = 0;
                    ApplyMap(inEvent.map, Vector2{ inEvent.map.spawnX, inEvent.map.spawnY });
                }
                else if constexpr (std::is_same_v<EventType, TownProtocol::PlayerAppear>)
                {
                    if (dungeonEntryState == DungeonEntryState::Entered || dungeonEntryState == DungeonEntryState::WaitingWorld) return;
                    if (inEvent.playerId != localPlayerId)
                    {
                        remotePlayers.erase(inEvent.playerId);
                        remotePlayers.try_emplace(
                            inEvent.playerId,
                            std::move(inEvent.playerName),
                            inEvent.characterId,
                            Vector2{ inEvent.position.x, inEvent.position.y },
                            Vector2{ inEvent.velocity.x, inEvent.velocity.y },
                            assetCatalog,
                            renderer,
                            animationDefinitions,
                            characterDefinitions);
                    }
                }
                else if constexpr (std::is_same_v<EventType, TownProtocol::PlayerMove>)
                {
                    if (dungeonEntryState == DungeonEntryState::Entered || dungeonEntryState == DungeonEntryState::WaitingWorld) return;
                    if (inEvent.playerId == localPlayerId)
                    {
                        const std::int8_t currentDirectionX = static_cast<std::int8_t>(
                            static_cast<int>(inInput.moveRight) - static_cast<int>(inInput.moveLeft));
                        const std::int8_t currentDirectionY = static_cast<std::int8_t>(
                            static_cast<int>(inInput.moveDown) - static_cast<int>(inInput.moveUp));
                        const bool inputMatchesSentState = currentDirectionX == lastSentDirectionX
                            && currentDirectionY == lastSentDirectionY;
                        if (inputMatchesSentState && inEvent.lastProcessedInput == movementSequence)
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
                    if (dungeonEntryState != DungeonEntryState::WaitingRoom
                        && dungeonEntryState != DungeonEntryState::Idle)
                    {
                        return;
                    }
                    if (!inEvent.succeeded)
                    {
                        if (dungeonEntryState == DungeonEntryState::WaitingRoom)
                        {
                            ResetDungeonEntry();
                        }
                        return;
                    }

                    isDungeonSelectionOpen = false;
                    SetSystemUiPage(SystemUiPage::Closed);
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
                    if (dungeonEntryState == DungeonEntryState::Idle && !inEvent.dungeons.empty()
                        && systemUiPage == SystemUiPage::Closed
                        && (partySnapshot.partyId == 0 || IsPartyLeader()))
                    {
                        activeDungeonZoneId = std::move(inEvent.zoneId);
                        dungeonOptions = std::move(inEvent.dungeons);
                        selectedDungeonIndex = 0;
                        isDungeonSelectionOpen = true;
                    }
                }
                else if constexpr (std::is_same_v<EventType, TownProtocol::PartyInvitation>)
                {
                    pendingPartyInvitation = std::move(inEvent);
                    partyInvitationAnswerPending = false;
                    partyStatusText = L"Party invitation received.";
                }
                else if constexpr (std::is_same_v<EventType, TownProtocol::PartySnapshot>)
                {
                    const std::uint64_t previousPartyId = partySnapshot.partyId;
                    const bool partyChanged = previousPartyId != inEvent.partyId;
                    partySnapshot = std::move(inEvent);
                    if (partyChanged || !editingPartyTitle)
                    {
                        partyTitleDraft = Utf8ToWide(partySnapshot.title);
                        editingPartyTitle = false;
                    }
                    selectedPartySlot = 0;
                    if (partySnapshot.partyId == 0)
                    {
                        partyStatusText = L"You are not in a party.";
                    }
                    if (previousPartyId != 0 && partySnapshot.partyId == 0
                        && systemUiPage == SystemUiPage::Party)
                    {
                        SetSystemUiPage(SystemUiPage::PartyDirectory);
                    }
                    else if (previousPartyId == 0 && partySnapshot.partyId != 0
                        && (systemUiPage == SystemUiPage::PartyDirectory
                            || systemUiPage == SystemUiPage::PartyCreate))
                    {
                        partyCreationPending = false;
                        SetSystemUiPage(SystemUiPage::Party);
                    }
                }
                else if constexpr (std::is_same_v<EventType, TownProtocol::PartyOperationResult>)
                {
                    partyStatusText = GetPartyResultText(inEvent.result);
                    if (inEvent.operation == TownProtocol::PartyOperationType::Create
                        && inEvent.result != TownProtocol::PartyResultCode::Succeeded)
                    {
                        partyCreationPending = false;
                    }
                    if (inEvent.operation == TownProtocol::PartyOperationType::AnswerInvitation)
                    {
                        if (inEvent.result != TownProtocol::PartyResultCode::Busy)
                        {
                            pendingPartyInvitation.reset();
                        }
                        partyInvitationAnswerPending = false;
                    }
                }
                else if constexpr (std::is_same_v<EventType, TownProtocol::PartyDirectoryPage>)
                {
                    if (systemUiPage == SystemUiPage::PartyDirectory)
                    {
                        partyDirectoryPage = std::move(inEvent);
                        directoryPageRequestPending = false;
                    }
                }
                else if constexpr (std::is_same_v<EventType, TownProtocol::PartyDirectoryChanged>)
                {
                    if (systemUiPage == SystemUiPage::PartyDirectory
                        && !directoryPageRequestPending
                        && inEvent.revision > partyDirectoryPage.revision)
                    {
                        RequestPartyDirectoryPage(std::max(1U, partyDirectoryPage.page));
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
                    dungeonEntryState = DungeonEntryState::WaitingWorld;
                    dungeonClient.RequestWorld();
                }
                else if constexpr (std::is_same_v<EventType, DungeonWorldEvent>)
                {
                    if (dungeonEntryState != DungeonEntryState::WaitingWorld) { ResetDungeonEntry(); return; }
                    try
                    {
                        dungeonWorld = DungeonWorld::Parse(inEvent.json, dungeonRoomId, localPlayerId);
                        dungeonMovementSequence = lastDungeonStateSequence = 0;
                        lastTownPosition = player.GetGroundPosition();
                        ApplyDungeonMap(dungeonWorld->GetEntryMapId(), dungeonWorld->GetSpawn());
                        dungeonEntryState = DungeonEntryState::Entered;
                    }
                    catch (const std::exception&) { ResetDungeonEntry(); }
                }
                else if constexpr (std::is_same_v<EventType, DungeonPlayerStateEvent>)
                {
                    if (dungeonEntryState != DungeonEntryState::Entered || !dungeonWorld
                        || inEvent.sequence <= lastDungeonStateSequence) return;
                    lastDungeonStateSequence = inEvent.sequence;
                    try
                    {
                        if (inEvent.mapId != dungeonMapId) ApplyDungeonMap(inEvent.mapId, {inEvent.x, inEvent.y});
                        else if (inEvent.sequence == dungeonMovementSequence)
                            player.ReconcileGroundPosition({inEvent.x, inEvent.y});
                    }
                    catch (const std::exception&) { ResetDungeonEntry(); }
                }
            }, event);
        }
    }

    void GameWorld::UpdateDungeonSelection(const InputState& inInput)
    {
        if (dungeonEntryState != DungeonEntryState::Idle || localPlayerId == 0
            || systemUiPage != SystemUiPage::Closed || pendingPartyInvitation.has_value()
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

    void GameWorld::SetSystemUiPage(const SystemUiPage inPage)
    {
        SystemUiPage page = inPage;
        if (page == SystemUiPage::Party || page == SystemUiPage::PartyDirectory)
        {
            page = partySnapshot.partyId != 0 ? SystemUiPage::Party
                : SystemUiPage::PartyDirectory;
        }
        else if (page == SystemUiPage::PartyCreate && partySnapshot.partyId != 0)
        {
            page = SystemUiPage::Party;
        }
        if (systemUiPage == page)
        {
            return;
        }
        if (systemUiPage == SystemUiPage::PartyDirectory)
        {
            townClient.UnsubscribePartyDirectory();
            directoryPageRequestPending = false;
        }
        if (page == SystemUiPage::PartyDirectory)
        {
            partyDirectoryPage = {};
            partyDirectoryPage.page = 1;
            partyDirectoryPage.totalPages = 1;
            RequestPartyDirectoryPage(1);
        }
        if (page == SystemUiPage::Party)
        {
            partyTitleDraft = Utf8ToWide(partySnapshot.title);
            editingPartyTitle = false;
        }
        if (page == SystemUiPage::PartyCreate)
        {
            newPartyTitleDraft.clear();
            newPartyIsPublic = false;
            partyCreationPending = false;
            partyStatusText.clear();
        }
        systemUiPage = page;
    }

    void GameWorld::RequestPartyDirectoryPage(const std::uint32_t inPage)
    {
        if (directoryPageRequestPending)
        {
            return;
        }
        directoryPageRequestPending = true;
        townClient.RequestPartyDirectoryPage(inPage);
    }

    void GameWorld::SubmitPartyTitle()
    {
        const std::optional<std::string> title = WideToUtf8(partyTitleDraft);
        if (!title.has_value() || title->size() > 96)
        {
            partyStatusText = L"Enter a valid party title (up to 96 UTF-8 bytes).";
            return;
        }
        townClient.UpdatePartySettings(*title, partySnapshot.isPublic);
        editingPartyTitle = false;
    }

    void GameWorld::SubmitPartyCreation()
    {
        if (partyCreationPending || localPlayerId == 0)
        {
            return;
        }
        const std::optional<std::string> title = WideToUtf8(newPartyTitleDraft);
        if (!title.has_value() || title->size() > 96
            || title->find_first_not_of(' ') == std::string::npos)
        {
            partyStatusText = L"파티 제목을 입력하세요 (UTF-8 최대 96바이트).";
            return;
        }
        partyCreationPending = true;
        townClient.CreateParty(*title, newPartyIsPublic);
    }

    void GameWorld::UpdateSystemInterface(const InputState& inInput)
    {
        if (pendingPartyInvitation.has_value())
        {
            return;
        }
        if (inInput.WasPressed(InputKey::ToggleSystemMenu))
        {
            if (systemUiPage == SystemUiPage::Closed)
            {
                SetSystemUiPage(SystemUiPage::Menu);
                isDungeonSelectionOpen = false;
                activeDungeonZoneId.clear();
                dungeonOptions.clear();
            }
            else if (systemUiPage == SystemUiPage::Menu)
            {
                SetSystemUiPage(SystemUiPage::Closed);
            }
            else
            {
                SetSystemUiPage(SystemUiPage::Menu);
            }
            return;
        }

        if (systemUiPage == SystemUiPage::ExitConfirmation && inInput.leftMousePressed)
        {
            const D2D1_RECT_F dialog = GetExitDialog(
                camera.GetViewportWidth(), camera.GetViewportHeight());
            const float middle = (dialog.left + dialog.right) * 0.5f;
            const D2D1_RECT_F cancelButton = D2D1::RectF(
                dialog.left + 28.0f, dialog.bottom - 62.0f, middle - 10.0f, dialog.bottom - 20.0f);
            const D2D1_RECT_F exitButton = D2D1::RectF(
                middle + 10.0f, dialog.bottom - 62.0f, dialog.right - 28.0f, dialog.bottom - 20.0f);
            if (ContainsPoint(cancelButton, inInput.clickX, inInput.clickY))
            {
                SetSystemUiPage(SystemUiPage::Menu);
                uiClickConsumed = true;
            }
            else if (ContainsPoint(exitButton, inInput.clickX, inInput.clickY))
            {
                exitRequested = true;
                uiClickConsumed = true;
            }
            return;
        }

        if (systemUiPage != SystemUiPage::Menu)
        {
            return;
        }

        const SystemMenuLayout layout = CalculateSystemMenuLayout(
            camera.GetViewportWidth(), camera.GetViewportHeight(), systemMenuEntries.size());
        if (inInput.mouseWheelDelta != 0)
        {
            constexpr float SCROLL_PIXELS_PER_WHEEL_STEP = 84.0f;
            systemMenuScrollOffset = std::clamp(
                systemMenuScrollOffset
                    - static_cast<float>(inInput.mouseWheelDelta) / 120.0f
                        * SCROLL_PIXELS_PER_WHEEL_STEP,
                0.0f,
                layout.maxScrollOffset);
        }
        else
        {
            systemMenuScrollOffset = std::clamp(
                systemMenuScrollOffset, 0.0f, layout.maxScrollOffset);
        }

        if (!inInput.leftMousePressed
            || !ContainsPoint(D2D1::RectF(layout.left, layout.top, layout.right, layout.bottom),
                inInput.clickX, inInput.clickY))
        {
            return;
        }
        for (std::size_t index = 0; index < systemMenuEntries.size(); ++index)
        {
            const D2D1_RECT_F tile = GetSystemMenuTileRectangle(
                layout, index, systemMenuScrollOffset);
            if (!ContainsPoint(tile, inInput.clickX, inInput.clickY))
            {
                continue;
            }
            SetSystemUiPage(systemMenuEntries[index].action == SystemMenuAction::Party
                ? SystemUiPage::Party : SystemUiPage::ExitConfirmation);
            uiClickConsumed = true;
            return;
        }
    }

    void GameWorld::UpdatePartyInterface(const InputState& inInput)
    {
        if (pendingPartyInvitation.has_value())
        {
            if (partyInvitationAnswerPending || !inInput.leftMousePressed || uiClickConsumed)
            {
                return;
            }
            const D2D1_RECT_F dialog = GetInvitationDialog(camera.GetViewportWidth());
            const float middle = (dialog.left + dialog.right) * 0.5f;
            const D2D1_RECT_F acceptButton = D2D1::RectF(
                dialog.left + 24.0f, dialog.bottom - 58.0f, middle - 8.0f, dialog.bottom - 18.0f);
            const D2D1_RECT_F declineButton = D2D1::RectF(
                middle + 8.0f, dialog.bottom - 58.0f, dialog.right - 24.0f, dialog.bottom - 18.0f);
            if (ContainsPoint(acceptButton, inInput.clickX, inInput.clickY))
            {
                townClient.AnswerPartyInvitation(
                    pendingPartyInvitation->invitationId, true);
                partyInvitationAnswerPending = true;
                uiClickConsumed = true;
            }
            else if (ContainsPoint(declineButton, inInput.clickX, inInput.clickY))
            {
                townClient.AnswerPartyInvitation(
                    pendingPartyInvitation->invitationId, false);
                partyInvitationAnswerPending = true;
                uiClickConsumed = true;
            }
            return;
        }

        if (systemUiPage == SystemUiPage::PartyDirectory)
        {
            if (!inInput.leftMousePressed || uiClickConsumed)
            {
                return;
            }
            const PartyLayout layout = CalculatePartyLayout(
                camera.GetViewportWidth(), camera.GetViewportHeight());
            const float center = (layout.panel.left + layout.panel.right) * 0.5f;
            const float top = layout.panel.bottom - 60.0f;
            if (ContainsPoint(D2D1::RectF(layout.panel.right - 170.0f,
                layout.panel.top + 54.0f, layout.panel.right - 22.0f,
                layout.panel.top + 90.0f), inInput.clickX, inInput.clickY))
            {
                SetSystemUiPage(SystemUiPage::PartyCreate);
                uiClickConsumed = true;
            }
            else if (!directoryPageRequestPending && partyDirectoryPage.page > 1
                && ContainsPoint(D2D1::RectF(center - 150.0f, top, center - 80.0f, top + 36.0f),
                    inInput.clickX, inInput.clickY))
            {
                RequestPartyDirectoryPage(partyDirectoryPage.page - 1);
                uiClickConsumed = true;
            }
            else if (!directoryPageRequestPending
                && partyDirectoryPage.page < partyDirectoryPage.totalPages
                && ContainsPoint(D2D1::RectF(center + 80.0f, top, center + 150.0f, top + 36.0f),
                    inInput.clickX, inInput.clickY))
            {
                RequestPartyDirectoryPage(partyDirectoryPage.page + 1);
                uiClickConsumed = true;
            }
            return;
        }

        if (systemUiPage == SystemUiPage::PartyCreate)
        {
            AppendPartyTitleInput(newPartyTitleDraft, inInput.textInput);
            if (inInput.WasPressed(InputKey::ConfirmSelection))
            {
                SubmitPartyCreation();
                return;
            }
            if (!inInput.leftMousePressed || uiClickConsumed)
            {
                return;
            }
            const D2D1_RECT_F dialog = GetPartyCreateDialog(
                camera.GetViewportWidth(), camera.GetViewportHeight());
            const D2D1_RECT_F createButton = D2D1::RectF(
                dialog.left + 24.0f, dialog.bottom - 64.0f,
                dialog.left + 154.0f, dialog.bottom - 24.0f);
            const D2D1_RECT_F cancelButton = D2D1::RectF(
                dialog.left + 168.0f, dialog.bottom - 64.0f,
                dialog.left + 298.0f, dialog.bottom - 24.0f);
            const D2D1_RECT_F visibilityCheckbox = D2D1::RectF(
                dialog.left + 24.0f, dialog.top + 140.0f,
                dialog.right - 24.0f, dialog.top + 170.0f);
            if (ContainsPoint(createButton, inInput.clickX, inInput.clickY))
            {
                SubmitPartyCreation();
                uiClickConsumed = true;
            }
            else if (ContainsPoint(cancelButton, inInput.clickX, inInput.clickY))
            {
                SetSystemUiPage(SystemUiPage::PartyDirectory);
                uiClickConsumed = true;
            }
            else if (ContainsPoint(visibilityCheckbox, inInput.clickX, inInput.clickY)
                && !partyCreationPending)
            {
                newPartyIsPublic = !newPartyIsPublic;
                uiClickConsumed = true;
            }
            return;
        }

        if (systemUiPage != SystemUiPage::Party || localPlayerId == 0)
        {
            return;
        }

        if (editingPartyTitle)
        {
            AppendPartyTitleInput(partyTitleDraft, inInput.textInput);
            if (inInput.WasPressed(InputKey::ConfirmSelection))
            {
                SubmitPartyTitle();
                return;
            }
        }
        if (!inInput.leftMousePressed || uiClickConsumed)
        {
            return;
        }

        const PartyLayout layout = CalculatePartyLayout(
            camera.GetViewportWidth(), camera.GetViewportHeight());
        if (IsPartyLeader() && ContainsPoint(layout.titleField,
            inInput.clickX, inInput.clickY))
        {
            editingPartyTitle = true;
            uiClickConsumed = true;
            return;
        }
        if (IsPartyLeader() && ContainsPoint(layout.saveButton,
            inInput.clickX, inInput.clickY))
        {
            SubmitPartyTitle();
            uiClickConsumed = true;
            return;
        }
        if (IsPartyLeader() && ContainsPoint(layout.publicButton,
            inInput.clickX, inInput.clickY))
        {
            townClient.UpdatePartySettings(partySnapshot.title, !partySnapshot.isPublic);
            uiClickConsumed = true;
            return;
        }
        constexpr float ROW_HEIGHT = 38.0f;
        for (std::size_t index = 0; index < partySnapshot.members.size(); ++index)
        {
            const float top = layout.listTop + static_cast<float>(index) * ROW_HEIGHT;
            const D2D1_RECT_F row = D2D1::RectF(
                layout.panel.left + 22.0f, top, layout.panel.right - 22.0f,
                top + ROW_HEIGHT - 4.0f);
            if (ContainsPoint(row, inInput.clickX, inInput.clickY))
            {
                selectedPartySlot = partySnapshot.members[index].slot;
                uiClickConsumed = true;
                return;
            }
        }

        if (ContainsPoint(layout.leaveButton, inInput.clickX, inInput.clickY)
            && partySnapshot.partyId != 0)
        {
            townClient.LeaveParty();
            uiClickConsumed = true;
            return;
        }
        if (ContainsPoint(layout.kickButton, inInput.clickX, inInput.clickY) && IsPartyLeader())
        {
            const auto member = std::find_if(partySnapshot.members.begin(),
                partySnapshot.members.end(), [this](const TownProtocol::PartyMemberInfo& inMember)
                {
                    return inMember.slot == selectedPartySlot;
                });
            if (member != partySnapshot.members.end() && member->playerId != localPlayerId)
            {
                townClient.KickPartyMember(member->playerId);
            }
            uiClickConsumed = true;
        }
    }

    void GameWorld::RequestDungeon(const std::uint32_t inDungeonId)
    {
        if (partySnapshot.partyId != 0 && !IsPartyLeader())
        {
            partyStatusText = L"Only the party leader can enter a dungeon.";
            return;
        }
        townClient.RequestDungeon(activeDungeonZoneId, inDungeonId);
        dungeonEntryState = DungeonEntryState::WaitingRoom;
    }

    void GameWorld::ApplyMap(const TownProtocol::MapInfo& inMap, const Vector2 inPosition)
    {
        if (dungeonEntryState != DungeonEntryState::Idle) ResetDungeonEntry();
        lastTownMap = inMap;
        remotePlayers.clear();
        gameplayMap.Configure(inMap);
        mapBackground.Configure(inMap);
        transitionZones = inMap.transitionZones;
        player.ConfigureMovementSpeeds(inMap.walkSpeed, inMap.runSpeed);
        player.SetGroundPosition(inPosition);
        player.ResetActionState();
        commandQueue.Clear();
        projectileSystem.Clear();
        {
            std::scoped_lock lock(playerHitMutex);
            pendingPlayerHits.clear();
        }
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

    void GameWorld::ApplyDungeonMap(const std::string& inMapId, const Vector2 inPosition)
    {
        const auto& room = dungeonWorld->GetMap(inMapId);
        if (!dungeonMonsters.contains(inMapId))
        {
            std::vector<std::unique_ptr<Monster>> monsters;
            for (const auto& spawn : room.monsters)
            {
                // One image set is loaded; copies share the bitmap resources.
                if (!dummyTemplate) dummyTemplate = std::make_unique<Monster>(spawn, assetCatalog, renderer);
                monsters.push_back(std::make_unique<Monster>(*dummyTemplate, spawn));
            }
            std::sort(monsters.begin(), monsters.end(), [](const auto& a, const auto& b)
                { return a->GetGroundPosition().y < b->GetGroundPosition().y; });
            dungeonMonsters.emplace(inMapId, std::move(monsters));
        }
        gameplayMap.Configure(room.map);
        mapBackground.Configure(room.map);
        transitionZones = room.map.transitionZones;
        dungeonMapId = inMapId;
        remotePlayers.clear();
        player.SetGroundPosition(inPosition); player.ResetActionState(); player.ResetMovementSpeeds(); player.SetRunningEnabled(true);
        commandQueue.Clear();
        projectileSystem.Clear();
        { std::scoped_lock lock(playerHitMutex); pendingPlayerHits.clear(); }
        hasSentMovementInput = false; lastDungeonRun = false; movementSendAccumulator = 0;
        isDungeonSelectionOpen = false; dungeonOptions.clear();
        camera.Follow(inPosition, gameplayMap.GetWorldLeft(), gameplayMap.GetWorldTop(),
            gameplayMap.GetWorldRight(), gameplayMap.GetWorldBottom());
    }

    void GameWorld::ResetDungeonEntry()
    {
        dungeonClient.Stop();
        const bool restoreTown = dungeonWorld.has_value();
        dungeonWorld.reset(); dungeonMonsters.clear(); dungeonMapId.clear();
        player.SetRunningEnabled(false);
        dungeonEntryState = DungeonEntryState::Idle;
        dungeonRoomId = 0;
        combatSeed = 0;
        if (restoreTown && lastTownMap)
        {
            const auto map = *lastTownMap;
            ApplyMap(map, lastTownPosition);
        }
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

    void GameWorld::RenderSystemInterface(D2DRenderer& inRenderer) const
    {
        if (systemUiPage == SystemUiPage::Closed && !pendingPartyInvitation.has_value())
        {
            return;
        }

        const float viewportWidth = camera.GetViewportWidth();
        const float viewportHeight = camera.GetViewportHeight();
        inRenderer.FillRectangle(0.0f, 0.0f, viewportWidth, viewportHeight,
            D2D1::ColorF(0.22f, 0.23f, 0.25f, 0.76f));

        if (systemUiPage == SystemUiPage::Menu)
        {
            inRenderer.DrawText(L"SYSTEM MENU", 52.0f, 34.0f,
                viewportWidth - 52.0f, 70.0f, D2D1::ColorF(0.96f, 0.92f, 0.82f));
            inRenderer.DrawText(L"ESC  Close", viewportWidth - 190.0f, 38.0f,
                viewportWidth - 42.0f, 68.0f, D2D1::ColorF(0.72f, 0.74f, 0.78f));

            const SystemMenuLayout layout = CalculateSystemMenuLayout(
                viewportWidth, viewportHeight, systemMenuEntries.size());
            inRenderer.PushAxisAlignedClip(D2D1::RectF(
                layout.left, layout.top, layout.right, layout.bottom));
            for (std::size_t index = 0; index < systemMenuEntries.size(); ++index)
            {
                const D2D1_RECT_F tile = GetSystemMenuTileRectangle(
                    layout, index, systemMenuScrollOffset);
                const bool hovered = ContainsPoint(tile, uiMouseX, uiMouseY)
                    && ContainsPoint(D2D1::RectF(
                        layout.left, layout.top, layout.right, layout.bottom), uiMouseX, uiMouseY);
                inRenderer.FillRectangle(tile.left, tile.top, tile.right, tile.bottom,
                    hovered ? D2D1::ColorF(0.18f, 0.15f, 0.10f, 0.98f)
                        : D2D1::ColorF(0.045f, 0.055f, 0.075f, 0.96f));
                inRenderer.DrawRectangle(tile.left, tile.top, tile.right, tile.bottom,
                    hovered ? D2D1::ColorF(1.0f, 0.76f, 0.28f)
                        : D2D1::ColorF(0.48f, 0.54f, 0.62f), hovered ? 3.0f : 1.5f);
                const float iconLeft = tile.left + (MENU_TILE_WIDTH - MENU_ICON_SIZE) * 0.5f;
                const float iconTop = tile.top + 12.0f;
                if (systemMenuEntries[index].icon)
                {
                    const D2D1_SIZE_F bitmapSize = systemMenuEntries[index].icon->GetSize();
                    inRenderer.DrawBitmap(systemMenuEntries[index].icon.Get(),
                        D2D1::RectF(0.0f, 0.0f, bitmapSize.width, bitmapSize.height),
                        D2D1::RectF(iconLeft, iconTop,
                            iconLeft + MENU_ICON_SIZE, iconTop + MENU_ICON_SIZE));
                }
                inRenderer.DrawText(systemMenuEntries[index].label,
                    tile.left + 12.0f, tile.bottom - 42.0f,
                    tile.right - 12.0f, tile.bottom - 12.0f,
                    D2D1::ColorF(0.94f, 0.94f, 0.92f));
            }
            inRenderer.PopAxisAlignedClip();

            if (layout.maxScrollOffset > 0.0f)
            {
                const float trackLeft = layout.right + 10.0f;
                const float trackHeight = layout.bottom - layout.top;
                const float handleHeight = std::max(36.0f,
                    trackHeight * trackHeight / (trackHeight + layout.maxScrollOffset));
                const float handleTravel = trackHeight - handleHeight;
                const float handleTop = layout.top + handleTravel
                    * systemMenuScrollOffset / layout.maxScrollOffset;
                inRenderer.FillRectangle(trackLeft, layout.top, trackLeft + 6.0f, layout.bottom,
                    D2D1::ColorF(0.12f, 0.13f, 0.15f, 0.80f));
                inRenderer.FillRectangle(trackLeft, handleTop, trackLeft + 6.0f,
                    handleTop + handleHeight, D2D1::ColorF(0.86f, 0.68f, 0.28f));
            }
        }
        else if (systemUiPage == SystemUiPage::ExitConfirmation)
        {
            const D2D1_RECT_F dialog = GetExitDialog(viewportWidth, viewportHeight);
            const float middle = (dialog.left + dialog.right) * 0.5f;
            const D2D1_RECT_F cancelButton = D2D1::RectF(
                dialog.left + 28.0f, dialog.bottom - 62.0f, middle - 10.0f, dialog.bottom - 20.0f);
            const D2D1_RECT_F exitButton = D2D1::RectF(
                middle + 10.0f, dialog.bottom - 62.0f, dialog.right - 28.0f, dialog.bottom - 20.0f);
            inRenderer.FillRectangle(dialog.left, dialog.top, dialog.right, dialog.bottom,
                D2D1::ColorF(0.055f, 0.045f, 0.04f, 0.99f));
            inRenderer.DrawRectangle(dialog.left, dialog.top, dialog.right, dialog.bottom,
                D2D1::ColorF(0.90f, 0.66f, 0.24f), 2.0f);
            inRenderer.DrawText(L"게임을 종료하시겠습니까?", dialog.left + 28.0f, dialog.top + 34.0f,
                dialog.right - 28.0f, dialog.top + 78.0f, D2D1::ColorF(0.96f, 0.93f, 0.86f));
            inRenderer.FillRectangle(cancelButton.left, cancelButton.top,
                cancelButton.right, cancelButton.bottom, D2D1::ColorF(0.18f, 0.20f, 0.24f));
            inRenderer.FillRectangle(exitButton.left, exitButton.top,
                exitButton.right, exitButton.bottom, D2D1::ColorF(0.44f, 0.12f, 0.09f));
            inRenderer.DrawText(L"취소", cancelButton.left + 16.0f, cancelButton.top + 7.0f,
                cancelButton.right - 12.0f, cancelButton.bottom - 4.0f, D2D1::ColorF(0.92f, 0.94f, 0.96f));
            inRenderer.DrawText(L"게임 종료", exitButton.left + 16.0f, exitButton.top + 7.0f,
                exitButton.right - 12.0f, exitButton.bottom - 4.0f, D2D1::ColorF(1.0f, 0.88f, 0.78f));
        }
    }

    void GameWorld::RenderPartyInterface(D2DRenderer& inRenderer) const
    {
        if (systemUiPage == SystemUiPage::Party)
        {
            const PartyLayout layout = CalculatePartyLayout(
                camera.GetViewportWidth(), camera.GetViewportHeight());
            inRenderer.FillRectangle(layout.panel.left, layout.panel.top,
                layout.panel.right, layout.panel.bottom,
                D2D1::ColorF(0.035f, 0.045f, 0.065f, 0.98f));
            inRenderer.DrawRectangle(layout.panel.left, layout.panel.top,
                layout.panel.right, layout.panel.bottom,
                D2D1::ColorF(0.45f, 0.68f, 0.88f), 2.0f);
            inRenderer.DrawText(L"내 파티", layout.panel.left + 22.0f, layout.panel.top + 14.0f,
                layout.panel.right - 160.0f, layout.panel.top + 44.0f,
                D2D1::ColorF(0.90f, 0.94f, 1.0f));
            inRenderer.DrawText(L"ESC  메뉴로", layout.panel.right - 150.0f,
                layout.panel.top + 16.0f, layout.panel.right - 20.0f,
                layout.panel.top + 44.0f, D2D1::ColorF(0.68f, 0.74f, 0.82f));
            inRenderer.DrawText(L"파티 제목", layout.panel.left + 22.0f,
                layout.panel.top + 42.0f, layout.panel.right - 22.0f,
                layout.panel.top + 62.0f, D2D1::ColorF(0.82f, 0.86f, 0.92f));
            inRenderer.FillRectangle(layout.titleField.left, layout.titleField.top,
                layout.titleField.right, layout.titleField.bottom,
                editingPartyTitle ? D2D1::ColorF(0.22f, 0.20f, 0.12f)
                    : D2D1::ColorF(0.08f, 0.10f, 0.14f));
            inRenderer.DrawText(partyTitleDraft, layout.titleField.left + 12.0f,
                layout.titleField.top + 7.0f, layout.titleField.right - 8.0f,
                layout.titleField.bottom - 2.0f, D2D1::ColorF(0.94f, 0.94f, 0.92f));
            inRenderer.FillRectangle(layout.saveButton.left, layout.saveButton.top,
                layout.saveButton.right, layout.saveButton.bottom,
                IsPartyLeader() ? D2D1::ColorF(0.16f, 0.34f, 0.50f)
                    : D2D1::ColorF(0.12f, 0.13f, 0.15f));
            inRenderer.DrawText(L"제목 저장", layout.saveButton.left + 12.0f,
                layout.saveButton.top + 7.0f, layout.saveButton.right - 8.0f,
                layout.saveButton.bottom - 2.0f, D2D1::ColorF(0.92f, 0.95f, 1.0f));
            inRenderer.FillRectangle(layout.publicButton.left, layout.publicButton.top,
                layout.publicButton.right, layout.publicButton.bottom,
                IsPartyLeader() ? D2D1::ColorF(0.17f, 0.35f, 0.25f)
                    : D2D1::ColorF(0.12f, 0.13f, 0.15f));
            inRenderer.DrawText(partySnapshot.isPublic ? L"공개 파티 · 변경" : L"비공개 파티 · 변경",
                layout.publicButton.left + 12.0f, layout.publicButton.top + 7.0f,
                layout.publicButton.right - 8.0f, layout.publicButton.bottom - 2.0f,
                D2D1::ColorF(0.92f, 0.95f, 1.0f));
            constexpr float ROW_HEIGHT = 38.0f;

            for (std::size_t index = 0; index < partySnapshot.members.size(); ++index)
            {
                const auto& member = partySnapshot.members[index];
                const float top = layout.listTop + static_cast<float>(index) * ROW_HEIGHT;
                const bool selected = member.slot == selectedPartySlot;
                inRenderer.FillRectangle(layout.panel.left + 22.0f, top,
                    layout.panel.right - 22.0f,
                    top + ROW_HEIGHT - 4.0f, selected
                        ? D2D1::ColorF(0.30f, 0.22f, 0.08f, 0.95f)
                        : D2D1::ColorF(0.08f, 0.10f, 0.14f, 0.90f));
                std::wstring label;
                if (member.playerId == partySnapshot.leaderPlayerId)
                {
                    label = L"[파티장] ";
                }
                label += Utf8ToWide(member.playerName);
                inRenderer.DrawText(label, layout.panel.left + 30.0f, top + 6.0f,
                    layout.panel.right - 24.0f, top + 31.0f,
                    D2D1::ColorF(0.90f, 0.92f, 0.94f));
            }

            const bool canKick = IsPartyLeader();
            inRenderer.FillRectangle(layout.kickButton.left, layout.kickButton.top,
                layout.kickButton.right, layout.kickButton.bottom,
                canKick ? D2D1::ColorF(0.48f, 0.18f, 0.10f) : D2D1::ColorF(0.12f, 0.13f, 0.15f));
            inRenderer.FillRectangle(layout.leaveButton.left, layout.leaveButton.top,
                layout.leaveButton.right, layout.leaveButton.bottom,
                D2D1::ColorF(0.36f, 0.15f, 0.14f));
            inRenderer.DrawText(L"강퇴", layout.kickButton.left + 14.0f,
                layout.kickButton.top + 6.0f, layout.kickButton.right - 10.0f,
                layout.kickButton.bottom - 4.0f, D2D1::ColorF(0.96f, 0.90f, 0.86f));
            inRenderer.DrawText(L"탈퇴", layout.leaveButton.left + 14.0f,
                layout.leaveButton.top + 6.0f, layout.leaveButton.right - 10.0f,
                layout.leaveButton.bottom - 4.0f, D2D1::ColorF(0.96f, 0.90f, 0.86f));
            inRenderer.DrawText(partyStatusText, layout.panel.left + 22.0f,
                layout.panel.bottom - 42.0f, layout.panel.right - 22.0f,
                layout.panel.bottom - 12.0f, D2D1::ColorF(0.96f, 0.78f, 0.34f));
        }
        else if (systemUiPage == SystemUiPage::PartyDirectory)
        {
            const PartyLayout layout = CalculatePartyLayout(
                camera.GetViewportWidth(), camera.GetViewportHeight());
            inRenderer.FillRectangle(layout.panel.left, layout.panel.top,
                layout.panel.right, layout.panel.bottom,
                D2D1::ColorF(0.035f, 0.045f, 0.065f, 0.98f));
            inRenderer.DrawRectangle(layout.panel.left, layout.panel.top,
                layout.panel.right, layout.panel.bottom,
                D2D1::ColorF(0.45f, 0.68f, 0.88f), 2.0f);
            inRenderer.DrawText(L"공개 파티 목록", layout.panel.left + 22.0f,
                layout.panel.top + 14.0f, layout.panel.right - 160.0f,
                layout.panel.top + 44.0f, D2D1::ColorF(0.90f, 0.94f, 1.0f));
            inRenderer.DrawText(L"ESC  메뉴로", layout.panel.right - 150.0f,
                layout.panel.top + 16.0f, layout.panel.right - 20.0f,
                layout.panel.top + 44.0f, D2D1::ColorF(0.68f, 0.74f, 0.82f));
            inRenderer.DrawText(L"공개 파티 " + std::to_wstring(partyDirectoryPage.totalCount)
                + L"개", layout.panel.left + 22.0f, layout.panel.top + 58.0f,
                layout.panel.right - 22.0f, layout.panel.top + 84.0f,
                D2D1::ColorF(0.82f, 0.86f, 0.92f));
            constexpr float ROW_HEIGHT = 48.0f;
            const float listTop = layout.panel.top + 92.0f;
            for (std::size_t index = 0; index < partyDirectoryPage.parties.size(); ++index)
            {
                const auto& party = partyDirectoryPage.parties[index];
                const float top = listTop + static_cast<float>(index) * ROW_HEIGHT;
                inRenderer.FillRectangle(layout.panel.left + 22.0f, top,
                    layout.panel.right - 22.0f, top + ROW_HEIGHT - 4.0f,
                    D2D1::ColorF(0.08f, 0.10f, 0.14f, 0.90f));
                const std::wstring heading = Utf8ToWide(party.title) + L"  ["
                    + std::to_wstring(party.members.size()) + L"/8]  파티장 "
                    + Utf8ToWide(party.leaderName);
                inRenderer.DrawText(heading, layout.panel.left + 30.0f, top + 2.0f,
                    layout.panel.right - 30.0f, top + 24.0f,
                    D2D1::ColorF(0.96f, 0.92f, 0.80f));
                std::wstring members = L"인원: ";
                for (const auto& member : party.members)
                {
                    if (members.size() > 4)
                    {
                        members += L", ";
                    }
                    members += Utf8ToWide(member.playerName);
                }
                inRenderer.DrawText(members, layout.panel.left + 30.0f, top + 24.0f,
                    layout.panel.right - 30.0f, top + 44.0f,
                    D2D1::ColorF(0.75f, 0.82f, 0.90f));
            }
            const float center = (layout.panel.left + layout.panel.right) * 0.5f;
            const float buttonTop = layout.panel.bottom - 60.0f;
            inRenderer.FillRectangle(layout.panel.right - 170.0f,
                layout.panel.top + 54.0f, layout.panel.right - 22.0f,
                layout.panel.top + 90.0f,
                D2D1::ColorF(0.16f, 0.28f, 0.42f));
            inRenderer.DrawText(L"파티 생성", layout.panel.right - 158.0f,
                layout.panel.top + 60.0f, layout.panel.right - 30.0f,
                layout.panel.top + 86.0f,
                D2D1::ColorF(0.92f, 0.95f, 1.0f));
            inRenderer.FillRectangle(center - 150.0f, buttonTop,
                center - 80.0f, buttonTop + 36.0f,
                partyDirectoryPage.page > 1 ? D2D1::ColorF(0.20f, 0.32f, 0.46f)
                    : D2D1::ColorF(0.12f, 0.13f, 0.15f));
            inRenderer.FillRectangle(center + 80.0f, buttonTop,
                center + 150.0f, buttonTop + 36.0f,
                partyDirectoryPage.page < partyDirectoryPage.totalPages
                    ? D2D1::ColorF(0.20f, 0.32f, 0.46f)
                    : D2D1::ColorF(0.12f, 0.13f, 0.15f));
            inRenderer.DrawText(L"<", center - 128.0f, buttonTop + 6.0f,
                center - 88.0f, buttonTop + 32.0f, D2D1::ColorF(0.94f, 0.94f, 0.92f));
            inRenderer.DrawText(L">", center + 106.0f, buttonTop + 6.0f,
                center + 146.0f, buttonTop + 32.0f, D2D1::ColorF(0.94f, 0.94f, 0.92f));
            inRenderer.DrawText(std::to_wstring(std::max(1U, partyDirectoryPage.page)) + L"/"
                + std::to_wstring(std::max(1U, partyDirectoryPage.totalPages)),
                center - 70.0f, buttonTop + 6.0f, center + 70.0f, buttonTop + 32.0f,
                D2D1::ColorF(0.94f, 0.94f, 0.92f));
        }
        else if (systemUiPage == SystemUiPage::PartyCreate)
        {
            const D2D1_RECT_F dialog = GetPartyCreateDialog(
                camera.GetViewportWidth(), camera.GetViewportHeight());
            const D2D1_RECT_F createButton = D2D1::RectF(
                dialog.left + 24.0f, dialog.bottom - 64.0f,
                dialog.left + 154.0f, dialog.bottom - 24.0f);
            const D2D1_RECT_F cancelButton = D2D1::RectF(
                dialog.left + 168.0f, dialog.bottom - 64.0f,
                dialog.left + 298.0f, dialog.bottom - 24.0f);
            inRenderer.FillRectangle(dialog.left, dialog.top, dialog.right, dialog.bottom,
                D2D1::ColorF(0.035f, 0.045f, 0.065f, 0.98f));
            inRenderer.DrawRectangle(dialog.left, dialog.top, dialog.right, dialog.bottom,
                D2D1::ColorF(0.45f, 0.68f, 0.88f), 2.0f);
            inRenderer.DrawText(L"파티 생성", dialog.left + 24.0f, dialog.top + 20.0f,
                dialog.right - 24.0f, dialog.top + 52.0f,
                D2D1::ColorF(0.90f, 0.94f, 1.0f));
            inRenderer.DrawText(L"파티 제목", dialog.left + 24.0f, dialog.top + 58.0f,
                dialog.right - 24.0f, dialog.top + 84.0f,
                D2D1::ColorF(0.82f, 0.86f, 0.92f));
            inRenderer.FillRectangle(dialog.left + 24.0f, dialog.top + 88.0f,
                dialog.right - 24.0f, dialog.top + 128.0f,
                D2D1::ColorF(0.12f, 0.16f, 0.23f));
            inRenderer.DrawText(newPartyTitleDraft.empty() ? L"제목을 입력하세요"
                : newPartyTitleDraft, dialog.left + 36.0f, dialog.top + 96.0f,
                dialog.right - 36.0f, dialog.top + 124.0f,
                D2D1::ColorF(0.94f, 0.94f, 0.92f));
            inRenderer.FillRectangle(dialog.left + 24.0f, dialog.top + 144.0f,
                dialog.left + 46.0f, dialog.top + 166.0f,
                D2D1::ColorF(0.10f, 0.14f, 0.19f));
            inRenderer.DrawRectangle(dialog.left + 24.0f, dialog.top + 144.0f,
                dialog.left + 46.0f, dialog.top + 166.0f,
                D2D1::ColorF(0.65f, 0.78f, 0.88f), 1.5f);
            if (newPartyIsPublic)
            {
                inRenderer.FillRectangle(dialog.left + 29.0f, dialog.top + 149.0f,
                    dialog.left + 41.0f, dialog.top + 161.0f,
                    D2D1::ColorF(0.32f, 0.72f, 0.45f));
            }
            inRenderer.DrawText(L"공개 파티로 생성", dialog.left + 58.0f,
                dialog.top + 141.0f, dialog.right - 24.0f,
                dialog.top + 169.0f, D2D1::ColorF(0.88f, 0.91f, 0.94f));
            inRenderer.FillRectangle(createButton.left, createButton.top,
                createButton.right, createButton.bottom,
                partyCreationPending ? D2D1::ColorF(0.12f, 0.13f, 0.15f)
                    : D2D1::ColorF(0.16f, 0.34f, 0.50f));
            inRenderer.FillRectangle(cancelButton.left, cancelButton.top,
                cancelButton.right, cancelButton.bottom,
                D2D1::ColorF(0.18f, 0.20f, 0.24f));
            inRenderer.DrawText(partyCreationPending ? L"생성 중" : L"생성",
                createButton.left + 14.0f, createButton.top + 7.0f,
                createButton.right - 10.0f, createButton.bottom - 4.0f,
                D2D1::ColorF(0.92f, 0.95f, 1.0f));
            inRenderer.DrawText(L"취소", cancelButton.left + 14.0f,
                cancelButton.top + 7.0f, cancelButton.right - 10.0f,
                cancelButton.bottom - 4.0f,
                D2D1::ColorF(0.92f, 0.95f, 1.0f));
            inRenderer.DrawText(partyStatusText, dialog.left + 24.0f,
                dialog.top + 176.0f, dialog.right - 24.0f,
                dialog.top + 202.0f, D2D1::ColorF(0.96f, 0.78f, 0.34f));
        }

        if (pendingPartyInvitation.has_value())
        {
            const D2D1_RECT_F dialog = GetInvitationDialog(camera.GetViewportWidth());
            const float middle = (dialog.left + dialog.right) * 0.5f;
            const D2D1_RECT_F acceptButton = D2D1::RectF(
                dialog.left + 24.0f, dialog.bottom - 58.0f, middle - 8.0f, dialog.bottom - 18.0f);
            const D2D1_RECT_F declineButton = D2D1::RectF(
                middle + 8.0f, dialog.bottom - 58.0f, dialog.right - 24.0f, dialog.bottom - 18.0f);
            inRenderer.FillRectangle(dialog.left, dialog.top, dialog.right, dialog.bottom,
                D2D1::ColorF(0.08f, 0.06f, 0.04f, 0.99f));
            inRenderer.DrawRectangle(dialog.left, dialog.top, dialog.right, dialog.bottom,
                D2D1::ColorF(0.95f, 0.70f, 0.24f), 2.0f);
            const std::wstring message = Utf8ToWide(pendingPartyInvitation->inviterName)
                + L" 님의 파티 초대";
            inRenderer.DrawText(message, dialog.left + 24.0f, dialog.top + 22.0f,
                dialog.right - 24.0f, dialog.top + 64.0f, D2D1::ColorF(0.96f, 0.94f, 0.86f));
            inRenderer.FillRectangle(acceptButton.left, acceptButton.top,
                acceptButton.right, acceptButton.bottom, D2D1::ColorF(0.12f, 0.42f, 0.24f));
            inRenderer.FillRectangle(declineButton.left, declineButton.top,
                declineButton.right, declineButton.bottom, D2D1::ColorF(0.44f, 0.15f, 0.12f));
            inRenderer.DrawText(L"수락", acceptButton.left + 16.0f, acceptButton.top + 7.0f,
                acceptButton.right - 12.0f, acceptButton.bottom - 4.0f,
                D2D1::ColorF(0.92f, 1.0f, 0.94f));
            inRenderer.DrawText(L"거절", declineButton.left + 16.0f, declineButton.top + 7.0f,
                declineButton.right - 12.0f, declineButton.bottom - 4.0f,
                D2D1::ColorF(1.0f, 0.92f, 0.88f));
        }
    }

    bool GameWorld::IsPartyLeader() const noexcept
    {
        return partySnapshot.partyId != 0 && partySnapshot.leaderPlayerId == localPlayerId;
    }

    void GameWorld::UpdateRemotePlayers(const float inDeltaSeconds)
    {
        for (auto& [playerId, remotePlayer] : remotePlayers)
        {
            const bool isMoving = IsMoving(remotePlayer.velocity);
            if (isMoving)
            {
                remotePlayer.walkAnimation.Update(inDeltaSeconds);
                remotePlayer.idleAnimation.Reset();
            }
            else
            {
                remotePlayer.walkAnimation.Reset();
                remotePlayer.idleAnimation.Update(inDeltaSeconds);
            }
            if (std::abs(remotePlayer.velocity.x) > 1.0f)
            {
                remotePlayer.facingLeft = remotePlayer.velocity.x < 0.0f;
            }

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
        if (dungeonEntryState == DungeonEntryState::Entered)
        {
            const auto dx = static_cast<std::int8_t>(player.IsHitReacting() ? 0 :
                static_cast<int>(inInput.moveRight) - static_cast<int>(inInput.moveLeft));
            const auto dy = static_cast<std::int8_t>(player.IsHitReacting() ? 0 :
                static_cast<int>(inInput.moveDown) - static_cast<int>(inInput.moveUp));
            const bool running = player.IsRunning();
            if (hasSentMovementInput && dx == lastSentDirectionX && dy == lastSentDirectionY
                && running == lastDungeonRun && movementSendAccumulator < 0.1f) return;
            DungeonProtocol::DungeonMoveInput packet;
            packet.sequence = ++dungeonMovementSequence;
            packet.directionX = dx; packet.directionY = dy; packet.running = running ? 1 : 0;
            dungeonClient.SendReliable(packet);
            lastSentDirectionX = dx; lastSentDirectionY = dy; lastDungeonRun = running;
            movementSendAccumulator = 0; hasSentMovementInput = true;
            return;
        }
        if (localPlayerId == 0 || dungeonEntryState != DungeonEntryState::Idle)
        {
            return;
        }

        const std::int8_t directionX = static_cast<std::int8_t>(
            player.IsHitReacting() ? 0
                : static_cast<int>(inInput.moveRight) - static_cast<int>(inInput.moveLeft));
        const std::int8_t directionY = static_cast<std::int8_t>(
            player.IsHitReacting() ? 0
                : static_cast<int>(inInput.moveDown) - static_cast<int>(inInput.moveUp));
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
