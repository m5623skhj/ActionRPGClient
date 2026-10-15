#pragma once

#include <nlohmann/json.hpp>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace ActionRPG
{
    struct QuestObjectiveView
    {
        std::string id, description;
        std::uint32_t current{}, required{};
        bool done{};
    };
    struct QuestItemAmount { std::string definitionId; std::uint32_t count{}; };
    struct QuestEntryView
    {
        std::string id, cycleKey, state, blockedReason;
        std::uint32_t definitionVersion{}, stageIndex{};
        std::uint64_t acceptGeneration{};
        nlohmann::json definition;
        std::vector<QuestObjectiveView> objectives;
        std::vector<QuestItemAmount> turnInItems;
        bool canAccept{}, canAbandon{}, canReport{}, canClaim{};
    };
    struct QuestNpcView
    {
        std::string id, name, mapId;
        float x{}, y{};
        bool canAccept{}, canReport{}, canClaim{};
    };
    struct QuestSnapshotView
    {
        std::uint64_t characterId{}, revision{}, serverNowMs{};
        std::string snapshotId;
        std::vector<std::string> trackedQuestIds;
        std::vector<QuestEntryView> quests;
        std::vector<QuestNpcView> npcs;
        std::unordered_map<std::string, nlohmann::json> itemDefinitions;
    };

    // Game-thread-only DTO assembly. Never expose a partial snapshot to rendering or actions.
    class QuestState final
    {
    public:
        void Reset(std::uint64_t inCharacterId);
        void Invalidate();
        void RequireRevision(std::uint64_t inRevision);
        [[nodiscard]] bool ApplyPage(std::string_view inPayload);
        [[nodiscard]] bool IsReady() const { return ready; }
        [[nodiscard]] bool IsAssembling() const { return assembly.has_value(); }
        [[nodiscard]] const QuestSnapshotView& Get() const { return snapshot; }
        [[nodiscard]] std::uint64_t GetCharacterId() const { return characterId; }
        [[nodiscard]] static nlohmann::json ParseJson(std::string_view inPayload, std::size_t inLimit);
    private:
        struct Assembly
        {
            std::string requestId, snapshotId;
            std::uint64_t revision{}, serverNowMs{};
            std::size_t bytes{};
            std::vector<std::string> tracked;
            std::vector<std::optional<nlohmann::json>> pages;
        };
        std::uint64_t characterId{}, minimumRevision{};
        bool ready{};
        QuestSnapshotView snapshot;
        std::optional<Assembly> assembly;
        std::vector<std::string> retiredSnapshots;
    };
}
