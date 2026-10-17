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
    struct MeleeStrikePresentation
    {
        std::string motionId;
        float durationSeconds{}, prepareSeconds{}, hitSeconds{}, recoverSeconds{};
    };
    struct MeleePresentation
    {
        float bodyHeight{};
        std::vector<MeleeStrikePresentation> ground;
        MeleeStrikePresentation air, dash;
    };
    struct CombatRules
    {
        static constexpr double MIN_TEMPO_SCALE = 0.1;
        // Neutral default for legacy worlds. Durations/speeds remain combat-time units.
        float tempoScale{1.0f};
        float walkSpeed{}, runSpeed{}, shotPrepareSeconds{}, shotIntervalSeconds{}, shotRecoverSeconds{};
        float jumpSpeed{}, gravity{}, jumpPrepareSeconds{}, hitStunSeconds{}, downSeconds{}, riseSeconds{};
        float knockbackSeconds{0.18f};
        float projectileSpeed{}, muzzleHeight{}, airFireLift{}, airRecoilDistance{}, shotHitstopSeconds{};
        std::uint32_t maxHp{}, maxShots{};
        double tickIntervalSeconds{0.05};
        std::unordered_map<std::uint32_t,MeleePresentation> meleeDefinitions;
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
        // KBK1 distances/durations already include server HR scaling; never apply HR again.
        bool hasKnockbackState{}, knockbackActive{};
        std::uint32_t knockbackSequence{};
        float knockbackDirectionX{}, knockbackDirectionY{}, knockbackDistance{};
        float knockbackElapsedSeconds{}, knockbackDurationSeconds{};
        bool presentationMoving{};
        float presentationSpeed{};
        CombatReaction reaction{ CombatReaction::None };
    };
    // Return real seconds outside the wall-clock attacker-only stop interval; callers convert action time once.
    [[nodiscard]] float CombatActiveSeconds(const CombatActorState& inActor, double inStartMs, double inEndMs);
    [[nodiscard]] bool IsCombatHitstopped(const CombatActorState& inActor, double inTimeMs);
    // Predict only the unconsumed eased distance from a received XY, within the caller's bounded combat age.
    [[nodiscard]] Vector2 PredictCombatKnockbackPosition(const CombatActorState& inActor, float inCombatAgeSeconds);
    void ClearCombatKnockback(CombatActorState& inActor);
    struct CombatBuffState { std::string skillId; float remainingSeconds{}; };
    struct CombatPlayerState : CombatActorState
    {
        std::uint64_t playerId{};
        bool hasHudState{}, mpKnown{};
        std::uint32_t mp{}, maxMp{};
        std::uint64_t lastHitSequence{}, lastHitMonsterId{};
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
        bool hasItemUseState{};
        std::uint32_t itemUseSequence{};
        std::string itemUseDefinitionId, itemUseMotionId;
        float itemUseElapsedSeconds{}, itemUseDurationSeconds{};
        bool skillActive{}, skillAirborne{};
        float skillSeconds{}, movementMultiplier{1.0f}, skillPlaybackRate{1.0f};
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
        bool hasHudState{}, levelKnown{}, isBoss{};
        std::uint32_t level{};
        std::string name;
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
        std::string skillId, itemDefinitionId, visual;
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
