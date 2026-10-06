#pragma once
#include "Network/AuthClient.h"
#include "Game/SessionMenuAction.h"
#include "Input/InputState.h"
#include <cstdint>
#include <string>

namespace ActionRPG
{
    class AssetCatalog;
    class D2DRenderer;
    class DungeonClient;
    class TownClient;

    // Main-thread owner of credentials and UI state. Workers only deliver tagged events.
    class LoginFlow final
    {
    public:
        LoginFlow(const AssetCatalog& inAssets, TownClient& inTown, DungeonClient& inDungeon);
        ~LoginFlow();
        void Update(const InputState& inInput, float inWidth, float inHeight);
        void Render(D2DRenderer& inRenderer, float inWidth, float inHeight) const;
        void BeginLeave(SessionMenuAction inAction);
        [[nodiscard]] bool IsPlaying() const noexcept;
        [[nodiscard]] bool ConsumeExitRequested() noexcept;
    private:
        enum class State { Login, GoogleLogin, ServerSelection, Ticket, Town, Playing, Leaving };
        void StartLogin();
        void EnterSelectedTown();
        void CancelPending();
        void ProcessEvents();
        [[nodiscard]] bool HasToken() const;
        AuthSettings settings;
        AuthClient auth;
        TownClient& town;
        DungeonClient& dungeon;
        State state{ State::Login };
        std::uint64_t attempt{};
        std::string gameToken;
        std::chrono::steady_clock::time_point tokenExpiry{};
        std::wstring message;
        std::size_t selectedServer{};
        std::wstring playerName;
        std::uint32_t characterId{};
        SessionMenuAction leaveAction{ SessionMenuAction::None };
        bool chooseAccount{}, nameFocused{}, logoutPending{}, exitRequested{};
    };
}