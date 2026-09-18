#include "Game/Player.h"

#include "Game/Camera.h"
#include "Game/GameplayMap.h"
#include "Graphics/D2DRenderer.h"
#include "Resources/AssetCatalog.h"

#include <d2d1_1helper.h>

#include <algorithm>
#include <cmath>

namespace ActionRPG
{
    Player::Player(const Vector2 inInitialPosition, const AssetCatalog& inAssetCatalog,
        D2DRenderer& inRenderer)
        : groundPosition(inInitialPosition)
        , animationDefinitions(inAssetCatalog.GetDataPath("Animations"))
        , idleAnimation(inRenderer, inAssetCatalog, animationDefinitions, "PlayerIdle")
        , runAnimation(inRenderer, inAssetCatalog, animationDefinitions, "PlayerRun")
    {
    }

    void Player::Update(const float inDeltaSeconds, const InputState& inInput, const GameplayMap& inGameplayMap)
    {
        movementTimeSeconds += inDeltaSeconds;
        runState.Update(inInput, movementTimeSeconds);

        Vector2 direction{
            static_cast<float>(inInput.moveRight) - static_cast<float>(inInput.moveLeft),
            static_cast<float>(inInput.moveDown) - static_cast<float>(inInput.moveUp)
        };

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

            const float movementSpeed = runState.IsRunning() ? RUN_SPEED : WALK_SPEED;
            groundPosition.x += direction.x * movementSpeed * inDeltaSeconds;
            groundPosition.y += direction.y * movementSpeed * inDeltaSeconds;
        }

        if (runState.IsRunning() && lengthSquared > 0.0f)
        {
            runAnimation.Update(inDeltaSeconds);
        }
        else
        {
            runAnimation.Reset();
        }

        groundPosition = inGameplayMap.ClampGroundPosition(groundPosition, HORIZONTAL_RADIUS, DEPTH_RADIUS);

        if (inInput.WasPressed(InputKey::ActionC) && height == 0.0f)
        {
            verticalVelocity = JUMP_SPEED;
        }

        if (height > 0.0f || verticalVelocity > 0.0f)
        {
            verticalVelocity -= GRAVITY * inDeltaSeconds;
            height += verticalVelocity * inDeltaSeconds;

            if (height <= 0.0f)
            {
                height = 0.0f;
                verticalVelocity = 0.0f;
            }
        }

        skillEffectRemainingSeconds = std::max(0.0f, skillEffectRemainingSeconds - inDeltaSeconds);
        if (skillEffectRemainingSeconds == 0.0f)
        {
            activeSkillEffect.reset();
        }
    }

    void Player::ActivateCommandSkill(const SkillEffectDefinition& inEffect)
    {
        activeSkillEffect = inEffect;
        skillEffectRemainingSeconds = inEffect.durationSeconds;
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

        if (runState.IsRunning())
        {
            runAnimation.Draw(inRenderer, groundScreenPosition.x, bodyBottom, facingLeft);
        }
        else
        {
            idleAnimation.Draw(inRenderer, groundScreenPosition.x, bodyBottom, facingLeft);
        }
    }
}
