#pragma once

#include "Core/IniDocument.h"
#include "Game/RunState.h"
#include "Game/SkillDefinition.h"
#include "Game/Vector2.h"
#include "Input/InputState.h"
#include "Resources/SpriteAnimation.h"

#include <optional>

namespace ActionRPG
{
    class Camera;
    class D2DRenderer;
    class GameplayMap;
    class AssetCatalog;

    class Player final
    {
    public:
        Player(Vector2 inInitialPosition, const AssetCatalog& inAssetCatalog, D2DRenderer& inRenderer);

        // Ground movement uses x/y while jump motion uses an independent height axis.
        void Update(float inDeltaSeconds, const InputState& inInput, const GameplayMap& inGameplayMap);
        void ActivateCommandSkill(const SkillEffectDefinition& inEffect);
        void Render(D2DRenderer& inRenderer, const Camera& inCamera) const;

        [[nodiscard]] Vector2 GetGroundPosition() const { return groundPosition; }

    private:
        static constexpr float WIDTH = 64.0f;
        static constexpr float HEIGHT = 96.0f;
        static constexpr float HORIZONTAL_RADIUS = WIDTH * 0.5f;
        static constexpr float DEPTH_RADIUS = 18.0f;
        static constexpr float WALK_SPEED = 280.0f;
        static constexpr float RUN_SPEED = 480.0f;
        static constexpr float JUMP_SPEED = 700.0f;
        static constexpr float GRAVITY = 1800.0f;

        Vector2 groundPosition{};
        float height{};
        float verticalVelocity{};
        float skillEffectRemainingSeconds{};
        std::optional<SkillEffectDefinition> activeSkillEffect;
        double movementTimeSeconds{};
        bool facingLeft{};
        RunState runState;
        IniDocument animationDefinitions;
        SpriteAnimation idleAnimation;
        SpriteAnimation runAnimation;
    };
}
