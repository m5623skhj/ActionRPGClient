#include "Game/Game.h"

#include "Graphics/D2DRenderer.h"
#include "Network/TownClient.h"
#include "Resources/AssetCatalog.h"

#include <d2d1_1helper.h>

namespace ActionRPG
{
    Game::Game(const float inViewportWidth, const float inViewportHeight, const AssetCatalog& inAssetCatalog,
        D2DRenderer& inRenderer, TownClient& inTownClient)
        : world(inViewportWidth, inViewportHeight, inAssetCatalog, inRenderer, inTownClient)
    {
    }

    void Game::Update(const float inDeltaSeconds, const InputState& inInput)
    {
        world.Update(inDeltaSeconds, inInput);
    }

    void Game::Render(D2DRenderer& inRenderer) const
    {
        world.Render(inRenderer);
        inRenderer.DrawText(
            L"Move: Arrow Keys    Run: Double Tap    Jump: C    Shoot: X    Throw Rock: V    Skill: Left/Right x2 + Z",
            20.0f,
            16.0f,
            1080.0f,
            48.0f,
            D2D1::ColorF(0.92f, 0.95f, 1.0f));
    }

    void Game::Resize(const float inViewportWidth, const float inViewportHeight)
    {
        world.Resize(inViewportWidth, inViewportHeight);
    }
}
