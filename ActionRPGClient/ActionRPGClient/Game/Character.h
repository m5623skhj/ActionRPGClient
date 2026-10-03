#pragma once

#include "Core/IniDocument.h"
#include "Game/Vector2.h"
#include "Game/DungeonCombat.h"
#include "Resources/SpriteAnimation.h"

#include <cstdint>
#include <deque>
#include <optional>
#include <string_view>

namespace ActionRPG
{
    class AssetCatalog;
    class Camera;
    class D2DRenderer;
    class GameplayMap;

    enum class CharacterHitType
    {
        Normal,
        Airborne
    };

    enum class CharacterProjectileType
    {
        Straight,
        AirStraight,
        Arc
    };

    struct CharacterProjectileRequest
    {
        CharacterProjectileType type{ CharacterProjectileType::Straight };
        Vector2 throwerPosition{};
        float throwerHeight{};
        Vector2 direction{};
    };

    // Produced by player input or monster AI; no keyboard or network dependency.
    struct CharacterActions
    {
        Vector2 moveDirection{};
        bool run{};
        bool jump{};
        std::uint32_t attackPressCount{};
        std::optional<CharacterProjectileType> projectileType;
    };

    // Required animation sections are supplied by each character type.
    struct CharacterAnimationSet
    {
        std::string_view idle;
        std::string_view walk;
        std::string_view run;
        std::string_view attackStart;
        std::string_view attackFire;
        std::string_view attackEnd;
        std::string_view jumpStart;
        std::string_view jumpHold;
        std::string_view jumpLand;
        std::string_view airAttackStart;
        std::string_view airAttackFire;
        std::string_view airAttackEnd;
        std::string_view hit;
        std::string_view airHitStart;
        std::string_view airHitFall;
        std::string_view knockdown;
        std::string_view getUp;
    };

    // State, animation and projectile requests are mutated only on the game thread.
    class Character
    {
    public:
        virtual ~Character() = default;

        // Ground movement uses x/y while jump motion uses an independent height axis.
        void UpdateActions(float inDeltaSeconds, const CharacterActions& inActions,
            const GameplayMap& inGameplayMap);
        virtual void ApplyHit(CharacterHitType inType);
        virtual void ResetActionState();
        virtual void Render(D2DRenderer& inRenderer, const Camera& inCamera) const;
        [[nodiscard]] std::optional<CharacterProjectileRequest> ConsumeProjectileRequest();

        [[nodiscard]] Vector2 GetGroundPosition() const { return groundPosition; }
        [[nodiscard]] float GetHeight() const { return height; }
        [[nodiscard]] virtual bool IsHitReacting() const
        {
            return combatState ? combatState->reaction != CombatReaction::None : hitPhase != HitPhase::None;
        }
        [[nodiscard]] bool IsRunning() const { return !IsHitReacting() && runningEnabled && runningRequested; }
        [[nodiscard]] bool IsRunningEnabled() const { return runningEnabled; }
        void SetGroundPosition(Vector2 inPosition) { groundPosition = inPosition; }
        virtual void SetRunningEnabled(const bool inEnabled)
        {
            runningEnabled = inEnabled;
            if (!runningEnabled)
            {
                runningRequested = false;
            }
        }
        void ResetMovementSpeeds() { walkSpeed = DEFAULT_WALK_SPEED; runSpeed = DEFAULT_RUN_SPEED; }
        void ConfigureMovementSpeeds(float inWalkSpeed, float inRunSpeed);
        void ApplyCombatState(const CombatPlayerState& inState, const CombatRules& inRules,
            bool inSetPosition = false);
        void ApplyBufferedCombatState(const CombatPlayerState& inState, const CombatRules& inRules);
        void UpdateCombatPresentation(float inDeltaSeconds, const GameplayMap& inMap,
            const Vector2* inLocalDirection = nullptr, bool inRun = false);
        [[nodiscard]] bool HasCombatState() const { return combatState.has_value(); }
        [[nodiscard]] bool GetFacingLeft() const { return facingLeft; }
        [[nodiscard]] bool IsCombatDead() const { return combatState && combatState->hp == 0; }

