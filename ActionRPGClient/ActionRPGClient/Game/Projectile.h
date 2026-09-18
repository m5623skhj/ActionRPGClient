#pragma once

#include "Game/SkillDefinition.h"
#include "Game/Vector2.h"

#include <string>

namespace ActionRPG
{
    class Camera;
    class D2DRenderer;

    enum class ProjectileMotionType
    {
        Straight,
        Arc
    };

    struct ProjectileDefinition
    {
        std::string id;
        ProjectileMotionType motionType{ ProjectileMotionType::Straight };
        float speed{};
        float lifetimeSeconds{};
        float spawnForward{};
        float spawnHeight{};
        float verticalSpeed{};
        float gravity{};
        float radiusX{};
        float radiusY{};
        ColorValue bodyColor;
        ColorValue highlightColor;
    };

    class Projectile final
    {
    public:
        Projectile(const ProjectileDefinition& inDefinition, Vector2 inThrowerPosition,
            float inThrowerHeight, Vector2 inDirection);

        void Update(float inDeltaSeconds);
        void Render(D2DRenderer& inRenderer, const Camera& inCamera) const;
        void Expire() { isAlive = false; }

        [[nodiscard]] bool IsAlive() const { return isAlive; }
        [[nodiscard]] Vector2 GetGroundPosition() const { return groundPosition; }

    private:
        ProjectileDefinition definition;
        Vector2 groundPosition{};
        Vector2 groundVelocity{};
        float height{};
        float verticalVelocity{};
        float remainingSeconds{};
        bool isAlive{ true };
    };
}
