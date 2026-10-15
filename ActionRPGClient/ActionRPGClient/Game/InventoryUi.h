#pragma once

#include "Input/InputState.h"
#include "Game/HudLayout.h"

#include <d2d1_1.h>
#include <wrl/client.h>

#include <nlohmann/json.hpp>
#include <vector>
#include <array>
#include <cstdint>
#include <cstddef>
#include <string_view>
#include <optional>
#include <string>
#include <unordered_map>

namespace ActionRPG
{
    class AssetCatalog;
    class D2DRenderer;

    enum class InventoryCategory : std::uint8_t { Equipment, Material, Consumable, Quest };
    enum class EquipmentSlot : std::uint8_t { Weapon, Top, Bottom, Shoes, Ring, Necklace, Bracelet };
    enum class InventoryOperation : std::uint8_t { None, Equip, Unequip, Use };

    // Presentation data only. The server owns quantities, restrictions and persistent IDs.
    struct InventoryItemView
    {
        std::string instanceId, definitionId;
        std::wstring name, description, requirements, equipReason;
        std::string icon;
        std::uint32_t quantity{}, maxStack{};
        std::optional<EquipmentSlot> equipmentSlot;
        bool canEquip{}, throwItem{};
        std::uint32_t useRequiredLevel{};
        float useCooldownSeconds{};
    };

    struct InventorySnapshotView
    {
        std::uint64_t characterId{}, revision{};
        std::array<std::array<std::optional<InventoryItemView>, 40>, 4> bags;
        std::array<std::optional<InventoryItemView>, 7> equipment;
    };

    struct InventoryUiAction
    {
        InventoryOperation operation{InventoryOperation::None};
        std::uint64_t expectedRevision{};
        std::string requestId, instanceId;
        std::uint32_t quantity{};
        bool requestState{}, requestUseStatus{}, throwItem{};
        std::string useStatusRequestId;
    };

    // Owned and updated exclusively by GameWorld on the game thread.
    class InventoryUi final
    {
    public:
        InventoryUi(const AssetCatalog& inAssets, D2DRenderer& inRenderer);
        void SetCharacterContext(std::uint64_t inCharacterId, std::uint32_t inDefinitionId, std::uint32_t inLevel);
        void HandleState(std::string_view inPayload);
        void HandleDefinitions(std::string_view inPayload);
        [[nodiscard]] bool HandleOperation(std::string_view inPayload);
        void ApplyState(InventorySnapshotView inState);
        void ResolveRequest(const std::string& inRequestId, std::wstring inMessage, bool inStateCurrent);
        void SetConnected(bool inConnected);
        void ObserveRevision(std::uint64_t inCharacterId, std::uint64_t inRevision);
        [[nodiscard]] std::uint64_t GetObservedRevision() const
        { return latestServerRevision > state.revision ? latestServerRevision : state.revision; }
        void SetCombatTempoScale(float inTempoScale) { combatTempoScale = inTempoScale; }
        void Invalidate(std::wstring inMessage);
        [[nodiscard]] bool ConsumeCombatRefresh() { const bool value = combatRefresh; combatRefresh = false; return value; }
        void SetHotbarBounds(const D2D1_RECT_F& inBounds, float inScale = 1.0f)
        { skillBarBounds = HudLayout::ScaleRectangle(inBounds, 1.0f/inScale); }
        void RenderQuickSlots(D2DRenderer& inRenderer, float inWidth, float inHeight,
            const D2D1_RECT_F& inSkillBar, bool inInventory) const;
        [[nodiscard]] static D2D1_RECT_F QuickslotBounds(const D2D1_RECT_F& inSkillBar);
        void Reset();
        void Close();
        [[nodiscard]] bool IsDragging() const { return !dragging.empty() || !dragCandidate.empty(); }
        [[nodiscard]] InventoryUiAction Update(float inDeltaSeconds, const InputState& inInput,
            float inWidth, float inHeight, bool inVisible, bool inBarVisible, const D2D1_RECT_F& inSkillBar);
        void Render(D2DRenderer& inRenderer, float inWidth, float inHeight, bool inDungeon) const;

