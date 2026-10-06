#include "Game/Player.h"
#include "Game/PlayerSkillPresentation.h"

#include "Game/Camera.h"
#include "Game/GameplayMap.h"
#include "Graphics/D2DRenderer.h"

#include <d2d1_1helper.h>

#include <algorithm>
#include <cmath>
#include <utility>

namespace
{
    constexpr ActionRPG::CharacterAnimationSet PLAYER_ANIMATIONS{
        "PlayerIdle", "PlayerWalk", "PlayerRun",
        "PlayerShootStart", "PlayerShootFire", "PlayerShootEnd",
        "PlayerJumpStart", "PlayerJumpHold", "PlayerJumpLand",
        "PlayerAirShootStart", "PlayerAirShootFire", "PlayerAirShootEnd",
        "PlayerHit", "PlayerAirHitStart", "PlayerAirHitFall", "PlayerKnockdown", "PlayerGetUp", "PlayerSlide"
    };
}

namespace ActionRPG
{
    Player::Player(const Vector2 inInitialPosition, const AssetCatalog& inAssetCatalog,
        D2DRenderer& inRenderer)
        : Character(inInitialPosition, inAssetCatalog, inRenderer, PLAYER_ANIMATIONS)
    {
    }

    void Player::Update(const float inDeltaSeconds, const InputState& inInput, const GameplayMap& inGameplayMap)
    {
        movementTimeSeconds += inDeltaSeconds;
        if (HasCombatState())
        {
            activeSkillEffect.reset(); skillEffectRemainingSeconds = 0.0f;
            if (IsRunningEnabled() && !IsHitReacting()) runState.Update(inInput, movementTimeSeconds);
            else runState.Reset();
            const Vector2 direction{
                static_cast<float>(inInput.moveRight) - static_cast<float>(inInput.moveLeft),
                static_cast<float>(inInput.moveDown) - static_cast<float>(inInput.moveUp)};
            UpdateCombatPresentation(inDeltaSeconds, inGameplayMap, &direction, runState.IsRunning());
            return;
        }
        UpdateTownPositionCorrection(inDeltaSeconds, inGameplayMap);
        for (auto& [id, seconds] : townSkillCooldowns) seconds = std::max(0.0f, seconds - inDeltaSeconds);
        if (townSkill)
        {
            townSkill->skillSeconds += inDeltaSeconds;
            if (townSkill->skillSeconds < townSkillDuration)
            {
                UpdateActions(inDeltaSeconds, CharacterActions{}, inGameplayMap);
                return;
            }
            townSkill.reset();
        }
        skillEffectRemainingSeconds = std::max(0.0f, skillEffectRemainingSeconds - inDeltaSeconds);
        if (skillEffectRemainingSeconds == 0.0f)
        {
            activeSkillEffect.reset();
        }

        CharacterActions actions;
        if (IsHitReacting())
        {
            runState.Reset();
        }
        else
        {
            if (IsRunningEnabled())
            {
                runState.Update(inInput, movementTimeSeconds);
            }
            else
            {
                runState.Reset();
            }

            actions.moveDirection = Vector2{
                static_cast<float>(inInput.moveRight) - static_cast<float>(inInput.moveLeft),
                static_cast<float>(inInput.moveDown) - static_cast<float>(inInput.moveUp)
            };
            actions.run = runState.IsRunning();
            actions.jump = inInput.WasPressed(InputKey::ActionC);
            for (const InputKey key : inInput.pressedKeys)
            {
                if (key == InputKey::ActionX)
                {
                    ++actions.attackPressCount;
                }
            }
            if (inInput.WasPressed(InputKey::ActionV))
            {
                actions.projectileType = CharacterProjectileType::Arc;
            }
        }
        UpdateActions(inDeltaSeconds, actions, inGameplayMap);
    }

    void Player::ApplyHit(const CharacterHitType inType)
    {
        townSkill.reset();
        runState.Reset();
        Character::ApplyHit(inType);
    }

