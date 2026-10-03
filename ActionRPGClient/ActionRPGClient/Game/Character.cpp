#include "Game/Character.h"

#include "Game/Camera.h"
#include "Game/GameplayMap.h"
#include "Graphics/D2DRenderer.h"
#include "Resources/AssetCatalog.h"

#include <d2d1_1helper.h>

#include <algorithm>
#include <utility>
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
    Character::Character(const Vector2 inInitialPosition, const AssetCatalog& inAssetCatalog,
        D2DRenderer& inRenderer, const CharacterAnimationSet& inAnimations)
        : groundPosition(inInitialPosition)
        , animationDefinitions(std::in_place, inAssetCatalog.GetDataPath("Animations"))
        , idleAnimation(inRenderer, inAssetCatalog, *animationDefinitions, inAnimations.idle)
        , walkAnimation(inRenderer, inAssetCatalog, *animationDefinitions, inAnimations.walk)
        , runAnimation(inRenderer, inAssetCatalog, *animationDefinitions, inAnimations.run)
        , attackStartAnimation(inRenderer, inAssetCatalog, *animationDefinitions, inAnimations.attackStart)
        , attackFireAnimation(inRenderer, inAssetCatalog, *animationDefinitions, inAnimations.attackFire)
        , attackEndAnimation(inRenderer, inAssetCatalog, *animationDefinitions, inAnimations.attackEnd)
        , jumpStartAnimation(inRenderer, inAssetCatalog, *animationDefinitions, inAnimations.jumpStart)
        , jumpHoldAnimation(inRenderer, inAssetCatalog, *animationDefinitions, inAnimations.jumpHold)
        , jumpLandAnimation(inRenderer, inAssetCatalog, *animationDefinitions, inAnimations.jumpLand)
        , airAttackStartAnimation(inRenderer, inAssetCatalog, *animationDefinitions, inAnimations.airAttackStart)
        , airAttackFireAnimation(inRenderer, inAssetCatalog, *animationDefinitions, inAnimations.airAttackFire)
        , airAttackEndAnimation(inRenderer, inAssetCatalog, *animationDefinitions, inAnimations.airAttackEnd)
        , hitAnimation(inRenderer, inAssetCatalog, *animationDefinitions, inAnimations.hit)
        , airHitStartAnimation(inRenderer, inAssetCatalog, *animationDefinitions, inAnimations.airHitStart)
        , airHitFallAnimation(inRenderer, inAssetCatalog, *animationDefinitions, inAnimations.airHitFall)
        , knockdownAnimation(inRenderer, inAssetCatalog, *animationDefinitions, inAnimations.knockdown)
        , getUpAnimation(inRenderer, inAssetCatalog, *animationDefinitions, inAnimations.getUp)
    {
        attackEventFrame = ParseOneBasedFrame(*animationDefinitions, inAnimations.attackFire, "event_frame");
        if (attackEventFrame >= attackFireAnimation.GetFrameCount())
        {
            throw std::runtime_error(std::string(inAnimations.attackFire) + " event_frame exceeds frame_count.");
        }
        airAttackEventFrame = ParseOneBasedFrame(*animationDefinitions, inAnimations.airAttackFire, "event_frame");
        if (airAttackEventFrame >= airAttackFireAnimation.GetFrameCount())
        {
            throw std::runtime_error(std::string(inAnimations.airAttackFire) + " event_frame exceeds frame_count.");
        }
    }

    void Character::UpdateActions(const float inDeltaSeconds, const CharacterActions& inActions,
        const GameplayMap& inGameplayMap)
    {
        if (!animationDefinitions) throw std::logic_error("Use the monster presentation controller.");
        const Vector2 previousGroundPosition = groundPosition;
        if (IsHitReacting())
        {
            UpdateHit(inDeltaSeconds);
            return;
        }
        runningRequested = runningEnabled && inActions.run;
        Vector2 direction = inActions.moveDirection;

        if (inActions.jump && jumpPhase == JumpPhase::Grounded)
        {
            BeginJump();
        }

        // Attacks requested during preparation begin aiming immediately after takeoff.
        for (std::uint32_t attackIndex = 0; attackIndex < inActions.attackPressCount; ++attackIndex)
        {
            if (jumpPhase == JumpPhase::Landing)
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

        if (inActions.projectileType.has_value())
        {
            QueueProjectileRequest(*inActions.projectileType);
        }

        if (IsAttacking() && (!airAttack || jumpPhase == JumpPhase::Airborne))
        {
            UpdateAttack(airAttack ? airborneSeconds : inDeltaSeconds);
        }
        // Recoil needs a second collision check only when a shot displaced the character.
        if (groundPosition.x != beforeAttackPosition.x || groundPosition.y != beforeAttackPosition.y)
        {
            groundPosition = inGameplayMap.ConstrainGroundMovement(
                beforeAttackPosition, groundPosition, HORIZONTAL_RADIUS, DEPTH_RADIUS);
        }
    }

    /**
     * Interrupt actions immediately. A hit during flight always starts downward motion,
     * even when the incoming attack would otherwise launch a grounded character.
     */
    void Character::ApplyHit(const CharacterHitType inType)
    {
        const bool wasAirborne = height > 0.0f || jumpPhase == JumpPhase::Airborne
            || hitPhase == HitPhase::Launch || hitPhase == HitPhase::Falling;
        CancelAttack();
        pendingProjectileRequests.clear();
        runningRequested = false;
        isMoving = false;
        walkAnimation.Reset();
        runAnimation.Reset();
        jumpPhase = JumpPhase::Grounded;

        if (wasAirborne)
        {
            hitPhase = HitPhase::Falling;
            verticalVelocity = std::min(verticalVelocity, -AIR_HIT_FALL_SPEED);
            airHitFallAnimation.Reset();
        }
        else if (inType == CharacterHitType::Airborne)
        {
            hitPhase = HitPhase::Launch;
            height = 0.0f;
            verticalVelocity = AIRBORNE_HIT_SPEED;
            airHitStartAnimation.Reset();
        }
        else if (hitPhase == HitPhase::Knockdown || hitPhase == HitPhase::GetUp)
        {
            // A hit while down must not replace the lying pose with a standing stagger.
            if (hitPhase == HitPhase::GetUp)
            {
                knockdownAnimation.Reset();
            }
            hitPhase = HitPhase::Knockdown;
            knockdownHoldRemainingSeconds = KNOCKDOWN_HOLD_SECONDS;
        }
        else
        {
            height = 0.0f;
            verticalVelocity = 0.0f;
            hitPhase = HitPhase::Stagger;
            hitAnimation.Reset();
        }
    }

    void Character::ResetActionState()
    {
        bufferedPresentation = false; combatAnimationChanged = true; lastCombatAnimation = nullptr; lastCombatAnimationSeconds = 0;
        combatState.reset(); combatPresentationSeconds = deathPresentationSeconds = 0.0f;
        CancelAttack();
        pendingProjectileRequests.clear();
        jumpPhase = JumpPhase::Grounded;
        hitPhase = HitPhase::None;
        height = 0.0f;
        verticalVelocity = 0.0f;
        knockdownHoldRemainingSeconds = 0.0f;
        airShotCount = 0;
        isMoving = false;
        runningRequested = false;
        walkAnimation.Reset();
        runAnimation.Reset();
    }

    /**
     * Integrate forced flight to the apex/floor, then carry leftover frame time into
     * knockdown and get-up. Only the launch animates in flight; the falling pose is held.
     */
    void Character::UpdateHit(const float inDeltaSeconds)
    {
        float remainingSeconds = inDeltaSeconds;
        while (IsHitReacting())
        {
            if (hitPhase == HitPhase::Stagger)
            {
                (void)hitAnimation.AdvanceOnce(remainingSeconds);
                if (hitAnimation.IsFinished())
                {
                    hitPhase = HitPhase::None;
                }
                break;
            }
            if (hitPhase == HitPhase::Launch || hitPhase == HitPhase::Falling)
            {
                const bool launching = hitPhase == HitPhase::Launch;
                // This form avoids cancellation when already falling close to the floor.
                const float boundarySeconds = launching
                    ? std::max(0.0f, verticalVelocity / GRAVITY)
                    : height <= 0.0f ? 0.0f
                    : 2.0f * height / (std::sqrt(verticalVelocity * verticalVelocity
                        + 2.0f * GRAVITY * height) - verticalVelocity);
                const float elapsedSeconds = std::min(remainingSeconds, boundarySeconds);
                height += verticalVelocity * elapsedSeconds - 0.5f * GRAVITY * elapsedSeconds * elapsedSeconds;
                verticalVelocity -= GRAVITY * elapsedSeconds;
                remainingSeconds -= elapsedSeconds;
                if (launching)
                {
                    (void)airHitStartAnimation.AdvanceOnce(elapsedSeconds);
                }
                if (elapsedSeconds < boundarySeconds)
                {
                    break;
                }
                if (launching)
                {
                    verticalVelocity = 0.0f;
                    hitPhase = HitPhase::Falling;
                    airHitFallAnimation.Reset();
                }
                else
                {
                    height = 0.0f;
                    verticalVelocity = 0.0f;
                    hitPhase = HitPhase::Knockdown;
                    knockdownAnimation.Reset();
                    knockdownHoldRemainingSeconds = KNOCKDOWN_HOLD_SECONDS;
                }
                continue;
            }
            if (hitPhase == HitPhase::Knockdown)
            {
                remainingSeconds = knockdownAnimation.AdvanceOnce(remainingSeconds);
                if (!knockdownAnimation.IsFinished())
                {
                    break;
                }
                const float holdSeconds = std::min(remainingSeconds, knockdownHoldRemainingSeconds);
                remainingSeconds -= holdSeconds;
                knockdownHoldRemainingSeconds -= holdSeconds;
                if (knockdownHoldRemainingSeconds > 0.0f)
                {
                    break;
                }
                hitPhase = HitPhase::GetUp;
                getUpAnimation.Reset();
                continue;
            }
            (void)getUpAnimation.AdvanceOnce(remainingSeconds);
            if (getUpAnimation.IsFinished())
            {
                hitPhase = HitPhase::None;
            }
            break;
        }
    }

    void Character::BeginJump()
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
    float Character::UpdateJump(const float inDeltaSeconds)
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

    void Character::BeginAttack(const bool inAirAttack)
    {
        airAttack = inAirAttack;
        attackPhase = AttackPhase::Start;
        attackShotCount = 0;
        pendingAttackShots = 1;
        GetAttackAnimation().Reset();
    }

    void Character::CancelAttack()
    {
        // airShotCount belongs to the whole jump and is cleared only by BeginJump.
        attackPhase = AttackPhase::None;
        pendingAttackShots = 0;
        attackShotCount = 0;
        attackProjectileQueued = false;
        airAttack = false;
    }

    SpriteAnimation& Character::GetAttackAnimation()
    {
        if (airAttack)
        {
            return attackPhase == AttackPhase::Start ? airAttackStartAnimation
                : attackPhase == AttackPhase::Fire ? airAttackFireAnimation : airAttackEndAnimation;
        }
        return attackPhase == AttackPhase::Start ? attackStartAnimation
            : attackPhase == AttackPhase::Fire ? attackFireAnimation : attackEndAnimation;
    }

    const SpriteAnimation& Character::GetAttackAnimation() const
    {
        if (airAttack)
        {
            return attackPhase == AttackPhase::Start ? airAttackStartAnimation
                : attackPhase == AttackPhase::Fire ? airAttackFireAnimation : airAttackEndAnimation;
        }
        return attackPhase == AttackPhase::Start ? attackStartAnimation
            : attackPhase == AttackPhase::Fire ? attackFireAnimation : attackEndAnimation;
    }

    void Character::ApplyAirShotRecoil()
    {
        height += AIR_SHOT_RECOIL_LIFT;
        verticalVelocity += AIR_SHOT_RECOIL_SPEED;
        groundPosition.x += facingLeft ? AIR_SHOT_RECOIL_BACKWARD : -AIR_SHOT_RECOIL_BACKWARD;
    }

    /**
     * Carry unused update time across preparation, shot cycles and recovery.
     * Each accepted press reserves one shot; a shot cycle emits exactly one projectile.
     */
    void Character::UpdateAttack(const float inDeltaSeconds)
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
                    QueueProjectileRequest(airAttack ? CharacterProjectileType::AirStraight : CharacterProjectileType::Straight);
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

    void Character::ConfigureMovementSpeeds(const float inWalkSpeed, const float inRunSpeed)
    {
        walkSpeed = inWalkSpeed;
        runSpeed = inRunSpeed;
    }

    std::optional<CharacterProjectileRequest> Character::ConsumeProjectileRequest()
    {
        if (pendingProjectileRequests.empty())
        {
            return std::nullopt;
        }

        CharacterProjectileRequest request = pendingProjectileRequests.front();
        pendingProjectileRequests.pop_front();
        return request;
    }

    void Character::QueueProjectileRequest(const CharacterProjectileType inType)
    {
        pendingProjectileRequests.push_back(CharacterProjectileRequest{
            inType,
            groundPosition,
            height,
            Vector2{ facingLeft ? -1.0f : 1.0f, 0.0f }
        });
    }

    void Character::ApplyCombatState(const CombatPlayerState& inState, const CombatRules& inRules,
        const bool inSetPosition)
    {
        combatAnimationChanged = combatAnimationChanged || !combatState || combatState->reaction != inState.reaction
            || combatState->reactionSequence != inState.reactionSequence
            || combatState->shotSequence != inState.shotSequence || combatState->jumpSequence != inState.jumpSequence
            || combatState->shotPhase != inState.shotPhase || combatState->jumpPhase != inState.jumpPhase
            || combatState->shotCount != inState.shotCount || combatState->airShotCount != inState.airShotCount;
        bufferedPresentation = false;
        const bool wasDead = combatState && combatState->hp == 0;
        isMoving = combatState && (std::abs(inState.position.x - combatState->position.x) > 0.5f
            || std::abs(inState.position.y - combatState->position.y) > 0.5f);
        if (inSetPosition) groundPosition = inState.position;
        combatState = inState; combatRules = inRules; combatPresentationSeconds = 0.0f;
        if (!wasDead && inState.hp == 0) deathPresentationSeconds = 0.0f;
        height = inState.height; facingLeft = inState.facingLeft;
        ConfigureMovementSpeeds(inRules.walkSpeed, inRules.runSpeed);
        pendingProjectileRequests.clear();
    }

    void Character::ApplyBufferedCombatState(const CombatPlayerState& inState, const CombatRules& inRules)
    {
        ApplyCombatState(inState, inRules, true);
        bufferedPresentation = true;
        isMoving = inState.presentationMoving && inState.hp != 0 && inState.reaction == CombatReaction::None;
        runningRequested = isMoving && (inState.running || inState.presentationSpeed > (inRules.walkSpeed + inRules.runSpeed) * 0.5f);
        const float speed = runningRequested ? inRules.runSpeed : inRules.walkSpeed;
        movementAnimationScale = isMoving ? std::clamp(inState.presentationSpeed / speed, 0.25f, 2.0f) : 1.0f;
    }

    const SpriteAnimation& Character::GetCombatAnimation() const
    {
        const auto& state = *combatState;
        if (state.hp == 0 || state.reaction == CombatReaction::Dead || state.reaction == CombatReaction::Down)
            return knockdownAnimation;
        if (state.reaction == CombatReaction::Hit) return hitAnimation;
        if (state.reaction == CombatReaction::Falling)
            return state.verticalSpeed > 0.0f ? airHitStartAnimation : airHitFallAnimation;
        if (state.reaction == CombatReaction::Rising) return getUpAnimation;
        if (state.jumpPhase == CombatJumpPhase::Prepare) return jumpStartAnimation;
        if (state.shotPhase != CombatShotPhase::None)
        {
            if (state.airAttack)
                return state.shotPhase == CombatShotPhase::Prepare ? airAttackStartAnimation
                    : state.shotPhase == CombatShotPhase::Fire ? airAttackFireAnimation : airAttackEndAnimation;
            return state.shotPhase == CombatShotPhase::Prepare ? attackStartAnimation
                : state.shotPhase == CombatShotPhase::Fire ? attackFireAnimation : attackEndAnimation;
        }
        if (state.jumpPhase == CombatJumpPhase::Airborne) return jumpHoldAnimation;
        return isMoving ? (IsRunning() ? runAnimation : walkAnimation) : idleAnimation;
    }

    SpriteAnimation& Character::GetCombatAnimation()
    {
        return const_cast<SpriteAnimation&>(std::as_const(*this).GetCombatAnimation());
    }

    // Predict only ground movement; server snapshots decide combat phases, height and damage.
    void Character::UpdateCombatPresentation(const float inDeltaSeconds, const GameplayMap& inMap,
        const Vector2* inLocalDirection, const bool inRun)
    {
        if (!combatState) return;
        const auto& state = *combatState;
        combatPresentationSeconds = bufferedPresentation ? 0.0f : std::min(0.25f, combatPresentationSeconds + inDeltaSeconds);
        if (state.hp == 0) deathPresentationSeconds += inDeltaSeconds;
        if (inLocalDirection)
        {
            const auto previous = groundPosition;
            Vector2 direction = *inLocalDirection;
            const float length = std::sqrt(direction.x * direction.x + direction.y * direction.y);
            const bool allowed = state.hp != 0 && state.reaction == CombatReaction::None
                && state.shotPhase == CombatShotPhase::None && state.jumpPhase == CombatJumpPhase::Grounded;
            runningRequested = allowed && inRun;
            isMoving = allowed && length > 0.0f;
            if (isMoving)
            {
                const float speed = IsRunning() ? runSpeed : walkSpeed;
                groundPosition.x += direction.x / length * speed * inDeltaSeconds;
                groundPosition.y += direction.y / length * speed * inDeltaSeconds;
                if (direction.x != 0.0f) facingLeft = direction.x < 0.0f;
                groundPosition = inMap.ConstrainGroundMovement(previous, groundPosition, HORIZONTAL_RADIUS, DEPTH_RADIUS);
            }
        }
        else if (!bufferedPresentation)
        {
            const float blend = 1.0f - std::exp(-12.0f * inDeltaSeconds);
            groundPosition.x += (state.position.x - groundPosition.x) * blend;
            groundPosition.y += (state.position.y - groundPosition.y) * blend;
        }
        height = state.height;
        if (!bufferedPresentation && height > 0.0f && state.hp != 0)
            height = std::max(0.0f, height + state.verticalSpeed * combatPresentationSeconds
                - 0.5f * combatRules.gravity * combatPresentationSeconds * combatPresentationSeconds);
        auto& animation = GetCombatAnimation();
        float elapsed = combatPresentationSeconds;
        float duration = animation.GetDuration();
        bool loop = false;
        if (state.hp == 0) elapsed = deathPresentationSeconds;
        else if (state.reaction == CombatReaction::Hit)
        { elapsed += combatRules.hitStunSeconds - state.reactionSeconds; duration = combatRules.hitStunSeconds; }
        else if (state.reaction == CombatReaction::Down)
        { elapsed += combatRules.downSeconds - state.reactionSeconds; duration = combatRules.downSeconds; }
        else if (state.reaction == CombatReaction::Rising)
        { elapsed += combatRules.riseSeconds - state.reactionSeconds; duration = combatRules.riseSeconds; }
        else if (state.reaction == CombatReaction::Falling)
        { elapsed = state.verticalSpeed > 0.0f ? combatPresentationSeconds : 0.0f; }
        else if (state.jumpPhase == CombatJumpPhase::Prepare)
        { elapsed += state.jumpSeconds; duration = combatRules.jumpPrepareSeconds; }
        else if (state.shotPhase != CombatShotPhase::None)
        {
            elapsed += state.shotSeconds;
            duration = state.shotPhase == CombatShotPhase::Prepare ? combatRules.shotPrepareSeconds
                : state.shotPhase == CombatShotPhase::Fire ? combatRules.shotIntervalSeconds : combatRules.shotRecoverSeconds;
        }
        else if (state.jumpPhase == CombatJumpPhase::Airborne) elapsed = 0.0f;
        else { animation.Update(inDeltaSeconds * (bufferedPresentation ? movementAnimationScale : 1.0f)); return; }
        const float seconds = std::max(0.0f, elapsed) / duration * animation.GetDuration();
        lastCombatAnimationSeconds = combatAnimationChanged || lastCombatAnimation != &animation
            ? seconds : std::max(lastCombatAnimationSeconds, seconds);
        lastCombatAnimation = &animation; combatAnimationChanged = false;
        animation.Seek(lastCombatAnimationSeconds, loop);
    }

    void Character::Render(D2DRenderer& inRenderer, const Camera& inCamera) const
    {
        const Vector2 groundScreenPosition = inCamera.WorldToScreen(groundPosition);
        const float bodyBottom = groundScreenPosition.y - height;
        inRenderer.FillEllipse(
            groundScreenPosition.x,
            groundScreenPosition.y,
            WIDTH * 0.42f,
            12.0f,
            D2D1::ColorF(0.02f, 0.03f, 0.05f, 0.45f));

        if (combatState)
        {
            GetCombatAnimation().Draw(inRenderer, groundScreenPosition.x, bodyBottom, facingLeft);
        }
        else if (IsHitReacting())
        {
            const SpriteAnimation& animation = hitPhase == HitPhase::Stagger ? hitAnimation
                : hitPhase == HitPhase::Launch ? airHitStartAnimation
                : hitPhase == HitPhase::Falling ? airHitFallAnimation
                : hitPhase == HitPhase::Knockdown ? knockdownAnimation : getUpAnimation;
            animation.Draw(inRenderer, groundScreenPosition.x, bodyBottom, facingLeft);
        }
        else if (jumpPhase == JumpPhase::Preparing)
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
