#include "Game/DungeonCombat.h"
#include "Game/PlayerSkillCatalog.h"
#include <algorithm>
#include <nlohmann/json.hpp>
#include <bit>
#include <cmath>
#include <limits>
#include <iterator>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace
{
    using Json = nlohmann::json;
    // runSpeed 2000 * movementMultiplier 10 * distance seconds 5 / duration 0.05.
    constexpr float MAX_SLIDE_SPEED = 2000000.0f;
    // UI remaining time is real seconds, bounded by authored maxima / minimum supported tempo.
    constexpr double MAX_REAL_SKILL_COOLDOWN = 86400.0 / ActionRPG::CombatRules::MIN_TEMPO_SCALE;
    constexpr double MAX_REAL_BUFF_DURATION = 3600.0 / ActionRPG::CombatRules::MIN_TEMPO_SCALE;
    float Number(const Json& inValue, const bool inSigned = false, const bool inUnbounded = false)
    {
        if (!inValue.is_number()) throw std::runtime_error("Invalid combat number.");
        const float value = inValue.get<float>();
        if (inUnbounded && inValue.get<double>() > 0.0 && value == 0.0f)
            throw std::runtime_error("Combat number underflows float.");
        if (!std::isfinite(value) || (!inUnbounded && std::abs(value) > 1000000.0f) || (!inSigned && value < 0.0f))
            throw std::runtime_error("Invalid combat number.");
        return value;
    }
    void ValidateItemUse(const ActionRPG::CombatPlayerState& inPlayer)
    {
        if ((!inPlayer.itemUseDefinitionId.empty() && !ActionRPG::PlayerSkills::Catalog::IsId(inPlayer.itemUseDefinitionId))
            || (inPlayer.itemUseSequence != 0 && inPlayer.itemUseDefinitionId.empty())
            || (inPlayer.itemUseMotionId != "" && inPlayer.itemUseMotionId != "AttackFire")
            || inPlayer.itemUseDurationSeconds > 5.0f || inPlayer.itemUseElapsedSeconds > 600.0f
            || (!inPlayer.itemUseMotionId.empty() && (inPlayer.itemUseSequence == 0 || inPlayer.itemUseDurationSeconds <= 0.0f)))
            throw std::runtime_error("Invalid item use motion.");
    }
    void ValidateItemProjectile(const ActionRPG::CombatProjectileState& inProjectile)
    {
        if ((!inProjectile.itemDefinitionId.empty() && !ActionRPG::PlayerSkills::Catalog::IsId(inProjectile.itemDefinitionId))
            || (inProjectile.visual != "" && inProjectile.visual != "Stone")
            || (inProjectile.itemDefinitionId.empty() != inProjectile.visual.empty()))
            throw std::runtime_error("Invalid item projectile visual.");
    }
    void ValidatePlayerHud(const ActionRPG::CombatPlayerState& inPlayer)
    {
        if ((!inPlayer.mpKnown && (inPlayer.mp != 0 || inPlayer.maxMp != 0))
            || inPlayer.mp > inPlayer.maxMp || (inPlayer.lastHitMonsterId != 0 && inPlayer.lastHitSequence == 0))
            throw std::runtime_error("Invalid player HUD state.");
    }
    void ValidateMonsterHud(const ActionRPG::CombatMonsterState& inMonster)
    {
        if (inMonster.name.empty() || inMonster.name.size() > 128
            || std::any_of(inMonster.name.begin(), inMonster.name.end(), [](unsigned char c) { return c < 32 || c == 127; })
            || (inMonster.levelKnown ? inMonster.level == 0 : inMonster.level != 0))
            throw std::runtime_error("Invalid monster HUD state.");
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
            || inState.slideSpeed > MAX_SLIDE_SPEED || length > 1.001f
            || (inState.slideActive && (inState.slideSequence == 0 || inState.slideDurationSeconds <= 0.0f
                || inState.slideSeconds > inState.slideDurationSeconds + 0.001f
                || inState.slideSpeed <= 0.0f || length < 0.999f
                || inState.hp == 0 || inState.reaction != ActionRPG::CombatReaction::None
                || inState.jumpPhase != ActionRPG::CombatJumpPhase::Grounded || inState.height > 0.0f
                || inState.shotPhase != ActionRPG::CombatShotPhase::None || inState.skillActive
                || inState.slideSequence > inState.actionSequence)))
            throw std::runtime_error("Invalid slide state.");
    }
    void ValidateActorTiming(const ActionRPG::CombatActorState& inActor)
    {
        if (inActor.reactionSeconds > inActor.reactionDurationSeconds + 0.001f
            || (inActor.reaction == ActionRPG::CombatReaction::Hit && inActor.reactionDurationSeconds <= 0.0f)
            || inActor.hitstopRemainingSeconds > inActor.hitstopDurationSeconds + 0.001f
            || (inActor.hitstopDurationSeconds > 0.0f && (inActor.hitstopSequence == 0 || inActor.hitstopStartTimeMs == 0)))
            throw std::runtime_error("Invalid combat actor timing.");
    }
    void ValidateKnockback(const ActionRPG::CombatActorState& inActor)
    {
        using Reaction = ActionRPG::CombatReaction;
        if (inActor.knockbackSequence != inActor.reactionSequence)
            throw std::runtime_error("Knockback/reaction sequence mismatch.");
        if (!inActor.knockbackActive)
        {
            if (inActor.knockbackDirectionX != 0.0f || inActor.knockbackDirectionY != 0.0f
                || inActor.knockbackDistance != 0.0f || inActor.knockbackElapsedSeconds != 0.0f
                || inActor.knockbackDurationSeconds != 0.0f)
                throw std::runtime_error("Invalid inactive knockback state.");
            return;
        }
        const float length = std::hypot(inActor.knockbackDirectionX, inActor.knockbackDirectionY);
        if (inActor.knockbackSequence == 0 || inActor.hp == 0 || inActor.reaction == Reaction::None
            || length < 0.999f || length > 1.001f || inActor.knockbackDistance <= 0.0f
            || inActor.knockbackDistance > 500.0f || inActor.knockbackDurationSeconds <= 0.0f
            || inActor.knockbackDurationSeconds > 1.0f
            || inActor.knockbackElapsedSeconds > inActor.knockbackDurationSeconds
            || (inActor.height == 0.0f && inActor.knockbackDurationSeconds
                > inActor.reactionSeconds + inActor.knockbackElapsedSeconds + 0.001f))
            throw std::runtime_error("Invalid active knockback state.");
    }
    ActionRPG::CombatActorState Actor(const Json& inActor)
    {
        ActionRPG::CombatActorState actor;
        actor.position = {Number(inActor.at("x"), true), Number(inActor.at("y"), true)};
        actor.hp = Integer(inActor.at("hp")); actor.maxHp = Integer(inActor.at("maxHp"));
        actor.height = Number(inActor.at("height")); actor.verticalSpeed = Number(inActor.at("verticalSpeed"), true);
        actor.reactionSeconds = Number(inActor.at("reactionSeconds")); actor.facingLeft = inActor.at("facingLeft").get<bool>();
        actor.reactionSequence = Integer(inActor.value("reactionSequence", Json(0)));
        actor.hitRecovery = Number(inActor.at("hitRecovery"), false, true);
        actor.reactionDurationSeconds = Number(inActor.at("reactionDurationSeconds"), false, true);
        actor.hitstopRemainingSeconds = Number(inActor.at("hitstopRemainingSeconds"), false, true);
        actor.hitstopSequence = Integer(inActor.at("hitstopSequence"));
        actor.hitstopStartTimeMs = Id(inActor.at("hitstopStartTimeMs"), true);
        actor.hitstopDurationSeconds = Number(inActor.at("hitstopDurationSeconds"), false, true);
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
        ValidateActorTiming(actor);
        // Legacy v3 JSON has no extension; partially present KBK1-equivalent fields are invalid.
        constexpr const char* KNOCKBACK_FIELDS[] = { "knockbackSequence", "knockbackActive", "knockbackDirectionX",
            "knockbackDirectionY", "knockbackDistance", "knockbackElapsedSeconds", "knockbackDurationSeconds" };
        if (std::any_of(std::begin(KNOCKBACK_FIELDS), std::end(KNOCKBACK_FIELDS),
            [&inActor](const char* name) { return inActor.contains(name); }))
        {
            actor.hasKnockbackState = true;
            actor.knockbackSequence = Integer(inActor.at("knockbackSequence"));
            actor.knockbackActive = inActor.at("knockbackActive").get<bool>();
            actor.knockbackDirectionX = Number(inActor.at("knockbackDirectionX"), true);
            actor.knockbackDirectionY = Number(inActor.at("knockbackDirectionY"), true);
            actor.knockbackDistance = Number(inActor.at("knockbackDistance"));
            actor.knockbackElapsedSeconds = Number(inActor.at("knockbackElapsedSeconds"));
            actor.knockbackDurationSeconds = Number(inActor.at("knockbackDurationSeconds"));
            ValidateKnockback(actor);
        }
        return actor;
    }
}

