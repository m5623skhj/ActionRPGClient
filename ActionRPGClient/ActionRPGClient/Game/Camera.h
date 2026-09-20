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

        void Follow(const Vector2& inTargetCenter, const float inWorldLeft, const float inWorldTop,
            const float inWorldRight, const float inWorldBottom)
        {
            position.x = std::clamp(
                inTargetCenter.x - viewportWidth * 0.5f,
                inWorldLeft,
                std::max(inWorldLeft, inWorldRight - viewportWidth));
            position.y = std::clamp(
                inTargetCenter.y - viewportHeight * 0.5f,
                inWorldTop,
                std::max(inWorldTop, inWorldBottom - viewportHeight));
        }

        [[nodiscard]] float GetViewportWidth() const { return viewportWidth; }
        [[nodiscard]] float GetViewportHeight() const { return viewportHeight; }

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
