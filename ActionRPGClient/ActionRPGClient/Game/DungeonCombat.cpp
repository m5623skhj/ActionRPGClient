#include "Game/DungeonCombat.h"
#include "Game/PlayerSkillCatalog.h"
#include <algorithm>
#include <nlohmann/json.hpp>
#include <bit>
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
    double TickInterval(const Json& inValue, const double inFallback)
    {
        if (!inValue.contains("tickIntervalSeconds")) return inFallback;
        const auto& interval = inValue.at("tickIntervalSeconds");
        if (!interval.is_number()) throw std::runtime_error("Invalid combat tick interval.");
        const double seconds = interval.get<double>();
        if (!std::isfinite(seconds) || seconds <= 0.0 || seconds > 1.0)
            throw std::runtime_error("Invalid combat tick interval.");
        return seconds;
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
    void ValidateSlide(const ActionRPG::CombatPlayerState& inState)
    {
        const float length = std::hypot(inState.slideDirectionX, inState.slideDirectionY);
        if (inState.slideDurationSeconds > 2.0f || inState.slideSeconds > 2.0f
            || inState.slideSpeed > 20000.0f || length > 1.001f
            || (inState.slideActive && (inState.slideSequence == 0 || inState.slideDurationSeconds <= 0.0f
                || inState.slideSeconds > inState.slideDurationSeconds + 0.001f
                || inState.slideSpeed <= 0.0f || length < 0.999f
                || inState.hp == 0 || inState.reaction != ActionRPG::CombatReaction::None
                || inState.jumpPhase != ActionRPG::CombatJumpPhase::Grounded || inState.height > 0.0f
                || inState.shotPhase != ActionRPG::CombatShotPhase::None || inState.skillActive
                || inState.slideSequence > inState.actionSequence)))
            throw std::runtime_error("Invalid slide state.");
    }
    ActionRPG::CombatActorState Actor(const Json& inActor)
    {
        ActionRPG::CombatActorState actor;
        actor.position = {Number(inActor.at("x"), true), Number(inActor.at("y"), true)};
        actor.hp = Integer(inActor.at("hp")); actor.maxHp = Integer(inActor.at("maxHp"));
        actor.height = Number(inActor.at("height")); actor.verticalSpeed = Number(inActor.at("verticalSpeed"), true);
        actor.reactionSeconds = Number(inActor.at("reactionSeconds")); actor.facingLeft = inActor.at("facingLeft").get<bool>();
        actor.reactionSequence = Integer(inActor.value("reactionSequence", Json(0)));
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
        if (value.at("version") != 2) throw std::runtime_error("Unsupported combat rules.");
        CombatRules rules;
        rules.tickIntervalSeconds = TickInterval(value, rules.tickIntervalSeconds);
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
        if (value.at("version") != 2) throw std::runtime_error("Unsupported combat snapshot.");
        DungeonCombatSnapshot result;
        result.roomId = Id(value.at("roomId")); result.serverTick = Id(value.at("serverTick"), true);
        result.hasServerTime = value.contains("serverTimeMs");
        if (result.hasServerTime) result.serverTimeMs = Id(value.at("serverTimeMs"), true);
        result.tickIntervalSeconds = TickInterval(value, 0.0);
        result.mapId = Text(value.at("mapId")); result.state = Text(value.at("state"));
        result.mapEpoch = Integer(value.value("mapEpoch", Json(1)));
        if (result.mapEpoch == 0) throw std::runtime_error("Zero combat map epoch.");
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
            player.shotSequence = Integer(source.value("shotSequence", Json(0)));
            player.jumpSequence = Integer(source.value("jumpSequence", Json(0)));
            player.running = source.value("running", false);
            player.slideActive = source.at("slideActive").get<bool>();
            player.slideSequence = Integer(source.at("slideSequence"));
            player.slideSeconds = Number(source.at("slideSeconds"));
            player.slideDurationSeconds = Number(source.at("slideDurationSeconds"));
            player.slideDirectionX = Number(source.at("slideDirectionX"), true);
            player.slideDirectionY = Number(source.at("slideDirectionY"), true);
            player.slideSpeed = Number(source.at("slideSpeed"));
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
            if (source.contains("skillId"))
            {
                player.characterId = Integer(source.at("characterId")); player.skillSequence = Integer(source.at("skillSequence"));
                player.skillId = Text(source.at("skillId"), true); player.skillActive = source.at("skillActive").get<bool>(); player.skillAirborne = source.at("skillAirborne").get<bool>();
                player.skillSeconds = Number(source.at("skillSeconds")); player.movementMultiplier = Number(source.at("movementMultiplier"));
                if ((!player.skillId.empty() && !PlayerSkills::Catalog::IsId(player.skillId)) || player.skillSeconds > 600
                    || player.movementMultiplier < 0.1f || player.movementMultiplier > 10) throw std::runtime_error("Invalid skill state.");
                List(source.at("buffs"), 16);
                std::unordered_set<std::string> buffIds;
                for (const auto& buff : source.at("buffs"))
                {
                    CombatBuffState state{ Text(buff.at("skillId")), Number(buff.at("remainingSeconds")) };
                    if (!PlayerSkills::Catalog::IsId(state.skillId) || !buffIds.insert(state.skillId).second || state.remainingSeconds > 3600)
                        throw std::runtime_error("Invalid buff state.");
                    player.buffs.push_back(std::move(state));
                }
            }
            const bool hasProgression = source.contains("level") || source.contains("skillPoints")
                || source.contains("skillLevels") || source.contains("skillCooldowns");
            if (hasProgression)
            {
                const auto& levels=source.at("skillLevels"); const auto& cooldowns=source.at("skillCooldowns");
                if (!levels.is_object() || levels.size()>256 || !cooldowns.is_object() || cooldowns.size()>256)
                    throw std::runtime_error("Invalid combat skill maps.");
                player.level=Integer(source.at("level")); player.skillPoints=Integer(source.at("skillPoints"));
                if (player.level==0 || player.level>1000000) throw std::runtime_error("Invalid combat character level.");
                for (const auto& [id,value]:levels.items())
                {
                    const auto rank=Integer(value);
                    if (!PlayerSkills::Catalog::IsId(id) || rank==0 || rank>1000000) throw std::runtime_error("Invalid learned skill level.");
                    player.skillLevels.emplace(id,rank);
                }
                for (const auto& [id,value]:cooldowns.items())
                {
                    const auto seconds=Number(value);
                    if (!PlayerSkills::Catalog::IsId(id) || seconds>86400) throw std::runtime_error("Invalid skill cooldown.");
                    player.skillCooldowns.emplace(id,seconds);
                }
                player.hasSkillState=true;
            }
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
            monster.actionSequence = Integer(source.value("actionSequence", Json(0)));
            monster.actionSeconds = Number(source.at("actionSeconds"));
            monster.actionStarted = source.at("actionStarted").get<bool>(); monster.actionComplete = source.at("actionComplete").get<bool>();
            if (monster.actionType != "Wait" && monster.actionType != "MoveToTarget" && monster.actionType != "ReturnToSpawn"
                && monster.actionType != "UseSkill" && monster.actionType != "PlayMotion") throw std::runtime_error("Unknown monster action.");
            if ((monster.actionType == "UseSkill" && monster.animationId != "attack")
                || (monster.actionType == "PlayMotion" && monster.animationId != "idle" && monster.animationId != "move"
                    && monster.animationId != "attack" && monster.animationId != "hit" && monster.animationId != "airborne"))
                throw std::runtime_error("Unsupported combat monster animation.");
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
            if (source.contains("skillId"))
            {
                projectile.skillId = Text(source.at("skillId"), true);
                projectile.directionY = Number(source.at("directionY"), true); projectile.speed = Number(source.at("speed"));
                projectile.radius = Number(source.at("radius")); projectile.ageSeconds = Number(source.at("ageSeconds"));
                if ((!projectile.skillId.empty() && !PlayerSkills::Catalog::IsId(projectile.skillId))
                    || std::abs(projectile.directionY) > 1 || projectile.speed < 1 || projectile.speed > 5000
                    || (!projectile.skillId.empty() && projectile.speed > 4000)
                    || projectile.radius < 0.1f || projectile.radius > 100) throw std::runtime_error("Invalid skill projectile.");
            }
            result.projectiles.push_back(projectile);
        }
        for (const auto& player : result.players) ValidateSlide(player);
        return result;
    }
    // Binary payload v2: unaligned little-endian fields, never native struct layout.
    DungeonCombatSnapshot DungeonCombatSnapshot::ParseRealtime(const std::string_view inBytes)
    {
        class Reader
        {
        public:
            explicit Reader(std::string_view inData) : data(inData) {}
            std::uint64_t UInt(const unsigned inSize)
            {
                if (inSize > data.size() - offset) throw std::runtime_error("Truncated realtime frame.");
                std::uint64_t value{};
                for (unsigned index = 0; index < inSize; ++index)
                    value |= static_cast<std::uint64_t>(static_cast<unsigned char>(data[offset++])) << (index * 8);
                return value;
            }
            float Float(const bool inSigned = false)
            {
                const float value = std::bit_cast<float>(static_cast<std::uint32_t>(UInt(4)));
                if (!std::isfinite(value) || std::abs(value) > 1000000.0f || (!inSigned && value < 0))
                    throw std::runtime_error("Invalid realtime float.");
                return value;
            }
            bool Bool()
            {
                const auto value = UInt(1);
                if (value > 1) throw std::runtime_error("Invalid realtime boolean.");
                return value != 0;
            }
            std::string Text(const bool inAllowEmpty = false)
            {
                const auto size = UInt(2);
                if (size > 128 || (!inAllowEmpty && size == 0) || size > data.size() - offset)
                    throw std::runtime_error("Invalid realtime text length.");
                std::string value(data.substr(offset, size)); offset += size;
                if (value.find('\0') != std::string::npos) throw std::runtime_error("NUL in realtime identifier.");
                (void)Json(value).dump(); // Existing JSON library validates bounded UTF-8 identifiers.
                return value;
            }
            bool Finished() const { return offset == data.size(); }
        private:
            std::string_view data;
            std::size_t offset{};
        } reader(inBytes);
        if (inBytes.size() > 48 * 1024) throw std::runtime_error("Realtime frame too large.");
        const auto playerCount = reader.UInt(2), monsterCount = reader.UInt(2), projectileCount = reader.UInt(2);
        if (playerCount == 0 || playerCount > 4 || monsterCount > 256 || projectileCount > 256)
            throw std::runtime_error("Invalid realtime actor counts.");
        DungeonCombatSnapshot result;
        result.realtime = true;
        const auto actor = [&reader](CombatActorState& outActor, std::uint64_t& outId, std::uint32_t& outDataId)
        {
            outId = reader.UInt(8); outDataId = static_cast<std::uint32_t>(reader.UInt(4));
            outActor.position = {reader.Float(true), reader.Float(true)};
            outActor.height = reader.Float(); outActor.verticalSpeed = reader.Float(true);
            outActor.reactionSeconds = reader.Float();
            outActor.hp = static_cast<std::uint32_t>(reader.UInt(4)); outActor.maxHp = static_cast<std::uint32_t>(reader.UInt(4));
            outActor.facingLeft = reader.Bool();
            const auto reaction = reader.UInt(1);
            if (outId == 0 || reaction > 5 || outActor.maxHp == 0 || outActor.hp > outActor.maxHp
                || ((outActor.hp == 0) != (reaction == 5))) throw std::runtime_error("Invalid realtime actor.");
            outActor.reaction = static_cast<CombatReaction>(reaction);
            outActor.reactionSequence = static_cast<std::uint32_t>(reader.UInt(4));
        };
        std::unordered_set<std::uint64_t> ids;
        for (std::uint64_t index = 0; index < playerCount; ++index)
        {
            CombatPlayerState player;
            std::uint32_t dataId{}; actor(player, player.playerId, dataId);
            if (dataId != 0 || !ids.insert(player.playerId).second) throw std::runtime_error("Invalid realtime player ID.");
            player.moveSequence = static_cast<std::uint32_t>(reader.UInt(4));
            player.actionSequence = static_cast<std::uint32_t>(reader.UInt(4));
            player.shotSequence = static_cast<std::uint32_t>(reader.UInt(4));
            player.jumpSequence = static_cast<std::uint32_t>(reader.UInt(4));
            const auto shot = reader.UInt(1); player.airAttack = reader.Bool();
            player.shotSeconds = reader.Float();
            player.shotCount = static_cast<std::uint32_t>(reader.UInt(1));
            player.airShotCount = static_cast<std::uint32_t>(reader.UInt(1));
            const auto jump = reader.UInt(1); player.jumpSeconds = reader.Float(); player.running = reader.Bool();
            player.slideActive = reader.Bool();
            player.slideSequence = static_cast<std::uint32_t>(reader.UInt(4));
            player.slideSeconds = reader.Float(); player.slideDurationSeconds = reader.Float();
            player.slideDirectionX = reader.Float(true); player.slideDirectionY = reader.Float(true);
            player.slideSpeed = reader.Float();
            if (shot > 3 || jump > 2 || player.shotCount > 5 || player.airShotCount > 5)
                throw std::runtime_error("Invalid realtime player action.");
            player.shotPhase = static_cast<CombatShotPhase>(shot); player.jumpPhase = static_cast<CombatJumpPhase>(jump);
            result.players.push_back(std::move(player));
        }
        ids.clear();
        constexpr const char* ACTION_TYPES[] = {"Wait", "MoveToTarget", "ReturnToSpawn", "UseSkill", "PlayMotion"};
        for (std::uint64_t index = 0; index < monsterCount; ++index)
        {
            CombatMonsterState monster; actor(monster, monster.instanceId, monster.dataId);
            if (monster.dataId == 0 || !ids.insert(monster.instanceId).second) throw std::runtime_error("Invalid realtime monster ID.");
            monster.actionSequence = static_cast<std::uint32_t>(reader.UInt(4));
            const auto type = reader.UInt(1);
            if (type > 4) throw std::runtime_error("Invalid realtime monster action.");
            monster.actionType = ACTION_TYPES[type];
            monster.actionStarted = reader.Bool(); monster.actionComplete = reader.Bool(); monster.actionSeconds = reader.Float();
            monster.aiNodeId = reader.Text(); monster.animationId = reader.Text(true);
            if ((type == 3 && monster.animationId != "attack") || (type == 4 && monster.animationId != "idle"
                && monster.animationId != "move" && monster.animationId != "attack" && monster.animationId != "hit"
                && monster.animationId != "airborne")) throw std::runtime_error("Unsupported realtime motion.");
            result.monsters.push_back(std::move(monster));
        }
        ids.clear();
        for (std::uint64_t index = 0; index < projectileCount; ++index)
        {
            CombatProjectileState projectile;
            projectile.id = reader.UInt(8); projectile.ownerId = reader.UInt(8);
            projectile.position = {reader.Float(true), reader.Float(true)};
            projectile.height = reader.Float(); projectile.direction = reader.Float(true); projectile.heightDirection = reader.Float(true);
            if (projectile.id == 0 || projectile.ownerId == 0 || !ids.insert(projectile.id).second
                || std::abs(projectile.direction) > 1.0f || std::abs(projectile.heightDirection) > 1.0f)
                throw std::runtime_error("Invalid realtime projectile.");
            result.projectiles.push_back(projectile);
        }
        if (!reader.Finished())
        {
            if (reader.UInt(4) != 0x314c4b53 || reader.UInt(2) != playerCount)
                throw std::runtime_error("Unsupported skill extension.");
            std::unordered_set<std::uint64_t> skillPlayers;
            for (std::uint64_t index = 0; index < playerCount; ++index)
            {
                const auto id = reader.UInt(8);
                const auto found = std::find_if(result.players.begin(), result.players.end(), [id](const auto& actor) { return actor.playerId == id; });
                if (found == result.players.end() || !skillPlayers.insert(id).second) throw std::runtime_error("Invalid skill player ID.");
                found->characterId = static_cast<std::uint32_t>(reader.UInt(4));
                found->skillSequence = static_cast<std::uint32_t>(reader.UInt(4));
                found->skillId = reader.Text(true); found->skillActive = reader.Bool(); found->skillAirborne = reader.Bool();
                found->skillSeconds = reader.Float(); found->movementMultiplier = reader.Float();
                if (found->characterId == 0 || (!found->skillId.empty() && !PlayerSkills::Catalog::IsId(found->skillId))
                    || found->skillSeconds > 600 || found->movementMultiplier < 0.1f || found->movementMultiplier > 10)
                    throw std::runtime_error("Invalid realtime skill state.");
                const auto buffs = reader.UInt(1);
                if (buffs > 16) throw std::runtime_error("Too many buffs.");
                std::unordered_set<std::string> buffIds;
                for (std::uint64_t buff = 0; buff < buffs; ++buff)
                {
                    CombatBuffState value{reader.Text(), reader.Float()};
                    if (!PlayerSkills::Catalog::IsId(value.skillId) || !buffIds.insert(value.skillId).second || value.remainingSeconds > 3600)
                        throw std::runtime_error("Invalid realtime buff.");
                    found->buffs.push_back(std::move(value));
                }
            }
            if (reader.UInt(2) != projectileCount) throw std::runtime_error("Skill projectile count mismatch.");
            std::unordered_set<std::uint64_t> skillProjectiles;
            for (std::uint64_t index = 0; index < projectileCount; ++index)
            {
                const auto id = reader.UInt(8);
                const auto found = std::find_if(result.projectiles.begin(), result.projectiles.end(), [id](const auto& actor) { return actor.id == id; });
                if (found == result.projectiles.end() || !skillProjectiles.insert(id).second) throw std::runtime_error("Invalid skill projectile ID.");
                found->skillId = reader.Text(true); found->directionY = reader.Float(true); found->speed = reader.Float();
                found->radius = reader.Float(); found->ageSeconds = reader.Float();
                if ((!found->skillId.empty() && !PlayerSkills::Catalog::IsId(found->skillId)) || std::abs(found->directionY) > 1
                    || found->speed < 1 || found->speed > 5000 || (!found->skillId.empty() && found->speed > 4000) || found->radius < 0.1f || found->radius > 100)
                    throw std::runtime_error("Invalid realtime skill projectile.");
            }
        }
        if (!reader.Finished()) throw std::runtime_error("Trailing realtime bytes.");

        for (const auto& player : result.players) ValidateSlide(player);
        return result;
    }

}
