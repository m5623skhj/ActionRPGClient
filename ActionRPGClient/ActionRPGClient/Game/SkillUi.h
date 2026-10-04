#pragma once
#include "Game/SkillTreeCatalog.h"
#include "Game/DungeonCombat.h"
#include "Input/InputState.h"
#include <d2d1_1.h>
#include <wrl/client.h>
#include <array>
#include <filesystem>
#include <optional>

namespace ActionRPG
{
    class AssetCatalog;
    class D2DRenderer;
    struct SkillUiAction
    {
        std::string learnSkill;
        std::uint32_t expectedSkillLevel{};
        bool requestState{};
    };

    // Game-thread-only model/view. TCP events and combat snapshots are applied by GameWorld.
    class SkillUi final
    {
    public:
        SkillUi(const AssetCatalog& inAssets, D2DRenderer& inRenderer);
        void ApplyState(std::string_view inPayload, std::uint32_t inCharacterId);
        void ApplyCombatState(const CombatPlayerState& inState);
        void AcceptSkill(const std::string& inId);
        void ApplyTownCooldowns(const std::unordered_map<std::string, float>& inCooldowns);
        void ResetCombatState();
        void Invalidate();
        void CancelDrag();
        void SetConnected(bool inConnected)
        {
            if (!inConnected && ready) Invalidate();
            connected = inConnected;
        }
        void SetLearningAllowed(bool inAllowed) { learningAllowed = inAllowed; }
        [[nodiscard]] bool CanUse(const std::string& inId, bool inAirborne, bool inDungeon) const;
        [[nodiscard]] std::string Hotkey(const InputState& inInput) const;
        [[nodiscard]] const PlayerSkills::CharacterProgression& GetProgression() const { return progression; }
        [[nodiscard]] std::pair<std::uint32_t, std::uint32_t> GetMinimumClientSize() const;
        [[nodiscard]] SkillUiAction Update(float inDeltaSeconds, const InputState& inInput,
            float inWidth, float inHeight, bool inEditing, bool inBarVisible);
        void Render(D2DRenderer& inRenderer, float inWidth, float inHeight, bool inEditing) const;
        [[nodiscard]] D2D1_RECT_F GetHotbarBounds(float inWidth, float inHeight) const
        { return CalculateLayout(inWidth, inHeight).bar; }

    private:
        struct Layout
        {
            bool compact{}, stacked{};
            D2D1_RECT_F bar{}, panel{}, tree{}, detail{}, learnButton{}, treeTab{}, detailTab{};
            std::array<D2D1_RECT_F, 6> slots{};
        };
        [[nodiscard]] Layout CalculateLayout(float inWidth, float inHeight) const;
        [[nodiscard]] float Number(const nlohmann::json& inObject, const char* inKey) const;
        [[nodiscard]] D2D1_COLOR_F Color(const char* inKey) const;
        [[nodiscard]] bool IsOwned(const std::string& inId) const;
        [[nodiscard]] std::vector<std::string> OrderedSkills() const;
        [[nodiscard]] D2D1_RECT_F NodeRectangle(const Layout& inLayout, const std::string& inId) const;
        [[nodiscard]] std::wstring Details(const std::string& inId) const;
        [[nodiscard]] std::wstring LearnReason(const std::string& inId) const;
        [[nodiscard]] float Cooldown(const std::string& inId) const;
        [[nodiscard]] std::filesystem::path SlotPath() const;
        void LoadSlots();
        void SaveSlots();
        void Assign(std::size_t inSlot, const std::string& inId);
        void DrawIcon(D2DRenderer& inRenderer, const std::string& inId,
            const D2D1_RECT_F& inRect, bool inGray) const;

        nlohmann::json settings;
        const AssetCatalog& assets;
        D2DRenderer& renderer;
        PlayerSkills::Catalog catalog;
        PlayerSkills::SkillTreeCatalog trees;
        PlayerSkills::CharacterProgression progression;
        std::uint32_t characterId{};
        bool ready{}, connected{}, combatReady{}, learnPending{}, detailTabSelected{};
        bool learningAllowed{true};
        float learnSeconds{}, treeScrollX{}, treeScrollY{}, detailScroll{};
        std::array<std::string, 6> slots{};
        std::unordered_map<std::string, float> cooldowns;
        std::unordered_map<std::string, Microsoft::WRL::ComPtr<ID2D1Bitmap1>> icons;
        std::string selected, dragCandidate, dragging;
        float dragStartX{}, dragStartY{}, mouseX{}, mouseY{};
        std::wstring status;
    };
}
