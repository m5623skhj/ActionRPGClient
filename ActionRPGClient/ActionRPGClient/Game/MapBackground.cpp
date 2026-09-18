#include "Game/MapBackground.h"

#include "Game/Camera.h"
#include "Game/Vector2.h"
#include "Graphics/D2DRenderer.h"

#include <d2d1_1helper.h>

namespace ActionRPG
{
    MapBackground::MapBackground(const float inWorldWidth, const float inBackgroundHeight)
        : worldWidth(inWorldWidth)
        , backgroundHeight(inBackgroundHeight)
    {
    }

    void MapBackground::Render(D2DRenderer& inRenderer, const Camera& inCamera) const
    {
        const Vector2 worldTopLeft = inCamera.WorldToScreen(Vector2{ 0.0f, 0.0f });
        const Vector2 wallBottom = inCamera.WorldToScreen(Vector2{ worldWidth, backgroundHeight });
        inRenderer.FillRectangle(
            worldTopLeft.x,
            worldTopLeft.y,
            wallBottom.x,
            wallBottom.y,
            D2D1::ColorF(0.08f, 0.11f, 0.17f));

        constexpr float PILLAR_SPACING = 420.0f;
        constexpr float PILLAR_WIDTH = 72.0f;
        for (float x = 180.0f; x < worldWidth; x += PILLAR_SPACING)
        {
            const Vector2 pillarTop = inCamera.WorldToScreen(Vector2{ x, 70.0f });
            const Vector2 pillarBottom = inCamera.WorldToScreen(Vector2{ x + PILLAR_WIDTH, backgroundHeight });
            inRenderer.FillRectangle(
                pillarTop.x,
                pillarTop.y,
                pillarBottom.x,
                pillarBottom.y,
                D2D1::ColorF(0.12f, 0.16f, 0.23f));
        }

        const Vector2 trimTop = inCamera.WorldToScreen(Vector2{ 0.0f, backgroundHeight - 40.0f });
        const Vector2 trimBottom = inCamera.WorldToScreen(Vector2{ worldWidth, backgroundHeight - 10.0f });
        inRenderer.FillRectangle(
            trimTop.x,
            trimTop.y,
            trimBottom.x,
            trimBottom.y,
            D2D1::ColorF(0.24f, 0.30f, 0.39f));
    }
}
