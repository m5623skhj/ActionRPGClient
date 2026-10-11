#include "Game/InventoryUi.h"
#include "Graphics/D2DRenderer.h"
#include "Resources/AssetCatalog.h"
#include "Network/CharacterInventoryJson.h"
#include "Network/AuthSettings.h"

#include <d2d1_1helper.h>

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <sstream>
#include <iomanip>
#include <utility>

namespace
{
    constexpr std::array<const wchar_t*, 4> CATEGORY_NAMES{L"장비", L"재료", L"소모품", L"퀘스트"};
    constexpr std::array<const wchar_t*, 7> EQUIPMENT_NAMES{L"무기", L"상의", L"하의", L"신발", L"반지", L"목걸이", L"팔찌"};
    constexpr float REQUEST_TIMEOUT_SECONDS = 10.0f;
    constexpr float USE_STATUS_INTERVAL_SECONDS = 1.0f;

    bool IsItemId(const std::string& inId)
    {
        const auto letter = [](char value) { return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z'); };
        return !inId.empty() && inId.size() <= 64 && letter(inId.front())
            && std::all_of(inId.begin(), inId.end(), [&](char value)
                { return letter(value) || (value >= '0' && value <= '9') || value == '_' || value == '.' || value == '-'; })
            && inId != "__proto__" && inId != "prototype" && inId != "constructor";
    }

    std::wstring SecondsText(float inSeconds)
    {
        std::wostringstream text;
        text << std::fixed << std::setprecision(1) << std::max(0.0f, inSeconds);
        return text.str();
    }

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

    float TextHeight(const std::wstring& inText, const float inWidth)
    {
        // Use a full-width glyph estimate, consistent with the existing 14px UI text.
        const float columns = std::max(1.0f, inWidth / 14.0f);
        return (static_cast<float>(inText.size()) / columns
            + static_cast<float>(std::count(inText.begin(), inText.end(), L'\n')) + 1.0f) * 22.0f;
    }
}

namespace ActionRPG
{
    InventoryUi::InventoryUi(const AssetCatalog& inAssets, D2DRenderer& inRenderer)
        : assets(inAssets), renderer(inRenderer) {}

