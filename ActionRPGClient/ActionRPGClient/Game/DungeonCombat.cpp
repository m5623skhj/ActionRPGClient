#include "Game/DungeonCombat.h"
#include <nlohmann/json.hpp>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace
{
    using Json = nlohmann::json;
    float Number(const Json& inValue, const bool inSigned = false)
    {
        if (!inValue.is_number()) throw std::runtime_error("Invalid combat number.");
        const float value = inValue.get<float>();
        if (!std::isfinite(value) || std::abs(value) > 1000000.0f || (!inSigned && value < 0.0f))
            throw std::runtime_error("Invalid combat number.");
        return value;
    }
    std::uint64_t Id(const Json& inValue, const bool inAllowZero = false)
    {
        if (!inValue.is_number_integer() || (!inValue.is_number_unsigned() && inValue.get<std::int64_t>() < 0))
            throw std::runtime_error("Invalid combat ID.");
        const auto value = inValue.get<std::uint64_t>();
        if (!inAllowZero && value == 0) throw std::runtime_error("Zero combat ID.");
        return value;
    }
    std::uint32_t Integer(const Json& inValue)
    {
        const auto value = Id(inValue, true);
        if (value > std::numeric_limits<std::uint32_t>::max()) throw std::runtime_error("Invalid combat counter.");
        return static_cast<std::uint32_t>(value);
    }
    std::string Text(const Json& inValue, const bool inAllowEmpty = false)
    {
        const auto value = inValue.get<std::string>();
        if ((!inAllowEmpty && value.empty()) || value.size() > 128) throw std::runtime_error("Invalid combat text.");
        return value;
    }
    void List(const Json& inValue, const std::size_t inLimit)
    {
        if (!inValue.is_array() || inValue.size() > inLimit) throw std::runtime_error("Invalid combat list.");
    }
    ActionRPG::CombatActorState Actor(const Json& inActor)
    {
        ActionRPG::CombatActorState actor;
        actor.position = {Number(inActor.at("x"), true), Number(inActor.at("y"), true)};
        actor.hp = Integer(inActor.at("hp")); actor.maxHp = Integer(inActor.at("maxHp"));
        actor.height = Number(inActor.at("height")); actor.verticalSpeed = Number(inActor.at("verticalSpeed"), true);
        actor.reactionSeconds = Number(inActor.at("reactionSeconds")); actor.facingLeft = inActor.at("facingLeft").get<bool>();
        const auto reaction = Text(inActor.at("reaction"));
        using Reaction = ActionRPG::CombatReaction;
        if (reaction == "None") actor.reaction = Reaction::None;
        else if (reaction == "Hit") actor.reaction = Reaction::Hit;
        else if (reaction == "Falling") actor.reaction = Reaction::Falling;
        else if (reaction == "Down") actor.reaction = Reaction::Down;
        else if (reaction == "Rising") actor.reaction = Reaction::Rising;
        else if (reaction == "Dead") actor.reaction = Reaction::Dead;
        else throw std::runtime_error("Unknown combat reaction.");
        if (actor.maxHp == 0 || actor.hp > actor.maxHp || ((actor.hp == 0) != (actor.reaction == Reaction::Dead)))
            throw std::runtime_error("Invalid combat HP/reaction.");
        return actor;
    }
}

namespace ActionRPG
{
    CombatRules CombatRules::Parse(const std::string_view inJson)
    {
        const auto value = Json::parse(inJson);
        if (value.at("version") != 1) throw std::runtime_error("Unsupported combat rules.");
        CombatRules rules;
        rules.maxHp = Integer(value.at("maxHp")); rules.maxShots = Integer(value.at("maxShots"));
        if (rules.maxHp == 0 || rules.maxShots != 5) throw std::runtime_error("Unsupported combat limits.");
        const auto positive = [&value](const char* inName) {
            const float number = Number(value.at(inName));
            if (number == 0.0f) throw std::runtime_error("Invalid combat duration/speed.");
            return number;
        };
        rules.walkSpeed = positive("walkSpeed"); rules.runSpeed = positive("runSpeed");
        rules.shotPrepareSeconds = positive("shotPrepareSeconds"); rules.shotIntervalSeconds = positive("shotIntervalSeconds");
        rules.shotRecoverSeconds = positive("shotRecoverSeconds"); rules.jumpSpeed = positive("jumpSpeed");
        rules.gravity = positive("gravity"); rules.jumpPrepareSeconds = positive("jumpPrepareSeconds");
        rules.hitStunSeconds = positive("hitStunSeconds"); rules.downSeconds = positive("downSeconds");
        rules.riseSeconds = positive("riseSeconds"); rules.projectileSpeed = positive("projectileSpeed");
        rules.muzzleHeight = Number(value.at("muzzleHeight")); rules.airFireLift = Number(value.at("airFireLift"));
        rules.airRecoilDistance = Number(value.at("airRecoilDistance"));
        return rules;
    }

