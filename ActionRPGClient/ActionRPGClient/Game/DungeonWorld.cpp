#include "Game/DungeonWorld.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include "Network/CharacterInventoryJson.h"
#include <cmath>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace ActionRPG
{
    namespace
    {
        float Number(const nlohmann::json& inValue)
        {
            const float value = inValue.get<float>();
            if (!std::isfinite(value) || std::abs(value) > 1000000) throw std::runtime_error("Invalid dungeon coordinate.");
            return value;
        }
        float ActorNumber(const nlohmann::json& inValue)
        {
            if (!inValue.is_number()) throw std::runtime_error("Invalid actor number.");
            const float value = inValue.get<float>();
            if ((inValue.get<double>() > 0.0 && value == 0.0f) || !std::isfinite(value) || value < 0.0f) throw std::runtime_error("Invalid actor number.");
            return value;
        }
        Vector2 Position(const nlohmann::json& inValue)
        {
            return { Number(inValue.at("x")), Number(inValue.at("y")) };
        }
        TownProtocol::Polygon Polygon(const nlohmann::json& inValue)
        {
            if (!inValue.is_array() || inValue.size() < 3 || inValue.size() > 64)
                throw std::runtime_error("Invalid dungeon polygon.");
            TownProtocol::Polygon polygon;
            for (const auto& value : inValue)
            {
                const auto point = Position(value); polygon.push_back({point.x, point.y});
            }
            return polygon;
        }
    }
    DungeonWorld DungeonWorld::Parse(const std::string_view inJson, const std::uint64_t inRoomId,
        const std::uint64_t inPlayerId, const MonsterCatalog& inMonsters)
    {
        const auto world = nlohmann::json::parse(inJson);
        if (world.at("version") != 1 || world.at("roomId") != inRoomId
            || !world.at("maps").is_object() || world.at("maps").empty() || world.at("maps").size() > 256)
            throw std::runtime_error("Invalid dungeon world.");
        DungeonWorld result;
        result.combatRules = CombatRules::Parse(world.at("combatRules").dump());
        const auto& slides = world.at("combatRules").at("slideDefinitions");
        if (!slides.is_array() || slides.empty() || slides.size() > 256)
            throw std::runtime_error("Invalid slide definitions.");
        for (const auto& source : slides)
        {
            const auto& idValue = source.at("characterId");
            if (!idValue.is_number_integer() || idValue < 1 || idValue > 4)
                throw std::runtime_error("Unsupported slide character.");
            const auto id = idValue.get<std::uint32_t>();
            const auto& attack = source.at("attackPower");
            if (!attack.is_number_integer() || attack < 1 || attack > 1000000)
                throw std::runtime_error("Invalid character attack power.");
            SlideDefinition definition{Number(source.at("durationSeconds")), ActorNumber(source.at("distancePerRunSpeedSeconds")),
                ActorNumber(source.at("hitRecovery")), ActorNumber(source.at("hitstopSeconds")), attack.get<std::uint32_t>(),
                source.at("motionId").get<std::string>(),
                ActorNumber(source.at("hitStartRatio")), ActorNumber(source.at("hitEndRatio"))};
            if (definition.durationSeconds < 0.05f || definition.durationSeconds > 2.0f
                || definition.distancePerRunSpeedSeconds < 0.05f || definition.distancePerRunSpeedSeconds > 5.0f
                || definition.hitStartRatio >= definition.hitEndRatio || definition.hitEndRatio > 1.0f
                || definition.motionId != (id==4 ? "dashAttack":"slide") || !result.slideDefinitions.emplace(id, std::move(definition)).second)
                throw std::runtime_error("Invalid slide definition.");
        }
        for (std::uint32_t id = 1; id <= 3; ++id)
            if (!result.slideDefinitions.contains(id)) throw std::runtime_error("Missing slide character.");
        if (result.slideDefinitions.contains(4) != result.combatRules.meleeDefinitions.contains(4))
            throw std::runtime_error("Missing Character4 melee contract.");
        if (world.contains("playerSkills")) result.playerSkills = PlayerSkills::Catalog::Parse(world.at("playerSkills"));
        result.entryMapId = world.at("entryMapId").get<std::string>();
        const auto& initial = world.at("players").at(std::to_string(inPlayerId));
        result.spawn = Position(initial);
        if (initial.contains("hp"))
        {
            auto& hud = result.initialPlayerHud.emplace();
            hud.playerId = inPlayerId; hud.hasHudState = true;
            if (initial.contains("characterId"))
            {
                hud.characterId = CharacterInventoryJson::UInt32(initial.at("characterId"), 1, 4);
                if (!result.slideDefinitions.contains(hud.characterId)) throw std::runtime_error("Missing initial character contract.");
            }
            hud.hp = CharacterInventoryJson::UInt32(initial.at("hp"), 0, UINT32_MAX);
            hud.maxHp = CharacterInventoryJson::UInt32(initial.at("maxHp"), 1, UINT32_MAX);
            if (initial.at("mp").is_null() != initial.at("maxMp").is_null()) throw std::runtime_error("Invalid initial MP.");
            hud.mpKnown = !initial.at("mp").is_null();
            if (hud.mpKnown)
            {
                hud.mp = CharacterInventoryJson::UInt32(initial.at("mp"), 0, UINT32_MAX);
                hud.maxMp = CharacterInventoryJson::UInt32(initial.at("maxMp"), 0, UINT32_MAX);
            }
            const auto id = [](const nlohmann::json& value)
            {
                if (!value.is_number_integer() || (!value.is_number_unsigned() && value.get<std::int64_t>() < 0))
                    throw std::runtime_error("Invalid initial hit identity.");
                return value.get<std::uint64_t>();
            };
            hud.lastHitSequence = id(initial.at("lastHitSequence")); hud.lastHitMonsterId = id(initial.at("lastHitMonsterId"));
            if (hud.hp > hud.maxHp || hud.mp > hud.maxMp || (hud.lastHitMonsterId != 0 && hud.lastHitSequence == 0))
                throw std::runtime_error("Invalid initial resources.");
        }
        std::unordered_set<std::uint64_t> monsterIds;
        for (const auto& [id, source] : world.at("maps").items())
        {
            if (source.at("version") != 5 || source.at("format") != "DungeonRoom" || source.at("mapId") != id
                || id.empty() || id.size() > 64) throw std::runtime_error("Invalid dungeon room.");
            DungeonMap room;
            auto& map = room.map; map.mapId = id;
            const auto& bounds = source.at("world");
            map.worldLeft = Number(bounds.at("left")); map.worldTop = Number(bounds.at("top"));
            map.worldRight = Number(bounds.at("right")); map.worldBottom = Number(bounds.at("bottom"));
            if (map.worldLeft >= map.worldRight || map.worldTop >= map.worldBottom)
                throw std::runtime_error("Invalid dungeon bounds.");
            map.sectorWidth = Number(source.at("sectorWidth")); map.sectorHeight = Number(source.at("sectorHeight"));
            if (map.sectorWidth <= 0 || map.sectorHeight <= 0) throw std::runtime_error("Invalid dungeon sector.");
            const auto point = Position(source.at("spawn")); map.spawnX = point.x; map.spawnY = point.y;
            for (const auto& image : source.at("images"))
                map.images.push_back({image.at("asset").get<std::string>(), Number(image.at("x")),
                    Number(image.at("y")), Number(image.at("width")), Number(image.at("height"))});
            for (const auto& polygon : source.at("walkablePolygons")) map.walkablePolygons.push_back(Polygon(polygon));
            for (const auto& polygon : source.at("blockedPolygons")) map.blockedPolygons.push_back(Polygon(polygon));
            for (const auto& entry : source.at("entryPoints"))
            {
                const auto position = Position(entry.at("position"));
                map.entryPoints.push_back({entry.at("id").get<std::string>(), {position.x, position.y}});
            }
            for (const auto& zone : source.at("transitionZones"))
            {
                TownProtocol::TransitionZone transition;
                transition.id = zone.at("id").get<std::string>(); transition.polygon = Polygon(zone.at("polygon"));
                transition.targetMapId = zone.at("action").at("targetMapId").get<std::string>();
                transition.targetEntryPointId = zone.at("action").at("targetEntryPointId").get<std::string>();
                map.transitionZones.push_back(std::move(transition));
            }
            if (!source.at("monsters").is_array() || source.at("monsters").size() > 256)
                throw std::runtime_error("Invalid monster list.");
            for (const auto& monster : source.at("monsters"))
            {
                MonsterSpawn spawn;
                spawn.instanceId = monster.at("instanceId").get<std::uint64_t>();
                spawn.dataId = monster.at("dataId").get<std::uint32_t>();
                spawn.position = Position(monster.at("position"));
                spawn.facingLeft = monster.at("facingLeft").get<bool>();
                spawn.hp = monster.at("hp").get<std::uint32_t>(); spawn.maxHp = monster.at("maxHp").get<std::uint32_t>();
                if (monster.contains("name"))
                {
                    spawn.name = monster.at("name").get<std::string>(); spawn.isBoss = monster.at("isBoss").get<bool>();
                    if (!monster.at("level").is_null())
                    {
                        const auto& level = monster.at("level");
                        if (!level.is_number_integer() || level <= 0 || level > UINT32_MAX) throw std::runtime_error("Invalid monster level.");
                        spawn.level = level.get<std::uint32_t>();
                    }
                    if (spawn.name.empty() || spawn.name.size() > 128
                        || std::any_of(spawn.name.begin(), spawn.name.end(), [](unsigned char c) { return c < 32 || c == 127; }))
                        throw std::runtime_error("Invalid monster name.");
                }
                if (!inMonsters.Contains(spawn.dataId) || spawn.instanceId == 0 || !monsterIds.insert(spawn.instanceId).second
                    || spawn.maxHp == 0 || spawn.hp > spawn.maxHp) throw std::runtime_error("Invalid monster instance.");
                room.monsters.push_back(spawn);
            }
            result.maps.emplace(id, std::move(room));
        }
        if (monsterIds.size() > 4096) throw std::runtime_error("Too many monster instances.");
        (void)result.GetMap(result.entryMapId);
        return result;
    }
}