    void InventoryUi::Reset()
    {
        state = {};
        cooldowns.clear(); cooldownsKnown = combatRefresh = useStatusPending = useOutcomeUncertain = false;
        useStatusSeconds = cooldownRefreshSeconds = 0.0f;
        useNotice.clear();
        assembly.reset();
        expectedCharacterId = latestServerRevision = characterDefinitionId = characterLevel = 0;
        stateRequestId.clear();
        hovered.clear();
        pendingRequest.clear();
        pendingOperation = InventoryOperation::None;
        ready = connected = stateRequested = false;
        pendingSeconds = stateWaitSeconds = tooltipScroll = 0.0f;
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

    // Validate the whole snapshot before replacing any trusted cooldown state.
    std::optional<InventoryUi::CooldownMap> InventoryUi::ParseCooldowns(const nlohmann::json& inBody)
    {
        using namespace CharacterInventoryJson;
        if (!inBody.contains("cooldownsComplete")) return std::nullopt;
        if (!inBody.at("cooldownsComplete").is_boolean()) throw std::runtime_error("Invalid cooldown completeness.");
        if (!inBody.at("cooldownsComplete").get<bool>())
        {
            if (!inBody.at("cooldowns").is_null()) throw std::runtime_error("Incomplete cooldown snapshot must be null.");
            return std::nullopt;
        }
        const auto& rows = inBody.at("cooldowns");
        if (!rows.is_array()) throw std::runtime_error("Invalid cooldown snapshot.");
        CooldownMap next;
        for (const auto& row : rows)
        {
            const auto id = row.at("definitionId").get<std::string>();
            CooldownView value;
            value.serverTimeMs = UInt64(row.at("cooldownServerTimeMs"));
            const auto state = row.at("cooldownState").get<std::string>();
            if (state == "Pending")
            {
                value.state = CooldownState::Pending;
                if (!row.at("cooldownReadyAtMs").is_null() || !row.at("cooldownRemainingMs").is_null())
                    throw std::runtime_error("Pending cooldown has no final deadline.");
            }
            else if (state == "Active")
            {
                value.state = CooldownState::Active;
                value.readyAtMs = UInt64(row.at("cooldownReadyAtMs"));
                const auto remaining = UInt32(row.at("cooldownRemainingMs"), 0, 86400000);
                if ((remaining > 0 && value.readyAtMs <= value.serverTimeMs)
                    || (value.readyAtMs > value.serverTimeMs && value.readyAtMs - value.serverTimeMs > 86400000))
                    throw std::runtime_error("Invalid cooldown deadline.");
                value.remainingSeconds = static_cast<float>(std::min<std::uint64_t>(remaining,
                    value.readyAtMs > value.serverTimeMs ? value.readyAtMs - value.serverTimeMs : 0)) / 1000.0f;
            }
            else if (state == "None")
            {
                if (!row.at("cooldownReadyAtMs").is_null() || UInt32(row.at("cooldownRemainingMs"), 0, 0) != 0)
                    throw std::runtime_error("Invalid empty cooldown.");
            }
            else throw std::runtime_error("Unknown cooldown state.");
            if (!IsItemId(id) || !next.emplace(id, value).second)
                throw std::runtime_error("Invalid or duplicate cooldown definition.");
        }
        return next;
    }

    void InventoryUi::ApplyCooldowns(std::optional<CooldownMap> inCooldowns)
    {
        if (!inCooldowns) { cooldownsKnown = false; return; }
        // TCP events are ordered. Still retain newer per-definition server samples.
        for (auto& [id, value] : *inCooldowns)
            if (const auto found = cooldowns.find(id); found != cooldowns.end() && (found->second.serverTimeMs > value.serverTimeMs
                || (found->second.serverTimeMs == value.serverTimeMs && found->second.state == value.state
                    && found->second.readyAtMs == value.readyAtMs)))
                value = found->second;
        cooldowns = std::move(*inCooldowns);
        cooldownsKnown = true;
        cooldownRefreshSeconds = 0.0f;
    }

    const InventoryUi::CooldownView* InventoryUi::FindCooldown(const InventoryItemView& inItem) const
    {
        const auto found = cooldowns.find(inItem.definitionId);
        return found == cooldowns.end() ? nullptr : &found->second;
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
        if (id == 0 || id != expectedCharacterId || revision < state.revision || revision < latestServerRevision) return;
        const auto batchCount = UInt32(body.at("definitionBatchCount"), 0, 167);
        const auto& inventory = body.at("inventory");
        if (!inventory.is_object() || !inventory.at("items").is_array()
            || inventory.at("items").size() > 167) throw std::runtime_error("Invalid inventory state.");
        auto nextCooldowns = ParseCooldowns(body);
        assembly = Assembly{request, id, revision, inventory,
            std::vector<std::optional<Json>>(batchCount), std::move(nextCooldowns)};
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
        if (assembly->revision < latestServerRevision)
        {
            assembly.reset();
            stateRequested = false;
            return;
        }
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
            item.definitionId = definitionId;
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
            if (category == 2 && definition.contains("effects"))
            {
                const auto& effects = definition.at("effects");
                if (!effects.is_array() || effects.size() > 1) throw std::runtime_error("Invalid consumable effects.");
                for (const auto& effect : effects)
                {
                    const auto type = effect.at("type").get<std::string>();
                    if (type != "RestoreHp" && type != "Throw") throw std::runtime_error("Unsupported item use effect.");
                    item.throwItem = type == "Throw";
                    item.useRequiredLevel = UInt32(definition.at("useRequiredLevel"), 1, 1000000);
                    const auto& cooldown = definition.at("useCooldownSeconds");
                    if (!cooldown.is_number() || !std::isfinite(cooldown.get<float>())
                        || cooldown < 0 || cooldown > 86400) throw std::runtime_error("Invalid item cooldown definition.");
                    item.useCooldownSeconds = cooldown.get<float>();
                }
            }
            const auto container = UInt32(row.at("container"), 0, 4);
            const auto slot = UInt32(row.at("slot"), 0, container == 4 ? 6 : 39);
            if (container != 4 && container != category) throw std::runtime_error("Item category mismatch.");
            auto& destination = container == 4 ? next.equipment[slot] : next.bags[container][slot];
            if (destination) throw std::runtime_error("Duplicate inventory slot.");
            destination = std::move(item);
        }
        auto nextCooldowns = std::move(assembly->cooldowns);
        ApplyState(std::move(next));
        ApplyCooldowns(std::move(nextCooldowns));
        assembly.reset();
    }