    protected:
        [[nodiscard]] const CombatPlayerState* GetCombatPlayerState() const { return combatState ? &*combatState : nullptr; }
        [[nodiscard]] float GetCombatElapsedSeconds() const { return combatPresentationSeconds; }
        // Server-driven monster presentation needs a pose without the player action controller.
        explicit Character(Vector2 inInitialPosition) : groundPosition(inInitialPosition) {}
        Character(Vector2 inInitialPosition, const AssetCatalog& inAssetCatalog,
            D2DRenderer& inRenderer, const CharacterAnimationSet& inAnimations);

        void SetFacingLeft(bool inFacingLeft) { facingLeft = inFacingLeft; }
        void SetPresentationHeight(float inHeight) { height = inHeight; }

        static constexpr float DEFAULT_WALK_SPEED = 280.0f;
        static constexpr float DEFAULT_RUN_SPEED = 480.0f;
        static constexpr float WIDTH = 64.0f;
        static constexpr float HEIGHT = 96.0f;

    private:
        enum class HitPhase
        {
            None,
            Stagger,
            Launch,
            Falling,
            Knockdown,
            GetUp
        };

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
        void QueueProjectileRequest(CharacterProjectileType inType);
        void UpdateHit(float inDeltaSeconds);
        [[nodiscard]] SpriteAnimation& GetCombatAnimation();
        [[nodiscard]] const SpriteAnimation& GetCombatAnimation() const;

    private:
        static constexpr float HORIZONTAL_RADIUS = WIDTH * 0.5f;
        static constexpr float DEPTH_RADIUS = 18.0f;
        static constexpr float JUMP_SPEED = 700.0f;
        static constexpr float GRAVITY = 1800.0f;
        static constexpr float AIRBORNE_HIT_SPEED = 560.0f;
        static constexpr float AIR_HIT_FALL_SPEED = 500.0f;
        static constexpr float KNOCKDOWN_HOLD_SECONDS = 0.25f;
        static constexpr std::uint32_t MAX_ATTACK_SHOTS = 5;
        static constexpr float AIR_SHOT_RECOIL_LIFT = 3.0f;
        static constexpr float AIR_SHOT_RECOIL_SPEED = 35.0f;
        static constexpr float AIR_SHOT_RECOIL_BACKWARD = 4.0f;

        Vector2 groundPosition{};
        float height{};
        float verticalVelocity{};
        bool facingLeft{};
        bool isMoving{};
        AttackPhase attackPhase{ AttackPhase::None };
        JumpPhase jumpPhase{ JumpPhase::Grounded };
        HitPhase hitPhase{ HitPhase::None };
        float knockdownHoldRemainingSeconds{};
        bool airAttack{};
        bool attackProjectileQueued{};
        bool runningEnabled = true;
        std::uint32_t attackEventFrame{};
        std::uint32_t airAttackEventFrame{};
        std::uint32_t airShotCount{};
        std::uint32_t attackShotCount{};
        std::uint32_t pendingAttackShots{};
        float walkSpeed = DEFAULT_WALK_SPEED;
        float runSpeed = DEFAULT_RUN_SPEED;
        bool runningRequested{};
        std::optional<IniDocument> animationDefinitions;
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
        SpriteAnimation hitAnimation;
        SpriteAnimation airHitStartAnimation;
        SpriteAnimation airHitFallAnimation;
        SpriteAnimation knockdownAnimation;
        SpriteAnimation getUpAnimation;
        std::optional<CombatPlayerState> combatState;
        CombatRules combatRules;
        bool bufferedPresentation{}, combatAnimationChanged{true};
        const SpriteAnimation* lastCombatAnimation{};
        float lastCombatAnimationSeconds{}, movementAnimationScale{1.0f};
        float combatPresentationSeconds{}, deathPresentationSeconds{};
        std::deque<CharacterProjectileRequest> pendingProjectileRequests;
    };
}
