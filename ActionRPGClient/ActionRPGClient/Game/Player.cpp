#include "Game/Player.h"

#include "Game/Camera.h"
#include "Game/GameplayMap.h"
#include "Graphics/D2DRenderer.h"
#include "Resources/AssetCatalog.h"

#include <d2d1_1helper.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

namespace
{
    std::uint32_t ParseOneBasedFrame(const ActionRPG::IniDocument& inDocument,
        const std::string_view inSection, const std::string_view inKey)
    {
        const std::string& text = inDocument.GetValue(inSection, inKey);
        std::size_t parsedCharacters{};
        const unsigned long value = std::stoul(text, &parsedCharacters);
        if (parsedCharacters != text.size() || value == 0
            || value > std::numeric_limits<std::uint32_t>::max())
        {
            throw std::runtime_error("Invalid one-based animation frame: " + text);
        }
        return static_cast<std::uint32_t>(value - 1);
    }
}

namespace ActionRPG
{
    Player::Player(const Vector2 inInitialPosition, const AssetCatalog& inAssetCatalog,
        D2DRenderer& inRenderer)
        : groundPosition(inInitialPosition)
        , animationDefinitions(inAssetCatalog.GetDataPath("Animations"))
        , idleAnimation(inRenderer, inAssetCatalog, animationDefinitions, "PlayerIdle")
        , walkAnimation(inRenderer, inAssetCatalog, animationDefinitions, "PlayerWalk")
        , runAnimation(inRenderer, inAssetCatalog, animationDefinitions, "PlayerRun")
        , attackStartAnimation(inRenderer, inAssetCatalog, animationDefinitions, "PlayerShootStart")
        , attackFireAnimation(inRenderer, inAssetCatalog, animationDefinitions, "PlayerShootFire")
        , attackEndAnimation(inRenderer, inAssetCatalog, animationDefinitions, "PlayerShootEnd")
        , jumpStartAnimation(inRenderer, inAssetCatalog, animationDefinitions, "PlayerJumpStart")
        , jumpHoldAnimation(inRenderer, inAssetCatalog, animationDefinitions, "PlayerJumpHold")
        , jumpLandAnimation(inRenderer, inAssetCatalog, animationDefinitions, "PlayerJumpLand")
        , airAttackStartAnimation(inRenderer, inAssetCatalog, animationDefinitions, "PlayerAirShootStart")
        , airAttackFireAnimation(inRenderer, inAssetCatalog, animationDefinitions, "PlayerAirShootFire")
        , airAttackEndAnimation(inRenderer, inAssetCatalog, animationDefinitions, "PlayerAirShootEnd")
    {
        attackEventFrame = ParseOneBasedFrame(animationDefinitions, "PlayerShootFire", "event_frame");
        if (attackEventFrame >= attackFireAnimation.GetFrameCount())
        {
            throw std::runtime_error("PlayerShootFire event_frame exceeds frame_count.");
        }
        airAttackEventFrame = ParseOneBasedFrame(animationDefinitions, "PlayerAirShootFire", "event_frame");
        if (airAttackEventFrame >= airAttackFireAnimation.GetFrameCount())
        {
            throw std::runtime_error("PlayerAirShootFire event_frame exceeds frame_count.");
        }
    }