    void Player::ResetActionState()
    {
        pendingTownCorrection = {};
        townSkill.reset(); townSkillCooldowns.clear();
        skillPlayback.reset();
        runState.Reset();
        Character::ResetActionState();
    }

    void Player::SetRunningEnabled(const bool inEnabled)
    {
        Character::SetRunningEnabled(inEnabled);
        if (!inEnabled)
        {
            runState.Reset();
        }
    }

    void Player::ActivateCommandSkill(const SkillEffectDefinition& inEffect)
    {
        if (IsHitReacting())
        {
            return;
        }
        activeSkillEffect = inEffect;
        skillEffectRemainingSeconds = inEffect.durationSeconds;
    }

    bool Player::ActivateCatalogSkill(const std::string& inId, const std::unordered_map<std::string,std::uint32_t>& inSkillLevels)
    {
        const auto learned=inSkillLevels.find(inId);
        if (learned==inSkillLevels.end() || learned->second==0 || HasCombatState() || townSkill
            || !skillPresentation || !CanStartCommandSkill()) return false;
        const auto& definition = skillPresentation->GetDefinition(inId);
        const bool airborne = GetHeight() > 0;
        const auto& variant = definition.at(airborne ? "air" : "ground");
        const auto cooldown = townSkillCooldowns.find(inId);
        if (variant.is_null() || (cooldown != townSkillCooldowns.end() && cooldown->second > 0)) return false;
        PrepareCommandSkill();
        CombatPlayerState preview;
        preview.hp = 1; preview.skillId = inId; preview.skillActive = true;
        preview.skillAirborne = airborne; preview.facingLeft = GetFacingLeft();
        townSkill = std::move(preview); townSkillDuration = variant.at("durationSeconds").get<float>();
        townSkillCooldowns[inId] = definition.at("cooldownSeconds").get<float>();
        return true;
    }

    /** Align a matching slide snapshot to local preview time; ignore poses preceding its input. */
    void Player::ReconcileCombatGroundPosition(const CombatPlayerState& inState, const GameplayMap& inMap)
    {
        const auto pending = GetPendingSlideSequence();
        if (pending != 0 && inState.actionSequence < pending) return;
        Vector2 target = inState.position;
        if (inState.slideActive)
        {
            const float age = std::clamp(GetSlidePresentationSeconds() - inState.slideSeconds, 0.0f,
                std::min(0.1f, std::max(0.0f, inState.slideDurationSeconds - inState.slideSeconds)));
            target.x += inState.slideDirectionX * inState.slideSpeed * age;
            target.y += inState.slideDirectionY * inState.slideSpeed * age;
            target = inMap.ConstrainGroundMovement(inState.position, target, HORIZONTAL_RADIUS, DEPTH_RADIUS);
        }
        ReconcileGroundPosition(target);
    }

    void Player::ReconcileGroundPosition(const Vector2 inAuthoritativePosition)
    {
        Vector2 position = GetGroundPosition();
        const float differenceX = inAuthoritativePosition.x - position.x;
        const float differenceY = inAuthoritativePosition.y - position.y;
        const float distanceSquared = differenceX * differenceX + differenceY * differenceY;
        constexpr float CORRECTION_DEAD_ZONE = 16.0f;
        if (distanceSquared <= CORRECTION_DEAD_ZONE * CORRECTION_DEAD_ZONE) return;
        constexpr float SNAP_DISTANCE = 200.0f;
        if (distanceSquared > SNAP_DISTANCE * SNAP_DISTANCE)
        {
            SetGroundPosition(inAuthoritativePosition);
            return;
        }
        constexpr float CORRECTION_RATIO = 0.35f;
        position.x += differenceX * CORRECTION_RATIO;
        position.y += differenceY * CORRECTION_RATIO;
        SetGroundPosition(position);
    }