    bool InventoryUi::HandleOperation(const std::string_view inPayload)
    {
        using namespace CharacterInventoryJson;
        const auto body = Json::parse(inPayload);
        const auto request = HexId(body.at("requestId"), 64);
        const auto id = UInt64(body.at("characterId"));
        const auto revision = UInt64(body.at("revision"));
        if (request != pendingRequest || id != expectedCharacterId || revision < latestServerRevision) return false;
        auto nextCooldowns = ParseCooldowns(body);
        const auto result = body.at("result").get<std::string>();
        const bool isUse = pendingOperation == InventoryOperation::Use;
        if (isUse)
        {
            if (body.contains("inventory"))
            {
                const auto& inventory = body.at("inventory");
                if (!inventory.is_object() || !inventory.at("items").is_array() || inventory.at("items").size() > 167)
                    throw std::runtime_error("Invalid operation inventory snapshot.");
            }
            const auto action = body.at("action").get<std::string>();
            if (action == "UseStatus") { useStatusPending = false; useStatusSeconds = 0.0f; }
            const auto useState = body.at("useState").get<std::string>();
            if ((action != "Use" && action != "UseStatus")
                || (useState != "None" && useState != "Pending" && useState != "Applied"
                    && useState != "Cancelled" && useState != "ConsumedUnknown"))
                throw std::runtime_error("Invalid use result.");
            if ((result == "ConsumedUnknown" && useState != "ConsumedUnknown")
                || (result == "Cancelled" && useState != "Cancelled")
                || (result == "Succeeded" && useState != "Applied"))
                throw std::runtime_error("Inconsistent use result.");
            latestServerRevision = std::max(latestServerRevision, revision);
            const bool definiteRejection = result == "InvalidRequest" || result == "InvalidQuantity"
                || result == "NotImplemented" || result == "RevisionConflict" || result == "Cooldown"
                || result == "LevelRestricted" || result == "NotInDungeon" || result == "Dead"
                || result == "FullHealth" || result == "ActionUnavailable" || result == "ItemNotFound"
                || result == "NotUsable" || result == "UseLedgerFull";
            // A successful status lookup with no receipt is explicit non-execution.
            // A bare rejection only completes the original, still-certain pre-reserve Use.
            // After Pending, timeout or disconnect, require a terminal receipt or a successful no-receipt query.
            const bool initialRejection = action == "Use" && !useOutcomeUncertain && definiteRejection;
            const bool noReceipt = action == "UseStatus" && result == "None" && useState == "None"
                && body.contains("inventory");
            const bool terminal = result == "Succeeded" || result == "Cancelled" || result == "ConsumedUnknown";
            if (result != "Pending" && useState != "Pending" && !terminal
                && !(useState == "None" && (initialRejection || noReceipt)))
            {
                useOutcomeUncertain = true;
                ApplyCooldowns(std::move(nextCooldowns));
                status = result == "StaleOwner"
                    ? L"캐릭터 소유권을 확인하지 못했습니다. 사용 결과는 미확정이며 재접속 후 캐릭터를 다시 선택해 주세요"
                    : L"아이템 사용 결과를 확인하지 못했습니다. 같은 요청의 상태를 다시 조회합니다";
                return false;
            }
            if (result == "Pending" || useState == "Pending")
            {
                useOutcomeUncertain = true;
                ApplyCooldowns(std::move(nextCooldowns));
                status = useState == "ConsumedUnknown"
                    ? L"아이템은 소비됐으나 효과 확인 불가… 서버 정리 완료를 기다립니다"
                    : L"아이템 사용 처리 중… 최종 결과를 기다립니다";
                return false;
            }
        }
        std::wstring text = L"요청이 거절됐습니다. 상태를 다시 확인합니다";
        if (result == "Succeeded") text = L"처리됐습니다. 서버 확정 상태를 확인합니다";
        else if (result == "Cancelled") text = L"사용이 취소됐습니다. 서버 확정 아이템 상태를 확인합니다";
        else if (result == "ConsumedUnknown") text = L"아이템은 소비됐으나 효과 적용 여부를 확인할 수 없습니다";
        else if (result == "RevisionConflict") text = L"상태가 변경됐습니다. 최신 상태를 받는 중";
        else if (result == "InventoryFull") text = L"장비 가방에 빈 칸이 없습니다";
        else if (result == "EquipmentRequirements" || result == "LevelRequirement" || result == "LevelRestricted") text = L"필요 레벨 또는 사용 조건을 충족하지 않습니다";
        else if (result == "InvalidQuantity") text = L"수량을 확인해 주세요";
        else if (result == "NotImplemented") text = L"아직 지원하지 않는 기능입니다";
        else if (result == "NotUsable") text = L"사용할 수 없는 아이템입니다";
        else if (result == "None") text = L"해당 사용 기록이 없습니다. 서버 아이템 상태를 확인합니다";
        else if (result == "Cooldown") text = L"아이템 재사용 대기 중입니다";
        else if (result == "NotInDungeon") text = L"던전 안에서만 사용할 수 있습니다";
        else if (result == "Dead") text = L"사망 상태에서는 사용할 수 없습니다";
        else if (result == "FullHealth") text = L"HP가 가득 차 있어 사용하지 않았습니다";
        else if (result == "ActionUnavailable") text = L"현재 동작 중에는 투척할 수 없습니다";
        else if (result == "Busy") text = L"서버가 이전 작업을 처리 중입니다";
        else if (result == "UseLedgerFull") text = L"현재 던전의 아이템 사용 기록 한도에 도달했습니다";
        if (isUse)
        {
            useNotice = text;
            combatRefresh = result == "Succeeded" || result == "ConsumedUnknown";
        }
        ApplyCooldowns(std::move(nextCooldowns));
        const bool unchanged = !isUse && (result == "InventoryFull" || result == "EquipmentRequirements"
            || result == "InvalidQuantity" || result == "NotImplemented");
        ResolveRequest(request, std::move(text), unchanged);
        return result == "RevisionConflict";
    }

