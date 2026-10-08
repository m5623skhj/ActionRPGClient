#pragma once
#include "Network/AuthClient.h"
#include "Game/SessionMenuAction.h"
#include "Input/InputState.h"
#include <cstdint>
#include <string>
#include <vector>
#include <optional>
#include <unordered_map>
#include <nlohmann/json.hpp>

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
        enum class State { Login, GoogleLogin, ServerSelection, Ticket, Town, CharacterSelection, CharacterCreate, Playing, Leaving };
        void StartLogin();
        void EnterSelectedTown();
        void CancelPending();
        void ProcessEvents();
        void ProcessCharacterEvents();
        void RequestCharacters();
        void CreateCharacter();
        void SelectCharacter();
        void ClearCharacters();
        struct Character
        {
            std::uint64_t id{}, generation{}, revision{};
            std::uint32_t definitionId{}, level{};
            std::wstring name;
        };
        enum class CharacterRequest { None, List, Create, Select };
        std::vector<Character> characters;
        std::unordered_map<std::uint32_t, nlohmann::json> characterBatches;
        std::size_t selectedCharacter{};
        std::uint32_t characterBatchCount{};
        CharacterRequest characterRequest{CharacterRequest::None};
        std::string characterRequestId;
        std::optional<Character> pendingCharacter;
        std::chrono::steady_clock::time_point characterDeadline{};
        bool charactersCurrent{}, characterAccepted{};
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