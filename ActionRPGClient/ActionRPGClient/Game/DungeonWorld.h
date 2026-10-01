#pragma once
#include "Game/Monster.h"
#include "Network/TownProtocol.h"
#include <string>
#include <string_view>
#include <unordered_map>

namespace ActionRPG
{
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
            std::uint64_t inPlayerId);
        [[nodiscard]] const DungeonMap& GetMap(const std::string& inId) const { return maps.at(inId); }
        [[nodiscard]] const std::string& GetEntryMapId() const { return entryMapId; }
        [[nodiscard]] Vector2 GetSpawn() const { return spawn; }
    private:
        std::unordered_map<std::string, DungeonMap> maps;
        std::string entryMapId;
        Vector2 spawn{};
    };
}
