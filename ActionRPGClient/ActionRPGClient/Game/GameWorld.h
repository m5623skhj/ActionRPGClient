#pragma once

#include "Game/Camera.h"
#include "Game/GameplayMap.h"
#include "Game/InputCommandQueue.h"
#include "Game/MapBackground.h"
#include "Game/Player.h"
#include "Game/ProjectileSystem.h"
#include "Game/SkillCommandSystem.h"
#include "Input/InputState.h"

namespace ActionRPG
{
    class AssetCatalog;
    class D2DRenderer;

    class GameWorld final
    {
    public:
        GameWorld(float inViewportWidth, float inViewportHeight, const AssetCatalog& inAssetCatalog,
            D2DRenderer& inRenderer);

        void Update(float inDeltaSeconds, const InputState& inInput);
        void Render(D2DRenderer& inRenderer) const;
        void Resize(float inViewportWidth, float inViewportHeight);

    private:
        GameplayMap gameplayMap;
        MapBackground mapBackground;
        Camera camera;
        Player player;
        ProjectileSystem projectileSystem;
        InputCommandQueue commandQueue;
        SkillCommandSystem skillCommandSystem;
        double worldTimeSeconds{};
    };
}
