#include "Game/GameWorld.h"

#include "Graphics/D2DRenderer.h"
#include "Resources/AssetCatalog.h"

#include <d2d1_1helper.h>

#include <optional>
#include <string_view>

namespace ActionRPG
{
    GameWorld::GameWorld(const float inViewportWidth, const float inViewportHeight,
        const AssetCatalog& inAssetCatalog, D2DRenderer& inRenderer)
        : mapBackground(gameplayMap.GetWorldWidth(), gameplayMap.GetWalkableTop())
        , camera(inViewportWidth, inViewportHeight)
        , player(Vector2{ 640.0f, 640.0f }, inAssetCatalog, inRenderer)
        , projectileSystem(inAssetCatalog)
        , skillCommandSystem(inAssetCatalog)
    {
        camera.Follow(player.GetGroundPosition(), gameplayMap.GetWorldWidth(), gameplayMap.GetWorldHeight());
    }

    void GameWorld::Update(const float inDeltaSeconds, const InputState& inInput)
    {
        worldTimeSeconds += inDeltaSeconds;
        projectileSystem.Update(inDeltaSeconds, gameplayMap);
        commandQueue.Record(inInput, worldTimeSeconds);

        const std::optional<SkillActivation> skillActivation = skillCommandSystem.TryActivate(commandQueue);
        if (skillActivation.has_value())
        {
            player.ActivateCommandSkill(skillActivation->effect);
        }

        player.Update(inDeltaSeconds, inInput, gameplayMap);
        while (const std::optional<PlayerProjectileRequest> request = player.ConsumeProjectileRequest())
        {
            const std::string_view definitionId = request->type == PlayerProjectileType::Straight
                ? "PlayerBullet"
                : "PlayerRock";
            projectileSystem.Spawn(
                definitionId,
                request->throwerPosition,
                request->throwerHeight,
                request->direction);
        }
        camera.Follow(player.GetGroundPosition(), gameplayMap.GetWorldWidth(), gameplayMap.GetWorldHeight());
    }

    void GameWorld::Render(D2DRenderer& inRenderer) const
    {
        mapBackground.Render(inRenderer, camera);
        gameplayMap.Render(inRenderer, camera);
        projectileSystem.Render(inRenderer, camera);
        player.Render(inRenderer, camera);
    }

    void GameWorld::Resize(const float inViewportWidth, const float inViewportHeight)
    {
        camera.Resize(inViewportWidth, inViewportHeight);
        camera.Follow(player.GetGroundPosition(), gameplayMap.GetWorldWidth(), gameplayMap.GetWorldHeight());
    }
}
