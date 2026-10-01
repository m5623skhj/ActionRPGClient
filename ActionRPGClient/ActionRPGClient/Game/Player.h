#pragma once

#include "Core/IniDocument.h"
#include "Game/RunState.h"
#include "Game/SkillDefinition.h"
#include "Game/Vector2.h"
#include "Input/InputState.h"
#include "Resources/SpriteAnimation.h"

#include <cstdint>
#include <deque>
#include <optional>

namespace ActionRPG
{
    class Camera;
    class D2DRenderer;
    class GameplayMap;
    class AssetCatalog;

    enum class PlayerProjectileType
    {
        Straight,
        AirStraight,
        Arc
    };

    struct PlayerProjectileRequest
    {
        PlayerProjectileType type{ PlayerProjectileType::Straight };
        Vector2 throwerPosition{};
        float throwerHeight{};
        Vector2 direction{};
    };

    class Player final
    {
    public:
        Player(Vector2 inInitialPosition, const AssetCatalog& inAssetCatalog, D2DRenderer& inRenderer);

        // Ground movement uses x/y while jump motion uses an independent height axis.
        void Update(float inDeltaSeconds, const InputState& inInput, const GameplayMap& inGameplayMap);
        void ActivateCommandSkill(const SkillEffectDefinition& inEffect);
        [[nodiscard]] std::optional<PlayerProjectileRequest> ConsumeProjectileRequest();
        void Render(D2DRenderer& inRenderer, const Camera& inCamera) const;

        [[nodiscard]] Vector2 GetGroundPosition() const { return groundPosition; }
        [[nodiscard]] bool IsRunning() const { return runningEnabled && runState.IsRunning(); }
        void SetGroundPosition(Vector2 inPosition) { groundPosition = inPosition; }
        void SetRunningEnabled(const bool inEnabled)
        {
            runningEnabled = inEnabled;
            if (!runningEnabled)
            {
                runState.Reset();
            }
        }
        void ReconcileGroundPosition(Vector2 inAuthoritativePosition);
        void ConfigureMovementSpeeds(float inWalkSpeed, float inRunSpeed);

    private:
        enum class JumpPhase
        {
            Grounded,
            Preparing,
            Airborne,
            Landing
        };

        enum class AttackPhase
        {
            None,
            Start,
            Fire,
            End
        };

        [[nodiscard]] bool IsAttacking() const { return attackPhase != AttackPhase::None; }
        void BeginJump();
        float UpdateJump(float inDeltaSeconds);
        void BeginAttack(bool inAirAttack);
        void CancelAttack();
        [[nodiscard]] SpriteAnimation& GetAttackAnimation();
        [[nodiscard]] const SpriteAnimation& GetAttackAnimation() const;
        void UpdateAttack(float inDeltaSeconds);
        void ApplyAirShotRecoil();
        void QueueProjectileRequest(PlayerProjectileType inType);

    private:
        static constexpr float WIDTH = 64.0f;
        static constexpr float HEIGHT = 96.0f;
        static constexpr float HORIZONTAL_RADIUS = WIDTH * 0.5f;
        static constexpr float DEPTH_RADIUS = 18.0f;
        static constexpr float JUMP_SPEED = 700.0f;
        static constexpr float GRAVITY = 1800.0f;
        static constexpr std::uint32_t MAX_ATTACK_SHOTS = 5;
        static constexpr float AIR_SHOT_RECOIL_LIFT = 3.0f;
        static constexpr float AIR_SHOT_RECOIL_SPEED = 35.0f;
        static constexpr float AIR_SHOT_RECOIL_BACKWARD = 4.0f;

        Vector2 groundPosition{};
        float height{};
        float verticalVelocity{};
        float skillEffectRemainingSeconds{};
        std::optional<SkillEffectDefinition> activeSkillEffect;
        double movementTimeSeconds{};
        bool facingLeft{};
        bool isMoving{};
        AttackPhase attackPhase{ AttackPhase::None };
        JumpPhase jumpPhase{ JumpPhase::Grounded };
        bool airAttack{};
        bool attackProjectileQueued{};
        bool runningEnabled = true;
        std::uint32_t attackEventFrame{};
        std::uint32_t airAttackEventFrame{};
        std::uint32_t airShotCount{};
        std::uint32_t attackShotCount{};
        std::uint32_t pendingAttackShots{};
        float walkSpeed = 280.0f;
        float runSpeed = 480.0f;
        RunState runState;
        IniDocument animationDefinitions;
        SpriteAnimation idleAnimation;
        SpriteAnimation walkAnimation;
        SpriteAnimation runAnimation;
        SpriteAnimation attackStartAnimation;
        SpriteAnimation attackFireAnimation;
        SpriteAnimation attackEndAnimation;
        SpriteAnimation jumpStartAnimation;
        SpriteAnimation jumpHoldAnimation;
        SpriteAnimation jumpLandAnimation;
        SpriteAnimation airAttackStartAnimation;
        SpriteAnimation airAttackFireAnimation;
        SpriteAnimation airAttackEndAnimation;
        std::deque<PlayerProjectileRequest> pendingProjectileRequests;
    };
}
