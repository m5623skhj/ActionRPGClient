#pragma once

#include "Game/Vector2.h"

#include <algorithm>

namespace ActionRPG
{
    class Camera final
    {
    public:
        Camera(const float inViewportWidth, const float inViewportHeight)
            : viewportWidth(inViewportWidth)
            , viewportHeight(inViewportHeight)
        {
        }

        void Resize(const float inViewportWidth, const float inViewportHeight)
        {
            viewportWidth = inViewportWidth;
            viewportHeight = inViewportHeight;
        }

        void Follow(const Vector2& inTargetCenter, const float inWorldWidth, const float inWorldHeight)
        {
            position.x = std::clamp(
                inTargetCenter.x - viewportWidth * 0.5f,
                0.0f,
                std::max(0.0f, inWorldWidth - viewportWidth));
            position.y = std::clamp(
                inTargetCenter.y - viewportHeight * 0.5f,
                0.0f,
                std::max(0.0f, inWorldHeight - viewportHeight));
        }

        [[nodiscard]] Vector2 WorldToScreen(const Vector2& inWorldPosition) const
        {
            return Vector2{ inWorldPosition.x - position.x, inWorldPosition.y - position.y };
        }

    private:
        Vector2 position{};
        float viewportWidth{};
        float viewportHeight{};
    };
}