    void InventoryUi::Close()
    {
        hovered.clear();
        tooltipScroll = 0.0f;
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
            if (pendingOperation == InventoryOperation::Use && !pendingRequest.empty()) useOutcomeUncertain = true;
            useStatusPending = false;
            ready = stateRequested = false;
            if (pendingOperation != InventoryOperation::Use)
            {
                pendingRequest.clear();
                pendingOperation = InventoryOperation::None;
            }
            assembly.reset();
            Close();
            status = pendingRequest.empty()
                ? L"연결이 끊겼습니다. 표시된 상태는 다시 확인해야 합니다"
                : L"연결이 끊겼습니다. 아이템 사용 결과는 아직 확인되지 않았습니다";
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
            hovered.clear();
            pendingRequest.clear();
            pendingOperation = InventoryOperation::None;
            icons.clear();
        }
        if (!sameCharacter || inState.revision != state.revision) Close();
        state = std::move(inState);
        ready = true;
        stateRequested = false;
        if (!FindItem(hovered)) hovered.clear();
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
        if (pendingRequest.empty()) status = useNotice.empty()
            ? L"마우스 오버: 정보 / 우클릭: 사용·장착·해제 / 휠: 정보 스크롤" : useNotice;
    }

    void InventoryUi::ResolveRequest(const std::string& inRequestId, std::wstring inMessage,
        const bool inStateCurrent)
    {
        if (inRequestId.empty() || pendingRequest != inRequestId) return;
        pendingRequest.clear();
        pendingOperation = InventoryOperation::None;
        pendingSeconds = 0.0f;
        useStatusPending = useOutcomeUncertain = false;
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
        if (inOperation == InventoryOperation::Use && (equipped || item->equipmentSlot || inQuantity != 1 || item->quantity == 0))
            return {};
        if (inOperation == InventoryOperation::Use)
        {
            if (!cooldownsKnown) { status = L"아이템 재사용 상태를 확인하는 중입니다"; return {}; }
            if (item->useRequiredLevel > characterLevel) { status = L"아이템 사용에 필요한 레벨이 부족합니다"; return {}; }
            const auto cooldown = FindCooldown(*item);
            if (cooldown && (cooldown->state == CooldownState::Pending || cooldown->remainingSeconds > 0.0f))
            { status = L"아이템 사용 처리 중이거나 재사용 대기 중입니다"; return {}; }
        }
        try { pendingRequest = CharacterInventoryJson::NewRequestId(); }
        catch (const std::exception&) { status = L"요청을 생성하지 못했습니다. 다시 시도해 주세요"; return {}; }
        pendingOperation = inOperation;
        pendingSeconds = useStatusSeconds = 0.0f;
        useStatusPending = useOutcomeUncertain = false;
        useNotice.clear();
        status = L"서버 응답 대기 중… 아이템은 확정 응답 후 변경됩니다";
        InventoryUiAction action{inOperation, state.revision, pendingRequest, inId, inQuantity, false};
        action.throwItem = item->throwItem;
        return action;
    }