    void Player::Update(const float inDeltaSeconds, const InputState& inInput, const GameplayMap& inGameplayMap)
    {
        const Vector2 previousGroundPosition = groundPosition;
        movementTimeSeconds += inDeltaSeconds;
        if (runningEnabled)
        {
            runState.Update(inInput, movementTimeSeconds);
        }
        else
        {
            runState.Reset();
        }

        Vector2 direction{
            static_cast<float>(inInput.moveRight) - static_cast<float>(inInput.moveLeft),
            static_cast<float>(inInput.moveDown) - static_cast<float>(inInput.moveUp)
        };

        if (inInput.WasPressed(InputKey::ActionC) && jumpPhase == JumpPhase::Grounded)
        {
            BeginJump();
        }

        // Jump preparation also accepts X presses, to begin aiming immediately after takeoff.
        for (const InputKey key : inInput.pressedKeys)
        {
            if (key != InputKey::ActionX || jumpPhase == JumpPhase::Landing)
            {
                continue;
            }

            if (!IsAttacking())
            {
                const bool wantsAirAttack = jumpPhase != JumpPhase::Grounded;
                if (!wantsAirAttack || airShotCount < MAX_ATTACK_SHOTS)
                {
                    BeginAttack(wantsAirAttack);
                }
            }
            else if ((airAttack ? airShotCount : attackShotCount) + pendingAttackShots < MAX_ATTACK_SHOTS)
            {
                ++pendingAttackShots;
            }
        }

        const float lengthSquared = direction.x * direction.x + direction.y * direction.y;
        if (lengthSquared > 0.0f)
        {
            if (direction.x != 0.0f)
            {
                facingLeft = direction.x < 0.0f;
            }

            const float inverseLength = 1.0f / std::sqrt(lengthSquared);
            direction.x *= inverseLength;
            direction.y *= inverseLength;

            if (!IsAttacking()
                && (jumpPhase == JumpPhase::Grounded || jumpPhase == JumpPhase::Airborne))
            {
                const float movementSpeed = IsRunning() ? runSpeed : walkSpeed;
                groundPosition.x += direction.x * movementSpeed * inDeltaSeconds;
                groundPosition.y += direction.y * movementSpeed * inDeltaSeconds;
            }
        }

        isMoving = jumpPhase == JumpPhase::Grounded && !IsAttacking() && lengthSquared > 0.0f;
        if (isMoving && IsRunning())
        {
            runAnimation.Update(inDeltaSeconds);
            walkAnimation.Reset();
        }
        else if (isMoving)
        {
            walkAnimation.Update(inDeltaSeconds);
            runAnimation.Reset();
        }
        else
        {
            walkAnimation.Reset();
            runAnimation.Reset();
        }

        groundPosition = inGameplayMap.ConstrainGroundMovement(
            previousGroundPosition, groundPosition, HORIZONTAL_RADIUS, DEPTH_RADIUS);
        const Vector2 beforeAttackPosition = groundPosition;
        const float airborneSeconds = UpdateJump(inDeltaSeconds);

        if (inInput.WasPressed(InputKey::ActionV))
        {
            QueueProjectileRequest(PlayerProjectileType::Arc);
        }

        if (IsAttacking() && (!airAttack || jumpPhase == JumpPhase::Airborne))
        {
            UpdateAttack(airAttack ? airborneSeconds : inDeltaSeconds);
        }
        // Recoil needs a second collision check only when a shot displaced the player.
        if (groundPosition.x != beforeAttackPosition.x || groundPosition.y != beforeAttackPosition.y)
        {
            groundPosition = inGameplayMap.ConstrainGroundMovement(
                beforeAttackPosition, groundPosition, HORIZONTAL_RADIUS, DEPTH_RADIUS);
        }

        skillEffectRemainingSeconds = std::max(0.0f, skillEffectRemainingSeconds - inDeltaSeconds);
        if (skillEffectRemainingSeconds == 0.0f)
        {
            activeSkillEffect.reset();
        }
    }

    void Player::BeginJump()
    {
        CancelAttack();
        airShotCount = 0;
        jumpPhase = JumpPhase::Preparing;
        jumpStartAnimation.Reset();
    }

    /**
     * Advance preparation/landing once and integrate height with a fixed airborne sprite.
     * @return Time spent airborne this update, excluding time consumed by preparation.
     */
    float Player::UpdateJump(const float inDeltaSeconds)
    {
        float remainingSeconds = inDeltaSeconds;
        if (jumpPhase == JumpPhase::Preparing)
        {
            remainingSeconds = jumpStartAnimation.AdvanceOnce(remainingSeconds);
            if (!jumpStartAnimation.IsFinished())
            {
                return 0.0f;
            }
            jumpPhase = JumpPhase::Airborne;
            verticalVelocity = JUMP_SPEED;
        }

        if (jumpPhase == JumpPhase::Airborne)
        {
            verticalVelocity -= GRAVITY * remainingSeconds;
            height += verticalVelocity * remainingSeconds;
            if (height <= 0.0f && verticalVelocity <= 0.0f)
            {
                height = 0.0f;
                verticalVelocity = 0.0f;
                CancelAttack();
                jumpPhase = JumpPhase::Landing;
                jumpLandAnimation.Reset();
                return 0.0f;
            }
            return remainingSeconds;
        }

        if (jumpPhase == JumpPhase::Landing)
        {
            (void)jumpLandAnimation.AdvanceOnce(remainingSeconds);
            if (jumpLandAnimation.IsFinished())
            {
                jumpPhase = JumpPhase::Grounded;
            }
        }
        return 0.0f;
    }

