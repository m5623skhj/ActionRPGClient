#include "Game/InventoryUi.h"
#include "Graphics/D2DRenderer.h"
#include "Resources/AssetCatalog.h"
#include "Network/CharacterInventoryJson.h"
#include "Network/AuthSettings.h"

#include <d2d1_1helper.h>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace
{
    constexpr std::array<const wchar_t*, 4> CATEGORY_NAMES{L"장비", L"재료", L"소모품", L"퀘스트"};
    constexpr std::array<const wchar_t*, 7> EQUIPMENT_NAMES{L"무기", L"상의", L"하의", L"신발", L"반지", L"목걸이", L"팔찌"};
    constexpr float REQUEST_TIMEOUT_SECONDS = 10.0f;

    bool Contains(const D2D1_RECT_F& inRect, float inX, float inY)
    {
        return inX >= inRect.left && inX < inRect.right && inY >= inRect.top && inY < inRect.bottom;
    }

    void Button(ActionRPG::D2DRenderer& inRenderer, const D2D1_RECT_F& inRect,
        std::wstring_view inText, bool inEnabled)
    {
        inRenderer.FillRectangle(inRect.left, inRect.top, inRect.right, inRect.bottom,
            D2D1::ColorF(inEnabled ? 0.14f : 0.08f, inEnabled ? 0.25f : 0.10f, inEnabled ? 0.34f : 0.12f));
        inRenderer.DrawRectangle(inRect.left, inRect.top, inRect.right, inRect.bottom,
            D2D1::ColorF(inEnabled ? 0.65f : 0.28f, 0.45f, 0.50f));
        inRenderer.DrawUiText(inText, inRect, D2D1::ColorF(inEnabled ? 0.95f : 0.45f, 0.70f, 0.72f),
            13.0f, false, true);
    }

    D2D1_RECT_F Dialog(float inWidth, float inHeight)
    {
        const float width = std::min(440.0f, inWidth - 32.0f);
        const float left = (inWidth - width) * 0.5f, top = (inHeight - 244.0f) * 0.5f;
        return D2D1::RectF(left, top, left + width, top + 244.0f);
    }
}

namespace ActionRPG
{
    InventoryUi::InventoryUi(const AssetCatalog& inAssets, D2DRenderer& inRenderer)
        : assets(inAssets), renderer(inRenderer) {}

    void InventoryUi::Reset()
    {
        state = {};
        assembly.reset();
        expectedCharacterId = characterDefinitionId = characterLevel = 0;
        stateRequestId.clear();
        selected.clear();
        discardItem.clear();
        pendingRequest.clear();
        ready = connected = stateRequested = false;
        pendingSeconds = stateWaitSeconds = detailScroll = 0.0f;
        tab = 0;
        icons.clear();
        Close();
        status = L"인벤토리 상태를 기다리는 중";
    }

    void InventoryUi::SetCharacterContext(const std::uint64_t inCharacterId,
        const std::uint32_t inDefinitionId, const std::uint32_t inLevel)
    {
        if (expectedCharacterId != inCharacterId)
        {
            const bool wasConnected = connected;
            Reset();
            connected = wasConnected;
            expectedCharacterId = inCharacterId;
        }
        if (characterLevel != inLevel || characterDefinitionId != inDefinitionId)
        {
            characterDefinitionId = inDefinitionId;
            characterLevel = inLevel;
            Invalidate(L"장착 조건을 다시 확인하는 중");
        }
    }

    void InventoryUi::HandleState(const std::string_view inPayload)
    {
        using namespace CharacterInventoryJson;
        const auto body = Json::parse(inPayload);
        const auto request = HexId(body.at("requestId"), 64);
        const auto id = UInt64(body.at("characterId"));
        const auto revision = UInt64(body.at("revision"));
        const auto result = body.at("result").get<std::string>();
        if (result != "Succeeded")
        {
            if (request == stateRequestId)
            {
                assembly.reset();
                ready = false;
                status = L"인벤토리를 조회하지 못했습니다. 기존 표시는 확정 상태가 아닙니다";
            }
            return;
        }
        if (id == 0 || id != expectedCharacterId || revision < state.revision) return;
        const auto batchCount = UInt32(body.at("definitionBatchCount"), 0, 167);
        const auto& inventory = body.at("inventory");
        if (!inventory.is_object() || !inventory.at("items").is_array()
            || inventory.at("items").size() > 167) throw std::runtime_error("Invalid inventory state.");
        assembly = Assembly{request, id, revision, inventory,
            std::vector<std::optional<Json>>(batchCount)};
        ready = false;
        stateRequested = true;
        stateRequestId = request;
        stateWaitSeconds = 0.0f;
        Close();
        CompleteState();
    }

