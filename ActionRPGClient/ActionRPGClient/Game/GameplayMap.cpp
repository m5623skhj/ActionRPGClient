#include "Game/GameplayMap.h"

#include <algorithm>
#include <cmath>
#include <ranges>

namespace
{
    bool IsPointOnSegment(const ActionRPG::Vector2 inPoint, const TownProtocol::Vector2 inStart,
        const TownProtocol::Vector2 inEnd)
    {
        constexpr float EPSILON = 0.001f;
        const float deltaX = inEnd.x - inStart.x;
        const float deltaY = inEnd.y - inStart.y;
        const float cross = (inPoint.x - inStart.x) * deltaY - (inPoint.y - inStart.y) * deltaX;
        if (std::abs(cross) > EPSILON)
        {
            return false;
        }
        const float dot = (inPoint.x - inStart.x) * deltaX + (inPoint.y - inStart.y) * deltaY;
        const float lengthSquared = deltaX * deltaX + deltaY * deltaY;
        return dot >= -EPSILON && dot <= lengthSquared + EPSILON;
    }

    bool IsPointInPolygon(const ActionRPG::Vector2 inPoint,
        const TownProtocol::Polygon& inPolygon)
    {
        bool inside = false;
        for (std::size_t current = 0, previous = inPolygon.size() - 1;
            current < inPolygon.size(); previous = current++)
        {
            const TownProtocol::Vector2& start = inPolygon[previous];
            const TownProtocol::Vector2& end = inPolygon[current];
            if (IsPointOnSegment(inPoint, start, end))
            {
                return true;
            }
            if ((start.y > inPoint.y) != (end.y > inPoint.y))
            {
                const float intersectionX = (end.x - start.x) * (inPoint.y - start.y)
                    / (end.y - start.y) + start.x;
                if (inPoint.x < intersectionX)
                {
                    inside = !inside;
                }
            }
        }
        return inside;
    }

    bool IsPointInAnyPolygon(const ActionRPG::Vector2 inPoint,
        const std::vector<TownProtocol::Polygon>& inPolygons)
    {
        return std::ranges::any_of(inPolygons, [inPoint](const TownProtocol::Polygon& inPolygon)
        {
            return IsPointInPolygon(inPoint, inPolygon);
        });
    }
}

namespace ActionRPG
{
    void GameplayMap::Configure(const TownProtocol::MapInfo& inMap)
    {
        worldLeft = inMap.worldLeft;
        worldTop = inMap.worldTop;
        worldRight = inMap.worldRight;
        worldBottom = inMap.worldBottom;
        walkablePolygons = inMap.walkablePolygons;
        blockedPolygons = inMap.blockedPolygons;
    }

    bool GameplayMap::IsPositionValid(const Vector2 inPosition, const float inHorizontalRadius,
        const float inDepthRadius) const
    {
        if (inPosition.x < worldLeft || inPosition.x > worldRight
            || inPosition.y < worldTop || inPosition.y > worldBottom)
        {
            return false;
        }

        const auto isPointValid = [this](const Vector2 inPoint)
        {
            return IsPointInAnyPolygon(inPoint, walkablePolygons)
                && !IsPointInAnyPolygon(inPoint, blockedPolygons);
        };
        if (!isPointValid(inPosition))
        {
            return false;
        }

        constexpr int SAMPLE_COUNT = 16;
        constexpr float TWO_PI = 6.28318530717958647692f;
        for (int index = 0; index < SAMPLE_COUNT; ++index)
        {
            const float angle = TWO_PI * static_cast<float>(index) / static_cast<float>(SAMPLE_COUNT);
            if (!isPointValid(Vector2{
                inPosition.x + std::cos(angle) * inHorizontalRadius,
                inPosition.y + std::sin(angle) * inDepthRadius }))
            {
                return false;
            }
        }
        return true;
    }

    Vector2 GameplayMap::ConstrainGroundMovement(const Vector2 inPrevious, const Vector2 inProposed,
        const float inHorizontalRadius, const float inDepthRadius) const
    {
        if (!IsPositionValid(inPrevious, inHorizontalRadius, inDepthRadius))
        {
            return inProposed;
        }

        const float deltaX = inProposed.x - inPrevious.x;
        const float deltaY = inProposed.y - inPrevious.y;
        const float distance = std::sqrt(deltaX * deltaX + deltaY * deltaY);
        const int stepCount = std::max(1, static_cast<int>(std::ceil(distance / 4.0f)));
        Vector2 lastValid = inPrevious;
        for (int step = 1; step <= stepCount; ++step)
        {
            const float ratio = static_cast<float>(step) / static_cast<float>(stepCount);
            const Vector2 candidate{ inPrevious.x + deltaX * ratio, inPrevious.y + deltaY * ratio };
            if (IsPositionValid(candidate, inHorizontalRadius, inDepthRadius))
            {
                lastValid = candidate;
                continue;
            }

            Vector2 low = lastValid;
            Vector2 high = candidate;
            for (int iteration = 0; iteration < 12; ++iteration)
            {
                const Vector2 middle{ (low.x + high.x) * 0.5f, (low.y + high.y) * 0.5f };
                if (IsPositionValid(middle, inHorizontalRadius, inDepthRadius))
                {
                    low = middle;
                }
                else
                {
                    high = middle;
                }
            }
            return low;
        }
        return lastValid;
    }

    void GameplayMap::Render(D2DRenderer&, const Camera&) const
    {
        // Collision overlays belong to the editor; gameplay renders only authored background images.
    }
}