    void Player::BeginAttack(const bool inAirAttack)
    {
        airAttack = inAirAttack;
        attackPhase = AttackPhase::Start;
        attackShotCount = 0;
        pendingAttackShots = 1;
        GetAttackAnimation().Reset();
    }

    void Player::CancelAttack()
    {
        // airShotCount belongs to the whole jump and is cleared only by BeginJump.
        attackPhase = AttackPhase::None;
        pendingAttackShots = 0;
        attackShotCount = 0;
        attackProjectileQueued = false;
        airAttack = false;
    }

    SpriteAnimation& Player::GetAttackAnimation()
    {
        if (airAttack)
        {
            return attackPhase == AttackPhase::Start ? airAttackStartAnimation
                : attackPhase == AttackPhase::Fire ? airAttackFireAnimation : airAttackEndAnimation;
        }
        return attackPhase == AttackPhase::Start ? attackStartAnimation
            : attackPhase == AttackPhase::Fire ? attackFireAnimation : attackEndAnimation;
    }

    const SpriteAnimation& Player::GetAttackAnimation() const
    {
        if (airAttack)
        {
            return attackPhase == AttackPhase::Start ? airAttackStartAnimation
                : attackPhase == AttackPhase::Fire ? airAttackFireAnimation : airAttackEndAnimation;
        }
        return attackPhase == AttackPhase::Start ? attackStartAnimation
            : attackPhase == AttackPhase::Fire ? attackFireAnimation : attackEndAnimation;
    }

    void Player::ApplyAirShotRecoil()
    {
        height += AIR_SHOT_RECOIL_LIFT;
        verticalVelocity += AIR_SHOT_RECOIL_SPEED;
        groundPosition.x += facingLeft ? AIR_SHOT_RECOIL_BACKWARD : -AIR_SHOT_RECOIL_BACKWARD;
    }

    /**
     * Carry unused update time across preparation, shot cycles and recovery.
     * Each accepted press reserves one shot; a shot cycle emits exactly one projectile.
     */
    void Player::UpdateAttack(const float inDeltaSeconds)
    {
        float remainingSeconds = inDeltaSeconds;
        while (IsAttacking())
        {
            SpriteAnimation& animation = GetAttackAnimation();

            const auto emitShot = [this]()
            {
                if (attackPhase == AttackPhase::Fire && !attackProjectileQueued
                    && GetAttackAnimation().GetCurrentFrame() >= (airAttack ? airAttackEventFrame : attackEventFrame))
                {
                    QueueProjectileRequest(airAttack ? PlayerProjectileType::AirStraight : PlayerProjectileType::Straight);
                    attackProjectileQueued = true;
                    --pendingAttackShots;
                    ++attackShotCount;
                    if (airAttack)
                    {
                        ++airShotCount;
                        ApplyAirShotRecoil();
                    }
                }
            };

            emitShot();
            remainingSeconds = animation.AdvanceOnce(remainingSeconds);
            emitShot();
            if (!animation.IsFinished())
            {
                break;
            }

            if (attackPhase == AttackPhase::Start
                || (attackPhase == AttackPhase::Fire && pendingAttackShots > 0))
            {
                attackPhase = AttackPhase::Fire;
                attackProjectileQueued = false;
                GetAttackAnimation().Reset();
            }
            else if (attackPhase == AttackPhase::Fire)
            {
                attackPhase = AttackPhase::End;
                GetAttackAnimation().Reset();
            }
            else if (pendingAttackShots > 0)
            {
                // A late press during lowering starts with preparation, within the same five-shot limit.
                attackPhase = AttackPhase::Start;
                GetAttackAnimation().Reset();
            }
            else
            {
                CancelAttack();
            }
        }
    }