namespace ActionRPG
{
    float CombatActiveSeconds(const CombatActorState& inActor, const double inStartMs, const double inEndMs)
    {
        const double begin = static_cast<double>(inActor.hitstopStartTimeMs);
        const double end = begin + static_cast<double>(inActor.hitstopDurationSeconds) * 1000.0;
        const double stopped = std::max(0.0, std::min(inEndMs, end) - std::max(inStartMs, begin));
        return static_cast<float>(std::max(0.0, inEndMs - inStartMs - stopped) / 1000.0);
    }

    bool IsCombatHitstopped(const CombatActorState& inActor, const double inTimeMs)
    {
        const double begin = static_cast<double>(inActor.hitstopStartTimeMs);
        return inActor.hp != 0 && inActor.reaction == CombatReaction::None
            && inActor.hitstopDurationSeconds > 0.0f && inTimeMs >= begin
            && inTimeMs < begin + static_cast<double>(inActor.hitstopDurationSeconds) * 1000.0;
    }

    void ClearCombatKnockback(CombatActorState& inActor)
    {
        // Keep the ending sequence as a tombstone, including hits completed within one server tick.
        inActor.knockbackActive = false;
        inActor.knockbackDirectionX = inActor.knockbackDirectionY = inActor.knockbackDistance = 0.0f;
        inActor.knockbackElapsedSeconds = inActor.knockbackDurationSeconds = 0.0f;
    }