    /** Keep 8x5 bags and seven equipment cells visible without reserving a fixed detail panel. */
    InventoryUi::Layout InventoryUi::CalculateLayout(const float inWidth, const float inHeight) const
    {
        Layout result;
        result.compact = inWidth < 960.0f;
        const float width = std::min(1120.0f, inWidth - 24.0f), height = std::min(620.0f, inHeight - 24.0f);
        const float left = (inWidth - width) * 0.5f, top = (inHeight - height) * 0.5f;
        result.panel = D2D1::RectF(left, top, left + width, top + height);
        const float x = left + 12.0f, right = left + width - 12.0f;
        const float gridTop = top + 76.0f, bottom = top + height - 30.0f;
        constexpr float GAP = 4.0f;
        const float cell = std::min(64.0f, std::min((bottom - gridTop - 4.0f * GAP) / 5.0f,
            (width - 200.0f - 7.0f * GAP) / 8.0f));
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
        const float equipmentWidth = std::min(216.0f, right - equipmentLeft);
        const float equipmentHeight = std::min(84.0f, (bottom - gridTop - 3.0f * GAP) / 4.0f);
        for (std::size_t index = 0; index < result.equipment.size(); ++index)
        {
            const float slotX = equipmentLeft + (index % 2) * (equipmentWidth + GAP) / 2.0f;
            const float slotY = gridTop + (index / 2) * (equipmentHeight + GAP);
            result.equipment[index] = D2D1::RectF(slotX, slotY,
                slotX + (equipmentWidth - GAP) / 2.0f, slotY + equipmentHeight);
        }
        result.status = D2D1::RectF(x, bottom + 6.0f, right, top + height - 4.0f);
        return result;
    }

