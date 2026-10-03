#include "Game/Player.h"
#include "Game/PlayerSkillPresentation.h"

#include "Game/Camera.h"
#include "Graphics/D2DRenderer.h"

#include <d2d1_1helper.h>

#include <algorithm>

namespace
{
    constexpr ActionRPG::CharacterAnimationSet PLAYER_ANIMATIONS{
        "PlayerIdle", "PlayerWalk", "PlayerRun",
        "PlayerShootStart", "PlayerShootFire", "PlayerShootEnd",
        "PlayerJumpStart", "PlayerJumpHold", "PlayerJumpLand",
        "PlayerAirShootStart", "PlayerAirShootFire", "PlayerAirShootEnd",
        "PlayerHit", "PlayerAirHitStart", "PlayerAirHitFall", "PlayerKnockdown", "PlayerGetUp"
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
        runState.Reset();
        Character::ApplyHit(inType);
    }

    void Player::ResetActionState()
    {
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

    void Player::ReconcileGroundPosition(const Vector2 inAuthoritativePosition)
    {
        Vector2 position = GetGroundPosition();
        const float differenceX = inAuthoritativePosition.x - position.x;
        const float differenceY = inAuthoritativePosition.y - position.y;
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
            SetGroundPosition(inAuthoritativePosition);
            return;
        }

        constexpr float CORRECTION_RATIO = 0.35f;
        position.x += differenceX * CORRECTION_RATIO;
        position.y += differenceY * CORRECTION_RATIO;
        SetGroundPosition(position);
    }

    void Player::Render(D2DRenderer& inRenderer, const Camera& inCamera) const
    {
        if (const auto state = GetCombatPlayerState(); state && skillPresentation
            && skillPresentation->Render(*state, GetGroundPosition(), GetHeight(), GetCombatElapsedSeconds(), inRenderer, inCamera)) return;
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