    Vector2 PredictCombatKnockbackPosition(const CombatActorState& inActor, const float inCombatAgeSeconds)
    {
        if (!inActor.knockbackActive || inActor.hp == 0 || inActor.reaction == CombatReaction::None
            || inActor.knockbackDurationSeconds <= 0.0f) return inActor.position;
        const float remaining = inActor.knockbackDurationSeconds - inActor.knockbackElapsedSeconds;
        float age = std::clamp(inCombatAgeSeconds, 0.0f, std::max(0.0f, remaining));
        if (inActor.height == 0.0f) age = std::min(age, inActor.reactionSeconds);
        const double begin = std::clamp(static_cast<double>(inActor.knockbackElapsedSeconds)
            / inActor.knockbackDurationSeconds, 0.0, 1.0);
        const double end = std::clamp((static_cast<double>(inActor.knockbackElapsedSeconds) + age)
            / inActor.knockbackDurationSeconds, begin, 1.0);
        const float distance = static_cast<float>(inActor.knockbackDistance
            * ((2.0 * end - end * end) - (2.0 * begin - begin * begin)));
        return {inActor.position.x + inActor.knockbackDirectionX * distance,
            inActor.position.y + inActor.knockbackDirectionY * distance};
    }

    CombatRules CombatRules::Parse(const std::string_view inJson)
    {
        const auto value = Json::parse(inJson);
        if (value.at("version") != 3) throw std::runtime_error("Unsupported combat rules.");
        CombatRules rules;
        rules.tempoScale = static_cast<float>(PlayerSkills::Catalog::Number(value.value("tempoScale", Json(1.0)),
            CombatRules::MIN_TEMPO_SCALE, 1.0));
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
        rules.knockbackSeconds = static_cast<float>(PlayerSkills::Catalog::Number(
            value.value("knockbackSeconds", Json(0.18)), 0.01, 1.0));
        rules.muzzleHeight = Number(value.at("muzzleHeight")); rules.airFireLift = Number(value.at("airFireLift"));
        rules.airRecoilDistance = Number(value.at("airRecoilDistance"));
        rules.shotHitstopSeconds = Number(value.at("shotHitstopSeconds"), false, true);
        return rules;
    }

