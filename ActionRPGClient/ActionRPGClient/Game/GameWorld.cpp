#include "Game/GameWorld.h"
#include "Game/HudLayout.h"
#include "Network/CharacterInventoryJson.h"
#include <array>
#include <iomanip>
#include <sstream>
#include <locale>
#include <unordered_set>
#include <limits>

#include "Graphics/D2DRenderer.h"
#include "Network/DungeonClient.h"
#include "Network/DungeonProtocol.h"
#include "Network/TownClient.h"
#include "Platform/GameWindow.h"
#include "Platform/AudioMixer.h"
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
        constexpr float MONSTER_HUD_LEFT = 12.0f;
        constexpr float MONSTER_HUD_TOP = 12.0f;
        constexpr float TOP_RIGHT_UI_RESERVED_WIDTH = 320.0f;
        constexpr float TOP_UI_GAP = 12.0f;

        constexpr float MENU_TILE_WIDTH = 166.0f;
        constexpr float MENU_TILE_HEIGHT = 194.0f;
        constexpr float MENU_ICON_SIZE = 128.0f;
        constexpr float MENU_TILE_GAP = 18.0f;

        D2D1_RECT_F GetGameOptionsPanel(const float inWidth, const float inHeight)
        {
            const float left = (inWidth - 440.0f) * 0.5f, top = (inHeight - 350.0f) * 0.5f;
            return D2D1::RectF(left, top, left + 440.0f, top + 350.0f);
        }

        D2D1_RECT_F GetGameOptionsButton(const D2D1_RECT_F& inPanel, const std::size_t inIndex)
        {
            const bool footer = inIndex >= 4;
            const float left = inPanel.left + (footer ? 20.0f : 140.0f)
                + (inIndex % 2) * (footer ? 208.0f : 142.0f);
            const float top = inPanel.top + (footer ? 306.0f : inIndex < 2 ? 62.0f : 98.0f);
            return D2D1::RectF(left, top, left + (footer ? 192.0f : 132.0f), top + 28.0f);
        }

        D2D1_RECT_F GetVolumeButton(const D2D1_RECT_F& inPanel, const std::size_t inIndex)
        {
            const float left = inPanel.left + 140.0f + (inIndex % 2) * 116.0f;
            const float top = inPanel.top + 150.0f + (inIndex / 2) * 38.0f;
            return D2D1::RectF(left, top, left + 28.0f, top + 28.0f);
        }

        struct DungeonCompletionLayout
        {
            D2D1_RECT_F panel;
            std::array<D2D1_RECT_F, 2> buttons;
        };

        DungeonCompletionLayout CalculateDungeonCompletionLayout(const float inWidth, const float inHeight)
        {
            const float left = std::max(16.0f, (inWidth - 520.0f) * 0.5f);
            const float top = std::max(16.0f, (inHeight - 270.0f) * 0.5f);
            return {{left, top, left + 520.0f, top + 270.0f},
                {{{left + 28.0f, top + 126.0f, left + 250.0f, top + 178.0f},
                  {left + 270.0f, top + 126.0f, left + 492.0f, top + 178.0f}}}};
        }

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

        struct PartyNoticeLayout
        {
            D2D1_RECT_F panel, accept, decline;
        };

        PartyNoticeLayout CalculatePartyNoticeLayout(float inWidth, float inBottom)
        {
            const float right = inWidth - 16.0f;
            const float left = std::max(16.0f, right - 344.0f);
            const float bottom = std::max(164.0f, inBottom);
            const float top = bottom - 148.0f;
            const float middle = (left + right) * 0.5f;
            return {{left, top, right, bottom},
                {left + 12.0f, bottom - 44.0f, middle - 6.0f, bottom - 12.0f},
                {middle + 6.0f, bottom - 44.0f, right - 12.0f, bottom - 12.0f}};
        }

        PartyNoticeLayout CalculateKickedNoticeLayout(float inWidth, float inHeight)
        {
            const float width = std::min(420.0f, inWidth - 32.0f);
            const float left = (inWidth - width) * 0.5f;
            const float top = (inHeight - 148.0f) * 0.5f;
            return {{left, top, left + width, top + 148.0f},
                {left + 24.0f, top + 100.0f, left + width - 24.0f, top + 132.0f}, {}};
        }

        PartyNoticeLayout CalculatePartyDetailsLayout(float inWidth, float inHeight)
        {
            const float width = std::min(620.0f, inWidth - 32.0f);
            const float height = std::min(480.0f, inHeight - 32.0f);
            const float left = (inWidth - width) * 0.5f;
            const float top = (inHeight - height) * 0.5f;
            const float bottom = top + height;
            const float middle = (left + left + width) * 0.5f;
            return {{left, top, left + width, bottom},
                {left + 16.0f, bottom - 42.0f, middle - 6.0f, bottom - 10.0f},
                {middle + 6.0f, bottom - 42.0f, left + width - 16.0f, bottom - 10.0f}};
        }

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
            case TownProtocol::PartyResultCode::AlreadyRequested:
                return L"이미 응답을 기다리는 가입 요청이 있습니다.";
            case TownProtocol::PartyResultCode::JoinRequestNotFound:
                return L"해당 가입 요청이 종료되었습니다.";
            case TownProtocol::PartyResultCode::PartyNotFound:
                return L"해당 파티가 더 이상 존재하지 않습니다.";
            case TownProtocol::PartyResultCode::NotPublic:
                return L"비공개 파티에는 가입을 요청할 수 없습니다.";
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
        DungeonClient& inDungeonClient, AudioMixer& inAudio)
        : mapBackground(inAssetCatalog)
        , camera(inViewportWidth, inViewportHeight)
        , animationDefinitions(inAssetCatalog.GetDataPath("Animations"))
        , characterDefinitions(inAssetCatalog.GetDataPath("Characters"))
        , systemMenuDefinitions(inAssetCatalog.GetDataPath("SystemMenu"))
        , player(Vector2{ 640.0f, 640.0f }, inAssetCatalog, inRenderer)
        , projectileSystem(inAssetCatalog)
        , playerSkillPresentation(inAssetCatalog, inRenderer)
        , combatHitAnimation(inRenderer, inAssetCatalog, animationDefinitions, "CombatHitSpark")
        , skillUi(inAssetCatalog, inRenderer)
        , inventoryUi(inAssetCatalog, inRenderer)
        , questUi(inAssetCatalog, inRenderer)
        , assetCatalog(inAssetCatalog)
        , renderer(inRenderer)
        , townClient(inTownClient)
        , dungeonClient(inDungeonClient)
        , audio(inAudio)
        , monsterCatalog(inAssetCatalog)
    {
        audio.Stop();
        if (assetCatalog.HasAudio("TownBgm")) (void)audio.PlayMusic(assetCatalog.GetAudioPath("TownBgm"));
        if (assetCatalog.HasAudio("GunShot")) (void)audio.LoadEffect(assetCatalog.GetAudioPath("GunShot"), "GunShot");
        const IniDocument effects(assetCatalog.GetDataPath("Effects"));
        const auto effectAudio = effects.GetOptionalValue("DirectionalStrike", "audio_asset");
        if (!effectAudio.empty())
        {
            if (!assetCatalog.HasAudio(effectAudio)) throw std::runtime_error("Unknown DirectionalStrike audio asset.");
            (void)audio.LoadEffect(assetCatalog.GetAudioPath(effectAudio));
        }
        if (inAssetCatalog.HasImage("HudOrbFrame"))
            hudOrbFrame = inRenderer.LoadBitmap(inAssetCatalog.GetImagePath("HudOrbFrame"));
        player.SetSkillPresentation(&playerSkillPresentation);
        for (const std::string& section : systemMenuDefinitions.GetSectionNames())
        {
            const std::string actionName = systemMenuDefinitions.GetValue(section, "Action");
            SystemMenuAction action{};
            if (actionName == "Party")
            {
                action = SystemMenuAction::Party;
            }
            else if (actionName == "Skills") action = SystemMenuAction::Skills;
            else if (actionName == "Inventory") action = SystemMenuAction::Inventory;
            else if (actionName == "Quests") action = SystemMenuAction::Quests;
            else if (actionName == "GameOptions") action = SystemMenuAction::GameOptions;
            else if (actionName == "Logout") action = SystemMenuAction::Logout;
            else if (actionName == "SwitchAccount") action = SystemMenuAction::SwitchAccount;
            else if (actionName == "SelectTown") action = SystemMenuAction::SelectTown;
            else if (actionName == "Exit")
            {
                action = SystemMenuAction::Exit;
            }
            else
            {
                throw std::runtime_error("Unknown system menu action: " + actionName);
            }
            const std::string iconAssetId = systemMenuDefinitions.GetValue(section, "Icon");
            Microsoft::WRL::ComPtr<ID2D1Bitmap1> menuIcon;
            if (action == SystemMenuAction::Skills)
            { try { menuIcon=inRenderer.LoadBitmap(inAssetCatalog.GetImagePath(iconAssetId)); } catch (const std::exception&) {} }
            else if (action != SystemMenuAction::Inventory && action != SystemMenuAction::Quests && action != SystemMenuAction::GameOptions) menuIcon=inRenderer.LoadBitmap(inAssetCatalog.GetImagePath(iconAssetId));
            systemMenuEntries.push_back(SystemMenuEntry{
                Utf8ToWide(systemMenuDefinitions.GetValue(section, "Label")), action, std::move(menuIcon)});
        }
        // Decode each portrait image once on the game thread and validate the authored pixel crop.
        std::unordered_map<std::string, Microsoft::WRL::ComPtr<ID2D1Bitmap1>> portraitImages;
        for (const auto& [dataId, definition] : monsterCatalog.GetDefinitions())
        {
            if (!definition.portrait) continue;
            const auto& portrait = *definition.portrait;
            auto image = portraitImages.find(portrait.image);
            if (image == portraitImages.end())
                image = portraitImages.emplace(portrait.image,
                    inRenderer.LoadBitmap(inAssetCatalog.GetAssetPath(portrait.image))).first;
            const auto pixels = image->second->GetPixelSize();
            const auto dips = image->second->GetSize();
            const auto& rect = portrait.sourceRect;
            if (!pixels.width || !pixels.height || !std::isfinite(dips.width) || !std::isfinite(dips.height)
                || dips.width <= 0 || dips.height <= 0
                || rect.right > pixels.width || rect.bottom > pixels.height)
                throw std::runtime_error("Monster portrait exceeds its image: " + portrait.image);
            const float dipX = dips.width / pixels.width, dipY = dips.height / pixels.height;
            monsterPortraits.emplace(dataId, MonsterPortrait{image->second,
                D2D1::RectF(rect.left * dipX, rect.top * dipY, rect.right * dipX, rect.bottom * dipY),
                D2D1::SizeF(rect.right - rect.left, rect.bottom - rect.top)});
        }
        player.SetRunningEnabled(false);
        camera.Follow(player.GetGroundPosition(), gameplayMap.GetWorldLeft(), gameplayMap.GetWorldTop(),
            gameplayMap.GetWorldRight(), gameplayMap.GetWorldBottom());
    }

    GameWorld::~GameWorld()
    {
        GameWindow::SetChatInputMode(false);
        audio.Stop();
    }

    void GameWorld::Update(const float inDeltaSeconds, const InputState& inRawInput)
    {
        InputState inInput=inRawInput;
        const bool wasUiTextEditing=IsUiTextEditing();
        if (chatUi.IsEditing() || wasUiTextEditing)
        {
            inInput.moveLeft=inInput.moveRight=inInput.moveUp=inInput.moveDown=false;
            inInput.pressedKeys.clear();
        }
        InputState interfaceInput=inRawInput;
        GameWindow::ConsumeUiPointer(interfaceInput);
        const bool wasChatEditing=chatUi.IsEditing();
        uiMouseX = inInput.mouseX;
        uiMouseY = inInput.mouseY;
        uiClickConsumed = false;
        const auto selectedCharacterId=townClient.GetSelectedCharacterId();
        const auto connection=townClient.GetConnectionInfo();
        if (!townClient.IsConnected() || experienceCharacterId!=selectedCharacterId
            || experienceAttemptId!=connection.attemptId)
        {
            experienceView.reset();
            experienceCharacterId=selectedCharacterId;experienceAttemptId=connection.attemptId;
        }
        questUi.SetCharacterContext(selectedCharacterId);
        ProcessDungeonCompletion();
        ProcessNetworkEvents(inInput);
        if (!completionStopping) ProcessDungeonEvents();
        ProcessPlayerHits();
        if (IsDungeonUiRestricted() || !townClient.IsConnected())
        {
            ResetPartyRequestUi();
            pendingPartyInvitation.reset();
            partyInvitationAnswerPending = false;
            if (!townClient.IsConnected()) partyKickedNotice = false;
        }
        SyncUiWindows();
        inventoryUi.SetHotbarBounds(skillUi.GetHotbarBounds(camera.GetViewportWidth(),camera.GetViewportHeight()),HudLayout::Scale(camera.GetViewportWidth()));
        UpdateUiWindowOffsets();
        const bool chatAllowed = !IsUiTextEditing() && !skillUi.IsDragging() && !inventoryUi.IsDragging();
        InputState chatInput=interfaceInput;
        const auto maskPointer=[](InputState& input)
        {
            input.mouseX=input.mouseY=input.clickX=input.clickY=input.rightClickX=input.rightClickY=-10000;
            input.leftMousePressed=input.leftMouseDown=input.leftMouseReleased=input.rightMousePressed=false;
            input.mouseWheelDelta=0;
        };
        if (!chatUi.IsDragging() && (TopUiWindowAt(chatInput.mouseX,chatInput.mouseY)!=SystemUiPage::Closed
            || questUi.IsTrackerDragging() || draggedUiPage!=SystemUiPage::Closed))
        {
            if (chatInput.leftMousePressed) chatUi.CancelInput();
            maskPointer(chatInput);
        }
        // Enter belongs to the focused dialog, unless chat is already editing.
        const bool dialogUsesEnter=IsUiTextEditing() || systemUiPage==SystemUiPage::GameOptions
            || systemUiPage==SystemUiPage::DungeonSelection || systemUiPage==SystemUiPage::DungeonCompletion
            || systemUiPage==SystemUiPage::KickedNotice;
        if (!chatUi.IsEditing() && dialogUsesEnter)
            std::erase(chatInput.pressedKeys,InputKey::ConfirmSelection);
        const auto chatBar=skillUi.GetHotbarBounds(camera.GetViewportWidth(),camera.GetViewportHeight());
        const auto chatHud=HudLayout::Calculate(chatBar,HudLayout::Scale(camera.GetViewportWidth()));
        const auto chatAction=chatUi.Update(inDeltaSeconds,chatInput,renderer,camera.GetViewportWidth(),
            camera.GetViewportHeight(),chatHud.hp.top,chatAllowed,IsDungeonUiRestricted(),
            partySnapshot.partyId!=0,townClient.IsConnected());
        const auto chatInputPosition=chatUi.GetInputPosition();
        const float chatScale=HudLayout::Scale(camera.GetViewportWidth());
        GameWindow::SetChatInputMode(chatUi.IsEditing(),chatInputPosition.x*chatScale,chatInputPosition.y*chatScale);
        chatUi.SetLimits(TownProtocol::MAX_CHAT_CODE_POINTS,TownProtocol::MAX_CHAT_UTF8_BYTES);
        for(auto it=pendingChatResults.begin();it!=pendingChatResults.end();)
        {
            it->second-=inDeltaSeconds;
            if(it->second<=0 || !townClient.IsConnected())
            {chatUi.Notice(L"채팅 전송 결과를 확인하지 못했습니다.");it=pendingChatResults.erase(it);}
            else ++it;
        }
        if(chatAction.submission)
        {
            const auto text=WideToUtf8(chatAction.submission->text);
            const auto channel=chatAction.submission->channel==ChatChannel::Party ? TownProtocol::ChatChannel::Party:TownProtocol::ChatChannel::General;
            if(pendingChatResults.size()>=8) chatUi.Notice(L"채팅 전송 결과를 기다리고 있습니다.");
            else if(text)
            {
                if(const auto sequence=townClient.SendChat(channel,*text)) pendingChatResults.emplace(*sequence,5.0f);
                else chatUi.Notice(L"채팅을 전송할 수 없습니다. 연결 상태를 확인해 주세요.");
            }
            else chatUi.Notice(L"유효한 Unicode 문장을 입력해 주세요.");
        }
        if (chatAction.consumeInput)
        {
            if (wasChatEditing || chatUi.IsEditing())
            {
                inInput.moveLeft=inInput.moveRight=inInput.moveUp=inInput.moveDown=false;
                inInput.pressedKeys.clear();
            }
            inInput.textInput.clear();inInput.textEdits.clear();
            // Keep Escape independent of chat; all typed action/UI keys remain consumed.
            std::erase_if(interfaceInput.pressedKeys,[](auto key){return key!=InputKey::ToggleSystemMenu;});
            interfaceInput.textInput.clear();interfaceInput.textEdits.clear();
        }
        if (chatAction.consumePointer)
        {
            inInput.leftMousePressed=false;inInput.mouseWheelDelta=0;
            maskPointer(interfaceInput);
            uiClickConsumed=true;
        }
        if (!townClient.IsConnected()) chatUi.ClearBubbles();
        UpdateUiWindows(interfaceInput);
        const auto focusedInput=UiWindowInput(systemUiPage,interfaceInput);
        switch (systemUiPage)
        {
        case SystemUiPage::Menu: case SystemUiPage::GameOptions: case SystemUiPage::ExitConfirmation:
            UpdateSystemInterface(focusedInput);break;
        case SystemUiPage::Party: case SystemUiPage::PartyDirectory: case SystemUiPage::PartyDetails:
        case SystemUiPage::PartyCreate: case SystemUiPage::Invitation:
            UpdatePartyInterface(focusedInput);break;
        case SystemUiPage::PartyNotice: case SystemUiPage::KickedNotice:
            UpdatePartyNotifications(focusedInput);break;
        case SystemUiPage::DungeonSelection: UpdateDungeonSelection(focusedInput);break;
        default: break;
        }
        UpdateDungeonCompletion(inDeltaSeconds,systemUiPage==SystemUiPage::DungeonCompletion ? focusedInput:InputState{});
        SyncUiWindows();
        UpdateUiWindowOffsets();
        if (wasUiTextEditing)
            std::erase_if(interfaceInput.pressedKeys,[](auto key){return key>=InputKey::Item1&&key<=InputKey::Item6;});
        const float uiTempoScale = dungeonWorld && IsDungeonUiRestricted() ? dungeonWorld->GetCombatRules().tempoScale : 1.0f;
        skillUi.SetCombatTempoScale(uiTempoScale);
        inventoryUi.SetCombatTempoScale(uiTempoScale);
        skillUi.SetConnected(townClient.IsConnected());
        skillUi.SetLearningAllowed(!IsDungeonUiRestricted());
        const bool skillBarVisible=!dungeonCleared && !completionStopping;
        const auto hudSkillBar = skillUi.GetHotbarBounds(camera.GetViewportWidth(), camera.GetViewportHeight());
        const auto hudMenu = HudLayout::Calculate(hudSkillBar, HudLayout::Scale(camera.GetViewportWidth())).menu;
        const bool hudInteractive=pointerUiPage==SystemUiPage::Closed && !uiChromeConsumed && skillBarVisible;
        if (hudInteractive && interfaceInput.leftMousePressed && ContainsPoint(hudMenu,interfaceInput.clickX,interfaceInput.clickY))
        {SetSystemUiPage(SystemUiPage::Menu);uiChromeConsumed=true;uiClickConsumed=true;}
        const bool skillHotbarInteractive=!uiChromeConsumed && !chatAction.consumePointer
            && TopUiWindowAt(interfaceInput.mouseX,interfaceInput.mouseY)==SystemUiPage::Closed
            && !questUi.IsTrackerAt(interfaceInput.mouseX,interfaceInput.mouseY,camera.GetViewportWidth(),camera.GetViewportHeight(),chatHud.hp.top);
        const auto skillUiAction=skillUi.Update(inDeltaSeconds,UiWindowInput(SystemUiPage::Skills,interfaceInput,true),
            camera.GetViewportWidth(),camera.GetViewportHeight(),IsUiPageOpen(SystemUiPage::Skills),skillBarVisible,skillHotbarInteractive);
        if (skillUiAction.requestState) townClient.RequestSkillState();
        if (!IsDungeonUiRestricted() && !skillUiAction.learnSkill.empty()) townClient.LearnSkill(skillUiAction.learnSkill,skillUiAction.expectedSkillLevel);
        inventoryUi.SetConnected(townClient.IsConnected());
        const bool inventoryVisible=IsUiPageOpen(SystemUiPage::Inventory);
        const auto inventoryAction=inventoryUi.Update(inDeltaSeconds,UiWindowInput(SystemUiPage::Inventory,interfaceInput,true),
            camera.GetViewportWidth(),camera.GetViewportHeight(),inventoryVisible,skillBarVisible,hudSkillBar);
        if (inventoryAction.requestState) townClient.RequestInventoryState(inventoryAction.requestId);
        if (inventoryAction.requestUseStatus) townClient.RequestUseStatus(inventoryAction.useStatusRequestId);
        if (inventoryAction.operation != InventoryOperation::None)
        {
            const char* action = inventoryAction.operation == InventoryOperation::Equip ? "Equip"
                : inventoryAction.operation == InventoryOperation::Unequip ? "Unequip" : "Use";
            townClient.ChangeInventory(inventoryAction.requestId, inventoryAction.expectedRevision,
                action, inventoryAction.instanceId, inventoryAction.quantity,
                inventoryAction.throwItem ? std::optional<bool>(player.GetFacingLeft()) : std::nullopt);
        }
        questUi.SetConnected(townClient.IsConnected());
        questUi.SetMapContext(IsDungeonUiRestricted() || !lastTownMap ? std::string{} : lastTownMap->mapId);
        const auto questInput=UiWindowInput(SystemUiPage::Quests,interfaceInput,true);
        const bool questVisible=IsUiPageOpen(SystemUiPage::Quests);
        const bool questPointerAllowed=(pointerUiPage==SystemUiPage::Closed
            || (pointerUiPage==SystemUiPage::Quests && TopUiWindowAt(interfaceInput.mouseX,interfaceInput.mouseY)==SystemUiPage::Closed))
            && !uiChromeConsumed
            && !chatAction.consumePointer && !chatUi.IsEditing() && !skillUi.IsDragging() && !inventoryUi.IsDragging();
        try
        {
            const auto questAction = questUi.Update(inDeltaSeconds, questInput,
                camera.GetViewportWidth(), camera.GetViewportHeight(), chatHud.hp.top, questVisible,
                questPointerAllowed && !IsDungeonUiRestricted(), questPointerAllowed || questUi.IsTrackerDragging(),
                player.GetGroundPosition(), camera);
            if (!questAction.stateRequestId.empty()) townClient.RequestQuestState(questAction.stateRequestId);
            if (!questAction.operationJson.empty()) townClient.SendQuestAction(questAction.operationJson);
            if (questAction.refreshProgression) townClient.RequestSkillState();
            if (questAction.openWindow) SetSystemUiPage(SystemUiPage::Quests);
            if (questAction.consumePointer)
            {
                uiClickConsumed = true; inInput.leftMousePressed = false; inInput.mouseWheelDelta = 0;
                interfaceInput.leftMousePressed = false; interfaceInput.mouseWheelDelta = 0;
            }
        }
        catch (const std::exception&) { questUi.InvalidResponse(); }
        InputState gameplayInput = inInput;
        const bool gameplayBlocked=chatUi.IsEditing() || wasChatEditing || wasUiTextEditing || IsUiTextEditing()
            || dungeonCleared || completionStopping || player.IsHitReacting()
            || (dungeonEntryState!=DungeonEntryState::Idle && dungeonEntryState!=DungeonEntryState::Entered);
        if (gameplayBlocked)
        {
            gameplayInput = InputState{};
        }
        std::erase_if(gameplayInput.pressedKeys,[](InputKey key){return key>=InputKey::Item1;});
        if (inventoryAction.operation == InventoryOperation::Use)
        { gameplayInput.pressedKeys.clear(); commandQueue.Clear(); }
        if (gameplayBlocked) { commandQueue.Clear(); }
        townHp.Update(inDeltaSeconds); townMp.Update(inDeltaSeconds);
        dungeonHp.Update(inDeltaSeconds); dungeonMp.Update(inDeltaSeconds); targetHp.Update(inDeltaSeconds);
        worldTimeSeconds += inDeltaSeconds;
        projectileSystem.Update(inDeltaSeconds, gameplayMap);
        commandQueue.Record(gameplayInput, worldTimeSeconds);

        if (dungeonEntryState == DungeonEntryState::Idle && !gameplayBlocked)
        {
            bool matched=false;
            const auto command=playerSkillPresentation.TryCommand(commandQueue,localCharacterId,skillUi.GetProgression().skillLevels,matched);
            const auto hotkey=skillUi.Hotkey(gameplayInput);
            const auto id=!hotkey.empty() ? hotkey:command;
            if (matched || !hotkey.empty())
            {
                commandQueue.Clear();
                std::erase_if(gameplayInput.pressedKeys,[](InputKey key)
                    { return key==InputKey::ActionX || key==InputKey::ActionC || key==InputKey::ActionV; });
                if (!id.empty() && (hotkey.empty() || !player.BoostCatalogSkill(id)) && skillUi.CanUse(id,player.GetHeight()>0,false)
                    && player.ActivateCatalogSkill(id,skillUi.GetProgression().skillLevels)
                    && id == "Character1.FrontKick") (void)audio.PlayEffect();
                // Unavailable skill input is discarded immediately; never reserve it for a later frame.
            }
        }

        if (dungeonEntryState==DungeonEntryState::Idle)
        {
            const bool movementBlocked=player.IsHitReacting() || player.IsAttacking() || player.IsLocallyCasting();
            const auto directionX=static_cast<std::int8_t>(movementBlocked ? 0:
                static_cast<int>(gameplayInput.moveRight)-static_cast<int>(gameplayInput.moveLeft));
            const auto directionY=static_cast<std::int8_t>(movementBlocked ? 0:
                static_cast<int>(gameplayInput.moveDown)-static_cast<int>(gameplayInput.moveUp));
            if (directionX!=lastSentDirectionX || directionY!=lastSentDirectionY)
                player.ClearTownPositionCorrection();
        }
        player.Update(inDeltaSeconds, gameplayInput, gameplayMap);
        if (dungeonEntryState==DungeonEntryState::Idle) skillUi.ApplyTownCooldowns(player.GetTownSkillCooldowns());
        SendMovementInput(gameplayInput, inDeltaSeconds);
        if (dungeonEntryState == DungeonEntryState::Entered)
        {
            UpdateDungeonCombat(inDeltaSeconds);
            SendCombatActions(gameplayInput);
        }
        UpdateRemotePlayers(inDeltaSeconds);
        if (const auto monsters = dungeonMonsters.find(dungeonMapId); monsters != dungeonMonsters.end())
        {
            for (const auto& monster : monsters->second) monster->Update(inDeltaSeconds);
            std::sort(monsters->second.begin(), monsters->second.end(), [](const auto& a, const auto& b)
                { return a->GetGroundPosition().y < b->GetGroundPosition().y; });
        }
        while (const std::optional<CharacterProjectileRequest> request = player.ConsumeProjectileRequest())
        {
            if (dungeonEntryState == DungeonEntryState::Entered) continue;
            if (request->type == CharacterProjectileType::Straight || request->type == CharacterProjectileType::AirStraight)
                (void)audio.PlayEffect("GunShot");
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
            skillUi.CancelDrag();
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
        if (dungeonEntryState == DungeonEntryState::Entered) RenderCombatProjectiles(inRenderer);
        else projectileSystem.Render(inRenderer, camera);
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
        if (dungeonEntryState == DungeonEntryState::Entered)
            for (const auto& [id, otherPlayer] : combatPlayers) otherPlayer->Render(inRenderer, camera);
        if (monsters != dungeonMonsters.end())
            for (const auto& monster : monsters->second)
                if (monster->GetGroundPosition().y <= player.GetGroundPosition().y) monster->Render(inRenderer, camera);
        if (!IsDungeonUiRestricted()) questUi.RenderNpcs(inRenderer, camera, player.GetGroundPosition().y, false);
        player.Render(inRenderer, camera);
        if (monsters != dungeonMonsters.end())
            for (const auto& monster : monsters->second)
                if (monster->GetGroundPosition().y > player.GetGroundPosition().y) monster->Render(inRenderer, camera);
        if (dungeonEntryState == DungeonEntryState::Entered)
            for (const auto& effect : combatHitEffects)
            {
                const auto position = camera.WorldToScreen(effect.position);
                effect.animation.Draw(inRenderer, position.x, position.y - effect.height, false);
            }
        if (!IsDungeonUiRestricted()) questUi.RenderNpcs(inRenderer, camera, player.GetGroundPosition().y, true);
        inRenderer.PopAxisAlignedClip();
        RenderCombatHud(inRenderer);
        if (!dungeonCleared && !completionStopping)
        {
            const auto bar=skillUi.GetHotbarBounds(camera.GetViewportWidth(),camera.GetViewportHeight());
            const auto hud=HudLayout::Calculate(bar,HudLayout::Scale(camera.GetViewportWidth()));
            questUi.RenderTracker(inRenderer,camera.GetViewportWidth(),camera.GetViewportHeight(),hud.hp.top);
            skillUi.Render(inRenderer,camera.GetViewportWidth(),camera.GetViewportHeight(),false);
            inventoryUi.RenderQuickSlots(inRenderer,camera.GetViewportWidth(),camera.GetViewportHeight(),bar,
                IsUiPageOpen(SystemUiPage::Inventory));
        }
        std::vector<ChatBubbleAnchor> chatAnchors;
        if (const auto anchor=ChatBubblePosition(localPlayerId)) chatAnchors.push_back({localPlayerId,*anchor});
        for (const auto& [id,actor]:remotePlayers)
            if (const auto anchor=ChatBubblePosition(id)) chatAnchors.push_back({id,*anchor});
        for (const auto& [id,actor]:combatPlayers)
            if (const auto anchor=ChatBubblePosition(id)) chatAnchors.push_back({id,*anchor});
        chatUi.RenderBubbles(inRenderer,chatAnchors,camera.GetViewportWidth(),camera.GetViewportHeight());
        chatUi.Render(inRenderer,camera.GetViewportWidth());
        RenderUiWindows(inRenderer);
        if (!dungeonCleared && !completionStopping && systemUiPage!=SystemUiPage::Menu)
            skillUi.RenderDragOverlay(inRenderer,camera.GetViewportWidth(),camera.GetViewportHeight());
    }

    std::optional<Vector2> GameWorld::ChatBubblePosition(const std::uint64_t inPlayerId) const
    {
        Vector2 ground{}; float height=0, headHeight=0;
        if (inPlayerId==localPlayerId && localPlayerId!=0)
        { ground=player.GetGroundPosition();height=player.GetHeight();headHeight=player.GetChatHeadHeight(); }
        else if (dungeonEntryState==DungeonEntryState::Entered)
        {
            const auto actor=combatPlayers.find(inPlayerId);
            if(actor==combatPlayers.end())return std::nullopt;
            ground=actor->second->GetGroundPosition();height=actor->second->GetHeight();
            headHeight=actor->second->GetChatHeadHeight();
        }
        else
        {
            const auto actor=remotePlayers.find(inPlayerId);
            if(actor==remotePlayers.end())return std::nullopt;
            ground=actor->second.displayedPosition;
            headHeight=actor->second.idleAnimation.GetTopOffset();
        }
        const auto position=camera.WorldToScreen(ground);
        if(position.x<0 || position.x>camera.GetViewportWidth() || position.y-height<0
            || position.y-height>camera.GetViewportHeight()+96)return std::nullopt;
        return Vector2{position.x,position.y-height-headHeight};
    }

    void GameWorld::Resize(const float inViewportWidth, const float inViewportHeight)
    {
        draggedUiPage=SystemUiPage::Closed;
        questUi.CancelTrackerDrag();
        chatUi.CancelInput();
        GameWindow::SetChatInputMode(false);
        commandQueue.Clear();
        skillUi.CancelDrag();
        inventoryUi.Close();
        inventoryUi.SetHotbarBounds(skillUi.GetHotbarBounds(inViewportWidth, inViewportHeight), HudLayout::Scale(inViewportWidth));
        camera.Resize(inViewportWidth, inViewportHeight);
        camera.Follow(player.GetGroundPosition(), gameplayMap.GetWorldLeft(), gameplayMap.GetWorldTop(),
            gameplayMap.GetWorldRight(), gameplayMap.GetWorldBottom());
    }

    D2D1_RECT_F GameWorld::GetGameplayTextBounds() const
    {
        // Keep the top-left combat strip and the reserved top-right UI clear of hints/status.
        const bool inDungeon = dungeonEntryState == DungeonEntryState::Entered;
        const float top = inDungeon ? 72.0f : 16.0f;
        const float right = inDungeon ? camera.GetViewportWidth() - TOP_RIGHT_UI_RESERVED_WIDTH - TOP_UI_GAP : 1080.0f;
        return D2D1::RectF(20.0f, top, right, top + 28.0f);
    }

    std::wstring_view GameWorld::GetDungeonStatusText() const
    {
        if (!combatStatus.empty()) return combatStatus;
        if (dungeonCleared) return L"Dungeon: cleared";
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
        return dungeonCleared || completionStopping || isDungeonSelectionOpen || systemUiPage != SystemUiPage::Closed
            || pendingPartyInvitation.has_value() || partyKickedNotice;
    }

    bool GameWorld::HandleChatEvent(const TownEvent& inEvent)
    {
        if(const auto message=std::get_if<TownProtocol::ChatMessage>(&inEvent))
        {
            const bool self=message->senderPlayerId==localPlayerId;
            const auto channel=message->channel==TownProtocol::ChatChannel::Party ? ChatChannel::Party:ChatChannel::General;
            chatUi.Receive(message->messageId,message->senderPlayerId,channel,Utf8ToWide(message->senderName),
                Utf8ToWide(message->message),self,ChatBubblePosition(message->senderPlayerId).has_value());
            if(self) pendingChatResults.erase(message->requestSequence);
            return true;
        }
        if(const auto result=std::get_if<TownProtocol::ChatResult>(&inEvent))
        {
            if(pendingChatResults.erase(result->sequence)==0)return true;
            using Code=TownProtocol::ChatResultCode;
            switch(result->result)
            {
            case Code::Succeeded: break; // ACK has no body; only ChatMessage enters history.
            case Code::NotInParty: chatUi.Notice(L"파티가 없어 전송할 수 없습니다.");break;
            case Code::RateLimited: chatUi.Notice(L"너무 빠르게 전송했습니다. 잠시 후 다시 보내 주세요.");break;
            case Code::InvalidMessage: chatUi.Notice(L"빈 문장과 제어 문자는 전송할 수 없습니다. 입력 한도는 50자입니다.");break;
            case Code::NotEntered: chatUi.Notice(L"캐릭터 입장 후 채팅할 수 있습니다.");break;
            case Code::StaleSequence:
            case Code::SequenceConflict: chatUi.Notice(L"채팅 요청 순서가 맞지 않습니다. 다시 접속해 주세요.");break;
            default: chatUi.Notice(L"서버가 채팅 요청을 처리하지 못했습니다.");break;
            }
            return true;
        }
        return false;
    }

    // Process committed progression independently of room shutdown/deferred world events.
    bool GameWorld::HandleExperienceEvent(const TownEvent& inEvent)
    {
        const auto* event=std::get_if<TownProtocol::ExperienceState>(&inEvent);
        if (!event) return false;
        try { ApplyExperienceState(event->json); }
        catch (const std::exception&) { partyStatusText=L"경험치 상태를 확인하지 못했습니다."; }
        return true;
    }

    /** Validate a complete committed snapshot, then publish it atomically on the game thread. */
    void GameWorld::ApplyExperienceState(const std::string_view inPayload)
    {
        using namespace CharacterInventoryJson;
        if (inPayload.empty() || inPayload.size()>1024) throw std::runtime_error("Invalid experience payload size.");
        const auto value=Json::parse(inPayload);
        PlayerSkills::Catalog::Keys(value,{"characterId","revision","level","experience","requiredExperience","atMaxLevel"});
        ExperienceView next;
        next.characterId=UInt64(value.at("characterId"));
        if (next.characterId==0 || next.characterId!=townClient.GetSelectedCharacterId() || !townClient.IsConnected()) return;
        next.revision=UInt64(value.at("revision"));
        if (experienceView && next.revision<experienceView->revision) return;
        next.level=UInt32(value.at("level"),1,1000000);
        next.experience=UInt64(value.at("experience"));
        next.requiredExperience=UInt64(value.at("requiredExperience"));
        if (!value.at("atMaxLevel").is_boolean()) throw std::runtime_error("Invalid maximum-level flag.");
        next.atMaxLevel=value.at("atMaxLevel").get<bool>();
        if (next.atMaxLevel!=(next.level==1000000)
            || (next.atMaxLevel ? (next.experience!=0 || next.requiredExperience!=0)
                : (next.requiredExperience==0 || next.experience>=next.requiredExperience)))
            throw std::runtime_error("Invalid experience state.");
        if (experienceView && next.revision==experienceView->revision)
        {
            if (next!=*experienceView) throw std::runtime_error("Conflicting experience revision.");
            return;
        }
        const bool levelChanged=!experienceView || experienceView->level!=next.level;
        experienceView=next;
        if (levelChanged)
        {
            // Refresh authoritative SP/skill permissions and level-gated views; never calculate rewards here.
            if (skillUi.GetProgression().level!=next.level) townClient.RequestSkillState();
            if (localCharacterId!=0) inventoryUi.SetCharacterContext(next.characterId,localCharacterId,next.level);
            questUi.RequestRefresh();
        }
    }

    void GameWorld::ProcessNetworkEvents(const InputState& inInput)
    {
        auto incoming = townClient.ConsumeEvents();
        // Chat and committed EXP update on the game thread even while room cleanup defers world events.
        std::erase_if(incoming,[this](const auto& event){return HandleChatEvent(event) || HandleExperienceEvent(event);});
        if (completionStopping)
        {
            for (auto& event : incoming) deferredTownEvents.push_back(std::move(event));
            return;
        }
        auto events = std::move(deferredTownEvents);
        deferredTownEvents.clear();
        for (auto& event : incoming) events.push_back(std::move(event));
        for (TownEvent& event : events)
        {
            if (completionStopping)
            {
                deferredTownEvents.push_back(std::move(event));
                continue;
            }
            std::visit([this, &inInput](auto& inEvent)
            {
                using EventType = std::decay_t<decltype(inEvent)>;
                if constexpr (std::is_same_v<EventType, TownProtocol::EnterTownResponse>)
                {
                    localPlayerId = inEvent.playerId;
                    localCharacterId = inEvent.characterId;
                    player.ConfigureJumpSpeed(characterDefinitions, localCharacterId);
                    skillUi.Invalidate();
                    townHp = {}; townMp = {}; dungeonHp = {}; dungeonMp = {};
                    inventoryUi.Reset();
                    inventoryUi.SetCharacterContext(townClient.GetSelectedCharacterId(),localCharacterId,experienceView ? experienceView->level:0);
                    partySnapshot = {};
                    ResetPartyRequestUi();
                    partyKickedNotice = false;
                    wantedPartyDirectoryRevision = 0;
                    pendingPartyInvitation.reset();
                    partyInvitationAnswerPending = false;
                    uiWindows.clear();systemUiPage=SystemUiPage::Closed;
                    draggedUiPage=SystemUiPage::Closed;skillUi.CancelDrag();questUi.Close();
                    completionWindowHidden=invitationHidden=false;hiddenPartyRequestId=0;
                    partyStatusText.clear();
                    movementSequence = 0;
                    ApplyMap(inEvent.map, Vector2{ inEvent.map.spawnX, inEvent.map.spawnY });
                }
                else if constexpr (std::is_same_v<EventType, SkillStateEvent>)
                {
                    try
                    {
                        skillUi.ApplyState(inEvent.payload,localCharacterId);
                        questUi.RequestRefresh();
                        ApplyTownResources(inEvent.payload);
                        inventoryUi.SetCharacterContext(townClient.GetSelectedCharacterId(),localCharacterId,
                            experienceView ? experienceView->level:skillUi.GetProgression().level);
                        if (dungeonEntryState==DungeonEntryState::Entered) dungeonClient.RequestCombatRefresh();
                    }
                    catch (const std::exception& error)
                    { skillUi.Invalidate(); partyStatusText=L"스킬 상태 오류: "+Utf8ToWide(error.what()); }
                }
                else if constexpr (std::is_same_v<EventType, QuestEvent>)
                {
                    try
                    {
                        if (inEvent.kind == QuestEvent::Kind::StatePage) questUi.ApplyPage(inEvent.payload);
                        else questUi.ApplyResult(inEvent.payload);
                        inventoryUi.ObserveRevision(townClient.GetSelectedCharacterId(), questUi.GetObservedRevision());
                    }
                    catch (const std::exception&) { questUi.InvalidResponse(); }
                }
                else if constexpr (std::is_same_v<EventType, InventoryEvent>)
                {
                    try
                    {
                        if (inEvent.kind == InventoryEvent::Kind::State) inventoryUi.HandleState(inEvent.payload);
                        else if (inEvent.kind == InventoryEvent::Kind::Definitions) inventoryUi.HandleDefinitions(inEvent.payload);
                        else
                        {
                            if (inventoryUi.HandleOperation(inEvent.payload)) townClient.RequestSkillState();
                            questUi.RequestRefresh();
                        }
                        questUi.ObserveRevision(inventoryUi.GetObservedRevision());
                        if (inventoryUi.ConsumeCombatRefresh()) dungeonClient.RequestCombatRefresh();
                    }
                    catch (const std::exception&)
                    {
                        inventoryUi.Invalidate(L"서버 인벤토리 상태를 확인하지 못했습니다. 다시 조회합니다");
                    }
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
                        if (!hasSentMovementInput || inEvent.lastProcessedInput>movementSequence
                            || (hasTownMovementTick && inEvent.serverTick<=lastTownMovementTick)) return;
                        lastTownMovementTick=inEvent.serverTick;
                        hasTownMovementTick=true;
                        const auto sent=std::find_if(townMovementSends.begin(),townMovementSends.end(),
                            [&inEvent](const auto& input){return input.sequence==inEvent.lastProcessedInput;});
                        if (sent==townMovementSends.end()) return;
                        // An ack may describe an older heartbeat with the same direction. Use that
                        // input's actual send time once; never pretend this is synchronized snapshot age.
                        if (inEvent.lastProcessedInput>lastTownAcknowledgedSequence)
                        {
                            const float responseSeconds=std::chrono::duration<float>(
                                std::chrono::steady_clock::now()-sent->sentTime).count();
                            const float sample=std::clamp(responseSeconds*0.5f,0.0f,0.25f);
                            townSnapshotDelaySeconds=hasTownDelaySample
                                ? townSnapshotDelaySeconds+(sample-townSnapshotDelaySeconds)*0.2f:sample;
                            hasTownDelaySample=true;
                            lastTownAcknowledgedSequence=inEvent.lastProcessedInput;
                        }
                        // Do not reconcile across a direction/stop change, including turning away
                        // and back to the same direction. Continuous same-state heartbeats are safe.
                        const bool movementBlocked=chatUi.IsEditing() || IsUiTextEditing() || dungeonCleared || completionStopping || player.IsHitReacting()
                            || player.IsAttacking() || player.IsLocallyCasting();
                        const auto currentDirectionX=static_cast<std::int8_t>(movementBlocked ? 0:
                            static_cast<int>(inInput.moveRight)-static_cast<int>(inInput.moveLeft));
                        const auto currentDirectionY=static_cast<std::int8_t>(movementBlocked ? 0:
                            static_cast<int>(inInput.moveDown)-static_cast<int>(inInput.moveUp));
                        const bool inputMatchesSentState=currentDirectionX==lastSentDirectionX
                            && currentDirectionY==lastSentDirectionY;
                        if (inputMatchesSentState && inEvent.lastProcessedInput>=townMovementStateSequence
                            && inEvent.lastProcessedInput>=lastTownAcknowledgedSequence)
                            player.ReconcileTownGroundPosition({inEvent.position.x,inEvent.position.y},
                                {inEvent.velocity.x,inEvent.velocity.y},townSnapshotDelaySeconds,gameplayMap);
                        // Keep the acknowledged entry while subsequent snapshots still refer to it.
                        while (!townMovementSends.empty() && townMovementSends.front().sequence<lastTownAcknowledgedSequence)
                            townMovementSends.pop_front();

                    }
                    else if (auto iterator = remotePlayers.find(inEvent.playerId); iterator != remotePlayers.end())
                    {
                        const Vector2 snapshotPosition{ inEvent.position.x, inEvent.position.y };
                        const Vector2 velocity{ inEvent.velocity.x, inEvent.velocity.y };
                        iterator->second.snapshotPosition = snapshotPosition;
                        iterator->second.velocity = velocity;
                        iterator->second.secondsSinceSnapshot = 0.0f;
                        const Vector2 difference{snapshotPosition.x - iterator->second.displayedPosition.x,
                            snapshotPosition.y - iterator->second.displayedPosition.y};
                        constexpr float SNAP_DISTANCE = 200.0f;
                        if (difference.x * difference.x + difference.y * difference.y > SNAP_DISTANCE * SNAP_DISTANCE)
                            iterator->second.displayedPosition = snapshotPosition;
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
                else if constexpr (std::is_same_v<EventType, TownProtocol::DungeonCompletionResponse>)
                {
                    if (inEvent.previousRoomId != dungeonRoomId
                        || dungeonEntryState != DungeonEntryState::Entered) return;
                    if (!inEvent.succeeded)
                    {
                        completionPending = false;
                        completionStatus = L"요청을 처리하지 못했습니다. 다시 선택해 주세요.";
                        return;
                    }
                    completionResponse = std::move(inEvent);
                    dungeonCleared = true;
                    completionPending = completionStopping = true;
                    completionStatus = completionResponse->retry ? L"재도전 준비 중..." : L"마을로 이동 중...";
                    dungeonClient.RequestStop();
                }
                else if constexpr (std::is_same_v<EventType, TownProtocol::MapChanged>)
                {
                    ApplyMap(inEvent.map, Vector2{ inEvent.position.x, inEvent.position.y });
                }
                else if constexpr (std::is_same_v<EventType, TownProtocol::DungeonSelectionOpen>)
                {
                    if (dungeonEntryState == DungeonEntryState::Idle && !inEvent.dungeons.empty()
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
                    if (IsDungeonUiRestricted()) return;
                    pendingPartyInvitation = std::move(inEvent);
                    invitationHidden=false;
                    partyInvitationAnswerPending = false;
                    partyStatusText = L"Party invitation received.";
                }
                else if constexpr (std::is_same_v<EventType, TownProtocol::PartySnapshot>)
                {
                    const std::uint64_t previousPartyId = partySnapshot.partyId;
                    const bool partyChanged = previousPartyId != inEvent.partyId;
                    const bool leaderChanged = partySnapshot.leaderPlayerId != inEvent.leaderPlayerId;
                    partySnapshot = std::move(inEvent);
                    if (partyChanged || leaderChanged || !IsPartyLeader())
                    {
                        partyJoinRequests.clear();
                        answeringPartyJoinRequestId = 0;
                    }
                    if (partySnapshot.partyId != 0)
                    {
                        partyCreationPending=false;
                        ownPartyJoinRequest.reset();
                        partyJoinSendPending = false;
                    }
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
                        && IsUiPageOpen(SystemUiPage::Party))
                    {
                        SetSystemUiPage(SystemUiPage::PartyDirectory);
                    }
                    else if (previousPartyId == 0 && partySnapshot.partyId != 0
                        && (IsUiPageOpen(SystemUiPage::PartyDirectory)
                            || IsUiPageOpen(SystemUiPage::PartyCreate) || IsUiPageOpen(SystemUiPage::PartyDetails)))
                    {
                        partyCreationPending = false;
                        SetSystemUiPage(SystemUiPage::Party);
                    }
                }
                else if constexpr (std::is_same_v<EventType, TownProtocol::PartyOperationResult>)
                {
                    partyStatusText = GetPartyResultText(inEvent.result);
                    if (inEvent.operation == TownProtocol::PartyOperationType::RequestJoin)
                    {
                        if (inEvent.result != TownProtocol::PartyResultCode::Succeeded) partyJoinSendPending = false;
                        else partyStatusText = L"가입 요청을 보냈습니다. 파티장의 응답을 기다리는 중입니다.";
                    }
                    if (inEvent.operation == TownProtocol::PartyOperationType::AnswerJoin
                        && inEvent.result != TownProtocol::PartyResultCode::Succeeded)
                    {
                        // Results contain no requestId: never remove another queued request here.
                        answeringPartyJoinRequestId = 0;
                    }
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
                else if constexpr (std::is_same_v<EventType, TownProtocol::PartyDetailResponse>)
                {
                    if (!IsUiPageOpen(SystemUiPage::PartyDetails) || inEvent.partyId != selectedDirectoryPartyId
                        || IsDungeonUiRestricted()) return;
                    partyDetailRequestPending = false;
                    partyDetails = std::move(inEvent);
                    if (partyDetails->result != TownProtocol::PartyResultCode::Succeeded)
                        partyStatusText = GetPartyResultText(partyDetails->result);
                    if (partyDetailRefreshNeeded)
                    {
                        partyDetailRefreshNeeded = false;
                        RequestSelectedPartyDetail();
                    }
                }
                else if constexpr (std::is_same_v<EventType, TownProtocol::PartyJoinRequestUpdate>)
                {
                    const bool pending = inEvent.state == TownProtocol::PartyJoinRequestState::Pending;
                    if (inEvent.requesterPlayerId == localPlayerId)
                    {
                        partyJoinSendPending = false;
                        if (pending)
                        {
                            if (!IsDungeonUiRestricted() && partySnapshot.partyId == 0)
                            {
                                ownPartyJoinRequest = inEvent;
                                partyStatusText = L"파티장의 응답을 기다리는 중입니다.";
                            }
                        }
                        else if (!ownPartyJoinRequest || ownPartyJoinRequest->requestId == inEvent.requestId)
                        {
                            ownPartyJoinRequest.reset();
                            partyStatusText = inEvent.state == TownProtocol::PartyJoinRequestState::Accepted
                                ? L"가입이 승인되었습니다."
                                : inEvent.state == TownProtocol::PartyJoinRequestState::Rejected
                                    ? L"가입 요청이 거절되었습니다." : std::wstring(GetPartyResultText(inEvent.result));
                        }
                    }
                    if (!pending)
                    {
                        // Remove the exact ID from the visible request and the rest of the queue.
                        std::erase_if(partyJoinRequests, [&inEvent](const auto& request)
                            { return request.requestId == inEvent.requestId; });
                        if (answeringPartyJoinRequestId == inEvent.requestId) answeringPartyJoinRequestId = 0;
                    }
                    else if (IsPartyLeader() && partySnapshot.partyId == inEvent.partyId
                        && !IsDungeonUiRestricted() && inEvent.requesterPlayerId != localPlayerId)
                    {
                        const auto position = std::lower_bound(partyJoinRequests.begin(), partyJoinRequests.end(),
                            inEvent.requestId, [](const auto& request, std::uint64_t inId)
                                { return request.requestId < inId; });
                        if (position == partyJoinRequests.end() || position->requestId != inEvent.requestId)
                            partyJoinRequests.insert(position, std::move(inEvent));
                    }
                }
                else if constexpr (std::is_same_v<EventType, TownProtocol::PartyKicked>)
                {
                    // Explicit victim-only server event; never infer a kick from an empty snapshot.
                    partyKickedNotice = true;
                    skillUi.CancelDrag();
                    commandQueue.Clear();
                }
                else if constexpr (std::is_same_v<EventType, TownProtocol::PartyDirectoryPage>)
                {
                    if (IsUiPageOpen(SystemUiPage::PartyDirectory) || IsUiPageOpen(SystemUiPage::PartyDetails))
                    {
                        partyDirectoryPage = std::move(inEvent);
                        directoryPageRequestPending = false;
                        if (partyDirectoryPage.revision < wantedPartyDirectoryRevision)
                            RequestPartyDirectoryPage(std::max(1U, partyDirectoryPage.page));
                    }
                }
                else if constexpr (std::is_same_v<EventType, TownProtocol::PartyDirectoryChanged>)
                {
                    if ((IsUiPageOpen(SystemUiPage::PartyDirectory) || IsUiPageOpen(SystemUiPage::PartyDetails))
                        && inEvent.revision > partyDirectoryPage.revision)
                    {
                        wantedPartyDirectoryRevision = std::max(wantedPartyDirectoryRevision, inEvent.revision);
                        RequestPartyDirectoryPage(std::max(1U, partyDirectoryPage.page));
                        if (IsUiPageOpen(SystemUiPage::PartyDetails)) RequestSelectedPartyDetail();
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
                        dungeonWorld = DungeonWorld::Parse(inEvent.json, dungeonRoomId, localPlayerId, monsterCatalog);
                        playerSkillPresentation.ValidateServer(dungeonWorld->GetPlayerSkills());
                        dungeonMovementSequence = lastDungeonStateSequence = 0;
                        lastTownPosition = player.GetGroundPosition();
                        ApplyDungeonMap(dungeonWorld->GetEntryMapId(), dungeonWorld->GetSpawn());
                        dungeonEntryState = DungeonEntryState::Entered;
                        combatStatus.clear();
                        dungeonClient.StartRealtime(dungeonRoomId);
                        dungeonClient.StartCombatPolling();
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
                else if constexpr (std::is_same_v<EventType, DungeonCombatEvent>)
                {
                    if (dungeonEntryState != DungeonEntryState::Entered || !dungeonWorld) return;
                    try { ApplyCombatSnapshot(inEvent.json); }
                    catch (const std::exception& error)
                    {
                        ResetDungeonEntry();
                        combatStatus = L"Dungeon combat: " + Utf8ToWide(error.what());
                    }
                }
                else if constexpr (std::is_same_v<EventType, DungeonRealtimeResetEvent>)
                {
                    if (inEvent.mapEpoch <= pendingMapEpoch) return;
                    pendingMapEpoch = inEvent.mapEpoch;
                    player.ClearCombatKnockback();
                    targetMonsterId = 0; targetHp = {};
                    inventoryUi.Close();
                    player.ClearPredictedAttackFacing();
                    player.ClearSlideState();
                    skillUi.CancelDrag();
                    commandQueue.Clear();
                    combatBuffer.Clear(); presentationSnapshot.reset();
                    for (auto& room : dungeonMonsters)
                        for (auto& monster : room.second) monster->ClearAttackEffect();
                }
                else if constexpr (std::is_same_v<EventType, DungeonRealtimeEvent>)
                {
                    if (dungeonEntryState != DungeonEntryState::Entered || !dungeonWorld) return;
                    try { ApplyCombatState(std::move(inEvent.snapshot)); }
                    catch (const std::exception&) { /* Invalid full frame: keep reliable recovery. */ }
                }
                else if constexpr (std::is_same_v<EventType, DungeonActionResultEvent>)
                {
                    if (inEvent.sequence > combatActionSequence) { ResetDungeonEntry(); return; }
                    player.ResolvePredictedAttackFacing(inEvent.sequence, inEvent.accepted);
                    player.ResolvePredictedSlide(inEvent.sequence, inEvent.accepted);
                    if (const auto skill=skillActionIds.find(inEvent.sequence); skill!=skillActionIds.end())
                    {
                        const bool boost = skillBoostSequences.erase(inEvent.sequence) != 0;
                        if (inEvent.accepted) { lastAcceptedSkillSequence=std::max(lastAcceptedSkillSequence,inEvent.sequence); if (!boost) skillUi.AcceptSkill(skill->second, dungeonWorld ? dungeonWorld->GetCombatRules().tempoScale : 1.0f); dungeonClient.RequestCombatRefresh(); }
                        skillActionIds.erase(skill);
                    }
                    if (inEvent.sequence <= lastCombatActionResult) return;
                    lastCombatActionResult = inEvent.sequence;
                    if (!inEvent.accepted) rejectedActionSeconds = 1.0f;
                }
            }, event);
        }
    }

    void GameWorld::ApplyCombatSnapshot(const std::string_view inJson)
    {
        auto snapshot=DungeonCombatSnapshot::Parse(inJson);
        // Reliable JSON carries progression/CD even when newer realtime poses are already displayed.
        // Use an independent monotonic tick; never apply these JSON positions over realtime poses.
        if (dungeonWorld && snapshot.roomId==dungeonRoomId && snapshot.mapEpoch>=pendingMapEpoch
            && snapshot.mapEpoch>=appliedMapEpoch && snapshot.mapId==dungeonMapId
            && (!hasSkillStateTick || snapshot.serverTick>=lastSkillStateTick))
        {
            const auto self=std::find_if(snapshot.players.begin(),snapshot.players.end(),[this](const auto& actor){return actor.playerId==localPlayerId;});
            if (self!=snapshot.players.end() && self->actionSequence>=lastAcceptedSkillSequence && self->actionSequence<=combatActionSequence && self->moveSequence<=dungeonMovementSequence)
            { skillUi.ApplyCombatState(*self); lastSkillStateTick=snapshot.serverTick; hasSkillStateTick=true; }
        }
        ApplyCombatState(std::move(snapshot));
    }

    void GameWorld::ApplyCombatState(DungeonCombatSnapshot snapshot)
    {
        if (snapshot.roomId != dungeonRoomId || snapshot.mapEpoch < pendingMapEpoch
            || snapshot.mapEpoch < appliedMapEpoch) return;
        if (hasCombatTick && snapshot.serverTick < lastCombatTick) return;
        if (dungeonCleared && !snapshot.cleared && snapshot.state != "Stopped") return;
        if (!snapshot.realtime && combatBuffer.HasFreshRealtime() && !snapshot.cleared) return;
        if (!snapshot.realtime && hasCombatTick && snapshot.serverTick == lastCombatTick
            && !(snapshot.cleared && !dungeonCleared)) return;
        for (auto& actor : snapshot.players)
        {
            actor.presentationTimeMs = static_cast<double>(snapshot.serverTimeMs);
            if (snapshot.cleared) ClearCombatKnockback(actor);
        }
        for (auto& actor : snapshot.monsters)
        {
            actor.presentationTimeMs = static_cast<double>(snapshot.serverTimeMs);
            if (snapshot.cleared) ClearCombatKnockback(actor);
        }
        const auto self = std::find_if(snapshot.players.begin(), snapshot.players.end(), [this](const auto& inPlayer)
            { return inPlayer.playerId == localPlayerId; });
        if (self == snapshot.players.end()) throw std::runtime_error("Combat snapshot omits the local player.");
        if (self->actionSequence > combatActionSequence || self->moveSequence > dungeonMovementSequence)
            throw std::runtime_error("Combat acknowledgement exceeds the sent sequence.");
        if (snapshot.mapId != dungeonMapId && self->moveSequence <= lastDungeonStateSequence
            && snapshot.mapEpoch <= appliedMapEpoch) return;
        const auto& map = dungeonWorld->GetMap(snapshot.mapId);
        for (const auto& source : snapshot.monsters)
        {
            const auto spawn = std::find_if(map.monsters.begin(), map.monsters.end(), [&source](const auto& inSpawn)
                { return inSpawn.instanceId == source.instanceId && inSpawn.dataId == source.dataId; });
            if (spawn == map.monsters.end() || !monsterCatalog.Contains(source.dataId))
                throw std::runtime_error("Combat monster does not belong to this map.");
        }
        if (snapshot.state == "Stopped") { ResetDungeonEntry(); return; }
        if (snapshot.mapId != dungeonMapId || snapshot.mapEpoch != appliedMapEpoch)
        {
            ApplyDungeonMap(snapshot.mapId, self->position);
            combatBuffer.Clear(); presentationSnapshot.reset();
        }
        if (combatSnapshot)
        {
            QueueCombatHitEffects(*combatSnapshot, snapshot, true);
            QueueCombatShotSounds(*combatSnapshot, snapshot, true);
        }
        if (snapshot.mapEpoch != appliedMapEpoch)
        {
            targetMonsterId = 0; targetHp = {};
            // Consume the boundary's old target, including a same-map epoch reset. Await a fresh hit.
            if (appliedMapEpoch != 0 && self->hasHudState)
                lastHudHitSequence = std::max(lastHudHitSequence, self->lastHitSequence);
        }
        ApplyHudSnapshot(snapshot, *self);
        appliedMapEpoch = pendingMapEpoch = snapshot.mapEpoch;
        const auto& rules = dungeonWorld->GetCombatRules();
        // Old movement acknowledgements affect only position correction, not HP/entities/clear state.
        // First observation, map changes and repeated snapshots must not replay a skill sound.
        if (combatSnapshot && combatSnapshot->mapEpoch == snapshot.mapEpoch && combatSnapshot->mapId == snapshot.mapId
            && self->skillActive && self->skillId == "Character1.FrontKick")
        {
            const auto previous = std::find_if(combatSnapshot->players.begin(), combatSnapshot->players.end(),
                [this](const auto& actor) { return actor.playerId == localPlayerId; });
            if (previous != combatSnapshot->players.end() && self->skillSequence > previous->skillSequence)
                (void)audio.PlayEffect();
        }
        if (combatSnapshot && combatSnapshot->mapEpoch == snapshot.mapEpoch && combatSnapshot->mapId == snapshot.mapId)
        {
            const auto previous = std::find_if(combatSnapshot->players.begin(), combatSnapshot->players.end(),
                [this](const auto& actor) { return actor.playerId == localPlayerId; });
            if (previous != combatSnapshot->players.end() && self->hasKnockbackState
                && self->knockbackSequence != previous->knockbackSequence) commandQueue.Clear();
        }
        player.ApplyCombatState(*self, rules);
        if (self->moveSequence >= lastDungeonStateSequence) player.ReconcileCombatGroundPosition(*self, gameplayMap);
        lastDungeonStateSequence = std::max(lastDungeonStateSequence, self->moveSequence);
        std::unordered_set<std::uint64_t> presentPlayers;
        for (const auto& state : snapshot.players)
        {
            if (state.playerId == localPlayerId) continue;
            presentPlayers.insert(state.playerId);
            auto& actor = combatPlayers[state.playerId];
            const bool created = !actor;
            if (created) { actor = std::make_unique<Player>(player); actor->ResetActionState(); }
            if (created) actor->ApplyCombatState(state, rules, true);
        }
        std::erase_if(combatPlayers, [&presentPlayers](const auto& inEntry) { return !presentPlayers.contains(inEntry.first); });
        auto& monsters = dungeonMonsters.at(dungeonMapId);
        std::unordered_set<std::uint64_t> presentMonsters;
        for (const auto& state : snapshot.monsters)
        {
            presentMonsters.insert(state.instanceId);
            const auto actor = std::find_if(monsters.begin(), monsters.end(), [&state](const auto& inMonster)
                { return inMonster->GetInstanceId() == state.instanceId; });
            if (actor == monsters.end())
            {
                MonsterSpawn spawn{state.instanceId, state.dataId, state.position, state.facingLeft, state.hp, state.maxHp};
                auto created = std::make_unique<Monster>(*monsterTemplates.at(state.dataId), spawn);
                created->ApplyCombatState(state, rules, true, snapshot.mapEpoch); monsters.push_back(std::move(created));
            }
        }
        std::erase_if(monsters, [&presentMonsters](const auto& inMonster) { return !presentMonsters.contains(inMonster->GetInstanceId()); });
        if (snapshot.cleared && !dungeonCleared)
        {
            completionPending = completionStopping = false;
            completionWaitSeconds = 0.0f;
            selectedCompletionIndex = 0;
            completionStatus.clear();
            isDungeonSelectionOpen = false;
        }
        // Prefer the captured server clock. Older JSON falls back to the exact tick duration,
        // never the rounded millisecond interval from the subscription response.
        if (!snapshot.realtime && !snapshot.hasServerTime)
        {
            const double tickSeconds = snapshot.tickIntervalSeconds > 0.0
                ? snapshot.tickIntervalSeconds : rules.tickIntervalSeconds;
            const long double timeMs = std::round(static_cast<long double>(snapshot.serverTick) * tickSeconds * 1000.0L);
            if (timeMs >= static_cast<long double>(std::numeric_limits<std::uint64_t>::max()))
                throw std::runtime_error("Combat tick time exceeds the supported range.");
            snapshot.serverTimeMs = static_cast<std::uint64_t>(timeMs);
        }
        if (snapshot.cleared)
        {
            player.ClearPredictedAttackFacing(); player.ClearSlideState(); combatBuffer.Clear();
            for (auto& room : dungeonMonsters)
                for (auto& monster : room.second) monster->ClearAttackEffect();
        }
        combatBuffer.Push(snapshot);
        lastCombatTick = snapshot.serverTick; hasCombatTick = true;
        if (snapshot.cleared && !dungeonCleared) completionWindowHidden=false;
        dungeonCleared = snapshot.cleared;
        combatSnapshotAge = 0.0f; combatSnapshot = std::move(snapshot);
    }

    void GameWorld::SendCombatActions(const InputState& inInput)
    {
        if (!combatSnapshot || combatSnapshot->state!="Running" || player.IsCombatDead()
            || pendingMapEpoch>appliedMapEpoch) { commandQueue.Clear(); return; }
        const auto actor=std::find_if(combatSnapshot->players.begin(),combatSnapshot->players.end(),
            [this](const auto& value){return value.playerId==localPlayerId;});
        if (player.IsHitstopped()) { commandQueue.Clear(); return; }
        if (player.IsSliding() || player.HasPendingSlide() || (actor != combatSnapshot->players.end() && actor->slideActive))
        {
            commandQueue.Clear();
            return; // Sliding never reserves a shot, jump or skill for its ending frame.
        }
        bool matched=false;
        const auto command=playerSkillPresentation.TryCommand(commandQueue,localCharacterId,skillUi.GetProgression().skillLevels,matched);
        const auto hotkey=skillUi.Hotkey(inInput);
        const auto id=!hotkey.empty() ? hotkey:command;
        if (matched || !hotkey.empty())
        {
            commandQueue.Clear();
            const bool boost = actor!=combatSnapshot->players.end() && actor->skillActive && actor->skillId==id
                && !hotkey.empty() && playerSkillPresentation.GetDefinition(id).at("type")=="area";
            if (actor!=combatSnapshot->players.end() && actor->hp>0 && actor->reaction==CombatReaction::None
                && (!actor->skillActive || boost) && !actor->slideActive && actor->jumpPhase!=CombatJumpPhase::Prepare
                && (actor->shotPhase==CombatShotPhase::None || actor->shotPhase==CombatShotPhase::Recover)
                && !id.empty() && (boost || skillUi.CanUse(id,actor->height>0,true)) && combatInputsThisSecond<20 && skillActionIds.size()<64)
            {
                if (combatActionSequence==std::numeric_limits<std::uint32_t>::max()) { ResetDungeonEntry(); return; }
                ++combatInputsThisSecond;
                const auto sequence=++combatActionSequence;
                skillActionIds.emplace(sequence,id);
                if (boost) skillBoostSequences.insert(sequence);
                else player.PredictAttackFacing(sequence, true);
                dungeonClient.SendSkill(sequence,id,player.GetFacingLeft());
            }
            return; // No skill reservation; an unavailable command ending in X/C cannot also fire/jump.
        }
        if (actor==combatSnapshot->players.end() || actor->hp==0 || actor->reaction!=CombatReaction::None) return;
        const Vector2 direction{
            static_cast<float>(inInput.moveRight)-static_cast<float>(inInput.moveLeft),
            static_cast<float>(inInput.moveDown)-static_cast<float>(inInput.moveUp)};
        const bool slideIntent = inInput.WasPressed(InputKey::ActionX) && player.IsDungeonRunInput()
            && dungeonWorld && actor->jumpPhase == CombatJumpPhase::Grounded && player.GetHeight() <= 0.0f
            && !actor->skillActive && (direction.x != 0.0f || direction.y != 0.0f);
        for (const auto key:inInput.pressedKeys)
        {
            if (slideIntent && key != InputKey::ActionX) continue;
            const std::uint8_t action=key==InputKey::ActionX ? 1:key==InputKey::ActionC ? 2:0;
            if (action==0 || combatInputsThisSecond>=20) continue;
            if (combatActionSequence==std::numeric_limits<std::uint32_t>::max()) { ResetDungeonEntry(); return; }
            const bool slide = action == 1 && slideIntent;
            DungeonProtocol::DungeonActionInput packet;
            packet.sequence = combatActionSequence + 1;
            packet.action = slide ? 3 : action;
            // Horizontal intent establishes facing on the start frame; vertical intent keeps current facing.
            packet.facingLeft = slide && direction.x != 0.0f ? direction.x < 0.0f : player.GetFacingLeft();
            packet.mapEpoch = appliedMapEpoch;
            packet.moveSequence = dungeonMovementSequence;
            // Preserve original run input for moveSequence validation; slide motion uses facing only.
            packet.directionX = static_cast<std::int8_t>(direction.x);
            packet.directionY = static_cast<std::int8_t>(direction.y);
            packet.running = player.IsDungeonRunInput() ? 1 : 0;
            if (!dungeonClient.SendAction(packet)) continue;
            ++combatInputsThisSecond;
            combatActionSequence = packet.sequence;
            if (slide)
            {
                const auto& definition = dungeonWorld->GetSlideDefinition(localCharacterId);
                player.PredictSlide(packet.sequence, packet.facingLeft != 0, definition.durationSeconds,
                    definition.distancePerRunSpeedSeconds);
                commandQueue.Clear();
                break; // Consume X exactly once; no same-frame C/second X reservation.
            }
            if (action == 1) player.PredictAttackFacing(packet.sequence);
        }
    }

    void GameWorld::UpdateDungeonCombat(const float inDeltaSeconds)
    {
        combatInputWindowSeconds += inDeltaSeconds;
        if (combatInputWindowSeconds >= 1.0f) { combatInputWindowSeconds = 0.0f; combatInputsThisSecond = 0; }
        combatSnapshotAge = std::min(0.25f, combatSnapshotAge + inDeltaSeconds);
        rejectedActionSeconds = std::max(0.0f, rejectedActionSeconds - inDeltaSeconds);
        const float effectDelta = inDeltaSeconds * (dungeonWorld ? dungeonWorld->GetCombatRules().tempoScale : 1.0f);
        for (auto& effect : combatHitEffects) (void)effect.animation.AdvanceOnce(effectDelta);
        std::erase_if(combatHitEffects, [](const auto& effect) { return effect.animation.IsFinished(); });
        auto sampled = dungeonWorld ? combatBuffer.Sample(dungeonWorld->GetCombatRules()) : std::nullopt;
        if (sampled && presentationSnapshot)
        {
            QueueCombatHitEffects(*presentationSnapshot, *sampled, false);
            QueueCombatShotSounds(*presentationSnapshot, *sampled, false);
        }
        presentationSnapshot = std::move(sampled);
        if (!presentationSnapshot || !dungeonWorld) return;
        const auto& rules = dungeonWorld->GetCombatRules();
        for (const auto& state : presentationSnapshot->players)
        {
            if (state.playerId == localPlayerId) continue;
            if (const auto actor = combatPlayers.find(state.playerId); actor != combatPlayers.end())
            {
                actor->second->ApplyBufferedCombatState(state, rules);
                actor->second->UpdateCombatPresentation(inDeltaSeconds, gameplayMap);
            }
        }
        if (const auto monsters = dungeonMonsters.find(dungeonMapId); monsters != dungeonMonsters.end())
            for (const auto& state : presentationSnapshot->monsters)
                for (const auto& actor : monsters->second)
                    if (actor->GetInstanceId() == state.instanceId) actor->ApplyCombatState(state, rules, true, presentationSnapshot->mapEpoch);
    }

    // Shot counts advance on actual fire, not on input/Prepare. Local sound follows authority;
    // remote sound follows the same buffered clock as its pose. Never replay a snapshot baseline.
    void GameWorld::QueueCombatShotSounds(const DungeonCombatSnapshot& inPrevious,
        const DungeonCombatSnapshot& inCurrent, const bool inLocalPlayer)
    {
        if (inPrevious.roomId != inCurrent.roomId || inPrevious.mapId != inCurrent.mapId
            || inPrevious.mapEpoch != inCurrent.mapEpoch || inCurrent.cleared || inCurrent.state != "Running") return;
        for (const auto& actor : inCurrent.players)
        {
            if ((actor.playerId == localPlayerId) != inLocalPlayer) continue;
            const auto previous = std::find_if(inPrevious.players.begin(), inPrevious.players.end(),
                [&actor](const auto& old) { return old.playerId == actor.playerId; });
            const auto& baseline = previous != inPrevious.players.end() ? *previous : actor;
            auto& played = shotSounds.try_emplace(actor.playerId,
                ShotSoundState{baseline.shotSequence, baseline.shotCount}).first->second;
            if (actor.shotSequence < played.sequence
                || (actor.shotSequence == played.sequence && actor.shotCount <= played.count)) continue;
            played = {actor.shotSequence, actor.shotCount};
            // Collapse missed shots to one current sound rather than a delayed burst.
            if (actor.shotCount != 0) (void)audio.PlayEffect("GunShot");
        }
    }

    // Server reactionSequence advances only on actual damage. HP arrives before buffered reactions,
    // so remote contact effects follow the new reaction even if HP already changed in an earlier sample.
    // Repeated snapshots, first observations and map changes never replay a contact effect.
    void GameWorld::QueueCombatHitEffects(const DungeonCombatSnapshot& inPrevious,
        const DungeonCombatSnapshot& inCurrent, const bool inLocalPlayer)
    {
        if (inPrevious.roomId != inCurrent.roomId || inPrevious.mapId != inCurrent.mapId
            || inPrevious.mapEpoch != inCurrent.mapEpoch) return;
        const auto queue = [this](const CombatActorState& before, const CombatActorState& after, float bodyHeight)
        {
            if (after.reactionSequence <= before.reactionSequence || after.hp > before.hp) return;
            constexpr std::size_t MAX_HIT_EFFECTS = 64;
            if (combatHitEffects.size() == MAX_HIT_EFFECTS) combatHitEffects.erase(combatHitEffects.begin());
            combatHitEffects.push_back({ combatHitAnimation, after.position, after.height + bodyHeight });
        };
        for (const auto& actor : inCurrent.players)
        {
            if ((actor.playerId == localPlayerId) != inLocalPlayer) continue;
            const auto before = std::find_if(inPrevious.players.begin(), inPrevious.players.end(),
                [&actor](const auto& value) { return value.playerId == actor.playerId; });
            if (before != inPrevious.players.end()) queue(*before, actor, 96.0f);
        }
        if (inLocalPlayer) return;
        for (const auto& actor : inCurrent.monsters)
        {
            const auto before = std::find_if(inPrevious.monsters.begin(), inPrevious.monsters.end(),
                [&actor](const auto& value) { return value.instanceId == actor.instanceId; });
            if (before != inPrevious.monsters.end())
            {
                const auto& clips = monsterCatalog.Get(actor.dataId).clips;
                const auto idle = clips.find(MonsterMotion::Idle);
                const float bodyHeight = idle != clips.end() && !idle->second.frames.empty()
                    ? idle->second.frames.front().pivot.y * idle->second.scale * 0.5f : 48.0f;
                queue(*before, actor, bodyHeight);
            }
        }
    }

    void GameWorld::RenderCombatProjectiles(D2DRenderer& inRenderer) const
    {
        if (!combatSnapshot || !dungeonWorld) return;
        // Buffered positions are already slowed; only fallback velocity extrapolation needs combat age.
        const float age = presentationSnapshot ? 0.0f : combatSnapshotAge * dungeonWorld->GetCombatRules().tempoScale;
        const auto& projectiles = presentationSnapshot ? presentationSnapshot->projectiles : combatSnapshot->projectiles;
        for (const auto& projectile : projectiles)
        {
            const float distance = (projectile.speed > 0 ? projectile.speed : dungeonWorld->GetCombatRules().projectileSpeed) * age;
            const float height = projectile.height + projectile.heightDirection * distance;
            if (height < 0.0f) continue;
            const auto position = camera.WorldToScreen({projectile.position.x + projectile.direction * distance, projectile.position.y + projectile.directionY * distance});
            if (projectile.visual == "Stone")
            {
                // PlayerRock's existing stone palette, with server-owned position/radius.
                inRenderer.FillEllipse(position.x, position.y - height, projectile.radius, projectile.radius,
                    D2D1::ColorF(0.30f, 0.27f, 0.24f));
                inRenderer.FillEllipse(position.x - projectile.radius * 0.25f, position.y - height - projectile.radius * 0.25f,
                    projectile.radius * 0.35f, projectile.radius * 0.35f, D2D1::ColorF(0.62f, 0.56f, 0.48f));
            }
            else
            {
                const float speed = projectile.speed > 0 ? projectile.speed : dungeonWorld->GetCombatRules().projectileSpeed;
                const float screenDirectionY = projectile.directionY - projectile.heightDirection;
                const float travelDistance = speed * std::max(0.0f, projectile.ageSeconds + age)
                    * std::hypot(projectile.direction, screenDirectionY);
                inRenderer.DrawLuminousProjectile(position.x, position.y - height, projectile.direction, screenDirectionY,
                    projectile.radius, travelDistance, D2D1::ColorF(1.0f, 0.72f, 0.16f), D2D1::ColorF(1.0f, 0.98f, 0.84f));
            }
        }
    }

    void GameWorld::HudGauge::Set(const std::uint32_t inValue, const std::uint32_t inMaximum)
    {
        if (inValue > inMaximum) throw std::runtime_error("Invalid HUD resource.");
        const bool reset = !known || maximum != inMaximum;
        const auto previous = value;
        value = inValue; maximum = inMaximum; known = true;
        if (reset) { trailingRatio = Ratio(); holdSeconds = 0.0f; return; }
        trailingRatio = std::max(trailingRatio, Ratio());
        if (value < previous) holdSeconds = 0.2f;
    }

    // Consume hold and shrink times separately, including a frame crossing the hold boundary.
    void GameWorld::HudGauge::Update(const float inDeltaSeconds)
    {
        if (!known) return;
        const float elapsed = std::max(0.0f, inDeltaSeconds), held = std::min(holdSeconds, elapsed);
        holdSeconds -= held;
        trailingRatio = std::max(Ratio(), trailingRatio - (elapsed - held) * 0.6f);
    }

    void GameWorld::ApplyTownResources(const std::string_view inPayload)
    {
        using namespace CharacterInventoryJson;
        const auto body = Json::parse(inPayload);
        auto nextHp = townHp, nextMp = townMp;
        const auto read = [&body](const char* key, const char* maxKey, HudGauge& outGauge)
        {
            if (!body.contains(key) && !body.contains(maxKey)) { outGauge = {}; return; }
            const auto& value = body.at(key); const auto& maximum = body.at(maxKey);
            if (value.is_null() && maximum.is_null()) { outGauge = {}; return; }
            outGauge.Set(UInt32(value, 0, UINT32_MAX), UInt32(maximum, 0, UINT32_MAX));
        };
        read("hp", "maxHp", nextHp); read("mp", "maxMp", nextMp);
        // Town TCP state cannot write Dungeon resources, even if it arrives during combat.
        townHp = nextHp; townMp = nextMp;
    }

    void GameWorld::ApplyHudSnapshot(const DungeonCombatSnapshot& inSnapshot, const CombatPlayerState& inSelf)
    {
        dungeonHp.Set(inSelf.hp, inSelf.maxHp);
        if (inSelf.hasHudState)
        {
            if (inSelf.mpKnown) dungeonMp.Set(inSelf.mp, inSelf.maxMp); else dungeonMp = {};
            if (inSelf.lastHitSequence > lastHudHitSequence)
            {
                lastHudHitSequence = inSelf.lastHitSequence;
                if (targetMonsterId != inSelf.lastHitMonsterId) targetHp = {};
                targetMonsterId = inSelf.lastHitMonsterId;
            }
        }
        const auto target = std::find_if(inSnapshot.monsters.begin(), inSnapshot.monsters.end(),
            [this](const auto& actor) { return actor.instanceId == targetMonsterId; });
        if (target == inSnapshot.monsters.end() || target->hp == 0 || inSnapshot.cleared)
        { targetMonsterId = 0; targetHp = {}; }
        else targetHp.Set(target->hp, target->maxHp);
    }

    void GameWorld::RenderCombatHud(D2DRenderer& inRenderer) const
    {
        const float width = camera.GetViewportWidth(), height = camera.GetViewportHeight();
        const bool inDungeon = dungeonEntryState == DungeonEntryState::Entered;
        const auto iron = D2D1::ColorF(0x70757F), brass = D2D1::ColorF(0xBC9364);
        const auto panel = D2D1::ColorF(0x10141B), textColor = D2D1::ColorF(0xF0EAE0);
        const auto percent = [](const HudGauge& gauge)
        {
            if (!gauge.known || gauge.maximum == 0) return std::wstring(L"—");
            std::wostringstream text; text.imbue(std::locale::classic());
            text << std::fixed << std::setprecision(2) << static_cast<double>(gauge.value) * 100.0 / gauge.maximum << L"%";
            return text.str();
        };
        if (dungeonEntryState == DungeonEntryState::Entered && combatSnapshot && targetMonsterId != 0 && targetHp.known)
        {
            const auto target = std::find_if(combatSnapshot->monsters.begin(), combatSnapshot->monsters.end(),
                [this](const auto& actor) { return actor.instanceId == targetMonsterId; });
            if (target != combatSnapshot->monsters.end() && target->hp > 0)
            {
                const auto& spawns = dungeonWorld->GetMap(dungeonMapId).monsters;
                const auto spawn = std::find_if(spawns.begin(), spawns.end(), [this](const auto& actor) { return actor.instanceId == targetMonsterId; });
                const bool boss = target->hasHudState ? target->isBoss : spawn != spawns.end() && spawn->isBoss;
                const auto name = target->hasHudState ? target->name : spawn != spawns.end() ? spawn->name : std::string{};
                const auto level = target->hasHudState
                    ? (target->levelKnown ? std::optional<std::uint32_t>(target->level) : std::nullopt)
                    : spawn != spawns.end() ? spawn->level : std::nullopt;
                const float frameWidth = width - MONSTER_HUD_LEFT - TOP_RIGHT_UI_RESERVED_WIDTH - TOP_UI_GAP;
                const float frameHeight = boss ? 48.0f : 44.0f;
                const auto frame = D2D1::RectF(MONSTER_HUD_LEFT, MONSTER_HUD_TOP,
                    MONSTER_HUD_LEFT + frameWidth, MONSTER_HUD_TOP + frameHeight);
                const auto border = boss ? brass : iron;
                const auto hpColor = boss ? D2D1::ColorF(0xBE2434) : D2D1::ColorF(0x4C9A63);
                inRenderer.FillRectangle(frame.left, frame.top, frame.right, frame.bottom, panel);
                inRenderer.DrawRectangle(frame.left, frame.top, frame.right, frame.bottom, border, boss ? 3.0f : 2.0f);
                const float portraitSize = frameHeight - 8.0f;
                const auto portraitRect = D2D1::RectF(frame.left + 4, frame.top + 4,
                    frame.left + 4 + portraitSize, frame.bottom - 4);
                inRenderer.FillRectangle(portraitRect.left, portraitRect.top, portraitRect.right, portraitRect.bottom,
                    D2D1::ColorF(0x030405));
                if (const auto portrait = monsterPortraits.find(target->dataId); portrait != monsterPortraits.end())
                {
                    const auto& source = portrait->second.sourceRect;
                    const auto& pixels = portrait->second.sourcePixels;
                    const float scale = std::min(portraitSize / pixels.width, portraitSize / pixels.height);
                    const float imageWidth = pixels.width * scale;
                    const float imageHeight = pixels.height * scale;
                    const float x = (portraitRect.left + portraitRect.right - imageWidth) * 0.5f;
                    const float y = (portraitRect.top + portraitRect.bottom - imageHeight) * 0.5f;
                    inRenderer.DrawBitmap(portrait->second.bitmap.Get(), source,
                        D2D1::RectF(x, y, x + imageWidth, y + imageHeight));
                }
                inRenderer.DrawRectangle(portraitRect.left, portraitRect.top, portraitRect.right, portraitRect.bottom, border);
                const auto bar = D2D1::RectF(portraitRect.right + 8, frame.top + 7, frame.right - 5, frame.bottom - 7);
                const float length = bar.right - bar.left;
                inRenderer.FillRectangle(bar.left, bar.top, bar.right, bar.bottom, D2D1::ColorF(0x030405));
                inRenderer.FillRectangle(bar.left, bar.top, bar.left + length * targetHp.trailingRatio, bar.bottom, D2D1::ColorF(0x777B82));
                inRenderer.FillRectangle(bar.left, bar.top, bar.left + length * targetHp.Ratio(), bar.bottom, hpColor);
                inRenderer.DrawRectangle(bar.left, bar.top, bar.right, bar.bottom, border);
                if (boss) inRenderer.DrawRectangle(frame.left + 4, frame.top + 4, frame.right - 4, frame.bottom - 4, brass);
                float labelLeft = bar.left + 8;
                if (boss)
                {
                    const auto badge = D2D1::RectF(labelLeft, bar.top + 3, labelLeft + 48, bar.bottom - 3);
                    inRenderer.FillRectangle(badge.left, badge.top, badge.right, badge.bottom, D2D1::ColorF(0x332415));
                    inRenderer.DrawRectangle(badge.left, badge.top, badge.right, badge.bottom, brass);
                    inRenderer.DrawUiText(L"BOSS", badge, D2D1::ColorF(0xF5DCA4), 12, false, true);
                    labelLeft = badge.right + 8;
                }
                // Separate clipped name/HP/percent regions prevent long server names or u32 HP
                // values from covering one another; shadow text stays legible over every fill state.
                const auto hudText = [&](const std::wstring& text, const D2D1_RECT_F& rect)
                {
                    auto shadow = rect; shadow.left += 1; shadow.top += 1; shadow.right += 1; shadow.bottom += 1;
                    inRenderer.DrawUiText(text, shadow, D2D1::ColorF(0x000000), 14, false, true);
                    inRenderer.DrawUiText(text, rect, textColor, 14, false, true);
                };
                const std::wstring label = (level ? L"Lv." + std::to_wstring(*level) : std::wstring(L"Lv.—"))
                    + L"  " + (name.empty() ? std::wstring(L"이름 미제공") : Utf8ToWide(name));
                const auto nameRect = D2D1::RectF(labelLeft, bar.top + (bar.bottom - bar.top - 18) * 0.5f,
                    bar.right - 294, bar.bottom);
                // Name is left aligned, whereas the two numeric columns are centered.
                auto nameShadow = nameRect; nameShadow.left += 1; nameShadow.top += 1;
                nameShadow.right += 1; nameShadow.bottom += 1;
                inRenderer.DrawUiText(label, nameShadow, D2D1::ColorF(0x000000), 14);
                inRenderer.DrawUiText(label, nameRect, textColor, 14);
                const auto amount = std::to_wstring(targetHp.value) + L" / " + std::to_wstring(targetHp.maximum);
                hudText(amount, D2D1::RectF(bar.right - 282, bar.top, bar.right - 96, bar.bottom));
                hudText(percent(targetHp), D2D1::RectF(bar.right - 88, bar.top, bar.right - 6, bar.bottom));
            }
        }
        {
            const float scale = HudLayout::Scale(width);
            const D2DRenderer::UiScaleScope uiScale(inRenderer, scale);
            const auto skillBar = HudLayout::ScaleRectangle(skillUi.GetHotbarBounds(width, height), 1.0f/scale);
            const auto hud = HudLayout::Calculate(skillBar);
            const auto orb = [&](const D2D1_RECT_F& rect, const HudGauge& gauge, const wchar_t* label, const D2D1_COLOR_F& color)
            {
                const float x = (rect.left + rect.right) * 0.5f, y = (rect.top + rect.bottom) * 0.5f;
                constexpr float RADIUS = HudLayout::ORB_SIZE / 2;
                // The 96px frame has a 64px transparent aperture; at 92px reference display size,
                // keep the fill just inside its 61.3px opening so its height ratio stays accurate.
                constexpr float INNER_RADIUS = 30.0f;
                if (!hudOrbFrame)
                {
                    inRenderer.FillEllipse(x, y, RADIUS - 1, RADIUS - 1, D2D1::ColorF(0x080706));
                    inRenderer.FillEllipse(x, y, RADIUS - 3, RADIUS - 3, D2D1::ColorF(0x67583A));
                    inRenderer.FillEllipse(x, y, RADIUS - 5, RADIUS - 5, D2D1::ColorF(0x17120D));
                }
                inRenderer.FillEllipse(x, y, INNER_RADIUS, INNER_RADIUS, D2D1::ColorF(0x030405));
                if (gauge.known)
                {
                    inRenderer.PushAxisAlignedClip(D2D1::RectF(x - INNER_RADIUS, y + INNER_RADIUS - 2 * INNER_RADIUS * gauge.trailingRatio, x + INNER_RADIUS, y + INNER_RADIUS));
                    inRenderer.FillEllipse(x, y, INNER_RADIUS, INNER_RADIUS, D2D1::ColorF(0x777B82));
                    inRenderer.PopAxisAlignedClip();
                    inRenderer.PushAxisAlignedClip(D2D1::RectF(x - INNER_RADIUS, y + INNER_RADIUS - 2 * INNER_RADIUS * gauge.Ratio(), x + INNER_RADIUS, y + INNER_RADIUS));
                    inRenderer.FillEllipse(x, y, INNER_RADIUS, INNER_RADIUS, color);
                    inRenderer.FillEllipse(x - 5, y - 6, 15, 14, D2D1::ColorF(1, 1, 1, 0.12f));
                    inRenderer.PopAxisAlignedClip();
                }
                // Frame art overlays only the rim; current/trailing fill remains server-owned.
                if (hudOrbFrame)
                {
                    const auto size = hudOrbFrame->GetSize();
                    inRenderer.DrawBitmap(hudOrbFrame.Get(), D2D1::RectF(0,0,size.width,size.height), rect);
                }
                const auto value = gauge.known ? percent(gauge) : std::wstring(L"—");
                const auto valueRect = D2D1::RectF(x - 30, y - 9, x + 30, y + 9);
                auto shadow = valueRect; shadow.left += 1; shadow.top += 1; shadow.right += 1; shadow.bottom += 1;
                inRenderer.DrawUiText(value, shadow, D2D1::ColorF(0x000000), 12, false, true);
                inRenderer.DrawUiText(value, valueRect, textColor, 12, false, true);
                if (ContainsPoint(rect, uiMouseX/scale, uiMouseY/scale))
                {
                    const auto tooltip = D2D1::RectF(x-106,rect.top-42,x+106,rect.top-6);
                    const auto amount = gauge.known ? std::to_wstring(gauge.value) + L" / " + std::to_wstring(gauge.maximum)
                        : std::wstring(L"미제공");
                    inRenderer.FillRectangle(tooltip.left,tooltip.top,tooltip.right,tooltip.bottom,D2D1::ColorF(0x100D09));
                    inRenderer.DrawRectangle(tooltip.left,tooltip.top,tooltip.right,tooltip.bottom,D2D1::ColorF(0x74603D));
                    inRenderer.DrawUiText(std::wstring(label)+L"  "+amount,tooltip,textColor,12,false,true);
                }
            };
            orb(hud.hp, inDungeon ? dungeonHp : townHp, L"HP", D2D1::ColorF(0xC5111B));
            orb(hud.mp, inDungeon ? dungeonMp : townMp, L"MP", D2D1::ColorF(0x095FD1));
            const auto& menu = hud.menu;
            inRenderer.FillRectangle(menu.left, menu.top, menu.right, menu.bottom, D2D1::ColorF(0x17130D));
            inRenderer.DrawRectangle(menu.left, menu.top, menu.right, menu.bottom, D2D1::ColorF(0x080604));
            inRenderer.DrawRectangle(menu.left + 1, menu.top + 1, menu.right - 1, menu.bottom - 1, D2D1::ColorF(0x74603D));
            inRenderer.DrawLine(menu.left + 2, menu.top + 2, menu.right - 2, menu.top + 2, D2D1::ColorF(0xAE9460));
            inRenderer.DrawUiText(L"메뉴 ESC", menu, D2D1::ColorF(0xEACB7C), 12, false, true);
            const bool experienceKnown=experienceView.has_value()
                && experienceView->characterId==townClient.GetSelectedCharacterId() && townClient.IsConnected();
            const auto levelText=experienceKnown ? L"Lv."+std::to_wstring(experienceView->level):std::wstring(L"Lv.—");
            inRenderer.FillRectangle(hud.level.left,hud.level.top,hud.level.right,hud.level.bottom,D2D1::ColorF(0x17130D));
            inRenderer.DrawRectangle(hud.level.left,hud.level.top,hud.level.right,hud.level.bottom,D2D1::ColorF(0x74603D));
            inRenderer.DrawUiText(levelText,hud.level,D2D1::ColorF(0xEACB7C),14,false,true);
            const auto& exp= hud.experience;
            const double ratio=experienceKnown ? (experienceView->atMaxLevel ? 1.0
                : static_cast<double>(experienceView->experience)/static_cast<double>(experienceView->requiredExperience)):0.0;
            inRenderer.FillRectangle(exp.left,exp.top,exp.right,exp.bottom,D2D1::ColorF(0x100D09));
            inRenderer.FillRectangle(exp.left+2,exp.top+2,exp.left+2+(exp.right-exp.left-4)*static_cast<float>(std::clamp(ratio,0.0,1.0)),
                exp.bottom-2,D2D1::ColorF(0x47599F));
            inRenderer.DrawRectangle(exp.left,exp.top,exp.right,exp.bottom,D2D1::ColorF(0x74603D));
            std::wstring experienceText=L"EXP · 확인 중";
            if (experienceKnown)
            {
                if (experienceView->atMaxLevel) experienceText=L"EXP MAX";
                else
                {
                    std::wostringstream percentText;percentText.imbue(std::locale::classic());
                    percentText<<std::fixed<<std::setprecision(1)<<ratio*100.0;
                    experienceText=L"EXP "+std::to_wstring(experienceView->experience)+L" / "
                        +std::to_wstring(experienceView->requiredExperience)+L"  ("+percentText.str()+L"%)";
                }
            }
            auto experienceShadow=exp;experienceShadow.left+=1;experienceShadow.right+=1;experienceShadow.top+=1;experienceShadow.bottom+=1;
            inRenderer.DrawUiText(experienceText,experienceShadow,D2D1::ColorF(0x000000),13,false,true);
            inRenderer.DrawUiText(experienceText,exp,D2D1::ColorF(0xF0EFF8),13,false,true);
        }
        if (dungeonCleared) inRenderer.DrawUiText(L"DUNGEON CLEAR", D2D1::RectF(width * 0.5f - 150, 148, width * 0.5f + 150, 182), brass, 22, false, true);
        else if (inDungeon && player.IsCombatDead()) inRenderer.DrawUiText(L"YOU ARE DOWN", D2D1::RectF(24, 148, 300, 182), D2D1::ColorF(0xBE2434), 20);
        if (rejectedActionSeconds > 0.0f)
            inRenderer.DrawUiText(L"ACTION NOT ACCEPTED", D2D1::RectF(24, 188, 360, 218), brass, 14);
    }

    void GameWorld::UpdateDungeonCompletion(const float inDeltaSeconds, const InputState& inInput)
    {
        if (!dungeonCleared || completionStopping) return;
        if (completionPending)
        {
            completionWaitSeconds += inDeltaSeconds;
            if (townClient.IsConnected() && completionWaitSeconds < 30.0f) return;
            completionPending = false;
            completionStatus = L"응답이 지연되고 있습니다. 다시 선택해 주세요.";
        }
        if (!townClient.IsConnected())
        {
            completionStatus = L"마을 서버 연결을 기다리는 중...";
            return;
        }
        if (partySnapshot.partyId != 0 && !IsPartyLeader()) return;
        if (inInput.WasPressed(InputKey::MoveLeft) || inInput.WasPressed(InputKey::MoveUp)) selectedCompletionIndex = 0;
        if (inInput.WasPressed(InputKey::MoveRight) || inInput.WasPressed(InputKey::MoveDown)) selectedCompletionIndex = 1;
        bool confirm = inInput.WasPressed(InputKey::ConfirmSelection);
        const auto layout = CalculateDungeonCompletionLayout(camera.GetViewportWidth(), camera.GetViewportHeight());
        if (inInput.leftMousePressed)
        {
            uiClickConsumed = true;
            for (std::size_t index = 0; index < layout.buttons.size(); ++index)
                if (ContainsPoint(layout.buttons[index], inInput.clickX, inInput.clickY))
                { selectedCompletionIndex = index; confirm = true; }
        }
        if (!confirm) return;
        completionPending = true;
        completionWaitSeconds = 0.0f;
        completionStatus = L"서버 응답을 기다리는 중...";
        townClient.RequestDungeonCompletion(dungeonRoomId, selectedCompletionIndex == 1);
    }

    // Poll cleanup without joining on the game thread, then reset before replaying town events.
    void GameWorld::ProcessDungeonCompletion()
    {
        if (!completionStopping || !dungeonClient.IsStopComplete()) return;
        auto response = std::move(*completionResponse);
        ResetDungeonEntry();
        if (!response.retry) return;
        dungeonRoomId = response.roomId;
        combatSeed = response.combatSeed;
        if (dungeonClient.Start(std::move(response.sessionBrokerAddress), response.sessionBrokerPort))
            dungeonEntryState = DungeonEntryState::Connecting;
        else
        {
            ResetDungeonEntry();
            partyStatusText = L"재도전 연결에 실패했습니다.";
        }
    }

    void GameWorld::RenderDungeonCompletion(D2DRenderer& inRenderer) const
    {
        if (!dungeonCleared) return;
        const auto layout = CalculateDungeonCompletionLayout(camera.GetViewportWidth(), camera.GetViewportHeight());
        const auto& panel = layout.panel;
        const bool canChoose = !completionPending && !completionStopping
            && townClient.IsConnected() && (partySnapshot.partyId == 0 || IsPartyLeader());
        inRenderer.FillRectangle(panel.left, panel.top, panel.right, panel.bottom, D2D1::ColorF(0.10f, 0.08f, 0.055f, 0.97f));
        inRenderer.DrawRectangle(panel.left, panel.top, panel.right, panel.bottom, D2D1::ColorF(0.82f, 0.62f, 0.24f), 3.0f);
        inRenderer.DrawText(L"던전 클리어", panel.left + 28.0f, panel.top + 24.0f, panel.right - 28.0f, panel.top + 62.0f,
            D2D1::ColorF(1.0f, 0.85f, 0.25f));
        inRenderer.DrawText(L"다음 행동을 선택해 주세요.", panel.left + 28.0f, panel.top + 70.0f, panel.right - 28.0f, panel.top + 106.0f,
            D2D1::ColorF(D2D1::ColorF::White));
        for (std::size_t index = 0; index < layout.buttons.size(); ++index)
        {
            const auto& button = layout.buttons[index];
            const bool selected = canChoose && (selectedCompletionIndex == index || (pointerUiPage==SystemUiPage::DungeonCompletion && ContainsPoint(button, uiMouseX-UiWindowOffset(SystemUiPage::DungeonCompletion).x, uiMouseY-UiWindowOffset(SystemUiPage::DungeonCompletion).y)));
            inRenderer.FillRectangle(button.left, button.top, button.right, button.bottom,
                selected ? D2D1::ColorF(0.32f, 0.23f, 0.10f) : D2D1::ColorF(0.16f, 0.14f, 0.11f));
            inRenderer.DrawRectangle(button.left, button.top, button.right, button.bottom,
                selected ? D2D1::ColorF(1.0f, 0.85f, 0.25f) : D2D1::ColorF(0.45f, 0.40f, 0.30f), 2.0f);
            inRenderer.DrawText(index == 0 ? L"마을로 이동" : L"재도전", button.left + 20.0f, button.top + 12.0f,
                button.right - 12.0f, button.bottom - 8.0f,
                canChoose ? D2D1::ColorF(D2D1::ColorF::White) : D2D1::ColorF(0.55f, 0.55f, 0.55f));
        }
        const auto text = !completionStatus.empty() ? completionStatus
            : partySnapshot.partyId != 0 && !IsPartyLeader() ? L"파티장의 선택을 기다리는 중..." : L"버튼 클릭 또는 Enter로 확인";
        inRenderer.DrawText(text, panel.left + 28.0f, panel.top + 200.0f, panel.right - 28.0f, panel.bottom - 20.0f,
            D2D1::ColorF(0.85f, 0.82f, 0.72f));
    }

    void GameWorld::UpdateDungeonSelection(const InputState& inInput)
    {
        if (uiClickConsumed || dungeonEntryState!=DungeonEntryState::Idle || localPlayerId==0
            || !isDungeonSelectionOpen || dungeonOptions.empty()) return;
        selectedDungeonIndex=std::min(selectedDungeonIndex,dungeonOptions.size()-1);
        const float width=camera.GetViewportWidth();
        const float left=std::max(32.0f,width*0.10f),right=std::max(left+480,width-left);
        const float listTop=132,listRight=left+(right-left)*0.46f;
        const std::size_t first=selectedDungeonIndex<4 ? 0:selectedDungeonIndex-3;
        bool confirm=inInput.WasPressed(InputKey::ConfirmSelection);
        if (inInput.mouseWheelDelta!=0)
        {
            if (inInput.mouseWheelDelta>0 && selectedDungeonIndex>0) --selectedDungeonIndex;
            else if (inInput.mouseWheelDelta<0 && selectedDungeonIndex+1<dungeonOptions.size()) ++selectedDungeonIndex;
        }
        if (inInput.leftMousePressed)
        {
            for (std::size_t index=first;index<std::min(first+4,dungeonOptions.size());++index)
            {
                const float top=listTop+static_cast<float>(index-first)*72;
                if (ContainsPoint({left+24,top,listRight,top+62},inInput.clickX,inInput.clickY))
                {selectedDungeonIndex=index;uiClickConsumed=true;break;}
            }
            if (ContainsPoint({listRight+48,listTop+198,right-42,listTop+234},inInput.clickX,inInput.clickY))
            {confirm=true;uiClickConsumed=true;}
        }
        if (confirm)
        {
            const auto dungeonId=dungeonOptions[selectedDungeonIndex].dungeonId;
            isDungeonSelectionOpen=false;
            RequestDungeon(dungeonId);
        }
    }

    GameWorld::SystemUiPage GameWorld::UiWindowGroup(SystemUiPage inPage) const
    { return IsPartyUiPage(inPage) ? SystemUiPage::Party : inPage; }

    bool GameWorld::IsUiPageOpen(SystemUiPage inPage) const
    {
        if (inPage == SystemUiPage::Menu) return systemUiPage == inPage;
        return std::find(uiWindows.begin(),uiWindows.end(),inPage) != uiWindows.end();
    }
    bool GameWorld::IsUiTextEditing() const
    { return (systemUiPage==SystemUiPage::PartyCreate || systemUiPage==SystemUiPage::Party) && editingPartyTitle; }

    Vector2 GameWorld::UiWindowOffset(SystemUiPage inPage) const
    { return uiWindowOffsets[static_cast<std::size_t>(UiWindowGroup(inPage))]; }

    D2D1_RECT_F GameWorld::UiWindowBounds(SystemUiPage inPage) const
    {
        const float width=camera.GetViewportWidth(),height=camera.GetViewportHeight();
        const auto bar=skillUi.GetHotbarBounds(width,height);
        const auto hud=HudLayout::Calculate(bar,HudLayout::Scale(width));
        if (inPage==SystemUiPage::Skills) return skillUi.GetWindowBounds(width,height);
        if (inPage==SystemUiPage::Inventory) return inventoryUi.GetWindowBounds(width,height);
        if (inPage==SystemUiPage::Quests) return questUi.GetWindowBounds(width,height,hud.hp.top);
        D2D1_RECT_F rect{};
        switch (inPage)
        {
        case SystemUiPage::Party: case SystemUiPage::PartyDirectory: rect=CalculatePartyLayout(width,height).panel;break;
        case SystemUiPage::PartyDetails: rect=CalculatePartyDetailsLayout(width,height).panel;break;
        case SystemUiPage::PartyCreate: rect=GetPartyCreateDialog(width,height);break;
        case SystemUiPage::GameOptions: rect=GetGameOptionsPanel(width,height);break;
        case SystemUiPage::ExitConfirmation: rect=GetExitDialog(width,height);break;
        case SystemUiPage::Invitation: rect=GetInvitationDialog(width);break;
        case SystemUiPage::PartyNotice: rect=CalculatePartyNoticeLayout(width,bar.top-12).panel;break;
        case SystemUiPage::KickedNotice: rect=CalculateKickedNoticeLayout(width,height).panel;break;
        case SystemUiPage::DungeonCompletion: rect=CalculateDungeonCompletionLayout(width,height).panel;break;
        case SystemUiPage::DungeonSelection:
        {
            const float left=std::max(32.0f,width*0.10f);
            rect={left,56,std::max(left+480,width-left),std::max(486.0f,height-56)};break;
        }
        default: return rect;
        }
        const auto offset=UiWindowOffset(inPage);
        return {rect.left+offset.x,rect.top+offset.y,rect.right+offset.x,rect.bottom+offset.y};
    }
    void GameWorld::FocusUiWindow(SystemUiPage inPage)
    {
        if (systemUiPage != inPage) editingPartyTitle=false;
        std::erase(uiWindows,inPage);uiWindows.push_back(inPage);systemUiPage=inPage;
    }
    void GameWorld::CloseUiWindow(SystemUiPage inPage)
    {
        if (inPage==SystemUiPage::Menu)
        { systemUiPage=uiWindows.empty()?SystemUiPage::Closed:uiWindows.back();return; }
        if (inPage==SystemUiPage::Inventory) inventoryUi.Close();
        if (inPage==SystemUiPage::Skills) skillUi.CancelDrag();
        if (inPage==SystemUiPage::Quests) questUi.Close();
        if (IsPartyUiPage(inPage))
        {
            if (IsUiPageOpen(SystemUiPage::PartyDirectory) || IsUiPageOpen(SystemUiPage::PartyDetails))
            { townClient.UnsubscribePartyDirectory();directoryPageRequestPending=false; }
            selectedDirectoryPartyId=0;partyDetails.reset();partyDetailRequestPending=partyDetailRefreshNeeded=false;
            editingPartyTitle=false;
        }
        if (inPage==SystemUiPage::Invitation && pendingPartyInvitation)
        {
            if (!partyInvitationAnswerPending)
            { townClient.AnswerPartyInvitation(pendingPartyInvitation->invitationId,false);partyInvitationAnswerPending=true; }
            invitationHidden=true;
        }
        if (inPage==SystemUiPage::PartyNotice && !partyJoinRequests.empty())
        {
            hiddenPartyRequestId=partyJoinRequests.front().requestId;
            if (answeringPartyJoinRequestId==0)
            { answeringPartyJoinRequestId=hiddenPartyRequestId;townClient.AnswerPartyJoin(hiddenPartyRequestId,false); }
        }
        if (inPage==SystemUiPage::KickedNotice) partyKickedNotice=false;
        if (inPage==SystemUiPage::DungeonCompletion) completionWindowHidden=true;
        if (inPage==SystemUiPage::DungeonSelection)
        { isDungeonSelectionOpen=false;activeDungeonZoneId.clear();dungeonOptions.clear(); }
        const auto group=UiWindowGroup(inPage);
        std::erase_if(uiWindows,[&](auto page){return UiWindowGroup(page)==group;});
        if (draggedUiPage==inPage) draggedUiPage=SystemUiPage::Closed;
        if (systemUiPage!=SystemUiPage::Menu)
            systemUiPage=uiWindows.empty()?SystemUiPage::Closed:uiWindows.back();
    }
    void GameWorld::SetSystemUiPage(SystemUiPage page)
    {
        if (page==SystemUiPage::Closed) {CloseUiWindow(systemUiPage);return;}
        if (page==SystemUiPage::Menu) {systemUiPage=page;editingPartyTitle=false;return;}
        if (IsDungeonUiRestricted() && IsPartyUiPage(page)) return;
        if (page==SystemUiPage::Party || page==SystemUiPage::PartyDirectory)
            page=partySnapshot.partyId!=0?SystemUiPage::Party:SystemUiPage::PartyDirectory;
        if (page==SystemUiPage::PartyCreate && partySnapshot.partyId!=0) page=SystemUiPage::Party;
        if (IsUiPageOpen(page)) {FocusUiWindow(page);return;}
        if (IsPartyUiPage(page))
        {
            const bool wasDirectory=IsUiPageOpen(SystemUiPage::PartyDirectory)||IsUiPageOpen(SystemUiPage::PartyDetails);
            const bool isDirectory=page==SystemUiPage::PartyDirectory||page==SystemUiPage::PartyDetails;
            if (wasDirectory && !isDirectory) {townClient.UnsubscribePartyDirectory();directoryPageRequestPending=false;}
            if (IsUiPageOpen(SystemUiPage::PartyDetails) && page!=SystemUiPage::PartyDetails)
            {selectedDirectoryPartyId=0;partyDetails.reset();partyDetailRequestPending=partyDetailRefreshNeeded=false;}
            std::erase_if(uiWindows,[](auto value){return IsPartyUiPage(value);});
            if (page==SystemUiPage::PartyDirectory && !wasDirectory)
            {partyDirectoryPage={};wantedPartyDirectoryRevision=0;partyDirectoryPage.page=partyDirectoryPage.totalPages=1;RequestPartyDirectoryPage(1);}
            if (page==SystemUiPage::Party)
            {partyTitleDraft=Utf8ToWide(partySnapshot.title);editingPartyTitle=false;}
            if (page==SystemUiPage::PartyCreate)
            {editingPartyTitle=false;if(!partyCreationPending){newPartyTitleDraft.clear();newPartyIsPublic=false;partyStatusText.clear();}}
        }
        if (page==SystemUiPage::GameOptions)
        {
            const auto settings=GameWindow::GetScreenSettings();
            if (!screenApplyRequested)
            {selectedOptionsRow=0;screenWidthDraft=settings.width;fullscreenDraft=settings.fullscreen;screenSettingsNotice.clear();}
        }
        if (page==SystemUiPage::Skills) townClient.RequestSkillState();
        FocusUiWindow(page);
    }
    void GameWorld::SyncUiWindows()
    {
        const auto sync=[this](SystemUiPage page,bool show)
        {
            if (show && !IsUiPageOpen(page))
            {
                const bool menuOpen=systemUiPage==SystemUiPage::Menu;
                FocusUiWindow(page);
                if (menuOpen) systemUiPage=SystemUiPage::Menu;
            }
            else if (!show) std::erase(uiWindows,page);
        };
        if (!pendingPartyInvitation) invitationHidden=false;
        sync(SystemUiPage::Invitation,pendingPartyInvitation.has_value()&&!invitationHidden);
        sync(SystemUiPage::PartyNotice,IsPartyJoinNoticeVisible()
            && partyJoinRequests.front().requestId!=hiddenPartyRequestId);
        sync(SystemUiPage::KickedNotice,partyKickedNotice);
        sync(SystemUiPage::DungeonSelection,isDungeonSelectionOpen&&!dungeonOptions.empty());
        sync(SystemUiPage::DungeonCompletion,dungeonCleared&&!completionStopping&&!completionWindowHidden);
        if (IsDungeonUiRestricted())
        {
            const auto party=std::find_if(uiWindows.begin(),uiWindows.end(),[](auto page){return IsPartyUiPage(page);});
            if (party!=uiWindows.end()) CloseUiWindow(*party);
        }
        if (!IsUiPageOpen(draggedUiPage)) draggedUiPage=SystemUiPage::Closed;
        if (systemUiPage!=SystemUiPage::Menu && !IsUiPageOpen(systemUiPage))
            systemUiPage=uiWindows.empty()?SystemUiPage::Closed:uiWindows.back();
    }
    GameWorld::SystemUiPage GameWorld::TopUiWindowAt(float inX,float inY) const
    {
        if (systemUiPage==SystemUiPage::Menu) return SystemUiPage::Menu;
        for (auto it=uiWindows.rbegin();it!=uiWindows.rend();++it)
            if (ContainsPoint(UiWindowBounds(*it),inX,inY)) return *it;
        return SystemUiPage::Closed;
    }
    void GameWorld::UpdateUiWindowOffsets()
    {
        const float scale=HudLayout::Scale(camera.GetViewportWidth());
        const auto apply=[&]()
        {
            auto offset=UiWindowOffset(SystemUiPage::Skills);skillUi.SetWindowOffset(offset.x/scale,offset.y/scale);
            offset=UiWindowOffset(SystemUiPage::Inventory);inventoryUi.SetWindowOffset(offset.x/scale,offset.y/scale);
            offset=UiWindowOffset(SystemUiPage::Quests);questUi.SetWindowOffset(offset.x/scale,offset.y/scale);
        };
        apply();
        for (auto page:uiWindows)
        {
            const auto rect=UiWindowBounds(page);
            auto& offset=uiWindowOffsets[static_cast<std::size_t>(UiWindowGroup(page))];
            offset.x+=std::clamp(rect.left,0.0f,std::max(0.0f,camera.GetViewportWidth()-(rect.right-rect.left)))-rect.left;
            offset.y+=std::clamp(rect.top,0.0f,std::max(0.0f,camera.GetViewportHeight()-(rect.bottom-rect.top)))-rect.top;
        }
        apply();
    }
    // Route pointer input to one visible window or its captured drag. State/timeouts still update in every model.
    InputState GameWorld::UiWindowInput(SystemUiPage page,const InputState& raw,bool inHud) const
    {
        InputState input=raw;
        const bool ownsDrag=(page==SystemUiPage::Inventory&&inventoryUi.IsDragging())
            ||(page==SystemUiPage::Skills&&skillUi.IsDragging())||(page==SystemUiPage::Quests&&questUi.IsTrackerDragging());
        if (uiChromeConsumed || (!ownsDrag && pointerUiPage!=page && !(inHud&&pointerUiPage==SystemUiPage::Closed)))
        {
            input.mouseX=input.mouseY=input.clickX=input.clickY=input.rightClickX=input.rightClickY=-10000;
            input.leftMousePressed=input.leftMouseDown=input.leftMouseReleased=input.rightMousePressed=false;input.mouseWheelDelta=0;
        }
        if (systemUiPage!=page || uiChromeConsumed)
        {
            std::erase_if(input.pressedKeys,[&](auto key){return !(inHud&&page==SystemUiPage::Inventory
                && key>=InputKey::Item1&&key<=InputKey::Item6&&!IsUiTextEditing());});
            input.textInput.clear();input.textEdits.clear();input.composition.clear();
        }
        // Movement keys always belong to the character; windows use pointer controls.
        std::erase_if(input.pressedKeys,[](auto key){return key>=InputKey::MoveLeft&&key<=InputKey::MoveDown;});
        input.moveLeft=input.moveRight=input.moveUp=input.moveDown=false;
        if (page!=SystemUiPage::Skills&&page!=SystemUiPage::Inventory&&page!=SystemUiPage::Quests)
        {
            const auto offset=UiWindowOffset(page);
            input.mouseX-=offset.x;input.clickX-=offset.x;input.rightClickX-=offset.x;
            input.mouseY-=offset.y;input.clickY-=offset.y;input.rightClickY-=offset.y;
        }
        return input;
    }
    void GameWorld::UpdateUiWindows(const InputState& input)
    {
        uiChromeConsumed=false;
        if (!chatUi.IsEditing()&&!IsUiTextEditing())
        {
            const std::pair<InputKey,SystemUiPage> keys[]{
                {InputKey::ToggleInventory,SystemUiPage::Inventory},{InputKey::ToggleSkills,SystemUiPage::Skills},
                {InputKey::ToggleQuests,SystemUiPage::Quests},{InputKey::ToggleParty,SystemUiPage::Party},
                {InputKey::ToggleOptions,SystemUiPage::GameOptions}};
            for (const auto& [key,page]:keys) if(input.WasPressed(key))
            {
                const auto found=std::find_if(uiWindows.begin(),uiWindows.end(),[&](auto open){return UiWindowGroup(open)==UiWindowGroup(page);});
                if(found!=uiWindows.end()) CloseUiWindow(*found);
                else {if(page==SystemUiPage::Quests)questUi.OpenAll();SetSystemUiPage(page);}
            }
        }
        if(input.WasPressed(InputKey::ToggleSystemMenu))
        {
            if(systemUiPage==SystemUiPage::Menu)CloseUiWindow(SystemUiPage::Menu);
            else if(skillUi.IsDragging()||inventoryUi.IsDragging()){skillUi.CancelDrag();inventoryUi.Close();}
            else if(!uiWindows.empty())CloseUiWindow(uiWindows.back());
            else SetSystemUiPage(SystemUiPage::Menu);
            draggedUiPage=SystemUiPage::Closed;uiChromeConsumed=true;
        }
        if(input.cancelDrag)draggedUiPage=SystemUiPage::Closed;
        if(draggedUiPage!=SystemUiPage::Closed)
        {
            if(input.leftMouseDown)
            {
                auto& offset=uiWindowOffsets[static_cast<std::size_t>(UiWindowGroup(draggedUiPage))];
                offset={uiDragOffset.x+input.mouseX-uiDragMouse.x,uiDragOffset.y+input.mouseY-uiDragMouse.y};
            }
            else draggedUiPage=SystemUiPage::Closed;
            uiChromeConsumed=true;
        }
        UpdateUiWindowOffsets();
        pointerUiPage=TopUiWindowAt(input.mouseX,input.mouseY);
        const auto bar=skillUi.GetHotbarBounds(camera.GetViewportWidth(),camera.GetViewportHeight());
        const auto hud=HudLayout::Calculate(bar,HudLayout::Scale(camera.GetViewportWidth()));
        if (pointerUiPage==SystemUiPage::Closed && !dungeonCleared && !completionStopping
            && questUi.IsTrackerAt(input.mouseX,input.mouseY,camera.GetViewportWidth(),camera.GetViewportHeight(),hud.hp.top))
            pointerUiPage=SystemUiPage::Quests;
        if(skillUi.IsDragging())pointerUiPage=SystemUiPage::Skills;
        else if(inventoryUi.IsDragging())pointerUiPage=SystemUiPage::Inventory;
        else if(questUi.IsTrackerDragging())pointerUiPage=SystemUiPage::Quests;
        if(input.leftMousePressed&&!uiChromeConsumed&&!questUi.IsTrackerDragging()&&!skillUi.IsDragging()&&!inventoryUi.IsDragging()&&!chatUi.IsDragging())
        {
            const auto page=TopUiWindowAt(input.clickX,input.clickY);
            if (page==SystemUiPage::Closed) editingPartyTitle=false;
            if(page!=SystemUiPage::Closed&&page!=SystemUiPage::Menu)
            {
                FocusUiWindow(page);pointerUiPage=page;
                const auto rect=UiWindowBounds(page);
                if(ContainsPoint({rect.right-30,rect.top+4,rect.right-5,rect.top+29},input.clickX,input.clickY))
                {CloseUiWindow(page);uiChromeConsumed=true;}
                else if(input.clickY<rect.top+32)
                {
                    editingPartyTitle=false;
                    draggedUiPage=page;uiDragMouse={input.mouseX,input.mouseY};uiDragOffset=UiWindowOffset(page);
                    uiChromeConsumed=true;
                }
            }
        }
        const D2D1_RECT_F restore{camera.GetViewportWidth()*0.5f-130,24,camera.GetViewportWidth()*0.5f+130,58};
        if(systemUiPage!=SystemUiPage::Menu&&dungeonCleared&&completionWindowHidden&&input.leftMousePressed&&ContainsPoint(restore,input.clickX,input.clickY))
        {completionWindowHidden=false;SyncUiWindows();uiChromeConsumed=true;}
        UpdateUiWindowOffsets();
        if(uiChromeConsumed)uiClickConsumed=true;
    }
    void GameWorld::RenderUiWindowChrome(D2DRenderer& draw,SystemUiPage page) const
    {
        const auto rect=UiWindowBounds(page);
        const D2D1_RECT_F close{rect.right-30,rect.top+4,rect.right-5,rect.top+29};
        draw.DrawRectangle(rect.left,rect.top,rect.right,rect.bottom,
            D2D1::ColorF(systemUiPage==page?0xBDA46C:0x535F70));
        draw.FillRectangle(close.left,close.top,close.right,close.bottom,D2D1::ColorF(0x29313D));
        draw.DrawUiText(L"X",close,D2D1::ColorF(0xF0EAE0),15,false,true);
    }
    void GameWorld::RenderUiWindows(D2DRenderer& draw) const
    {
        const float width=camera.GetViewportWidth(),height=camera.GetViewportHeight();
        const auto bar=skillUi.GetHotbarBounds(width,height);
        const auto hud=HudLayout::Calculate(bar,HudLayout::Scale(width));
        for(auto page:uiWindows)
        {
            if(page==SystemUiPage::Skills)skillUi.Render(draw,width,height,true,false);
            else if(page==SystemUiPage::Inventory)inventoryUi.Render(draw,width,height,IsDungeonUiRestricted());
            else if(page==SystemUiPage::Quests)questUi.Render(draw,width,height,hud.hp.top);
            else
            {
                const auto offset=UiWindowOffset(page);
                const D2DRenderer::UiTranslationScope scope(draw,offset.x,offset.y);
                if(IsPartyUiPage(page)||page==SystemUiPage::Invitation)RenderPartyInterface(draw,page);
                else if(page==SystemUiPage::PartyNotice||page==SystemUiPage::KickedNotice)RenderPartyNotifications(draw,page);
                else if(page==SystemUiPage::DungeonSelection)RenderDungeonSelection(draw);
                else if(page==SystemUiPage::DungeonCompletion)RenderDungeonCompletion(draw);
                else RenderSystemInterface(draw,page);
            }
            RenderUiWindowChrome(draw,page);
        }
        if(dungeonCleared&&completionWindowHidden)
            draw.DrawUiText(L"클리어 결과 보기",{width*0.5f-130,24,width*0.5f+130,58},D2D1::ColorF(0xEACB7C),16,false,true);
        if(systemUiPage==SystemUiPage::Menu)RenderSystemInterface(draw,SystemUiPage::Menu);
    }

    void GameWorld::RequestSelectedPartyDetail()
    {
        if (IsDungeonUiRestricted() || !townClient.IsConnected() || selectedDirectoryPartyId == 0) return;
        if (partyDetailRequestPending)
        {
            partyDetailRefreshNeeded = true;
            return;
        }
        partyDetailRequestPending = true;
        townClient.RequestPartyDetail(selectedDirectoryPartyId);
    }

    bool GameWorld::CanRequestPartyJoin() const noexcept
    {
        return !IsDungeonUiRestricted() && townClient.IsConnected() && partySnapshot.partyId == 0
            && !partyJoinSendPending && !ownPartyJoinRequest && !partyDetailRequestPending
            && partyDetails && partyDetails->partyId == selectedDirectoryPartyId
            && partyDetails->result == TownProtocol::PartyResultCode::Succeeded
            && partyDetails->isPublic && !partyDetails->busy && partyDetails->members.size() < 8;
    }

    void GameWorld::ResetPartyRequestUi()
    {
        partyJoinRequests.clear();
        ownPartyJoinRequest.reset();
        answeringPartyJoinRequestId = 0;
        partyJoinSendPending = false;
        selectedDirectoryPartyId = 0;
        partyDetails.reset();
        partyDetailRequestPending = false;
        partyDetailRefreshNeeded = false;
    }

    bool GameWorld::IsPartyJoinNoticeVisible() const noexcept
    {
        return townClient.IsConnected() && !IsDungeonUiRestricted() && IsPartyLeader()
            && !partyJoinRequests.empty() && !pendingPartyInvitation && !partyKickedNotice;
    }

    void GameWorld::UpdatePartyNotifications(const InputState& inInput)
    {
        if (systemUiPage==SystemUiPage::KickedNotice && partyKickedNotice)
        {
            const auto layout = CalculateKickedNoticeLayout(camera.GetViewportWidth(), camera.GetViewportHeight());
            if (inInput.WasPressed(InputKey::ConfirmSelection) || inInput.WasPressed(InputKey::ToggleSystemMenu)
                || (inInput.leftMousePressed && ContainsPoint(layout.accept, inInput.clickX, inInput.clickY)))
                partyKickedNotice = false;
            uiClickConsumed = true;
            skillUi.CancelDrag();
            return;
        }
        if (systemUiPage!=SystemUiPage::PartyNotice || !IsPartyJoinNoticeVisible() || !inInput.leftMousePressed || uiClickConsumed) return;
        const auto layout = CalculatePartyNoticeLayout(camera.GetViewportWidth(),
            skillUi.GetHotbarBounds(camera.GetViewportWidth(), camera.GetViewportHeight()).top - 12.0f);
        if (!ContainsPoint(layout.panel, inInput.clickX, inInput.clickY)) return;
        uiClickConsumed = true;
        skillUi.CancelDrag();
        if (answeringPartyJoinRequestId != 0) return;
        const bool accepted = ContainsPoint(layout.accept, inInput.clickX, inInput.clickY);
        if (!accepted && !ContainsPoint(layout.decline, inInput.clickX, inInput.clickY)) return;
        answeringPartyJoinRequestId = partyJoinRequests.front().requestId;
        townClient.AnswerPartyJoin(answeringPartyJoinRequestId, accepted);
        // Keep this ID until its authoritative Accepted/Rejected/Invalidated update arrives.
    }

    void GameWorld::RenderPartyNotifications(D2DRenderer& inRenderer,const SystemUiPage inPage) const
    {
        const auto drawPanel = [&inRenderer](const D2D1_RECT_F& inPanel)
        {
            inRenderer.FillRectangle(inPanel.left, inPanel.top, inPanel.right, inPanel.bottom,
                D2D1::ColorF(0.04f, 0.06f, 0.09f, 0.98f));
            inRenderer.DrawRectangle(inPanel.left, inPanel.top, inPanel.right, inPanel.bottom,
                D2D1::ColorF(0.55f, 0.73f, 0.94f), 2.0f);
        };
        const auto drawButton = [&inRenderer](const D2D1_RECT_F& inButton, std::wstring_view inText, bool inEnabled)
        {
            inRenderer.FillRectangle(inButton.left, inButton.top, inButton.right, inButton.bottom,
                inEnabled ? D2D1::ColorF(0.17f, 0.31f, 0.46f) : D2D1::ColorF(0.18f, 0.18f, 0.18f));
            inRenderer.DrawUiText(inText, inButton, D2D1::ColorF(0.93f, 0.95f, 1.0f), 16.0f, false, true);
        };
        if (inPage==SystemUiPage::KickedNotice && partyKickedNotice)
        {
            const auto layout = CalculateKickedNoticeLayout(camera.GetViewportWidth(), camera.GetViewportHeight());
            drawPanel(layout.panel);
            inRenderer.DrawUiText(L"파티에서 강퇴되었습니다.",
                {layout.panel.left + 16.0f, layout.panel.top + 24.0f, layout.panel.right - 16.0f, layout.panel.top + 84.0f},
                D2D1::ColorF(0.96f, 0.94f, 0.89f), 18.0f, true, true);
            drawButton(layout.accept, L"확인", true);
        }
        else if (inPage==SystemUiPage::PartyNotice && IsPartyJoinNoticeVisible())
        {
            const auto layout = CalculatePartyNoticeLayout(camera.GetViewportWidth(),
                skillUi.GetHotbarBounds(camera.GetViewportWidth(), camera.GetViewportHeight()).top - 12.0f);
            drawPanel(layout.panel);
            inRenderer.DrawUiText(L"파티 가입 요청 · " + std::to_wstring(partyJoinRequests.size()) + L"건",
                {layout.panel.left + 12.0f, layout.panel.top + 10.0f, layout.panel.right - 12.0f, layout.panel.top + 34.0f},
                D2D1::ColorF(0.92f, 0.94f, 1.0f), 16.0f);
            inRenderer.DrawUiText(Utf8ToWide(partyJoinRequests.front().requesterName) + L" 님의 가입 요청",
                {layout.panel.left + 12.0f, layout.panel.top + 40.0f, layout.panel.right - 12.0f, layout.panel.bottom - 52.0f},
                D2D1::ColorF(0.94f, 0.91f, 0.80f), 16.0f, true);
            const bool enabled = answeringPartyJoinRequestId == 0;
            drawButton(layout.accept, enabled ? L"승인" : L"처리 중", enabled);
            drawButton(layout.decline, L"거절", enabled);
        }
    }

    void GameWorld::RenderPartyDetails(D2DRenderer& inRenderer) const
    {
        const auto layout = CalculatePartyDetailsLayout(camera.GetViewportWidth(), camera.GetViewportHeight());
        const auto& panel = layout.panel;
        inRenderer.FillRectangle(panel.left, panel.top, panel.right, panel.bottom,
            D2D1::ColorF(0.035f, 0.045f, 0.065f, 0.98f));
        inRenderer.DrawRectangle(panel.left, panel.top, panel.right, panel.bottom,
            D2D1::ColorF(0.45f, 0.68f, 0.88f), 2.0f);
        const bool valid = partyDetails && partyDetails->result == TownProtocol::PartyResultCode::Succeeded;
        inRenderer.DrawUiText(valid ? Utf8ToWide(partyDetails->title) : L"파티 상세 정보",
            {panel.left + 16.0f, panel.top + 10.0f, panel.right - 16.0f, panel.top + 38.0f},
            D2D1::ColorF(0.95f, 0.94f, 0.85f), 18.0f);
        if (valid)
        {
            inRenderer.DrawUiText(L"파티원 " + std::to_wstring(partyDetails->members.size()) + L"/8 · 공개 파티",
                {panel.left + 16.0f, panel.top + 40.0f, panel.right - 16.0f, panel.top + 64.0f},
                D2D1::ColorF(0.82f, 0.86f, 0.94f), 16.0f);
            const float rowHeight = std::min(36.0f, (panel.bottom - panel.top - 154.0f) / 8.0f);
            for (std::size_t index = 0; index < partyDetails->members.size(); ++index)
            {
                const auto& member = partyDetails->members[index];
                const float top = panel.top + 70.0f + static_cast<float>(index) * rowHeight;
                const bool leader = member.playerId == partyDetails->leaderPlayerId;
                const auto text = std::wstring(leader ? L"[파티장] " : L"[파티원] ")
                    + Utf8ToWide(member.playerName) + L" · 슬롯 " + std::to_wstring(member.slot + 1);
                inRenderer.DrawUiText(text, {panel.left + 16.0f, top, panel.right - 16.0f, top + rowHeight},
                    leader ? D2D1::ColorF(0.96f, 0.85f, 0.48f) : D2D1::ColorF(0.84f, 0.89f, 0.96f), 16.0f);
            }
        }
        std::wstring status = partyStatusText;
        if (partyDetailRequestPending) status = L"파티 정보를 확인하는 중입니다.";
        else if (ownPartyJoinRequest || partyJoinSendPending) status = L"파티장의 응답을 기다리는 중입니다.";
        else if (valid && partyDetails->busy) status = L"던전 입장 중인 파티에는 가입할 수 없습니다.";
        else if (valid && partyDetails->members.size() >= 8) status = L"파티 인원이 가득 찼습니다.";
        inRenderer.DrawUiText(status, {panel.left + 16.0f, panel.bottom - 80.0f, panel.right - 16.0f, panel.bottom - 46.0f},
            D2D1::ColorF(0.95f, 0.80f, 0.43f), 14.0f, true);
        const bool canJoin = CanRequestPartyJoin();
        inRenderer.FillRectangle(layout.accept.left, layout.accept.top, layout.accept.right, layout.accept.bottom,
            canJoin ? D2D1::ColorF(0.16f, 0.34f, 0.50f) : D2D1::ColorF(0.16f, 0.16f, 0.16f));
        inRenderer.FillRectangle(layout.decline.left, layout.decline.top, layout.decline.right, layout.decline.bottom,
            D2D1::ColorF(0.19f, 0.23f, 0.29f));
        inRenderer.DrawUiText(L"가입 요청", layout.accept, D2D1::ColorF(0.93f, 0.95f, 1.0f), 16.0f, false, true);
        inRenderer.DrawUiText(L"목록으로", layout.decline, D2D1::ColorF(0.93f, 0.95f, 1.0f), 16.0f, false, true);
    }

    void GameWorld::RequestPartyDirectoryPage(const std::uint32_t inPage)
    {
        if (IsDungeonUiRestricted()) return;
        if (directoryPageRequestPending)
        {
            return;
        }
        directoryPageRequestPending = true;
        townClient.RequestPartyDirectoryPage(inPage);
    }

    void GameWorld::SubmitPartyTitle()
    {
        if (IsDungeonUiRestricted()) return;
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
        if (IsDungeonUiRestricted()) return;
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

    void GameWorld::UpdateGameOptions(const InputState& inInput)
    {
        if (screenApplyRequested && !GameWindow::IsScreenSettingsPending())
        {
            const auto settings = GameWindow::GetScreenSettings();
            screenSettingsNotice = settings.width == screenWidthDraft && settings.fullscreen == fullscreenDraft
                ? L"화면 설정이 적용되었습니다." : L"화면 설정을 적용하지 못했습니다.";
            screenApplyRequested = false;
        }
        if (screenApplyRequested) return;
        if (inInput.WasPressed(InputKey::MoveUp)) selectedOptionsRow = (selectedOptionsRow + 4) % 5;
        if (inInput.WasPressed(InputKey::MoveDown)) selectedOptionsRow = (selectedOptionsRow + 1) % 5;
        const auto adjustVolume = [this](std::size_t index, float delta)
        {
            const auto channel = static_cast<AudioChannel>(index);
            const float volume = std::round(std::clamp(audio.GetVolume(channel) + delta, 0.0f, 1.0f) * 20.0f) / 20.0f;
            (void)audio.SetVolume(channel, volume);
        };
        const bool left = inInput.WasPressed(InputKey::MoveLeft), right = inInput.WasPressed(InputKey::MoveRight);
        if (left || right)
        {
            screenSettingsNotice.clear();
            if (selectedOptionsRow == 0) screenWidthDraft = right ? 1920U : 1280U;
            else if (selectedOptionsRow == 1)
            {
                if (right || GameWindow::CanUseWindowed(screenWidthDraft, screenWidthDraft == 1280 ? 720U : 1080U))
                    fullscreenDraft = right;
            }
            else adjustVolume(selectedOptionsRow - 2, right ? 0.05f : -0.05f);
        }
        bool apply = inInput.WasPressed(InputKey::ConfirmSelection);
        const auto panel = GetGameOptionsPanel(camera.GetViewportWidth(), camera.GetViewportHeight());
        if (inInput.leftMousePressed)
        {
            for (std::size_t index = 0; index < 6; ++index)
            {
                if (!ContainsPoint(GetGameOptionsButton(panel, index), inInput.clickX, inInput.clickY)) continue;
                uiClickConsumed = true;
                screenSettingsNotice.clear();
                if (index < 2) { selectedOptionsRow = 0; screenWidthDraft = index == 0 ? 1280U : 1920U; }
                else if (index < 4)
                {
                    selectedOptionsRow = 1;
                    if (index == 3 || GameWindow::CanUseWindowed(screenWidthDraft, screenWidthDraft == 1280 ? 720U : 1080U))
                        fullscreenDraft = index == 3;
                }
                else if (index == 4) apply = true;
                else { CloseUiWindow(SystemUiPage::GameOptions); return; }
                break;
            }
            for (std::size_t index = 0; index < 6; ++index)
                if (ContainsPoint(GetVolumeButton(panel, index), inInput.clickX, inInput.clickY))
                {
                    uiClickConsumed = true;
                    selectedOptionsRow = index / 2 + 2;
                    adjustVolume(index / 2, index % 2 ? 0.05f : -0.05f);
                    break;
                }
        }
        if (apply)
        {
            const ScreenSettings settings{screenWidthDraft, screenWidthDraft == 1280 ? 720U : 1080U, fullscreenDraft};
            screenApplyRequested = GameWindow::RequestScreenSettings(settings);
            screenSettingsNotice = screenApplyRequested ? L"화면 설정을 적용하고 있습니다."
                : L"선택한 창 크기가 작업 영역보다 큽니다.";
        }
    }

    void GameWorld::RenderGameOptions(D2DRenderer& inRenderer) const
    {
        const float width = camera.GetViewportWidth(), height = camera.GetViewportHeight();
        const auto panel = GetGameOptionsPanel(width, height);
        const auto text = D2D1::ColorF(0xE9E4D8), brass = D2D1::ColorF(0xA98948), muted = D2D1::ColorF(0x707885);
        inRenderer.FillRectangle(panel.left, panel.top, panel.right, panel.bottom, D2D1::ColorF(0x141922));
        inRenderer.DrawRectangle(panel.left, panel.top, panel.right, panel.bottom, brass, 2);
        inRenderer.DrawUiText(L"게임 옵션", D2D1::RectF(panel.left + 20, panel.top + 12, panel.right - 20, panel.top + 38), text, 20);
        inRenderer.DrawUiText(L"버튼으로 설정 · 볼륨 5% 단위 · Enter 화면 적용", D2D1::RectF(panel.left + 20, panel.top + 40, panel.right - 20, panel.top + 56), text, 11);
        constexpr std::array<std::wstring_view, 5> ROW_LABELS{L"해상도", L"화면 모드", L"마스터 볼륨", L"효과음", L"배경음악"};
        for (std::size_t row = 0; row < ROW_LABELS.size(); ++row)
        {
            const float top = panel.top + (row < 2 ? 62.0f + row * 36.0f : 150.0f + (row - 2) * 38.0f);
            const auto label = D2D1::RectF(panel.left + 20, top, panel.left + 132, top + 28);
            if (selectedOptionsRow == row) inRenderer.DrawRectangle(label.left, label.top, label.right, label.bottom, brass);
            inRenderer.DrawUiText(ROW_LABELS[row], label, text, 14, false, true);
        }
        const bool fits = GameWindow::CanUseWindowed(screenWidthDraft, screenWidthDraft == 1280 ? 720U : 1080U);
        constexpr std::array<std::wstring_view, 6> LABELS{L"1280 × 720", L"1920 × 1080", L"창모드", L"전체화면", L"화면 적용", L"닫기 · ESC"};
        for (std::size_t index = 0; index < LABELS.size(); ++index)
        {
            const auto rect = GetGameOptionsButton(panel, index);
            const bool selected = index < 2 ? screenWidthDraft == (index == 0 ? 1280U : 1920U)
                : index < 4 && fullscreenDraft == (index == 3);
            const bool enabled = !screenApplyRequested && (index != 2 || fits) && (index != 4 || fullscreenDraft || fits);
            inRenderer.FillRectangle(rect.left, rect.top, rect.right, rect.bottom, D2D1::ColorF(enabled ? (selected ? 0x384657 : 0x242C38) : 0x1B2028));
            inRenderer.DrawRectangle(rect.left, rect.top, rect.right, rect.bottom, selected ? brass : D2D1::ColorF(0x45515F));
            inRenderer.DrawUiText(LABELS[index], rect, enabled ? text : muted, 13, false, true);
        }
        for (std::size_t index = 0; index < 6; ++index)
        {
            const auto rect = GetVolumeButton(panel, index);
            inRenderer.FillRectangle(rect.left, rect.top, rect.right, rect.bottom, D2D1::ColorF(0x242C38));
            inRenderer.DrawRectangle(rect.left, rect.top, rect.right, rect.bottom, D2D1::ColorF(0x45515F));
            inRenderer.DrawUiText(index % 2 ? L"+" : L"−", rect, audio.IsAvailable() ? text : muted, 16, false, true);
            if (index % 2 == 0)
            {
                const auto volume = std::to_wstring(static_cast<int>(std::lround(audio.GetVolume(static_cast<AudioChannel>(index / 2)) * 100))) + L"%";
                inRenderer.DrawUiText(volume, D2D1::RectF(rect.right + 10, rect.top, rect.right + 78, rect.bottom), text, 14, false, true);
            }
        }
        const auto current = GameWindow::GetScreenSettings();
        const auto currentText = L"현재: " + std::to_wstring(current.width) + L" × " + std::to_wstring(current.height)
            + (current.fullscreen ? L" · 전체화면" : L" · 창모드");
        inRenderer.DrawUiText(screenSettingsNotice.empty() ? currentText : screenSettingsNotice,
            D2D1::RectF(panel.left + 20, panel.top + 262, panel.right - 20, panel.top + 280), text, 11);
        const auto audioNotice = !audio.GetNotice().empty() ? audio.GetNotice()
            : !audio.HasSounds() ? std::wstring_view(L"음원이 아직 등록되지 않았습니다.") : std::wstring_view(L"볼륨 변경은 바로 적용됩니다.");
        inRenderer.DrawUiText(audioNotice, D2D1::RectF(panel.left + 20, panel.top + 282, panel.right - 20, panel.top + 300), text, 11);
    }

    void GameWorld::UpdateSystemInterface(const InputState& inInput)
    {
        if (uiClickConsumed) return;
        if (systemUiPage == SystemUiPage::GameOptions)
        {
            UpdateGameOptions(inInput);
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
                CloseUiWindow(SystemUiPage::ExitConfirmation);
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
            const auto action=systemMenuEntries[index].action;
            if (action==SystemMenuAction::Party && IsDungeonUiRestricted())
            { uiClickConsumed=true; return; }
            if (action == SystemMenuAction::Logout || action == SystemMenuAction::SwitchAccount
                || action == SystemMenuAction::SelectTown)
            {
                pendingSessionAction = action == SystemMenuAction::Logout ? SessionMenuAction::Logout
                    : action == SystemMenuAction::SwitchAccount ? SessionMenuAction::SwitchAccount
                    : SessionMenuAction::SelectTown;
                SetSystemUiPage(SystemUiPage::Closed);
                uiClickConsumed = true;
                return;
            }
            if (action == SystemMenuAction::Quests) questUi.OpenAll();
            SetSystemUiPage(action==SystemMenuAction::Party ? SystemUiPage::Party
                : action==SystemMenuAction::Skills ? SystemUiPage::Skills
                : action==SystemMenuAction::Inventory ? SystemUiPage::Inventory
                : action==SystemMenuAction::Quests ? SystemUiPage::Quests
                : action==SystemMenuAction::GameOptions ? SystemUiPage::GameOptions:SystemUiPage::ExitConfirmation);
            uiClickConsumed = true;
            return;
        }
    }

    void GameWorld::UpdatePartyInterface(const InputState& inInput)
    {
        if (IsDungeonUiRestricted() || !townClient.IsConnected() ) return;
        if (systemUiPage==SystemUiPage::Invitation && pendingPartyInvitation.has_value())
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

        if (systemUiPage == SystemUiPage::PartyDetails)
        {
            if (!inInput.leftMousePressed || uiClickConsumed) return;
            const auto layout = CalculatePartyDetailsLayout(camera.GetViewportWidth(), camera.GetViewportHeight());
            if (ContainsPoint(layout.decline, inInput.clickX, inInput.clickY))
            {
                SetSystemUiPage(SystemUiPage::PartyDirectory);
                uiClickConsumed = true;
            }
            else if (ContainsPoint(layout.accept, inInput.clickX, inInput.clickY))
            {
                uiClickConsumed = true;
                if (CanRequestPartyJoin())
                {
                    partyJoinSendPending = true;
                    partyStatusText = L"가입 요청을 보내는 중입니다.";
                    townClient.RequestPartyJoin(selectedDirectoryPartyId);
                }
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
            else if (!directoryPageRequestPending)
            {
                for (std::size_t index = 0; index < partyDirectoryPage.parties.size(); ++index)
                {
                    const float rowTop = layout.panel.top + 92.0f + static_cast<float>(index) * 48.0f;
                    if (!ContainsPoint({layout.panel.left + 22.0f, rowTop, layout.panel.right - 22.0f, rowTop + 44.0f},
                        inInput.clickX, inInput.clickY)) continue;
                    selectedDirectoryPartyId = partyDirectoryPage.parties[index].partyId;
                    partyDetails.reset();
                    partyDetailRequestPending = false;
                    partyDetailRefreshNeeded = false;
                    partyStatusText.clear();
                    SetSystemUiPage(SystemUiPage::PartyDetails);
                    RequestSelectedPartyDetail();
                    uiClickConsumed = true;
                    break;
                }
            }
            return;
        }

        if (systemUiPage == SystemUiPage::PartyCreate)
        {
            const auto titleDialog=GetPartyCreateDialog(camera.GetViewportWidth(),camera.GetViewportHeight());
            if (inInput.leftMousePressed)
                editingPartyTitle=ContainsPoint({titleDialog.left+24,titleDialog.top+88,titleDialog.right-24,titleDialog.top+128},
                    inInput.clickX,inInput.clickY);
            if (editingPartyTitle && !partyCreationPending) AppendPartyTitleInput(newPartyTitleDraft, inInput.textInput);
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
        if (!ContainsPoint(layout.titleField,inInput.clickX,inInput.clickY)) editingPartyTitle=false;
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
        chatUi.ClearBubbles();
        if (dungeonEntryState != DungeonEntryState::Idle) ResetDungeonEntry();
        lastTownMap = inMap;
        if (assetCatalog.HasAudio("TownBgm")) (void)audio.PlayMusic(assetCatalog.GetAudioPath("TownBgm"));
        remotePlayers.clear();
        gameplayMap.Configure(inMap);
        mapBackground.Configure(inMap);
        transitionZones = inMap.transitionZones;
        player.ConfigureMovementSpeeds(inMap.walkSpeed, inMap.runSpeed);
        player.SetGroundPosition(inPosition);
        player.ResetActionState();
        skillUi.CancelDrag();
        commandQueue.Clear();
        projectileSystem.Clear();
        {
            std::scoped_lock lock(playerHitMutex);
            pendingPlayerHits.clear();
        }
        movementSendAccumulator = 0.0f;
        townMovementSends.clear();
        townMovementStateSequence = 0;
        lastTownAcknowledgedSequence = lastTownMovementTick = 0;
        townSnapshotDelaySeconds = 0.0f;
        hasTownMovementTick = hasTownDelaySample = false;
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
        chatUi.ClearBubbles();
        const auto& room = dungeonWorld->GetMap(inMapId);
        // Spawn metadata survives boss death, so the room keeps its boss BGM until departure.
        const bool bossRoom = std::any_of(room.monsters.begin(), room.monsters.end(),
            [](const auto& monster) { return monster.isBoss; });
        const std::string_view musicId = bossRoom ? "DungeonBossBgm" : "DungeonFieldBgm";
        if (assetCatalog.HasAudio(musicId)) (void)audio.PlayMusic(assetCatalog.GetAudioPath(musicId));
        if (dungeonMapId.empty())
        {
            if (const auto& initial = dungeonWorld->GetInitialPlayerHud(); initial)
            {
                dungeonHp.Set(initial->hp, initial->maxHp);
                if (initial->mpKnown) dungeonMp.Set(initial->mp, initial->maxMp); else dungeonMp = {};
            }
        }
        if (!dungeonMonsters.contains(inMapId))
        {
            std::vector<std::unique_ptr<Monster>> monsters;
            for (const auto& spawn : room.monsters)
            {
                // Each type loads its sheets once; instances share images but own playback state.
                auto& monsterTemplate = monsterTemplates[spawn.dataId];
                if (!monsterTemplate)
                    monsterTemplate = std::make_unique<Monster>(spawn, monsterCatalog, assetCatalog, renderer);
                monsters.push_back(std::make_unique<Monster>(*monsterTemplate, spawn));
            }
            std::sort(monsters.begin(), monsters.end(), [](const auto& a, const auto& b)
                { return a->GetGroundPosition().y < b->GetGroundPosition().y; });
            dungeonMonsters.emplace(inMapId, std::move(monsters));
        }
        for (const auto& actor : dungeonMonsters.at(inMapId)) actor->ClearPresentationHistory();
        gameplayMap.Configure(room.map);
        mapBackground.Configure(room.map);
        transitionZones = room.map.transitionZones;
        dungeonMapId = inMapId;
        targetMonsterId = 0; targetHp = {};
        inventoryUi.Close();
        combatBuffer.Clear(); presentationSnapshot.reset(); combatHitEffects.clear();
        combatPlayers.clear(); combatSnapshot.reset(); combatSnapshotAge = 0.0f;
        shotSounds.clear();
        skillUi.CancelDrag();
        remotePlayers.clear();
        player.SetGroundPosition(inPosition); player.ResetActionState(); player.ResetMovementSpeeds(); player.SetRunningEnabled(true);
        CombatPlayerState initial;
        initial.playerId = localPlayerId; initial.position = inPosition;
        initial.hitRecovery = dungeonWorld->GetSlideDefinition(localCharacterId).hitRecovery;
        initial.hp = initial.maxHp = dungeonWorld->GetCombatRules().maxHp;
        player.ApplyCombatState(initial, dungeonWorld->GetCombatRules());
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
        skillUi.ResetCombatState(); skillActionIds.clear(); skillBoostSequences.clear(); lastSkillStateTick=0; lastAcceptedSkillSequence=0; hasSkillStateTick=false;
        commandQueue.Clear();
        completionPending = completionStopping = false;
        completionWaitSeconds = 0.0f;
        completionResponse.reset(); completionStatus.clear();
        dungeonClient.Stop();
        const bool restoreTown = dungeonWorld.has_value();
        dungeonWorld.reset(); dungeonMonsters.clear(); dungeonMapId.clear();
        combatBuffer.Clear(); presentationSnapshot.reset(); combatHitEffects.clear(); appliedMapEpoch = pendingMapEpoch = 0;
        combatPlayers.clear(); combatSnapshot.reset(); hasCombatTick = dungeonCleared = false;
        completionWindowHidden=false;
        shotSounds.clear();
        targetMonsterId = lastHudHitSequence = 0; targetHp = {}; dungeonHp = {}; dungeonMp = {};
        lastCombatTick = 0; combatActionSequence = lastCombatActionResult = combatInputsThisSecond = 0;
        combatInputWindowSeconds = combatSnapshotAge = rejectedActionSeconds = 0.0f;
        player.ClearPredictedAttackFacing();
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
        inRenderer.FillRectangle(detailLeft+22,listTop+198,panelRight-42,listTop+234,D2D1::ColorF(0x514023));
        inRenderer.DrawText(L"ENTER  Enter dungeon", detailLeft + 22.0f, listTop + 204.0f,
            panelRight - 42.0f, listTop + 232.0f, D2D1::ColorF(0.98f, 0.82f, 0.42f));
        inRenderer.DrawText(L"X / ESC  Close", detailLeft + 22.0f, listTop + 238.0f,
            panelRight - 42.0f, listTop + 266.0f, D2D1::ColorF(0.78f, 0.82f, 0.86f));
        inRenderer.DrawText(L"Click / Wheel: Select   |   Drag title: Move", panelLeft + 28.0f, panelBottom - 42.0f,
            panelRight - 28.0f, panelBottom - 14.0f, D2D1::ColorF(0.72f, 0.72f, 0.68f));
    }

    void GameWorld::RenderSystemInterface(D2DRenderer& inRenderer,const SystemUiPage inPage) const
    {
        if (inPage == SystemUiPage::GameOptions)
        {
            RenderGameOptions(inRenderer);
            return;
        }
        const float viewportWidth = camera.GetViewportWidth();
        const float viewportHeight = camera.GetViewportHeight();
        if (inPage==SystemUiPage::Menu) inRenderer.FillRectangle(0.0f, 0.0f, viewportWidth, viewportHeight,
            D2D1::ColorF(0.22f, 0.23f, 0.25f, 0.76f));

        if (inPage == SystemUiPage::Menu)
        {
            inRenderer.DrawText(L"SYSTEM MENU", 52.0f, 34.0f,
                viewportWidth - 52.0f, 70.0f, D2D1::ColorF(0.96f, 0.92f, 0.82f));
            inRenderer.DrawText(L"ESC  Close", viewportWidth - 190.0f, 38.0f,
                viewportWidth - 42.0f, 68.0f, D2D1::ColorF(0.72f, 0.74f, 0.78f));

            inRenderer.DrawUiText(L"I 인벤토리   K 스킬   J 퀘스트   P 파티   O 옵션",
                {52,76,viewportWidth-52,98},D2D1::ColorF(0xB8C5D6),14);
            const SystemMenuLayout layout = CalculateSystemMenuLayout(
                viewportWidth, viewportHeight, systemMenuEntries.size());
            inRenderer.PushAxisAlignedClip(D2D1::RectF(
                layout.left, layout.top, layout.right, layout.bottom));
            for (std::size_t index = 0; index < systemMenuEntries.size(); ++index)
            {
                const D2D1_RECT_F tile = GetSystemMenuTileRectangle(
                    layout, index, systemMenuScrollOffset);
                const bool disabled=systemMenuEntries[index].action==SystemMenuAction::Party && IsDungeonUiRestricted();
                const bool hovered = !disabled && ContainsPoint(tile, uiMouseX, uiMouseY)
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
                    const auto destination=D2D1::RectF(iconLeft,iconTop,iconLeft+MENU_ICON_SIZE,iconTop+MENU_ICON_SIZE);
                    if (disabled || systemMenuEntries[index].action==SystemMenuAction::Skills)
                        inRenderer.DrawUiIcon(systemMenuEntries[index].icon.Get(),destination,disabled);
                    else inRenderer.DrawBitmap(systemMenuEntries[index].icon.Get(),
                        D2D1::RectF(0.0f,0.0f,bitmapSize.width,bitmapSize.height),destination);
                }
                if (systemMenuEntries[index].action == SystemMenuAction::GameOptions)
                {
                    const float x = (tile.left + tile.right) * 0.5f, y = tile.top + 64.0f;
                    inRenderer.DrawRectangle(x - 45, y - 25, x + 45, y + 25, D2D1::ColorF(0xC7AC74), 3);
                    inRenderer.DrawLine(x, y + 25, x, y + 42, D2D1::ColorF(0xC7AC74), 3);
                    inRenderer.DrawLine(x - 24, y + 42, x + 24, y + 42, D2D1::ColorF(0xC7AC74), 3);
                }
                if (systemMenuEntries[index].action == SystemMenuAction::Inventory)
                    for (int row = 0; row < 3; ++row) for (int column = 0; column < 3; ++column)
                    {
                        const float x = iconLeft + 8.0f + column * 24.0f, y = iconTop + 8.0f + row * 24.0f;
                        inRenderer.DrawRectangle(x, y, x + 19.0f, y + 19.0f, D2D1::ColorF(0.90f,0.76f,0.40f));
                    }
                if (systemMenuEntries[index].action == SystemMenuAction::Quests)
                {
                    const float x = iconLeft + 20, y = iconTop + 10;
                    inRenderer.DrawRectangle(x, y, x + 88, y + 104, D2D1::ColorF(0xC7AC74), 3);
                    for (int row = 0; row < 4; ++row)
                        inRenderer.DrawLine(x + 14, y + 24 + row * 20, x + 74, y + 24 + row * 20, D2D1::ColorF(0xC7AC74), 2);
                }
                inRenderer.DrawText(systemMenuEntries[index].label,
                    tile.left + 12.0f, tile.bottom - 42.0f,
                    tile.right - 12.0f, tile.bottom - 12.0f,
                    disabled ? D2D1::ColorF(0.48f,0.54f,0.62f):D2D1::ColorF(0.94f,0.94f,0.92f));
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
        else if (inPage == SystemUiPage::ExitConfirmation)
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

    void GameWorld::RenderPartyInterface(D2DRenderer& inRenderer,const SystemUiPage inPage) const
    {
        if (IsDungeonUiRestricted()) return;
        if (inPage == SystemUiPage::PartyDetails)
        {
            RenderPartyDetails(inRenderer);
        }
        else if (inPage == SystemUiPage::Party)
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
            inRenderer.DrawText(L"ESC  닫기", layout.panel.right - 150.0f,
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
        else if (inPage == SystemUiPage::PartyDirectory)
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
            inRenderer.DrawText(L"ESC  닫기", layout.panel.right - 150.0f,
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
        else if (inPage == SystemUiPage::PartyCreate)
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
            if (inPage==systemUiPage && editingPartyTitle)
                inRenderer.DrawRectangle(dialog.left+24,dialog.top+88,dialog.right-24,dialog.top+128,D2D1::ColorF(0x9CCFE7),2);
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

        if (inPage==SystemUiPage::Invitation && pendingPartyInvitation.has_value())
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
            const bool running = player.IsDungeonRunInput();
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
            (player.IsHitReacting() || player.IsAttacking() || player.IsLocallyCasting()) ? 0
                : static_cast<int>(inInput.moveRight) - static_cast<int>(inInput.moveLeft));
        const std::int8_t directionY = static_cast<std::int8_t>(
            (player.IsHitReacting() || player.IsAttacking() || player.IsLocallyCasting()) ? 0
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
        constexpr std::size_t MAX_TOWN_MOVEMENT_SENDS = 64;
        const auto sequence=++movementSequence;
        if (stateChanged)
        {
            townMovementStateSequence=sequence;
            player.ClearTownPositionCorrection();
        }
        townMovementSends.push_back({sequence,std::chrono::steady_clock::now()});
        if (townMovementSends.size()>MAX_TOWN_MOVEMENT_SENDS) townMovementSends.pop_front();
        townClient.SendMovement(TownProtocol::MoveInput{
            sequence,
            directionX,
            directionY,
            false
        });
        lastSentDirectionX = directionX;
        lastSentDirectionY = directionY;
        hasSentMovementInput = true;
    }
}
