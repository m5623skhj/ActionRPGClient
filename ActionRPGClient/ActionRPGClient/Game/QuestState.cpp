#include "Game/QuestState.h"
#include "Network/CharacterInventoryJson.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <stdexcept>
#include <unordered_set>

namespace ActionRPG
{
    namespace
    {
        using Json = nlohmann::json;
        constexpr std::size_t MAX_QUEST_ITEM_DEFINITIONS = 8192;
        using namespace CharacterInventoryJson;
        void Require(bool inCondition)
        { if (!inCondition) throw std::runtime_error("Invalid quest state."); }
        std::string Text(const Json& inValue, std::size_t inMaximum, bool inAllowEmpty = false)
        {
            Require(inValue.is_string());
            const auto text = inValue.get<std::string>();
            Require((inAllowEmpty || !text.empty()) && text.size() <= inMaximum);
            Require(text.find('\0') == std::string::npos);
            Require(text.empty() || MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                static_cast<int>(text.size()), nullptr, 0) > 0);
            return text;
        }
        bool Flag(const Json& inValue) { Require(inValue.is_boolean()); return inValue.get<bool>(); }
        void Array(const Json& inValue, std::size_t inMaximum)
        { Require(inValue.is_array() && inValue.size() <= inMaximum); }
        std::vector<std::string> Tracking(const Json& inValue)
        {
            Array(inValue, 3);
            std::vector<std::string> result;
            for (const auto& id : inValue)
            {
                const auto text = Text(id, 64);
                Require(std::find(result.begin(), result.end(), text) == result.end());
                result.push_back(text);
            }
            return result;
        }
        void Definition(const Json& inDefinition)
        {
            (void)Text(inDefinition.at("title"), 1024);
            (void)Text(inDefinition.at("description"), 1024, true);
            for (const auto* key : {"accept", "complete"})
            {
                const auto& rule = inDefinition.at(key);
                const auto mode = Text(rule.at("mode"), 16);
                Require(key == std::string_view("accept") ? (mode == "Automatic" || mode == "List" || mode == "Npc")
                    : (mode == "Automatic" || mode == "NpcReport"));
                if (!rule.at("npcId").is_null()) (void)Text(rule.at("npcId"), 64);
            }
            const auto& accept = inDefinition.at("accept");
            (void)UInt32(accept.at("minLevel"), 1, 1000000);
            for (const auto* key : {"prerequisites", "mapIds"})
            { Array(accept.at(key), 8); for (const auto& id : accept.at(key)) (void)Text(id, 64); }
            std::unordered_set<std::string> ids;
            for (const auto& step : inDefinition.at("steps"))
            {
                (void)Text(step.at("id"), 64);
                for (const auto& objective : step.at("objectives"))
                {
                    Require(ids.insert(Text(objective.at("id"), 64)).second);
                    if (objective.contains("description")) (void)Text(objective.at("description"), 256, true);
                    (void)UInt32(objective.at("count"), 1, 1000000);
                    const auto type = Text(objective.at("type"), 32);
                    const auto& target = objective.at("target");
                    if (type == "KillMonster") (void)UInt32(target.at("monsterDataId"), 1, 1000000);
                    else if (type == "ClearDungeon") (void)UInt32(target.at("dungeonId"), 1, 1000000);
                    else if (type == "HoldItem" || type == "AcquireItem") (void)Text(target.at("itemDefinitionId"), 64);
                    else if (type == "TalkNpc") (void)Text(target.at("npcId"), 64);
                    else if (type == "Reach") { (void)Text(target.at("mapId"), 64); if (target.contains("region")) (void)Text(target.at("region").at("id"), 64); }
                    else Require(false);
                }
            }
            const auto rewards = [](const Json& rows)
            {
                Array(rows, 8);
                for (const auto& reward : rows)
                {
                    const auto type = Text(reward.at("type"), 16);
                    if (type == "Item") { (void)Text(reward.at("definitionId"), 64); (void)UInt32(reward.at("count"), 1, 1000000); }
                    else { Require(type == "SkillPoints"); (void)UInt32(reward.at("amount"), 1, 1000000); }
                }
            };
            rewards(inDefinition.at("fixedRewards")); Array(inDefinition.at("choiceRewards"), 8);
            ids.clear();
            for (const auto& choice : inDefinition.at("choiceRewards"))
            { Require(ids.insert(Text(choice.at("id"), 64)).second); rewards(choice.at("rewards")); }
            const auto& event = inDefinition.at("event");
            if (!event.is_null())
                for (const auto* key : {"visibility", "acceptance", "progress", "reward"})
                    for (const auto* end : {"startUtc", "endUtc"}) (void)Text(event.at(key).at(end), 32);
        }
        QuestEntryView Entry(const Json& inRow)
        {
            QuestEntryView result;
            result.id = Text(inRow.at("questId"), 64);
            result.cycleKey = Text(inRow.at("cycleKey"), 32);
            result.definitionVersion = UInt32(inRow.at("definitionVersion"), 1, UINT32_MAX);
            result.acceptGeneration = UInt64(inRow.at("acceptGeneration"));
            result.state = Text(inRow.at("state"), 32);
            constexpr const char* STATES[]{"Available", "Unavailable", "Active", "ReadyToReport", "RewardPending",
                "Completed", "Abandoned", "Expired"};
            Require(std::find(std::begin(STATES), std::end(STATES), result.state) != std::end(STATES));
            result.stageIndex = UInt32(inRow.at("stageIndex"), 0, 8);
            result.definition = inRow.at("definition");
            Require(result.definition.is_object() && result.definition.dump().size() <= 4096);
            Require(result.definition.at("id") == result.id && result.definition.at("version") == result.definitionVersion);
            const auto& stages = result.definition.at("steps");
            Array(stages, 8); Require(!stages.empty() && result.stageIndex <= stages.size());
            for (const auto& stage : stages)
            { Array(stage.at("objectives"), 8); Require(!stage.at("objectives").empty()); }
            const auto category = Text(result.definition.at("category"), 16);
            Require(category == "Scenario" || category == "Normal" || category == "Event");
            const auto repeat = Text(result.definition.at("repeat"), 16);
            Require(repeat == "Once" || repeat == "Daily" || repeat == "Weekly");
            Definition(result.definition);
            Array(inRow.at("objectives"), 8);
            std::unordered_set<std::string> ids;
            for (const auto& objective : inRow.at("objectives"))
            {
                QuestObjectiveView view;
                view.id = Text(objective.at("id"), 64);
                if (objective.contains("description")) view.description = Text(objective.at("description"), 256, true);
                view.current = UInt32(objective.at("current"), 0, UINT32_MAX);
                view.required = UInt32(objective.at("required"), 1, UINT32_MAX);
                view.done = Flag(objective.at("done"));
                Require(ids.insert(view.id).second);
                result.objectives.push_back(std::move(view));
            }
            Array(inRow.at("turnInItems"), 64);
            ids.clear();
            for (const auto& item : inRow.at("turnInItems"))
            {
                QuestItemAmount amount{Text(item.at("definitionId"), 64), UInt32(item.at("count"), 1, UINT32_MAX)};
                Require(ids.insert(amount.definitionId).second);
                result.turnInItems.push_back(std::move(amount));
            }
            result.canAccept = Flag(inRow.at("canAccept"));
            result.canAbandon = Flag(inRow.at("canAbandon"));
            result.canReport = Flag(inRow.at("canReport"));
            result.canClaim = Flag(inRow.at("canClaim"));
            result.blockedReason = Text(inRow.at("blockedReason"), 256, true);
            return result;
        }
    }

    nlohmann::json QuestState::ParseJson(std::string_view inPayload, std::size_t inLimit)
    {
        Require(!inPayload.empty() && inPayload.size() <= inLimit);
        auto result = Json::parse(inPayload, [](int depth, Json::parse_event_t, Json&)
        { Require(depth <= 32); return true; });
        Require(result.is_object());
        return result;
    }
    void QuestState::Reset(std::uint64_t inCharacterId)
    {
        characterId = inCharacterId; minimumRevision = 0; snapshot = {};
        ready = false; assembly.reset(); retiredSnapshots.clear();
    }
    void QuestState::Invalidate() { ready = false; assembly.reset(); }
    void QuestState::RequireRevision(std::uint64_t inRevision)
    {
        minimumRevision = std::max(minimumRevision, inRevision);
        if (assembly && assembly->revision < minimumRevision) assembly.reset();
        ready = false;
    }

    /** Collect bounded pages by identity, reject mixed metadata, then atomically publish the complete DTO. */
    bool QuestState::ApplyPage(std::string_view inPayload)
    {
        const auto value = ParseJson(inPayload, 14 * 1024);
        if (UInt64(value.at("characterId")) != characterId || characterId == 0) return false;
        const auto revision = UInt64(value.at("revision"));
        const auto serverNow = UInt64(value.at("serverNowMs"));
        const auto request = Text(value.at("requestId"), 64, true);
        if (!request.empty()) (void)HexId(value.at("requestId"), 64);
        const auto id = Text(value.at("snapshotId"), 128);
        const auto count = UInt32(value.at("pageCount"), 1, 48);
        const auto index = UInt32(value.at("pageIndex"), 0, count - 1);
        if (revision < minimumRevision || revision < snapshot.revision
            || (revision == snapshot.revision && serverNow < snapshot.serverNowMs)
            || std::find(retiredSnapshots.begin(), retiredSnapshots.end(), id) != retiredSnapshots.end()) return false;
        const auto tracked = Tracking(value.at("trackedQuestIds"));
        if (!assembly || assembly->snapshotId != id)
        {
            if (assembly && (revision < assembly->revision
                || (revision == assembly->revision && serverNow < assembly->serverNowMs))) return false;
            if (assembly)
            {
                retiredSnapshots.push_back(assembly->snapshotId);
                if (retiredSnapshots.size() > 32) retiredSnapshots.erase(retiredSnapshots.begin());
            }
            ready = false;
            assembly = Assembly{request, id, revision, serverNow, 0, tracked,
                std::vector<std::optional<Json>>(count)};
        }
        Require(assembly->requestId == request && assembly->revision == revision
            && assembly->serverNowMs == serverNow && assembly->pages.size() == count && assembly->tracked == tracked);
        if (assembly->pages[index]) { Require(*assembly->pages[index] == value); return false; }
        Require(assembly->bytes + inPayload.size() <= 512 * 1024);
        Array(value.at("quests"), 80); Array(value.at("npcs"), 32); Array(value.at("itemDefinitions"), MAX_QUEST_ITEM_DEFINITIONS);
        assembly->bytes += inPayload.size(); assembly->pages[index] = value;
        if (std::any_of(assembly->pages.begin(), assembly->pages.end(), [](const auto& page) { return !page; })) return false;

        QuestSnapshotView next;
        next.characterId = characterId; next.revision = revision; next.serverNowMs = serverNow;
        next.snapshotId = id; next.trackedQuestIds = tracked;
        std::unordered_set<std::string> keys, definitions;
        for (const auto& page : assembly->pages)
        {
            for (const auto& row : page->at("quests"))
            {
                auto entry = Entry(row);
                Require(keys.insert(entry.id + ":" + entry.cycleKey).second);
                definitions.insert(entry.id);
                Require(next.quests.size() < 80 && definitions.size() <= 80);
                next.quests.push_back(std::move(entry));
            }
            for (const auto& npc : page->at("npcs"))
            {
                const auto npcId = Text(npc.at("id"), 64);
                const auto previous = std::find_if(next.npcs.begin(), next.npcs.end(), [&](const auto& n) { return n.id == npcId; });
                QuestNpcView view{npcId, Text(npc.at("name"), 256), Text(npc.at("mapId"), 64)};
                Require(npc.at("x").is_number() && npc.at("y").is_number());
                view.x = npc.at("x").get<float>(); view.y = npc.at("y").get<float>();
                Require(std::isfinite(view.x) && std::isfinite(view.y));
                view.canAccept = Flag(npc.at("canAccept")); view.canReport = Flag(npc.at("canReport")); view.canClaim = Flag(npc.at("canClaim"));
                if (previous != next.npcs.end())
                {
                    Require(previous->name == view.name && previous->mapId == view.mapId && previous->x == view.x
                        && previous->y == view.y && previous->canAccept == view.canAccept
                        && previous->canReport == view.canReport && previous->canClaim == view.canClaim);
                }
                else { Require(next.npcs.size() < 32); next.npcs.push_back(std::move(view)); }
            }
            for (const auto& definition : page->at("itemDefinitions"))
            {
                Require(definition.is_object() && definition.dump().size() <= 8192);
                const auto itemId = Text(definition.at("id"), 64);
                (void)Text(definition.at("name"), 512);
                const auto icon = Text(definition.at("icon"), 256, true);
                Require(icon.empty() || (icon.starts_with("Images/") && icon.find("..") == std::string::npos
                    && icon.find(':') == std::string::npos && icon.find('\\') == std::string::npos));
                const auto [found, inserted] = next.itemDefinitions.emplace(itemId, definition);
                Require(inserted || found->second == definition); Require(next.itemDefinitions.size() <= MAX_QUEST_ITEM_DEFINITIONS);
            }
        }
        for (const auto& entry : next.quests)
        {
            for (const auto& item : entry.turnInItems) Require(next.itemDefinitions.contains(item.definitionId));
            const auto rewards = [&](const Json& rows)
            { for (const auto& row : rows) if (row.at("type") == "Item") Require(next.itemDefinitions.contains(row.at("definitionId").get<std::string>())); };
            rewards(entry.definition.at("fixedRewards"));
            for (const auto& choice : entry.definition.at("choiceRewards")) rewards(choice.at("rewards"));
        }
        for (const auto& track : tracked)
            Require(std::any_of(next.quests.begin(), next.quests.end(), [&](const auto& q) { return q.id == track; }));
        snapshot = std::move(next); ready = true; assembly.reset();
        retiredSnapshots.push_back(id);
        if (retiredSnapshots.size() > 32) retiredSnapshots.erase(retiredSnapshots.begin());
        return true;
    }
}
