#pragma once

#include "Game/Projectile.h"

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace ActionRPG
{
    class AssetCatalog;
    class Camera;
    class D2DRenderer;
    class GameplayMap;

    // Updated and rendered on the game thread; network messages should enqueue spawn requests there.
    class ProjectileSystem final
    {
    public:
        explicit ProjectileSystem(const AssetCatalog& inAssetCatalog);

        void Spawn(std::string_view inDefinitionId, Vector2 inThrowerPosition,
            float inThrowerHeight, Vector2 inDirection);
        void Update(float inDeltaSeconds, const GameplayMap& inGameplayMap);
        void Render(D2DRenderer& inRenderer, const Camera& inCamera) const;

    private:
        std::unordered_map<std::string, ProjectileDefinition> definitions;
        std::vector<Projectile> projectiles;
    };
}
