#pragma once
#include "Game/Monster.h"
#include "Game/DungeonCombat.h"
#include "Game/PlayerSkillCatalog.h"
#include "Network/TownProtocol.h"
#include <string>
#include <string_view>
#include <unordered_map>

namespace ActionRPG
{
    struct SlideDefinition
    {
        float durationSeconds{}, distancePerRunSpeedSeconds{}, hitRecovery{}, hitstopSeconds{};
        std::uint32_t attackPower{};
        std::string motionId;
    };
    struct DungeonMap
    {
        TownProtocol::MapInfo map;
        std::vector<MonsterSpawn> monsters;
    };
    // Parsed on the game thread after bounded, authenticated world streaming.
    class DungeonWorld final
    {
    public:
        [[nodiscard]] static DungeonWorld Parse(std::string_view inJson, std::uint64_t inRoomId,
            std::uint64_t inPlayerId, const MonsterCatalog& inMonsters);
        [[nodiscard]] const DungeonMap& GetMap(const std::string& inId) const { return maps.at(inId); }
        [[nodiscard]] const std::string& GetEntryMapId() const { return entryMapId; }
        [[nodiscard]] Vector2 GetSpawn() const { return spawn; }
        [[nodiscard]] const CombatRules& GetCombatRules() const { return combatRules; }
        [[nodiscard]] const SlideDefinition& GetSlideDefinition(std::uint32_t inCharacterId) const { return slideDefinitions.at(inCharacterId); }
        [[nodiscard]] const PlayerSkills::Catalog& GetPlayerSkills() const { return playerSkills; }
    private:
        std::unordered_map<std::string, DungeonMap> maps;
        std::string entryMapId;
        Vector2 spawn{};
        CombatRules combatRules;
        std::unordered_map<std::uint32_t, SlideDefinition> slideDefinitions;
        PlayerSkills::Catalog playerSkills;
    };
}