    D2D1_RECT_F InventoryUi::TooltipBounds(const float inWidth, const float inHeight) const
    {
        constexpr float MARGIN = 12.0f;
        const float width = std::min(360.0f, inWidth - 2.0f * MARGIN);
        const float height = std::min(std::min(440.0f, inHeight - 2.0f * MARGIN),
            std::max(120.0f, TextHeight(Details(), width - 24.0f) + 48.0f));
        float left = hoveredRect.right + 8.0f, top = hoveredRect.top;
        if (left + width > inWidth - MARGIN)
        {
            if (hoveredRect.left - 8.0f - width >= MARGIN) left = hoveredRect.left - 8.0f - width;
            else
            {
                left = mouseX + 16.0f;
                if (hoveredRect.bottom + 8.0f + height <= inHeight - MARGIN) top = hoveredRect.bottom + 8.0f;
                else if (hoveredRect.top - 8.0f - height >= MARGIN) top = hoveredRect.top - 8.0f - height;
            }
        }
        left = std::clamp(left, MARGIN, inWidth - MARGIN - width);
        top = std::clamp(top, MARGIN, inHeight - MARGIN - height);
        return D2D1::RectF(left, top, left + width, top + height);
    }

    InventoryUiAction InventoryUi::Update(const float inDeltaSeconds, const InputState& inInput,
        const float inWidth, const float inHeight, const bool inVisible)
    {
        InventoryUiAction action;
        const float elapsed = std::max(0.0f, inDeltaSeconds);
        for (auto& [id, cooldown] : cooldowns)
            if (cooldown.state == CooldownState::Active) cooldown.remainingSeconds = std::max(0.0f, cooldown.remainingSeconds - elapsed);
        cooldownRefreshSeconds += elapsed;
        if (connected && pendingOperation == InventoryOperation::Use && !pendingRequest.empty())
        {
            useStatusSeconds += elapsed;
            if (useStatusSeconds >= (useStatusPending ? REQUEST_TIMEOUT_SECONDS : USE_STATUS_INTERVAL_SECONDS))
            {
                action.requestUseStatus = true;
                action.useStatusRequestId = pendingRequest;
                useStatusSeconds = 0.0f;
                useStatusPending = true;
            }
        }
        if (!pendingRequest.empty() && pendingSeconds < REQUEST_TIMEOUT_SECONDS)
        {
            pendingSeconds += std::max(0.0f, inDeltaSeconds);
            if (pendingSeconds >= REQUEST_TIMEOUT_SECONDS)
            {
                if (pendingOperation == InventoryOperation::Use)
                {
                    // Keep the original identity locked: timeout does not prove non-execution.
                    useOutcomeUncertain = true;
                    status = L"아이템 사용 결과가 아직 확인되지 않았습니다";
                }
                else
                {
                    pendingRequest.clear();
                    pendingOperation = InventoryOperation::None;
                    ready = stateRequested = false;
                    Close();
                    status = L"처리 결과를 확인하지 못했습니다. 상태를 다시 요청합니다";
                }
            }
        }
        if (inInput.cancelDrag) { Close(); return action; }
        if (!inVisible) { Close(); return action; }
        if (connected && inVisible && !stateRequested && pendingRequest.empty()
            && ((!cooldownsKnown && cooldownRefreshSeconds >= USE_STATUS_INTERVAL_SECONDS)
                || cooldownRefreshSeconds >= REQUEST_TIMEOUT_SECONDS))
            ready = false;
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
        const auto layout = CalculateLayout(inWidth, inHeight);
        if (inInput.leftMousePressed)
            for (std::size_t index = 0; index < layout.tabs.size(); ++index)
                if (Contains(layout.tabs[index], inInput.clickX, inInput.clickY))
                {
                    tab = index;
                    Close();
                    return action;
                }

        mouseX = inInput.mouseX;
        std::string nextHover;
        for (std::size_t index = 0; index < layout.bags.size(); ++index)
            if (Contains(layout.bags[index], inInput.mouseX, inInput.mouseY) && state.bags[tab][index])
            {
                nextHover = state.bags[tab][index]->instanceId;
                hoveredRect = layout.bags[index];
            }
        for (std::size_t index = 0; index < layout.equipment.size(); ++index)
            if (Contains(layout.equipment[index], inInput.mouseX, inInput.mouseY) && state.equipment[index])
            {
                nextHover = state.equipment[index]->instanceId;
                hoveredRect = layout.equipment[index];
            }
        if (hovered != nextHover) tooltipScroll = 0.0f;
        hovered = std::move(nextHover);
        if (!hovered.empty())
        {
            const auto tooltip = TooltipBounds(inWidth, inHeight);
            const float maxScroll = std::max(0.0f, TextHeight(Details(), tooltip.right - tooltip.left - 24.0f)
                - (tooltip.bottom - tooltip.top - 48.0f));
            tooltipScroll = std::clamp(tooltipScroll - inInput.mouseWheelDelta / 120.0f * 40.0f, 0.0f, maxScroll);
        }
        if (inInput.rightMousePressed && ready && connected && pendingRequest.empty())
        {
            for (std::size_t index = 0; index < layout.bags.size(); ++index)
                if (Contains(layout.bags[index], inInput.rightClickX, inInput.rightClickY) && state.bags[tab][index])
                    return BeginRequest(tab == 0 ? InventoryOperation::Equip : InventoryOperation::Use,
                        state.bags[tab][index]->instanceId, 1);
            for (std::size_t index = 0; index < layout.equipment.size(); ++index)
                if (Contains(layout.equipment[index], inInput.rightClickX, inInput.rightClickY) && state.equipment[index])
                    return BeginRequest(InventoryOperation::Unequip, state.equipment[index]->instanceId, 1);
        }
        return action;
    }

