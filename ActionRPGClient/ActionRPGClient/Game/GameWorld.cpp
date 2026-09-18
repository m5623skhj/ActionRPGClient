#include "Game/GameWorld.h"

#include "Graphics/D2DRenderer.h"
#include "Resources/AssetCatalog.h"

#include <d2d1_1helper.h>

namespace ActionRPG
{
    GameWorld::GameWorld(const float inViewportWidth, const float inViewportHeight,
        const AssetCatalog& inAssetCatalog, D2DRenderer& inRenderer)
        : mapBackground(gameplayMap.GetWorldWidth(), gameplayMap.GetWalkableTop())
        , camera(inViewportWidth, inViewportHeight)
        , player(Vector2{ 640.0f, 640.0f }, inAssetCatalog, inRenderer)
        , skillCommandSystem(inAssetCatalog)
    {
        camera.Follow(player.GetGroundPosition(), gameplayMap.GetWorldWidth(), gameplayMap.GetWorldHeight());
    }

    void GameWorld::Update(const float inDeltaSeconds, const InputState& inInput)
    {
        worldTimeSeconds += inDeltaSeconds;
        commandQueue.Record(inInput, worldTimeSeconds);

        const std::optional<SkillActivation> skillActivation = skillCommandSystem.TryActivate(commandQueue);
        if (skillActivation.has_value())
        {
            player.ActivateCommandSkill(skillActivation->effect);
        }

        player.Update(inDeltaSeconds, inInput, gameplayMap);
        camera.Follow(player.GetGroundPosition(), gameplayMap.GetWorldWidth(), gameplayMap.GetWorldHeight());
    }

    void GameWorld::Render(D2DRenderer& inRenderer) const
    {
        mapBackground.Render(inRenderer, camera);
        gameplayMap.Render(inRenderer, camera);
        player.Render(inRenderer, camera);
    }

    void GameWorld::Resize(const float inViewportWidth, const float inViewportHeight)
    {
        camera.Resize(inViewportWidth, inViewportHeight);
        camera.Follow(player.GetGroundPosition(), gameplayMap.GetWorldWidth(), gameplayMap.GetWorldHeight());
    }
}
