#pragma once

#include "Core/IniDocument.h"
#include "Game/RunState.h"
#include "Game/SkillDefinition.h"
#include "Game/Vector2.h"
#include "Input/InputState.h"
#include "Resources/SpriteAnimation.h"

#include <cstdint>
#include <deque>
#include <optional>

namespace ActionRPG
{
    class Camera;
    class D2DRenderer;
    class GameplayMap;
    class AssetCatalog;

    enum class PlayerProjectileType
    {
        Straight,
        Arc
    };

    struct PlayerProjectileRequest
    {
        PlayerProjectileType type{ PlayerProjectileType::Straight };
        Vector2 throwerPosition{};
        float throwerHeight{};
        Vector2 direction{};
    };

    class Player final
    {
    public:
        Player(Vector2 inInitialPosition, const AssetCatalog& inAssetCatalog, D2DRenderer& inRenderer);

        // Ground movement uses x/y while jump motion uses an independent height axis.
        void Update(float inDeltaSeconds, const InputState& inInput, const GameplayMap& inGameplayMap);
        void ActivateCommandSkill(const SkillEffectDefinition& inEffect);
        [[nodiscard]] std::optional<PlayerProjectileRequest> ConsumeProjectileRequest();
        void Render(D2DRenderer& inRenderer, const Camera& inCamera) const;

        [[nodiscard]] Vector2 GetGroundPosition() const { return groundPosition; }
        [[nodiscard]] bool IsRunning() const { return runState.IsRunning(); }
        void SetGroundPosition(Vector2 inPosition) { groundPosition = inPosition; }
        void ReconcileGroundPosition(Vector2 inAuthoritativePosition);
        void ConfigureMovementSpeeds(float inWalkSpeed, float inRunSpeed);

    private:
        void QueueProjectileRequest(PlayerProjectileType inType);

    private:
        static constexpr float WIDTH = 64.0f;
        static constexpr float HEIGHT = 96.0f;
        static constexpr float HORIZONTAL_RADIUS = WIDTH * 0.5f;
        static constexpr float DEPTH_RADIUS = 18.0f;
        static constexpr float JUMP_SPEED = 700.0f;
        static constexpr float GRAVITY = 1800.0f;

        Vector2 groundPosition{};
        float height{};
        float verticalVelocity{};
        float skillEffectRemainingSeconds{};
        std::optional<SkillEffectDefinition> activeSkillEffect;
        double movementTimeSeconds{};
        bool facingLeft{};
        bool isAttacking{};
        bool attackProjectileQueued{};
        std::uint32_t attackEventFrame{};
        float walkSpeed = 280.0f;
        float runSpeed = 480.0f;
        RunState runState;
        IniDocument animationDefinitions;
        SpriteAnimation idleAnimation;
        SpriteAnimation runAnimation;
        SpriteAnimation attackAnimation;
        std::deque<PlayerProjectileRequest> pendingProjectileRequests;
    };
}
