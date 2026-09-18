#pragma once

#include "Game/Vector2.h"

namespace ActionRPG
{
    class Camera;
    class D2DRenderer;

    // Owns only gameplay-relevant space such as movement bounds and collision geometry.
    class GameplayMap final
    {
    public:
        [[nodiscard]] Vector2 ClampGroundPosition(Vector2 inPosition, float inHorizontalRadius,
            float inDepthRadius) const;
        void Render(D2DRenderer& inRenderer, const Camera& inCamera) const;

        [[nodiscard]] float GetWorldWidth() const { return WORLD_WIDTH; }
        [[nodiscard]] float GetWorldHeight() const { return WORLD_HEIGHT; }
        [[nodiscard]] float GetWalkableTop() const { return WALKABLE_TOP; }

    private:
        static constexpr float WORLD_WIDTH = 2400.0f;
        static constexpr float WORLD_HEIGHT = 1400.0f;
        static constexpr float WALKABLE_LEFT = 0.0f;
        static constexpr float WALKABLE_TOP = 360.0f;
        static constexpr float WALKABLE_RIGHT = WORLD_WIDTH;
        static constexpr float WALKABLE_BOTTOM = WORLD_HEIGHT;
    };
}