    std::wstring InventoryUi::Details() const
    {
        bool equipped = false;
        const auto item = FindItem(hovered, &equipped);
        if (!item) return {};
        std::wstring text = item->name + L"\n" + item->description + L"\n";
        if (item->equipmentSlot)
        {
            text += L"부위: " + std::wstring(EQUIPMENT_NAMES[static_cast<std::size_t>(*item->equipmentSlot)])
                + (equipped ? L" (장착 중)\n" : L"\n");
            text += L"장착 조건: " + (item->requirements.empty() ? std::wstring(L"없음") : item->requirements) + L"\n";
            if (!equipped && !item->canEquip) text += item->equipReason + L"\n";
        }
        text += L"수량: " + std::to_wstring(item->quantity) + L" / 최대 스택 " + std::to_wstring(item->maxStack) + L"\n";
        if (item->useRequiredLevel != 0)
            text += L"사용 레벨: " + std::to_wstring(item->useRequiredLevel)
                + L"\n기본 쿨타임: " + SecondsText(item->useCooldownSeconds) + L"초\n";
        if (item->useRequiredLevel > characterLevel) text += L"사용에 필요한 레벨이 부족합니다\n";
        if (!item->equipmentSlot && !cooldownsKnown) text += L"재사용 상태 확인 중\n";
        else if (const auto cooldown = FindCooldown(*item))
        {
            if (cooldown->state == CooldownState::Pending) text += L"사용 처리 중 (쿨타임 미확정)\n";
            else if (cooldown->remainingSeconds > 0.0f)
                text += L"재사용 대기: " + SecondsText(cooldown->remainingSeconds) + L"초\n";
        }
        if (!connected) return text + L"연결이 끊겼습니다";
        if (!ready) return text + L"최신 상태 확인 중";
        if (!pendingRequest.empty()) return text + L"서버 응답 대기 중";
        return text + (equipped ? L"우클릭: 해제" : item->equipmentSlot ? L"우클릭: 장착" : L"우클릭: 1개 사용");
    }

