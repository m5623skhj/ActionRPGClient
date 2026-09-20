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
        , runAnimation(inRenderer, inAssetCatalog, animationDefinitions, "PlayerRun")
        , attackAnimation(inRenderer, inAssetCatalog, animationDefinitions, "PlayerShoot")
    {
        attackEventFrame = ParseOneBasedFrame(animationDefinitions, "PlayerShoot", "event_frame");
        if (attackEventFrame >= attackAnimation.GetFrameCount())
        {
            throw std::runtime_error("PlayerShoot event_frame exceeds frame_count.");
        }
    }

    void Player::Update(const float inDeltaSeconds, const InputState& inInput, const GameplayMap& inGameplayMap)
    {
        const Vector2 previousGroundPosition = groundPosition;
        movementTimeSeconds += inDeltaSeconds;
        runState.Update(inInput, movementTimeSeconds);

        Vector2 direction{
            static_cast<float>(inInput.moveRight) - static_cast<float>(inInput.moveLeft),
            static_cast<float>(inInput.moveDown) - static_cast<float>(inInput.moveUp)
        };

        if (inInput.WasPressed(InputKey::ActionX) && !isAttacking)
        {
            isAttacking = true;
            attackProjectileQueued = false;
            attackAnimation.Reset();
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

            if (!isAttacking)
            {
                const float movementSpeed = runState.IsRunning() ? runSpeed : walkSpeed;
                groundPosition.x += direction.x * movementSpeed * inDeltaSeconds;
                groundPosition.y += direction.y * movementSpeed * inDeltaSeconds;
            }
        }

        if (!isAttacking && runState.IsRunning() && lengthSquared > 0.0f)
        {
            runAnimation.Update(inDeltaSeconds);
        }
        else
        {
            runAnimation.Reset();
        }

        groundPosition = inGameplayMap.ConstrainGroundMovement(
            previousGroundPosition, groundPosition, HORIZONTAL_RADIUS, DEPTH_RADIUS);

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

        if (inInput.WasPressed(InputKey::ActionV))
        {
            QueueProjectileRequest(PlayerProjectileType::Arc);
        }

        if (isAttacking)
        {
            attackAnimation.Update(inDeltaSeconds, false);
            if (!attackProjectileQueued && attackAnimation.GetCurrentFrame() >= attackEventFrame)
            {
                QueueProjectileRequest(PlayerProjectileType::Straight);
                attackProjectileQueued = true;
            }

            if (attackAnimation.IsFinished())
            {
                isAttacking = false;
                attackAnimation.Reset();
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

    void Player::ReconcileGroundPosition(const Vector2 inAuthoritativePosition)
    {
        const float differenceX = inAuthoritativePosition.x - groundPosition.x;
        const float differenceY = inAuthoritativePosition.y - groundPosition.y;
        const float distanceSquared = differenceX * differenceX + differenceY * differenceY;
        if (distanceSquared > 200.0f * 200.0f)
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

        if (isAttacking)
        {
            attackAnimation.Draw(inRenderer, groundScreenPosition.x, bodyBottom, facingLeft);
        }
        else if (runState.IsRunning())
        {
            runAnimation.Draw(inRenderer, groundScreenPosition.x, bodyBottom, facingLeft);
        }
        else
        {
            idleAnimation.Draw(inRenderer, groundScreenPosition.x, bodyBottom, facingLeft);
        }
    }
}
