#pragma once

#include "Input/InputState.h"

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
    enum class InventoryOperation : std::uint8_t { None, Equip, Unequip, Discard };

    // Presentation data only. The server owns quantities, restrictions and persistent IDs.
    struct InventoryItemView
    {
        std::string instanceId;
        std::wstring name, description, requirements, equipReason;
        std::string icon;
        std::uint32_t quantity{}, maxStack{};
        std::optional<EquipmentSlot> equipmentSlot;
        bool canEquip{};
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
        bool requestState{};
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
        void Invalidate(std::wstring inMessage);
        void Reset();
        void Close();
        [[nodiscard]] bool HandleEscape();
        [[nodiscard]] InventoryUiAction Update(float inDeltaSeconds, const InputState& inInput,
            float inWidth, float inHeight, bool inVisible);
        void Render(D2DRenderer& inRenderer, float inWidth, float inHeight, bool inDungeon) const;

    private:
        struct Assembly
        {
            std::string requestId;
            std::uint64_t characterId{}, revision{};
            nlohmann::json inventory;
            std::vector<std::optional<nlohmann::json>> definitions;
        };
        void CompleteState();
        std::optional<Assembly> assembly;
        std::uint64_t expectedCharacterId{};
        std::uint32_t characterDefinitionId{}, characterLevel{};
        std::string stateRequestId;
        enum class DiscardStage { Closed, Quantity, Confirmation };
        struct Layout
        {
            D2D1_RECT_F panel, detail, detailText, discard, use, sell, status;
            std::array<D2D1_RECT_F, 4> tabs;
            std::array<D2D1_RECT_F, 40> bags;
            std::array<D2D1_RECT_F, 7> equipment;
            bool compact{};
        };
        [[nodiscard]] Layout CalculateLayout(float inWidth, float inHeight) const;
        [[nodiscard]] const InventoryItemView* FindItem(const std::string& inId, bool* inEquipped = nullptr) const;
        [[nodiscard]] InventoryUiAction BeginRequest(InventoryOperation inOperation,
            const std::string& inId, std::uint32_t inQuantity);
        [[nodiscard]] std::wstring Details() const;
        [[nodiscard]] std::optional<std::uint32_t> DiscardQuantity() const;
        void DrawItem(D2DRenderer& inRenderer, const D2D1_RECT_F& inRect,
            const InventoryItemView* inItem, std::wstring_view inLabel = {}) const;

        const AssetCatalog& assets;
        D2DRenderer& renderer;
        InventorySnapshotView state;
        std::unordered_map<std::string, Microsoft::WRL::ComPtr<ID2D1Bitmap1>> icons;
        std::size_t tab{};
        std::string selected, pendingRequest, discardItem;
        bool ready{}, connected{}, stateRequested{};
        float pendingSeconds{}, stateWaitSeconds{}, detailScroll{};
        DiscardStage discardStage{DiscardStage::Closed};
        std::wstring quantityText, status{L"인벤토리 상태를 기다리는 중"};
    };
}