    void InventoryUi::DrawItem(D2DRenderer& inRenderer, const D2D1_RECT_F& inRect,
        const InventoryItemView* inItem, const std::wstring_view inLabel) const
    {
        inRenderer.FillRectangle(inRect.left, inRect.top, inRect.right, inRect.bottom, D2D1::ColorF(0.07f, 0.09f, 0.12f));
        inRenderer.DrawRectangle(inRect.left, inRect.top, inRect.right, inRect.bottom,
            D2D1::ColorF(inItem && inItem->instanceId == hovered ? 1.0f : 0.40f,
                inItem && inItem->instanceId == hovered ? 0.75f : 0.47f, 0.52f));
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
        const auto cooldown = FindCooldown(*inItem);
        const bool cooling = cooldown && (cooldown->state == CooldownState::Pending || cooldown->remainingSeconds > 0.0f);
        const bool unusable = !ready || !connected || (!inItem->equipmentSlot && (!cooldownsKnown
            || cooling || inItem->useRequiredLevel > characterLevel));
        const auto icon = icons.find(inItem->icon);
        if (icon != icons.end() && icon->second)
            inRenderer.DrawUiIcon(icon->second.Get(), art, unusable);
        else inRenderer.DrawUiText(inItem->name.substr(0, 2), art, D2D1::ColorF(0.90f, 0.90f, 0.92f), 12.0f, false, true);
        if (!inItem->equipmentSlot)
        {
            const auto count = D2D1::RectF(inRect.left + 1.0f, inRect.bottom - 14.0f, inRect.right - 1.0f, inRect.bottom);
            inRenderer.FillRectangle(count.left, count.top, count.right, count.bottom, D2D1::ColorF(0.02f, 0.03f, 0.05f, 0.90f));
            inRenderer.DrawUiText(std::to_wstring(inItem->quantity), count, D2D1::ColorF(1.0f, 1.0f, 0.92f), 11.0f, false, true);
        }
        if (cooling)
        {
            const auto label = cooldown->state == CooldownState::Pending
                ? std::wstring(L"처리 중") : SecondsText(cooldown->remainingSeconds);
            const auto rect = D2D1::RectF(inRect.left + 2.0f, art.top + 2.0f, inRect.right - 2.0f, art.top + 18.0f);
            inRenderer.FillRectangle(rect.left, rect.top, rect.right, rect.bottom, D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.8f));
            inRenderer.DrawUiText(label, rect, D2D1::ColorF(1.0f, 0.9f, 0.6f), 11.0f, false, true);
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
        inRenderer.DrawUiText(status, layout.status, D2D1::ColorF(0.84f, 0.80f, 0.68f), 12.0f);
        if (!FindItem(hovered)) return;
        const auto tooltip = TooltipBounds(inWidth, inHeight);
        inRenderer.FillRectangle(tooltip.left, tooltip.top, tooltip.right, tooltip.bottom,
            D2D1::ColorF(0.06f, 0.08f, 0.12f, 0.98f));
        inRenderer.DrawRectangle(tooltip.left, tooltip.top, tooltip.right, tooltip.bottom,
            D2D1::ColorF(0.75f, 0.66f, 0.43f));
        const auto content = D2D1::RectF(tooltip.left + 12.0f, tooltip.top + 12.0f,
            tooltip.right - 12.0f, tooltip.bottom - 36.0f);
        const auto text = Details();
        auto textRect = content;
        textRect.top -= tooltipScroll;
        textRect.bottom = textRect.top + TextHeight(text, content.right - content.left);
        inRenderer.PushAxisAlignedClip(content);
        inRenderer.DrawUiText(text, textRect, D2D1::ColorF(0.94f, 0.94f, 0.96f), 14.0f, true);
        inRenderer.PopAxisAlignedClip();
        inRenderer.DrawUiText(L"아이콘 위에서 휠: 정보 스크롤",
            D2D1::RectF(content.left, tooltip.bottom - 28.0f, content.right, tooltip.bottom - 8.0f),
            D2D1::ColorF(0.78f, 0.79f, 0.83f), 12.0f);
    }
}
