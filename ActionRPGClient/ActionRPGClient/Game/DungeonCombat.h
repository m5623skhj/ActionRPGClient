#pragma once
#include "Game/Vector2.h"
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#include <unordered_map>

namespace ActionRPG
{
    enum class CombatReaction { None, Hit, Falling, Down, Rising, Dead };
    enum class CombatShotPhase { None, Prepare, Fire, Recover };
    enum class CombatJumpPhase { Grounded, Prepare, Airborne };
    struct CombatRules
    {
        float walkSpeed{}, runSpeed{}, shotPrepareSeconds{}, shotIntervalSeconds{}, shotRecoverSeconds{};
        float jumpSpeed{}, gravity{}, jumpPrepareSeconds{}, hitStunSeconds{}, downSeconds{}, riseSeconds{};
        float projectileSpeed{}, muzzleHeight{}, airFireLift{}, airRecoilDistance{}, shotHitstopSeconds{};
        std::uint32_t maxHp{}, maxShots{};
        double tickIntervalSeconds{0.05};
        static CombatRules Parse(std::string_view inJson);
    };
    struct CombatActorState
    {
        Vector2 position{};
        std::uint32_t hp{}, maxHp{};
        float height{}, verticalSpeed{}, reactionSeconds{};
        float hitRecovery{}, reactionDurationSeconds{}, hitstopRemainingSeconds{}, hitstopDurationSeconds{};
        std::uint32_t hitstopSequence{};
        std::uint64_t hitstopStartTimeMs{};
        // Client-only server-clock instant for this pose; never serialized.
        double presentationTimeMs{};
        bool facingLeft{};
        std::uint32_t reactionSequence{};
        bool presentationMoving{};
        float presentationSpeed{};
        CombatReaction reaction{ CombatReaction::None };
    };
    // Count actor time outside the retained authoritative attacker-only stop interval.
    [[nodiscard]] float CombatActiveSeconds(const CombatActorState& inActor, double inStartMs, double inEndMs);
    [[nodiscard]] bool IsCombatHitstopped(const CombatActorState& inActor, double inTimeMs);
    struct CombatBuffState { std::string skillId; float remainingSeconds{}; };
    struct CombatPlayerState : CombatActorState
    {
        std::uint64_t playerId{};
        CombatShotPhase shotPhase{ CombatShotPhase::None };
        CombatJumpPhase jumpPhase{ CombatJumpPhase::Grounded };
        float shotSeconds{}, jumpSeconds{};
        std::uint32_t shotCount{}, airShotCount{}, actionSequence{}, moveSequence{};
        bool airAttack{}, running{};
        bool slideActive{};
        std::uint32_t slideSequence{};
        float slideSeconds{}, slideDurationSeconds{}, slideDirectionX{}, slideDirectionY{}, slideSpeed{};
        std::uint32_t shotSequence{}, jumpSequence{};
        std::uint32_t characterId{}, skillSequence{};
        std::string skillId;
        bool skillActive{}, skillAirborne{};
        float skillSeconds{}, movementMultiplier{1.0f};
        std::vector<CombatBuffState> buffs;
        // JSON-only fields; SKL1 binary snapshots leave hasSkillState false.
        bool hasSkillState{};
        std::uint32_t level{}, skillPoints{};
        std::unordered_map<std::string,std::uint32_t> skillLevels;
        std::unordered_map<std::string,float> skillCooldowns;
    };
    struct CombatMonsterState : CombatActorState
    {
        std::uint64_t instanceId{};
        std::uint32_t dataId{};
        std::string aiNodeId, actionType, animationId;
        bool actionStarted{}, actionComplete{};
        std::uint32_t actionSequence{};
        float actionSeconds{};
    };
    struct CombatProjectileState
    {
        std::uint64_t id{}, ownerId{};
        Vector2 position{};
        float height{}, direction{}, heightDirection{};
        float directionY{}, speed{}, radius{4.0f}, ageSeconds{};
        std::string skillId;
    };
    struct DungeonCombatSnapshot
    {
        std::uint64_t roomId{}, serverTick{}, snapshotSequence{}, serverTimeMs{};
        std::uint32_t mapEpoch{1};
        bool realtime{}, hasServerTime{};
        double tickIntervalSeconds{};
        std::string mapId, state;
        bool cleared{};
        std::vector<CombatPlayerState> players;
        std::vector<CombatMonsterState> monsters;
        std::vector<CombatProjectileState> projectiles;
        static DungeonCombatSnapshot ParseRealtime(std::string_view inBytes);
        static DungeonCombatSnapshot Parse(std::string_view inJson);
    };
}
