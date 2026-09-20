#pragma once

#include "Game/Vector2.h"
#include "Network/TownProtocol.h"

#include <vector>

namespace ActionRPG
{
    class Camera;
    class D2DRenderer;

    // Holds the same immutable movement geometry used by TownServer for client prediction.
    class GameplayMap final
    {
    public:
        void Configure(const TownProtocol::MapInfo& inMap);
        [[nodiscard]] Vector2 ConstrainGroundMovement(Vector2 inPrevious, Vector2 inProposed,
            float inHorizontalRadius, float inDepthRadius) const;
        void Render(D2DRenderer& inRenderer, const Camera& inCamera) const;

        [[nodiscard]] float GetWorldLeft() const { return worldLeft; }
        [[nodiscard]] float GetWorldTop() const { return worldTop; }
        [[nodiscard]] float GetWorldRight() const { return worldRight; }
        [[nodiscard]] float GetWorldBottom() const { return worldBottom; }

    private:
        [[nodiscard]] bool IsPositionValid(Vector2 inPosition, float inHorizontalRadius,
            float inDepthRadius) const;

        float worldLeft = 0.0f;
        float worldTop = 0.0f;
        float worldRight = 2400.0f;
        float worldBottom = 1400.0f;
        std::vector<TownProtocol::Polygon> walkablePolygons{
            { { 0.0f, 360.0f }, { 2400.0f, 360.0f }, { 2400.0f, 1400.0f }, { 0.0f, 1400.0f } }
        };
        std::vector<TownProtocol::Polygon> blockedPolygons;
    };
}