    void Player::ActivateCommandSkill(const SkillEffectDefinition& inEffect)
    {
        activeSkillEffect = inEffect;
        skillEffectRemainingSeconds = inEffect.durationSeconds;
    }

    void Player::ReconcileGroundPosition(const Vector2 inAuthoritativePosition)
    {
        const float differenceX = inAuthoritativePosition.x - groundPosition.x;
        const float differenceY = inAuthoritativePosition.y - groundPosition.y;
        const float distanceSquared = differenceX * differenceX + differenceY * differenceY;

        // A 20 Hz server tick can leave the local prediction about 14 pixels ahead at walking speed.
        constexpr float CORRECTION_DEAD_ZONE = 16.0f;
        if (distanceSquared <= CORRECTION_DEAD_ZONE * CORRECTION_DEAD_ZONE)
        {
            return;
        }

        constexpr float SNAP_DISTANCE = 200.0f;
        if (distanceSquared > SNAP_DISTANCE * SNAP_DISTANCE)
        {
            groundPosition = inAuthoritativePosition;
            return;
        }

        constexpr float CORRECTION_RATIO = 0.35f;
        groundPosition.x += differenceX * CORRECTION_RATIO;
        groundPosition.y += differenceY * CORRECTION_RATIO;
    }

    void Player::ConfigureMovementSpeeds(const float inWalkSpeed, const float inRunSpeed)
    {
        walkSpeed = inWalkSpeed;
        runSpeed = inRunSpeed;
    }

    std::optional<PlayerProjectileRequest> Player::ConsumeProjectileRequest()
    {
        if (pendingProjectileRequests.empty())
        {
            return std::nullopt;
        }

        PlayerProjectileRequest request = pendingProjectileRequests.front();
        pendingProjectileRequests.pop_front();
        return request;
    }

    void Player::QueueProjectileRequest(const PlayerProjectileType inType)
    {
        pendingProjectileRequests.push_back(PlayerProjectileRequest{
            inType,
            groundPosition,
            height,
            Vector2{ facingLeft ? -1.0f : 1.0f, 0.0f }
        });
    }

    void Player::Render(D2DRenderer& inRenderer, const Camera& inCamera) const
    {
        const Vector2 groundScreenPosition = inCamera.WorldToScreen(groundPosition);
        const float bodyBottom = groundScreenPosition.y - height;
        const float bodyTop = bodyBottom - HEIGHT;

        if (activeSkillEffect.has_value())
        {
            const ColorValue& auraColor = activeSkillEffect->auraColor;
            inRenderer.FillEllipse(
                groundScreenPosition.x,
                bodyTop + HEIGHT * 0.5f,
                WIDTH * activeSkillEffect->auraScaleX,
                HEIGHT * activeSkillEffect->auraScaleY,
                D2D1::ColorF(auraColor.red, auraColor.green, auraColor.blue, auraColor.alpha));
        }

        inRenderer.FillEllipse(
            groundScreenPosition.x,
            groundScreenPosition.y,
            WIDTH * 0.42f,
            12.0f,
            D2D1::ColorF(0.02f, 0.03f, 0.05f, 0.45f));

        if (jumpPhase == JumpPhase::Preparing)
        {
            jumpStartAnimation.Draw(inRenderer, groundScreenPosition.x, bodyBottom, facingLeft);
        }
        else if (jumpPhase == JumpPhase::Landing)
        {
            jumpLandAnimation.Draw(inRenderer, groundScreenPosition.x, bodyBottom, facingLeft);
        }
        else if (IsAttacking())
        {
            GetAttackAnimation().Draw(inRenderer, groundScreenPosition.x, bodyBottom, facingLeft);
        }
        else if (jumpPhase == JumpPhase::Airborne)
        {
            jumpHoldAnimation.Draw(inRenderer, groundScreenPosition.x, bodyBottom, facingLeft);
        }
        else if (isMoving && IsRunning())
        {
            runAnimation.Draw(inRenderer, groundScreenPosition.x, bodyBottom, facingLeft);
        }
        else if (isMoving)
        {
            walkAnimation.Draw(inRenderer, groundScreenPosition.x, bodyBottom, facingLeft);
        }
        else
        {
            idleAnimation.Draw(inRenderer, groundScreenPosition.x, bodyBottom, facingLeft);
        }
    }
}