    DungeonCombatSnapshot DungeonCombatSnapshot::Parse(const std::string_view inJson)
    {
        if (inJson.size() > 512 * 1024) throw std::runtime_error("Combat snapshot too large.");
        const auto value = Json::parse(inJson);
        if (value.at("version") != 3) throw std::runtime_error("Unsupported combat snapshot.");
        DungeonCombatSnapshot result;
        result.roomId = Id(value.at("roomId")); result.serverTick = Id(value.at("serverTick"), true);
        result.hasServerTime = true;
        result.serverTimeMs = Id(value.at("serverTimeMs"));
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
            player.slideSpeed = Number(source.at("slideSpeed"), false, true);
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
                player.skillPlaybackRate = source.contains("skillPlaybackRate") ? Number(source.at("skillPlaybackRate")) : 1.0f;
                if (player.skillPlaybackRate < 1.0f || player.skillPlaybackRate > 1.2f) throw std::runtime_error("Invalid skill rate.");
                player.skillSeconds = Number(source.at("skillSeconds")); player.movementMultiplier = Number(source.at("movementMultiplier"));
                if ((!player.skillId.empty() && !PlayerSkills::Catalog::IsId(player.skillId)) || player.skillSeconds > 600
                    || player.movementMultiplier < 0.1f || player.movementMultiplier > 10) throw std::runtime_error("Invalid skill state.");
                List(source.at("buffs"), 16);
                std::unordered_set<std::string> buffIds;
                for (const auto& buff : source.at("buffs"))
                {
                    CombatBuffState state{ Text(buff.at("skillId")), Number(buff.at("remainingSeconds")) };
                    if (!PlayerSkills::Catalog::IsId(state.skillId) || !buffIds.insert(state.skillId).second || state.remainingSeconds > MAX_REAL_BUFF_DURATION)
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
                    if (!PlayerSkills::Catalog::IsId(id) || seconds>MAX_REAL_SKILL_COOLDOWN) throw std::runtime_error("Invalid skill cooldown.");
                    player.skillCooldowns.emplace(id,seconds);
                }
                player.hasSkillState=true;
            }
            if (source.contains("itemUseSequence"))
            {
                player.hasItemUseState = true;
                player.itemUseSequence = Integer(source.at("itemUseSequence"));
                player.itemUseDefinitionId = Text(source.at("itemUseDefinitionId"), true);
                player.itemUseMotionId = Text(source.at("itemUseMotionId"), true);
                player.itemUseElapsedSeconds = Number(source.at("itemUseElapsedSeconds"));
                player.itemUseDurationSeconds = Number(source.at("itemUseDurationSeconds"));
                ValidateItemUse(player);
            }
            if (source.contains("mp"))
            {
                player.hasHudState = true; player.mpKnown = !source.at("mp").is_null();
                if (player.mpKnown != !source.at("maxMp").is_null()) throw std::runtime_error("Invalid nullable MP.");
                if (player.mpKnown) { player.mp = Integer(source.at("mp")); player.maxMp = Integer(source.at("maxMp")); }
                player.lastHitSequence = Id(source.at("lastHitSequence"), true);
                player.lastHitMonsterId = Id(source.at("lastHitMonsterId"), true);
                ValidatePlayerHud(player);
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
            if (source.contains("name"))
            {
                monster.hasHudState = true; monster.name = Text(source.at("name")); monster.isBoss = source.at("isBoss").get<bool>();
                monster.levelKnown = !source.at("level").is_null();
                if (monster.levelKnown) monster.level = Integer(source.at("level"));
                ValidateMonsterHud(monster);
            }
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
            if (source.contains("itemDefinitionId"))
            {
                projectile.itemDefinitionId = Text(source.at("itemDefinitionId"), true);
                projectile.visual = Text(source.at("visual"), true);
                ValidateItemProjectile(projectile);
            }
            result.projectiles.push_back(projectile);
        }
        for (const auto& player : result.players) ValidateSlide(player);
        return result;
    }
    // Binary payload v3: unaligned little-endian fields, never native struct layout.
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
            float Float(const bool inSigned = false, const bool inUnbounded = false)
            {
                const float value = std::bit_cast<float>(static_cast<std::uint32_t>(UInt(4)));
                if (!std::isfinite(value) || (!inUnbounded && std::abs(value) > 1000000.0f) || (!inSigned && value < 0))
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
            outActor.hitRecovery = reader.Float(false, true);
            outActor.reactionDurationSeconds = reader.Float(false, true);
            outActor.hitstopRemainingSeconds = reader.Float(false, true);
            outActor.hitstopSequence = static_cast<std::uint32_t>(reader.UInt(4));
            outActor.hitstopStartTimeMs = reader.UInt(8);
            outActor.hitstopDurationSeconds = reader.Float(false, true);
            ValidateActorTiming(outActor);
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
            player.slideSpeed = reader.Float(false, true);
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
        std::unordered_set<std::uint64_t> extensionTags;
        while (!reader.Finished())
        {
            const auto tag = reader.UInt(4);
            if (!extensionTags.insert(tag).second) throw std::runtime_error("Duplicate realtime extension.");
            if (tag == 0x31445052) // RPD1: skill phase speed, 16 bytes per player.
            {
                if (reader.UInt(2) != playerCount) throw std::runtime_error("Skill rate count mismatch.");
                std::unordered_set<std::uint64_t> seen;
                for (std::uint64_t index = 0; index < playerCount; ++index)
                {
                    const auto id = reader.UInt(8);
                    const auto found = std::find_if(result.players.begin(), result.players.end(),
                        [id](const auto& value) { return value.playerId == id; });
                    const auto sequence = reader.UInt(4);
                    const float rate = reader.Float();
                    if (found == result.players.end() || !seen.insert(id).second
                        || found->skillSequence != sequence || rate < 1.0f || rate > 1.2f)
                        throw std::runtime_error("Invalid skill rate.");
                    found->skillPlaybackRate = rate;
                }
                continue;
            }
            if (tag == 0x314B424B) // KBK1: full actor lists, 33 bytes/record, little endian without padding.
            {
                const auto readKnockback = [&reader](CombatActorState& outActor)
                {
                    outActor.hasKnockbackState = true;
                    outActor.knockbackSequence = static_cast<std::uint32_t>(reader.UInt(4));
                    outActor.knockbackActive = reader.Bool();
                    outActor.knockbackDirectionX = reader.Float(true); outActor.knockbackDirectionY = reader.Float(true);
                    outActor.knockbackDistance = reader.Float(); outActor.knockbackElapsedSeconds = reader.Float();
                    outActor.knockbackDurationSeconds = reader.Float();
                    ValidateKnockback(outActor);
                };
                if (reader.UInt(2) != playerCount) throw std::runtime_error("Knockback player count mismatch.");
                std::unordered_set<std::uint64_t> seen;
                for (std::uint64_t index = 0; index < playerCount; ++index)
                {
                    const auto id = reader.UInt(8);
                    const auto found = std::find_if(result.players.begin(), result.players.end(),
                        [id](const auto& value) { return value.playerId == id; });
                    if (found == result.players.end() || !seen.insert(id).second)
                        throw std::runtime_error("Invalid knockback player ID.");
                    readKnockback(*found);
                }
                if (reader.UInt(2) != monsterCount) throw std::runtime_error("Knockback monster count mismatch.");
                seen.clear();
                for (std::uint64_t index = 0; index < monsterCount; ++index)
                {
                    const auto id = reader.UInt(8);
                    const auto found = std::find_if(result.monsters.begin(), result.monsters.end(),
                        [id](const auto& value) { return value.instanceId == id; });
                    if (found == result.monsters.end() || !seen.insert(id).second)
                        throw std::runtime_error("Invalid knockback monster ID.");
                    readKnockback(*found);
                }
                continue;
            }
            if (tag == 0x31445548) // HUD1: confirmed hits and authoritative resource/monster metadata.
            {
                if (reader.UInt(2) != playerCount) throw std::runtime_error("HUD player count mismatch.");
                std::unordered_set<std::uint64_t> seen;
                for (std::uint64_t index = 0; index < playerCount; ++index)
                {
                    const auto id = reader.UInt(8);
                    const auto found = std::find_if(result.players.begin(), result.players.end(), [id](const auto& actor) { return actor.playerId == id; });
                    if (found == result.players.end() || !seen.insert(id).second) throw std::runtime_error("Invalid HUD player ID.");
                    found->hasHudState = true; found->mpKnown = reader.Bool();
                    found->mp = static_cast<std::uint32_t>(reader.UInt(4)); found->maxMp = static_cast<std::uint32_t>(reader.UInt(4));
                    found->lastHitSequence = reader.UInt(8); found->lastHitMonsterId = reader.UInt(8);
                    ValidatePlayerHud(*found);
                }
                if (reader.UInt(2) != monsterCount) throw std::runtime_error("HUD monster count mismatch.");
                seen.clear();
                for (std::uint64_t index = 0; index < monsterCount; ++index)
                {
                    const auto id = reader.UInt(8);
                    const auto found = std::find_if(result.monsters.begin(), result.monsters.end(), [id](const auto& actor) { return actor.instanceId == id; });
                    if (found == result.monsters.end() || !seen.insert(id).second) throw std::runtime_error("Invalid HUD monster ID.");
                    found->hasHudState = true; found->levelKnown = reader.Bool(); found->level = static_cast<std::uint32_t>(reader.UInt(4));
                    found->isBoss = reader.Bool(); found->name = reader.Text();
                    ValidateMonsterHud(*found);
                }
                continue;
            }
            if (tag == 0x31535549) // IUS1: item use actor presentation; little endian.
            {
                const auto count = reader.UInt(2);
                if (count > playerCount) throw std::runtime_error("Invalid item use actor count.");
                std::unordered_set<std::uint64_t> seen;
                for (std::uint64_t index = 0; index < count; ++index)
                {
                    const auto id = reader.UInt(8);
                    const auto found = std::find_if(result.players.begin(), result.players.end(), [id](const auto& actor) { return actor.playerId == id; });
                    if (found == result.players.end() || !seen.insert(id).second) throw std::runtime_error("Invalid item use actor ID.");
                    found->hasItemUseState = true;
                    found->itemUseSequence = static_cast<std::uint32_t>(reader.UInt(4));
                    found->itemUseDefinitionId = reader.Text(true); found->itemUseMotionId = reader.Text(true);
                    found->itemUseElapsedSeconds = reader.Float(); found->itemUseDurationSeconds = reader.Float();
                    ValidateItemUse(*found);
                }
                continue;
            }
            if (tag == 0x31505449) // ITP1: item visuals attached to existing authoritative projectiles.
            {
                const auto count = reader.UInt(2);
                if (count > projectileCount) throw std::runtime_error("Invalid item projectile count.");
                std::unordered_set<std::uint64_t> seen;
                for (std::uint64_t index = 0; index < count; ++index)
                {
                    const auto id = reader.UInt(8);
                    const auto found = std::find_if(result.projectiles.begin(), result.projectiles.end(), [id](const auto& actor) { return actor.id == id; });
                    if (found == result.projectiles.end() || !seen.insert(id).second) throw std::runtime_error("Invalid item projectile ID.");
                    found->itemDefinitionId = reader.Text(true); found->visual = reader.Text(true);
                    ValidateItemProjectile(*found);
                }
                continue;
            }
            if (tag != 0x314c4b53 || reader.UInt(2) != playerCount)
                throw std::runtime_error("Unsupported realtime extension.");
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
                    if (!PlayerSkills::Catalog::IsId(value.skillId) || !buffIds.insert(value.skillId).second || value.remainingSeconds > MAX_REAL_BUFF_DURATION)
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
