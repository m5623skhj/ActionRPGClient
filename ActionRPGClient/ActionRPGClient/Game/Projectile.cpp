#include "Game/Projectile.h"

#include "Game/Camera.h"
#include "Graphics/D2DRenderer.h"

#include <d2d1_1helper.h>

#include <algorithm>

namespace ActionRPG
{
    Projectile::Projectile(const ProjectileDefinition& inDefinition, const Vector2 inThrowerPosition,
        const float inThrowerHeight, const Vector2 inDirection)
        : definition(inDefinition)
        , groundPosition{
            inThrowerPosition.x + inDirection.x * definition.spawnForward,
            inThrowerPosition.y + inDirection.y * definition.spawnForward }
        , groundVelocity{ inDirection.x * definition.speed, inDirection.y * definition.speed }
        , height(inThrowerHeight + definition.spawnHeight)
        , verticalVelocity(definition.verticalSpeed)
        , remainingSeconds(definition.lifetimeSeconds)
    {
    }

    void Projectile::Update(const float inDeltaSeconds)
    {
        remainingSeconds -= inDeltaSeconds;
        groundPosition.x += groundVelocity.x * inDeltaSeconds;
        groundPosition.y += groundVelocity.y * inDeltaSeconds;

        if (definition.motionType == ProjectileMotionType::Arc)
        {
            verticalVelocity -= definition.gravity * inDeltaSeconds;
            height += verticalVelocity * inDeltaSeconds;
            if (height <= 0.0f && verticalVelocity < 0.0f)
            {
                height = 0.0f;
                isAlive = false;
            }
        }

        if (remainingSeconds <= 0.0f)
        {
            isAlive = false;
        }
    }

    void Projectile::Render(D2DRenderer& inRenderer, const Camera& inCamera) const
    {
        const Vector2 groundScreenPosition = inCamera.WorldToScreen(groundPosition);
        if (definition.motionType == ProjectileMotionType::Arc)
        {
            inRenderer.FillEllipse(
                groundScreenPosition.x,
                groundScreenPosition.y,
                definition.radiusX,
                definition.radiusY * 0.45f,
                D2D1::ColorF(0.02f, 0.03f, 0.04f, 0.35f));
        }

        const ColorValue& bodyColor = definition.bodyColor;
        const float screenY = groundScreenPosition.y - height;
        inRenderer.FillEllipse(
            groundScreenPosition.x,
            screenY,
            definition.radiusX,
            definition.radiusY,
            D2D1::ColorF(bodyColor.red, bodyColor.green, bodyColor.blue, bodyColor.alpha));

        const ColorValue& highlightColor = definition.highlightColor;
        inRenderer.FillEllipse(
            groundScreenPosition.x - definition.radiusX * 0.25f,
            screenY - definition.radiusY * 0.25f,
            definition.radiusX * 0.35f,
            definition.radiusY * 0.35f,
            D2D1::ColorF(
                highlightColor.red,
                highlightColor.green,
                highlightColor.blue,
                highlightColor.alpha));
    }
}
