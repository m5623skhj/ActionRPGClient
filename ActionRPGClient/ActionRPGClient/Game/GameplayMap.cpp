#include "Game/GameplayMap.h"

#include "Game/Camera.h"
#include "Graphics/D2DRenderer.h"

#include <d2d1_1helper.h>

#include <algorithm>

namespace ActionRPG
{
    Vector2 GameplayMap::ClampGroundPosition(const Vector2 inPosition, const float inHorizontalRadius,
        const float inDepthRadius) const
    {
        return Vector2{
            std::clamp(inPosition.x, WALKABLE_LEFT + inHorizontalRadius, WALKABLE_RIGHT - inHorizontalRadius),
            std::clamp(inPosition.y, WALKABLE_TOP + inDepthRadius, WALKABLE_BOTTOM - inDepthRadius)
        };
    }

    void GameplayMap::Render(D2DRenderer& inRenderer, const Camera& inCamera) const
    {
        const Vector2 topLeft = inCamera.WorldToScreen(Vector2{ WALKABLE_LEFT, WALKABLE_TOP });
        const Vector2 bottomRight = inCamera.WorldToScreen(Vector2{ WALKABLE_RIGHT, WALKABLE_BOTTOM });

        inRenderer.FillRectangle(
            topLeft.x,
            topLeft.y,
            bottomRight.x,
            bottomRight.y,
            D2D1::ColorF(0.15f, 0.18f, 0.20f));

        constexpr float GRID_SIZE = 160.0f;
        for (float x = WALKABLE_LEFT; x <= WALKABLE_RIGHT; x += GRID_SIZE)
        {
            const Vector2 top = inCamera.WorldToScreen(Vector2{ x, WALKABLE_TOP });
            const Vector2 bottom = inCamera.WorldToScreen(Vector2{ x, WALKABLE_BOTTOM });
            inRenderer.FillRectangle(
                top.x,
                top.y,
                top.x + 1.0f,
                bottom.y,
                D2D1::ColorF(0.22f, 0.25f, 0.28f));
        }

        for (float y = WALKABLE_TOP; y <= WALKABLE_BOTTOM; y += GRID_SIZE)
        {
            const Vector2 left = inCamera.WorldToScreen(Vector2{ WALKABLE_LEFT, y });
            const Vector2 right = inCamera.WorldToScreen(Vector2{ WALKABLE_RIGHT, y });
            inRenderer.FillRectangle(
                left.x,
                left.y,
                right.x,
                left.y + 1.0f,
                D2D1::ColorF(0.22f, 0.25f, 0.28f));
        }

        inRenderer.DrawRectangle(
            topLeft.x,
            topLeft.y,
            bottomRight.x,
            bottomRight.y,
            D2D1::ColorF(0.52f, 0.58f, 0.64f),
            3.0f);
    }
}
