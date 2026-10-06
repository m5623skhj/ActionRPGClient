#pragma once

#include "Game/GameWorld.h"
#include "Input/InputState.h"

namespace ActionRPG
{
    class AssetCatalog;
    class D2DRenderer;
    class DungeonClient;
    class TownClient;

    class Game final
    {
    public:
        Game(float inViewportWidth, float inViewportHeight, const AssetCatalog& inAssetCatalog,
            D2DRenderer& inRenderer, TownClient& inTownClient, DungeonClient& inDungeonClient);

        void Update(float inDeltaSeconds, const InputState& inInput);
        void Render(D2DRenderer& inRenderer) const;
        void Resize(float inViewportWidth, float inViewportHeight);
        [[nodiscard]] bool ConsumeExitRequested() noexcept;
        [[nodiscard]] SessionMenuAction ConsumeSessionAction() noexcept { return world.ConsumeSessionAction(); }

    private:
        GameWorld world;
    };
}