    void InventoryUi::HandleDefinitions(const std::string_view inPayload)
    {
        using namespace CharacterInventoryJson;
        const auto body = Json::parse(inPayload);
        const auto request = HexId(body.at("requestId"), 64);
        const auto id = UInt64(body.at("characterId"));
        const auto revision = UInt64(body.at("revision"));
        if (!assembly || request != assembly->requestId || id != assembly->characterId
            || revision != assembly->revision) return;
        if (body.at("result") != "Succeeded") throw std::runtime_error("Definition query failed.");
        const auto count = UInt32(body.at("batchCount"), 1, 167);
        const auto index = UInt32(body.at("batchIndex"), 0, count - 1);
        const auto& definitions = body.at("definitions");
        if (count != assembly->definitions.size() || !definitions.is_array() || definitions.size() > 167)
            throw std::runtime_error("Invalid definition batch.");
        auto& target = assembly->definitions[index];
        if (target && *target != definitions) throw std::runtime_error("Conflicting definition batch.");
        target = definitions;
        CompleteState();
    }

    // Build a complete validated view before touching the displayed state. Game-thread only.
    void InventoryUi::CompleteState()
    {
        using namespace CharacterInventoryJson;
        if (!assembly || std::any_of(assembly->definitions.begin(), assembly->definitions.end(),
            [](const auto& value) { return !value.has_value(); })) return;
        constexpr std::array<const char*, 4> CATEGORIES{"Equipment", "Material", "Consumable", "Quest"};
        constexpr std::array<const char*, 7> SLOTS{"Weapon", "Top", "Bottom", "Shoes", "Ring", "Necklace", "Bracelet"};
        std::unordered_map<std::string, Json> definitions;
        for (const auto& batch : assembly->definitions)
            for (const auto& definition : *batch)
            {
                const auto id = definition.at("id").get<std::string>();
                const auto letter = [](char value) { return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z'); };
                if (id.empty() || id.size() > 64 || !letter(id.front())
                    || !std::all_of(id.begin(), id.end(), [&](char value)
                        { return letter(value) || (value >= '0' && value <= '9') || value == '_' || value == '.' || value == '-'; })
                    || id == "__proto__" || id == "prototype" || id == "constructor"
                    || definition.dump().size() > 8192 || !definitions.emplace(id, definition).second
                    || definitions.size() > 167) throw std::runtime_error("Invalid item definition.");
            }
        InventorySnapshotView next;
        next.characterId = assembly->characterId;
        next.revision = assembly->revision;
        for (const auto& row : assembly->inventory.at("items"))
        {
            const auto definitionId = row.at("definitionId").get<std::string>();
            const auto found = definitions.find(definitionId);
            if (found == definitions.end()) throw std::runtime_error("Missing item definition.");
            const auto& definition = found->second;
            const auto categoryText = definition.at("category").get<std::string>();
            const auto categoryIt = std::find(CATEGORIES.begin(), CATEGORIES.end(), categoryText);
            if (categoryIt == CATEGORIES.end()) throw std::runtime_error("Invalid item category.");
            const auto category = static_cast<std::uint32_t>(categoryIt - CATEGORIES.begin());
            InventoryItemView item;
            item.instanceId = HexId(row.at("instanceId"), 32);
            item.quantity = UInt32(row.at("count"), 1, UINT32_MAX);
            item.maxStack = UInt32(definition.at("maxStack"), 1, UINT32_MAX);
            const auto name = definition.at("name").get<std::string>();
            const auto description = definition.at("description").get<std::string>();
            item.icon = definition.at("icon").get<std::string>();
            if (name.empty() || name.size() > 128 || description.size() > 2048 || item.icon.size() > 255)
                throw std::runtime_error("Invalid item text.");
            item.name = AuthUtf8ToWide(name);
            item.description = AuthUtf8ToWide(description);
            if (!item.icon.starts_with("Images/") || !item.icon.ends_with(".png")
                || item.icon.find("..") != std::string::npos || item.icon.find_first_of("\\:") != std::string::npos
                || std::any_of(item.icon.begin(), item.icon.end(), [](unsigned char value) { return value < 32; }))
                throw std::runtime_error("Invalid icon path.");
            if (category == 0)
            {
                const auto slotText = definition.at("equipmentSlot").get<std::string>();
                const auto slotIt = std::find(SLOTS.begin(), SLOTS.end(), slotText);
                if (slotIt == SLOTS.end()) throw std::runtime_error("Invalid equipment slot.");
                item.equipmentSlot = static_cast<EquipmentSlot>(slotIt - SLOTS.begin());
                const auto level = UInt32(definition.at("requiredLevel"), 1, 1000000);
                const auto& kinds = definition.at("characterDefinitionIds");
                if (!kinds.is_array() || kinds.size() > 256) throw std::runtime_error("Invalid equipment requirements.");
                bool allowed = kinds.empty();
                item.requirements = L"필요 레벨: " + std::to_wstring(level) + L"\n캐릭터: ";
                for (const auto& kind : kinds)
                {
                    const auto value = UInt32(kind, 1, 1000000);
                    allowed = allowed || value == characterDefinitionId;
                    item.requirements += std::to_wstring(value) + L" ";
                }
                if (kinds.empty()) item.requirements += L"제한 없음";
                item.canEquip = allowed && characterLevel >= level;
                if (!allowed) item.equipReason = L"이 캐릭터는 장착할 수 없습니다";
                else if (characterLevel < level) item.equipReason = L"장착에 필요한 레벨이 부족합니다";
            }
            const auto container = UInt32(row.at("container"), 0, 4);
            const auto slot = UInt32(row.at("slot"), 0, container == 4 ? 6 : 39);
            if (container != 4 && container != category) throw std::runtime_error("Item category mismatch.");
            auto& destination = container == 4 ? next.equipment[slot] : next.bags[container][slot];
            if (destination) throw std::runtime_error("Duplicate inventory slot.");
            destination = std::move(item);
        }
        ApplyState(std::move(next));
        assembly.reset();
    }

    bool InventoryUi::HandleOperation(const std::string_view inPayload)
    {
        using namespace CharacterInventoryJson;
        const auto body = Json::parse(inPayload);
        const auto request = HexId(body.at("requestId"), 64);
        const auto id = UInt64(body.at("characterId"));
        (void)UInt64(body.at("revision"));
        if (request != pendingRequest || id != expectedCharacterId) return false;
        const auto result = body.at("result").get<std::string>();
        std::wstring text = L"요청이 거절됐습니다. 상태를 다시 확인합니다";
        if (result == "Succeeded") text = L"처리됐습니다. 확정 상태를 받는 중";
        else if (result == "RevisionConflict") text = L"상태가 변경됐습니다. 최신 상태를 받는 중";
        else if (result == "InventoryFull") text = L"장비 가방에 빈 칸이 없습니다";
        else if (result == "EquipmentRequirements") text = L"장착 조건을 충족하지 않습니다";
        else if (result == "InvalidQuantity") text = L"수량을 확인해 주세요";
        else if (result == "NotImplemented") text = L"아직 지원하지 않는 기능입니다";
        const bool unchanged = result == "InventoryFull" || result == "EquipmentRequirements"
            || result == "InvalidQuantity" || result == "NotImplemented";
        ResolveRequest(request, std::move(text), unchanged);
        return result == "RevisionConflict";
    }

    void InventoryUi::Close()
    {
        discardStage = DiscardStage::Closed;
        discardItem.clear();
        quantityText.clear();
    }

    bool InventoryUi::HandleEscape()
    {
        if (discardStage == DiscardStage::Closed) return false;
        Close();
        return true;
    }

    void InventoryUi::Invalidate(std::wstring inMessage)
    {
        ready = stateRequested = false;
        assembly.reset();
        Close();
        status = std::move(inMessage);
    }

    void InventoryUi::SetConnected(const bool inConnected)
    {
        if (!inConnected)
        {
            ready = stateRequested = false;
            pendingRequest.clear();
            assembly.reset();
            Close();
            status = L"연결이 끊겼습니다. 표시된 상태는 다시 확인해야 합니다";
        }
        connected = inConnected;
    }

    void InventoryUi::ApplyState(InventorySnapshotView inState)
    {
        if (inState.characterId == 0) throw std::runtime_error("Missing inventory character.");
        const bool sameCharacter = state.characterId == inState.characterId;
        if (sameCharacter && inState.revision < state.revision) return;
        std::unordered_map<std::string, bool> seen;
        const auto validate = [&](const InventoryItemView& item, bool equipment, std::size_t index)
        {
            if (item.instanceId.size() != 32 || !std::all_of(item.instanceId.begin(), item.instanceId.end(), [](unsigned char value)
                { return (value >= '0' && value <= '9') || (value >= 'a' && value <= 'f'); }) || item.name.empty() || item.maxStack == 0
                || item.quantity == 0 || item.quantity > item.maxStack
                || !seen.emplace(item.instanceId, true).second
                || (item.equipmentSlot && static_cast<std::size_t>(*item.equipmentSlot) >= EQUIPMENT_NAMES.size())
                || (equipment && (!item.equipmentSlot || static_cast<std::size_t>(*item.equipmentSlot) != index))
                || (item.equipmentSlot && (item.quantity != 1 || item.maxStack != 1)))
                throw std::runtime_error("Invalid inventory item state.");
        };
        for (std::size_t category = 0; category < inState.bags.size(); ++category)
            for (const auto& item : inState.bags[category])
                if (item)
                {
                    validate(*item, false, 0);
                    if ((category == 0) != item->equipmentSlot.has_value())
                        throw std::runtime_error("Invalid inventory category.");
                }
        for (std::size_t index = 0; index < inState.equipment.size(); ++index)
            if (inState.equipment[index]) validate(*inState.equipment[index], true, index);
        if (!sameCharacter)
        {
            selected.clear();
            pendingRequest.clear();
            icons.clear();
        }
        if (!sameCharacter || inState.revision != state.revision) Close();
        state = std::move(inState);
        ready = true;
        stateRequested = false;
        if (!FindItem(selected)) selected.clear();
        const auto loadIcon = [&](const InventoryItemView& item)
        {
            if (item.icon.empty() || icons.contains(item.icon)) return;
            Microsoft::WRL::ComPtr<ID2D1Bitmap1> bitmap;
            try { bitmap = renderer.LoadBitmap(assets.GetAssetPath(item.icon)); }
            catch (const std::exception&) {} // Missing presentation art does not erase server items.
            icons.emplace(item.icon, std::move(bitmap));
        };
        for (const auto& bag : state.bags) for (const auto& item : bag) if (item) loadIcon(*item);
        for (const auto& item : state.equipment) if (item) loadIcon(*item);
        if (pendingRequest.empty()) status = L"좌클릭: 상세 / 장비 우클릭: 장착 / 장비창 우클릭: 해제";
    }

    void InventoryUi::ResolveRequest(const std::string& inRequestId, std::wstring inMessage,
        const bool inStateCurrent)
    {
        if (inRequestId.empty() || pendingRequest != inRequestId) return;
        pendingRequest.clear();
        pendingSeconds = 0.0f;
        status = std::move(inMessage);
        if (!inStateCurrent) { ready = false; stateRequested = false; }
    }

    const InventoryItemView* InventoryUi::FindItem(const std::string& inId, bool* inEquipped) const
    {
        if (inEquipped) *inEquipped = false;
        if (inId.empty()) return nullptr;
        for (const auto& bag : state.bags)
            for (const auto& item : bag) if (item && item->instanceId == inId) return &*item;
        for (const auto& item : state.equipment)
            if (item && item->instanceId == inId)
            {
                if (inEquipped) *inEquipped = true;
                return &*item;
            }
        return nullptr;
    }

    InventoryUiAction InventoryUi::BeginRequest(const InventoryOperation inOperation,
        const std::string& inId, const std::uint32_t inQuantity)
    {
        bool equipped = false;
        const auto item = FindItem(inId, &equipped);
        if (!connected || !ready || !pendingRequest.empty() || !item) return {};
        if (inOperation == InventoryOperation::Equip && (!item->equipmentSlot || equipped || !item->canEquip))
        {
            status = item->equipReason.empty() ? L"장착 조건을 충족하지 않습니다" : item->equipReason;
            return {};
        }
        if (inOperation == InventoryOperation::Unequip && !equipped) return {};
        if (inOperation == InventoryOperation::Discard && (equipped || inQuantity == 0 || inQuantity > item->quantity))
            return {};
        try { pendingRequest = CharacterInventoryJson::NewRequestId(); }
        catch (const std::exception&) { status = L"요청을 생성하지 못했습니다. 다시 시도해 주세요"; return {}; }
        pendingSeconds = 0.0f;
        status = L"서버 응답 대기 중… 아이템은 확정 응답 후 변경됩니다";
        return {inOperation, state.revision, pendingRequest, inId, inQuantity, false};
    }

    /** Keep 8x5 and all seven equipment cells visible; compact view puts detail text below both grids. */
    InventoryUi::Layout InventoryUi::CalculateLayout(const float inWidth, const float inHeight) const
    {
        Layout result;
        result.compact = inWidth < 960.0f;
        const float width = std::min(1120.0f, inWidth - 24.0f), height = std::min(620.0f, inHeight - 24.0f);
        const float left = (inWidth - width) * 0.5f, top = (inHeight - height) * 0.5f;
        result.panel = D2D1::RectF(left, top, left + width, top + height);
        const float x = left + 12.0f, right = left + width - 12.0f;
        const float gridTop = top + 76.0f, bottom = top + height - 30.0f;
        const float detailHeight = result.compact ? std::clamp(height * 0.20f, 64.0f, 108.0f) : 0.0f;
        const float gridBottom = result.compact ? bottom - detailHeight - 8.0f : bottom;
        constexpr float GAP = 4.0f;
        const float cell = std::min(result.compact ? 48.0f : 56.0f,
            std::min((gridBottom - gridTop - 4.0f * GAP) / 5.0f,
                (width - (result.compact ? 200.0f : 430.0f) - 7.0f * GAP) / 8.0f));
        const float bagWidth = 8.0f * cell + 7.0f * GAP;
        for (std::size_t index = 0; index < result.bags.size(); ++index)
        {
            const float slotX = x + (index % 8) * (cell + GAP), slotY = gridTop + (index / 8) * (cell + GAP);
            result.bags[index] = D2D1::RectF(slotX, slotY, slotX + cell, slotY + cell);
        }
        for (std::size_t index = 0; index < result.tabs.size(); ++index)
        {
            const float tabX = x + index * bagWidth / 4.0f;
            result.tabs[index] = D2D1::RectF(tabX, gridTop - 32.0f, tabX + bagWidth / 4.0f - 2.0f, gridTop - 6.0f);
        }
        const float equipmentLeft = x + bagWidth + 16.0f;
        const float equipmentWidth = result.compact ? right - equipmentLeft : 176.0f;
        const float equipmentHeight = std::min(76.0f, (gridBottom - gridTop - 3.0f * GAP) / 4.0f);
        for (std::size_t index = 0; index < result.equipment.size(); ++index)
        {
            const float slotX = equipmentLeft + (index % 2) * (equipmentWidth + GAP) / 2.0f;
            const float slotY = gridTop + (index / 2) * (equipmentHeight + GAP);
            result.equipment[index] = D2D1::RectF(slotX, slotY,
                slotX + (equipmentWidth - GAP) / 2.0f, slotY + equipmentHeight);
        }
        result.detail = result.compact ? D2D1::RectF(x, bottom - detailHeight, right, bottom)
            : D2D1::RectF(equipmentLeft + equipmentWidth + 16.0f, gridTop, right, bottom);
        if (result.compact)
        {
            result.detailText = result.detail;
            result.detailText.right -= 96.0f;
            result.discard = D2D1::RectF(right - 90.0f, result.detail.top, right, result.detail.top + 28.0f);
            result.use = D2D1::RectF(right - 90.0f, result.detail.top + 32.0f, right - 47.0f, result.detail.top + 60.0f);
            result.sell = D2D1::RectF(right - 43.0f, result.detail.top + 32.0f, right, result.detail.top + 60.0f);
        }
        else
        {
            result.detailText = result.detail;
            result.detailText.bottom -= 72.0f;
            result.discard = D2D1::RectF(result.detail.left, bottom - 64.0f, right, bottom - 36.0f);
            const float middle = (result.detail.left + right) * 0.5f;
            result.use = D2D1::RectF(result.detail.left, bottom - 28.0f, middle - 2.0f, bottom);
            result.sell = D2D1::RectF(middle + 2.0f, bottom - 28.0f, right, bottom);
        }
        result.status = D2D1::RectF(x, bottom + 6.0f, right, top + height - 4.0f);
        return result;
    }

    std::optional<std::uint32_t> InventoryUi::DiscardQuantity() const
    {
        std::string digits;
        if (quantityText.empty() || quantityText.size() > 10) return std::nullopt;
        for (const auto value : quantityText)
        {
            if (value < L'0' || value > L'9') return std::nullopt;
            digits.push_back(static_cast<char>(value));
        }
        std::uint32_t quantity{};
        const auto result = std::from_chars(digits.data(), digits.data() + digits.size(), quantity);
        const auto item = FindItem(discardItem);
        if (result.ec != std::errc{} || result.ptr != digits.data() + digits.size() || !item
            || quantity == 0 || quantity > item->quantity) return std::nullopt;
        return quantity;
    }

    InventoryUiAction InventoryUi::Update(const float inDeltaSeconds, const InputState& inInput,
        const float inWidth, const float inHeight, const bool inVisible)
    {
        InventoryUiAction action;
        if (!pendingRequest.empty())
        {
            pendingSeconds += std::max(0.0f, inDeltaSeconds);
            if (pendingSeconds >= REQUEST_TIMEOUT_SECONDS)
            {
                pendingRequest.clear();
                ready = stateRequested = false;
                Close();
                status = L"처리 결과를 확인하지 못했습니다. 상태를 다시 요청합니다";
            }
        }
        if (inInput.cancelDrag) { Close(); return action; }
        if (!inVisible) { Close(); return action; }
        if (connected && !ready && stateRequested)
        {
            stateWaitSeconds += std::max(0.0f, inDeltaSeconds);
            if (stateWaitSeconds >= REQUEST_TIMEOUT_SECONDS) stateRequested = false;
        }
        if (connected && !ready && !stateRequested)
        {
            stateRequested = true;
            stateWaitSeconds = 0.0f;
            try { stateRequestId = CharacterInventoryJson::NewRequestId(); }
            catch (const std::exception&) { stateRequested = false; status = L"조회 요청을 만들지 못했습니다"; return action; }
            assembly.reset();
            action.requestId = stateRequestId;
            action.requestState = true;
            status = L"최신 인벤토리와 아이템 정의를 확인하는 중… 변경할 수 없습니다";
        }
        if (discardStage != DiscardStage::Closed)
        {
            const auto dialog = Dialog(inWidth, inHeight);
            const float middle = (dialog.left + dialog.right) * 0.5f;
            const auto cancel = D2D1::RectF(dialog.left + 16.0f, dialog.bottom - 48.0f, middle - 8.0f, dialog.bottom - 16.0f);
            const auto accept = D2D1::RectF(middle + 8.0f, dialog.bottom - 48.0f, dialog.right - 16.0f, dialog.bottom - 16.0f);
            if (discardStage == DiscardStage::Quantity)
                for (const auto value : inInput.textInput)
                    if (value == L'\b' && !quantityText.empty()) quantityText.pop_back();
                    else if (value >= L'0' && value <= L'9' && quantityText.size() < 10) quantityText += value;
            if (inInput.leftMousePressed)
            {
                if (Contains(cancel, inInput.clickX, inInput.clickY)) Close();
                else if (Contains(accept, inInput.clickX, inInput.clickY) && DiscardQuantity())
                {
                    if (discardStage == DiscardStage::Quantity) discardStage = DiscardStage::Confirmation;
                    else
                    {
                        action = BeginRequest(InventoryOperation::Discard, discardItem, *DiscardQuantity());
                        Close();
                    }
                }
            }
            return action; // Confirmation never shares clicks or Enter with the page underneath.
        }
        const auto layout = CalculateLayout(inWidth, inHeight);
        if (inInput.leftMousePressed)
        {
            for (std::size_t index = 0; index < layout.tabs.size(); ++index)
                if (Contains(layout.tabs[index], inInput.clickX, inInput.clickY))
                {
                    tab = index;
                    selected.clear();
                    detailScroll = 0.0f;
                    return action;
                }
            for (std::size_t index = 0; index < layout.bags.size(); ++index)
                if (Contains(layout.bags[index], inInput.clickX, inInput.clickY))
                {
                    selected = state.bags[tab][index] ? state.bags[tab][index]->instanceId : std::string{};
                    detailScroll = 0.0f;
                }
            for (std::size_t index = 0; index < layout.equipment.size(); ++index)
                if (Contains(layout.equipment[index], inInput.clickX, inInput.clickY))
                {
                    selected = state.equipment[index] ? state.equipment[index]->instanceId : std::string{};
                    detailScroll = 0.0f;
                }
            bool equipped = false;
            if (FindItem(selected, &equipped) && !equipped && ready && connected && pendingRequest.empty()
                && Contains(layout.discard, inInput.clickX, inInput.clickY))
            {
                discardItem = selected;
                quantityText = L"1";
                discardStage = DiscardStage::Quantity;
            }
        }
        if (discardStage != DiscardStage::Closed) return action;
        if (inInput.rightMousePressed && ready && connected && pendingRequest.empty())
        {
            if (tab == 0)
                for (std::size_t index = 0; index < layout.bags.size(); ++index)
                    if (Contains(layout.bags[index], inInput.rightClickX, inInput.rightClickY) && state.bags[0][index])
                    {
                        selected = state.bags[0][index]->instanceId;
                        return BeginRequest(InventoryOperation::Equip, selected, 1);
                    }
            for (std::size_t index = 0; index < layout.equipment.size(); ++index)
                if (Contains(layout.equipment[index], inInput.rightClickX, inInput.rightClickY) && state.equipment[index])
                {
                    selected = state.equipment[index]->instanceId;
                    return BeginRequest(InventoryOperation::Unequip, selected, 1);
                }
        }
        const auto detail = Details();
        const float columns = std::max(1.0f, (layout.detailText.right - layout.detailText.left) / 14.0f);
        const float textHeight = (detail.size() / columns + std::count(detail.begin(), detail.end(), L'\n') + 1) * 20.0f;
        const float maxScroll = std::max(0.0f, textHeight - (layout.detailText.bottom - layout.detailText.top));
        detailScroll = std::clamp(detailScroll, 0.0f, maxScroll);
        if (Contains(layout.detailText, inInput.mouseX, inInput.mouseY) && inInput.mouseWheelDelta)
            detailScroll = std::clamp(detailScroll - inInput.mouseWheelDelta / 120.0f * 40.0f, 0.0f, maxScroll);
        return action;
    }

    std::wstring InventoryUi::Details() const
    {
        bool equipped = false;
        const auto item = FindItem(selected, &equipped);
        if (!item) return L"아이템을 선택하면 상세 정보가 표시됩니다";
        std::wstring text = item->name + L"\n" + item->description + L"\n";
        if (item->equipmentSlot)
        {
            text += L"부위: " + std::wstring(EQUIPMENT_NAMES[static_cast<std::size_t>(*item->equipmentSlot)])
                + (equipped ? L" (장착 중)\n" : L"\n");
            text += L"장착 조건: " + (item->requirements.empty() ? std::wstring(L"없음") : item->requirements) + L"\n";
            if (!equipped && !item->canEquip) text += item->equipReason + L"\n";
        }
        else text += L"수량: " + std::to_wstring(item->quantity) + L" / 최대 스택 " + std::to_wstring(item->maxStack) + L"\n";
        return text + L"사용·판매·아이템 효과는 아직 지원하지 않습니다";
    }

    void InventoryUi::DrawItem(D2DRenderer& inRenderer, const D2D1_RECT_F& inRect,
        const InventoryItemView* inItem, const std::wstring_view inLabel) const
    {
        inRenderer.FillRectangle(inRect.left, inRect.top, inRect.right, inRect.bottom, D2D1::ColorF(0.07f, 0.09f, 0.12f));
        inRenderer.DrawRectangle(inRect.left, inRect.top, inRect.right, inRect.bottom,
            D2D1::ColorF(inItem && inItem->instanceId == selected ? 1.0f : 0.40f,
                inItem && inItem->instanceId == selected ? 0.75f : 0.47f, 0.52f));
        D2D1_RECT_F art = inRect;
        art.left += 3.0f; art.right -= 3.0f; art.top += 3.0f; art.bottom -= 3.0f;
        if (!inLabel.empty())
        {
            inRenderer.DrawUiText(inLabel, D2D1::RectF(inRect.left, inRect.top, inRect.right, inRect.top + 15.0f),
                D2D1::ColorF(0.80f, 0.83f, 0.90f), 11.0f, false, true);
            art.top += 13.0f;
        }
        if (!inItem) return;
        const float size = std::min(art.right - art.left, art.bottom - art.top);
        art.left = (art.left + art.right - size) * 0.5f;
        art.right = art.left + size;
        art.bottom = art.top + size;
        const auto icon = icons.find(inItem->icon);
        if (icon != icons.end() && icon->second)
            inRenderer.DrawUiIcon(icon->second.Get(), art, !ready);
        else inRenderer.DrawUiText(inItem->name.substr(0, 2), art, D2D1::ColorF(0.90f, 0.90f, 0.92f), 12.0f, false, true);
        if (!inItem->equipmentSlot)
        {
            const auto count = D2D1::RectF(inRect.left + 1.0f, inRect.bottom - 14.0f, inRect.right - 1.0f, inRect.bottom);
            inRenderer.FillRectangle(count.left, count.top, count.right, count.bottom, D2D1::ColorF(0.02f, 0.03f, 0.05f, 0.90f));
            inRenderer.DrawUiText(std::to_wstring(inItem->quantity), count, D2D1::ColorF(1.0f, 1.0f, 0.92f), 11.0f, false, true);
        }
    }

    void InventoryUi::Render(D2DRenderer& inRenderer, const float inWidth, const float inHeight,
        const bool inDungeon) const
    {
        const auto layout = CalculateLayout(inWidth, inHeight);
        inRenderer.FillRectangle(layout.panel.left, layout.panel.top, layout.panel.right, layout.panel.bottom,
            D2D1::ColorF(0.035f, 0.05f, 0.075f, 0.98f));
        inRenderer.DrawUiText(inDungeon ? L"인벤토리 · 던전 전투는 계속됩니다" : L"인벤토리",
            D2D1::RectF(layout.panel.left + 12.0f, layout.panel.top + 8.0f, layout.panel.right - 120.0f, layout.panel.top + 38.0f),
            D2D1::ColorF(0.96f, 0.91f, 0.80f), layout.compact ? 16.0f : 20.0f);
        inRenderer.DrawUiText(L"ESC 메뉴로", D2D1::RectF(layout.panel.right - 116.0f, layout.panel.top + 10.0f,
            layout.panel.right - 12.0f, layout.panel.top + 36.0f), D2D1::ColorF(0.80f, 0.82f, 0.87f), 13.0f, false, true);
        for (std::size_t index = 0; index < layout.tabs.size(); ++index)
        {
            Button(inRenderer, layout.tabs[index], CATEGORY_NAMES[index], true);
            if (tab == index) inRenderer.DrawRectangle(layout.tabs[index].left, layout.tabs[index].top,
                layout.tabs[index].right, layout.tabs[index].bottom, D2D1::ColorF(1.0f, 0.75f, 0.28f), 2.0f);
        }
        for (std::size_t index = 0; index < layout.bags.size(); ++index)
            DrawItem(inRenderer, layout.bags[index], state.bags[tab][index] ? &*state.bags[tab][index] : nullptr);
        for (std::size_t index = 0; index < layout.equipment.size(); ++index)
            DrawItem(inRenderer, layout.equipment[index], state.equipment[index] ? &*state.equipment[index] : nullptr, EQUIPMENT_NAMES[index]);
        inRenderer.PushAxisAlignedClip(layout.detailText);
        const auto detail = Details();
        auto textRect = layout.detailText;
        textRect.top -= detailScroll;
        const float columns = std::max(1.0f, (textRect.right - textRect.left) / 14.0f);
        textRect.bottom = textRect.top + (detail.size() / columns + std::count(detail.begin(), detail.end(), L'\n') + 1) * 20.0f;
        inRenderer.DrawUiText(detail, textRect, D2D1::ColorF(0.92f, 0.92f, 0.94f), 14.0f, true);
        inRenderer.PopAxisAlignedClip();
        bool equipped = false;
        const auto item = FindItem(selected, &equipped);
        Button(inRenderer, layout.discard, L"버리기", connected && ready && pendingRequest.empty() && item && !equipped);
        Button(inRenderer, layout.use, L"사용", false);
        Button(inRenderer, layout.sell, L"판매", false);
        inRenderer.DrawUiText(status,
            layout.status, D2D1::ColorF(0.84f, 0.80f, 0.68f), 12.0f);
        if (discardStage == DiscardStage::Closed) return;
        inRenderer.FillRectangle(0.0f, 0.0f, inWidth, inHeight, D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.75f));
        const auto dialog = Dialog(inWidth, inHeight);
        inRenderer.FillRectangle(dialog.left, dialog.top, dialog.right, dialog.bottom, D2D1::ColorF(0.09f, 0.10f, 0.14f));
        const auto discard = FindItem(discardItem);
        const auto text = discardStage == DiscardStage::Quantity
            ? L"버릴 수량을 입력하세요\n" + (discard ? discard->name : std::wstring{}) + L"\n보유 수량: "
                + std::to_wstring(discard ? discard->quantity : 0) + L"\n수량: " + quantityText
            : L"영구 삭제 확인\n" + (discard ? discard->name : std::wstring{}) + L" × " + quantityText
                + L"\n버린 아이템은 복구할 수 없습니다.\n정말 버리시겠습니까?";
        inRenderer.DrawUiText(text, D2D1::RectF(dialog.left + 16.0f, dialog.top + 16.0f, dialog.right - 16.0f, dialog.bottom - 60.0f),
            D2D1::ColorF(0.96f, 0.87f, 0.80f), 16.0f, true);
        const float middle = (dialog.left + dialog.right) * 0.5f;
        Button(inRenderer, D2D1::RectF(dialog.left + 16.0f, dialog.bottom - 48.0f, middle - 8.0f, dialog.bottom - 16.0f), L"취소 (ESC)", true);
        Button(inRenderer, D2D1::RectF(middle + 8.0f, dialog.bottom - 48.0f, dialog.right - 16.0f, dialog.bottom - 16.0f),
            discardStage == DiscardStage::Quantity ? L"다음" : L"영구 삭제 요청", connected && ready && pendingRequest.empty() && DiscardQuantity().has_value());
    }
}