    void Player::ReconcileTownGroundPosition(const Vector2 inAuthoritativePosition, const Vector2 inVelocity,
        const float inSnapshotDelaySeconds, const GameplayMap& inGameplayMap)
    {
        // Server positions describe a past sample. Bound extrapolation and respect map collision.
        const float age = std::clamp(inSnapshotDelaySeconds, 0.0f, 0.25f);
        const Vector2 extrapolated{inAuthoritativePosition.x + inVelocity.x * age,
            inAuthoritativePosition.y + inVelocity.y * age};
        const Vector2 target = inGameplayMap.ConstrainGroundMovement(inAuthoritativePosition,
            extrapolated, HORIZONTAL_RADIUS, DEPTH_RADIUS);
        const Vector2 position = GetGroundPosition();
        const Vector2 difference{target.x - position.x, target.y - position.y};
        const float distanceSquared = difference.x * difference.x + difference.y * difference.y;
        constexpr float CORRECTION_DEAD_ZONE = 16.0f;
        constexpr float SNAP_DISTANCE = 200.0f;
        if (distanceSquared > SNAP_DISTANCE * SNAP_DISTANCE)
        {
            SetGroundPosition(target);
            pendingTownCorrection = {};
        }
        else
        {
            pendingTownCorrection = distanceSquared > CORRECTION_DEAD_ZONE * CORRECTION_DEAD_ZONE
                ? difference : Vector2{};
        }
    }

    /** Apply small corrections per game frame rather than moving 35% on every arriving packet. */
    void Player::UpdateTownPositionCorrection(const float inDeltaSeconds, const GameplayMap& inGameplayMap)
    {
        constexpr float CORRECTION_RATE = 12.0f;
        const float blend = 1.0f - std::exp(-CORRECTION_RATE * inDeltaSeconds);
        const Vector2 position = GetGroundPosition();
        const Vector2 proposed{position.x + pendingTownCorrection.x * blend,
            position.y + pendingTownCorrection.y * blend};
        SetGroundPosition(inGameplayMap.ConstrainGroundMovement(position, proposed,
            HORIZONTAL_RADIUS, DEPTH_RADIUS));
        pendingTownCorrection.x *= 1.0f - blend;
        pendingTownCorrection.y *= 1.0f - blend;
    }

    void Player::Render(D2DRenderer& inRenderer, const Camera& inCamera) const
    {
        if (townSkill && skillPresentation
            && skillPresentation->Render(*townSkill, GetGroundPosition(), GetHeight(), townSkill->skillSeconds, inRenderer, inCamera)) return;
        const auto state = GetCombatPlayerState();
        if (state && !IsSliding() && !HasPendingSlide() && skillPresentation && !state->skillId.empty()
            && state->hp != 0 && state->reaction == CombatReaction::None)
        {
            const float seconds = state->skillSeconds + GetCombatElapsedSeconds();
            if (!skillPlayback || skillPlayback->sequence != state->skillSequence
                || skillPlayback->id != state->skillId || skillPlayback->airborne != state->skillAirborne)
                skillPlayback = SkillPlayback{state->skillSequence, state->skillId, state->skillAirborne, seconds};
            else skillPlayback->seconds = std::max(skillPlayback->seconds, seconds);
            if (skillPresentation->Render(*state, GetGroundPosition(), GetHeight(), skillPlayback->seconds, inRenderer, inCamera)) return;
        }
        else skillPlayback.reset();
        if (activeSkillEffect.has_value())
        {
            const Vector2 groundScreenPosition = inCamera.WorldToScreen(GetGroundPosition());
            const float bodyBottom = groundScreenPosition.y - GetHeight();
            const ColorValue& auraColor = activeSkillEffect->auraColor;
            inRenderer.FillEllipse(
                groundScreenPosition.x,
                bodyBottom - HEIGHT * 0.5f,
                WIDTH * activeSkillEffect->auraScaleX,
                HEIGHT * activeSkillEffect->auraScaleY,
                D2D1::ColorF(auraColor.red, auraColor.green, auraColor.blue, auraColor.alpha));
        }
        Character::Render(inRenderer, inCamera);
    }
}
