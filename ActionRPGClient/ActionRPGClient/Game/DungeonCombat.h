#pragma once
#include "Game/Vector2.h"
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace ActionRPG
{
    enum class CombatReaction { None, Hit, Falling, Down, Rising, Dead };
    enum class CombatShotPhase { None, Prepare, Fire, Recover };
    enum class CombatJumpPhase { Grounded, Prepare, Airborne };
    struct CombatRules
    {
        float walkSpeed{}, runSpeed{}, shotPrepareSeconds{}, shotIntervalSeconds{}, shotRecoverSeconds{};
        float jumpSpeed{}, gravity{}, jumpPrepareSeconds{}, hitStunSeconds{}, downSeconds{}, riseSeconds{};
        float projectileSpeed{}, muzzleHeight{}, airFireLift{}, airRecoilDistance{};
        std::uint32_t maxHp{}, maxShots{};
        static CombatRules Parse(std::string_view inJson);
    };
    struct CombatActorState
    {
        Vector2 position{};
        std::uint32_t hp{}, maxHp{};
        float height{}, verticalSpeed{}, reactionSeconds{};
        bool facingLeft{};
        CombatReaction reaction{ CombatReaction::None };
    };
    struct CombatPlayerState : CombatActorState
    {
        std::uint64_t playerId{};
        CombatShotPhase shotPhase{ CombatShotPhase::None };
        CombatJumpPhase jumpPhase{ CombatJumpPhase::Grounded };
        float shotSeconds{}, jumpSeconds{};
        std::uint32_t shotCount{}, airShotCount{}, actionSequence{}, moveSequence{};
        bool airAttack{};
    };
    struct CombatMonsterState : CombatActorState
    {
        std::uint64_t instanceId{};
        std::uint32_t dataId{};
        std::string aiNodeId, actionType, animationId;
        bool actionStarted{}, actionComplete{};
        float actionSeconds{};
    };
    struct CombatProjectileState
    {
        std::uint64_t id{}, ownerId{};
        Vector2 position{};
        float height{}, direction{}, heightDirection{};
    };
    struct DungeonCombatSnapshot
    {
        std::uint64_t roomId{}, serverTick{};
        std::string mapId, state;
        bool cleared{};
        std::vector<CombatPlayerState> players;
        std::vector<CombatMonsterState> monsters;
        std::vector<CombatProjectileState> projectiles;
        static DungeonCombatSnapshot Parse(std::string_view inJson);
    };
}