    private:
        enum class CooldownState { None, Pending, Active };
        struct CooldownView
        {
            CooldownState state{CooldownState::None};
            std::uint64_t serverTimeMs{}, readyAtMs{};
            float remainingSeconds{};
        };
        using CooldownMap = std::unordered_map<std::string, CooldownView>;
        [[nodiscard]] static std::optional<CooldownMap> ParseCooldowns(const nlohmann::json& inBody);
        void ApplyCooldowns(std::optional<CooldownMap> inCooldowns);
        [[nodiscard]] const CooldownView* FindCooldown(const InventoryItemView& inItem) const;
        struct Assembly
        {
            std::string requestId;
            std::uint64_t characterId{}, revision{};
            nlohmann::json inventory;
            std::vector<std::optional<nlohmann::json>> definitions;
            std::optional<CooldownMap> cooldowns;
        };
        void CompleteState();
        [[nodiscard]] static std::array<D2D1_RECT_F, HudLayout::ITEM_SLOT_COUNT> QuickslotRectangles(const D2D1_RECT_F& inSkillBar);
        [[nodiscard]] InventoryUiAction UpdateQuickSlots(const InputState& inInput, bool inInventory,
            const D2D1_RECT_F& inSkillBar, float inWidth, float inHeight);
        void RenderTooltip(D2DRenderer& inRenderer, float inWidth, float inHeight) const;
        std::optional<Assembly> assembly;
        std::uint64_t expectedCharacterId{}, latestServerRevision{};
        std::uint32_t characterDefinitionId{}, characterLevel{};
        std::string stateRequestId;
        struct Layout
        {
            D2D1_RECT_F panel, status, viewport;
            float maxScroll{};
            std::array<D2D1_RECT_F, 4> tabs;
            std::array<D2D1_RECT_F, 40> bags;
            std::array<D2D1_RECT_F, 7> equipment;
        };
        [[nodiscard]] Layout CalculateLayout(float inWidth, float inHeight) const;
        [[nodiscard]] const InventoryItemView* FindItem(const std::string& inId, bool* inEquipped = nullptr) const;
        [[nodiscard]] InventoryUiAction BeginRequest(InventoryOperation inOperation,
            const std::string& inId, std::uint32_t inQuantity);
        [[nodiscard]] std::wstring Details() const;
        [[nodiscard]] D2D1_RECT_F TooltipBounds(float inWidth, float inHeight) const;
        void DrawItem(D2DRenderer& inRenderer, const D2D1_RECT_F& inRect,
            const InventoryItemView* inItem, std::wstring_view inLabel = {}) const;

        const AssetCatalog& assets;
        D2DRenderer& renderer;
        InventorySnapshotView state;
        std::unordered_map<std::string, Microsoft::WRL::ComPtr<ID2D1Bitmap1>> icons;
        std::size_t tab{};
        std::string hovered, pendingRequest;
        InventoryOperation pendingOperation{InventoryOperation::None};
        D2D1_RECT_F hoveredRect{}, skillBarBounds{};
        std::unordered_map<std::uint64_t, std::array<std::string, HudLayout::ITEM_SLOT_COUNT>> quickSlotsByCharacter;
        std::string dragCandidate, dragging;
        float dragStartX{}, dragStartY{}, bagScroll{}, quickNoticeSeconds{};
        bool quickHover{};
        bool ready{}, connected{}, stateRequested{}, cooldownsKnown{}, combatRefresh{}, useStatusPending{}, useOutcomeUncertain{};
        CooldownMap cooldowns;
        float useStatusSeconds{}, cooldownRefreshSeconds{};
        std::wstring useNotice;
        float pendingSeconds{}, stateWaitSeconds{}, tooltipScroll{}, mouseX{}, mouseY{};
        float combatTempoScale{1.0f};
        std::wstring status{L"인벤토리 상태를 기다리는 중"};
    };
}