    DungeonCombatSnapshot DungeonCombatSnapshot::Parse(const std::string_view inJson)
    {
        if (inJson.size() > 512 * 1024) throw std::runtime_error("Combat snapshot too large.");
        const auto value = Json::parse(inJson);
        if (value.at("version") != 1) throw std::runtime_error("Unsupported combat snapshot.");
        DungeonCombatSnapshot result;
        result.roomId = Id(value.at("roomId")); result.serverTick = Id(value.at("serverTick"), true);
        result.mapId = Text(value.at("mapId")); result.state = Text(value.at("state"));
        result.cleared = value.at("cleared").get<bool>();
        if (result.mapId.size() > 64 || (result.state != "WaitingForPlayers" && result.state != "Running"
            && result.state != "Cleared" && result.state != "Stopped") || (result.state == "Cleared" && !result.cleared))
            throw std::runtime_error("Invalid dungeon combat state.");
        List(value.at("players"), 4); List(value.at("monsters"), 256); List(value.at("projectiles"), 256);
        std::unordered_set<std::uint64_t> ids;
        for (const auto& source : value.at("players"))
        {
            CombatPlayerState player;
            static_cast<CombatActorState&>(player) = Actor(source);
            player.playerId = Id(source.at("playerId"));
            if (!ids.insert(player.playerId).second) throw std::runtime_error("Duplicate combat player.");
            player.actionSequence = Integer(source.at("actionSequence")); player.moveSequence = Integer(source.at("moveSequence"));
            player.shotSeconds = Number(source.at("shotSeconds")); player.jumpSeconds = Number(source.at("jumpSeconds"));
            player.shotCount = Integer(source.at("shotCount")); player.airShotCount = Integer(source.at("airShotCount"));
            if (player.shotCount > 5 || player.airShotCount > 5) throw std::runtime_error("Invalid shot count.");
            player.airAttack = source.at("airAttack").get<bool>();
            const auto shot = Text(source.at("shotPhase")); const auto jump = Text(source.at("jumpPhase"));
            if (shot == "None") player.shotPhase = CombatShotPhase::None;
            else if (shot == "Prepare") player.shotPhase = CombatShotPhase::Prepare;
            else if (shot == "Fire") player.shotPhase = CombatShotPhase::Fire;
            else if (shot == "Recover") player.shotPhase = CombatShotPhase::Recover;
            else throw std::runtime_error("Unknown shot phase.");
            if (jump == "Grounded") player.jumpPhase = CombatJumpPhase::Grounded;
            else if (jump == "Prepare") player.jumpPhase = CombatJumpPhase::Prepare;
            else if (jump == "Airborne") player.jumpPhase = CombatJumpPhase::Airborne;
            else throw std::runtime_error("Unknown jump phase.");
            result.players.push_back(std::move(player));
        }
        ids.clear();
        for (const auto& source : value.at("monsters"))
        {
            CombatMonsterState monster;
            static_cast<CombatActorState&>(monster) = Actor(source);
            monster.instanceId = Id(source.at("instanceId")); monster.dataId = Integer(source.at("dataId"));
            if (monster.dataId == 0 || !ids.insert(monster.instanceId).second) throw std::runtime_error("Invalid combat monster ID.");
            monster.aiNodeId = Text(source.at("aiNodeId")); monster.actionType = Text(source.at("actionType"));
            monster.animationId = Text(source.at("animationId"), true);
            monster.actionSeconds = Number(source.at("actionSeconds"));
            monster.actionStarted = source.at("actionStarted").get<bool>(); monster.actionComplete = source.at("actionComplete").get<bool>();
            if (monster.actionType != "Wait" && monster.actionType != "MoveToTarget" && monster.actionType != "ReturnToSpawn"
                && monster.actionType != "UseSkill" && monster.actionType != "PlayMotion") throw std::runtime_error("Unknown monster action.");
            result.monsters.push_back(std::move(monster));
        }
        ids.clear();
        for (const auto& source : value.at("projectiles"))
        {
            CombatProjectileState projectile;
            projectile.id = Id(source.at("id")); projectile.ownerId = Id(source.at("ownerId"));
            if (!ids.insert(projectile.id).second) throw std::runtime_error("Duplicate combat projectile.");
            projectile.position = {Number(source.at("x"), true), Number(source.at("y"), true)};
            projectile.height = Number(source.at("height")); projectile.direction = Number(source.at("direction"), true);
            projectile.heightDirection = Number(source.at("heightDirection"), true);
            if (std::abs(projectile.direction) > 1.0f || std::abs(projectile.heightDirection) > 1.0f)
                throw std::runtime_error("Invalid projectile direction.");
            result.projectiles.push_back(projectile);
        }
        return result;
    }
}
