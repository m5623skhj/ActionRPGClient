#pragma once

#include "Game/Character.h"
#include "Game/RunState.h"
#include "Game/SkillDefinition.h"
#include "Input/InputState.h"

#include <optional>

namespace ActionRPG
{
    class Player final : public Character
    {
    public:
        Player(Vector2 inInitialPosition, const AssetCatalog& inAssetCatalog, D2DRenderer& inRenderer);

        void Update(float inDeltaSeconds, const InputState& inInput, const GameplayMap& inGameplayMap);
        void ApplyHit(CharacterHitType inType) override;
        void ResetActionState() override;
        void SetRunningEnabled(bool inEnabled) override;
        void ActivateCommandSkill(const SkillEffectDefinition& inEffect);
        void ReconcileGroundPosition(Vector2 inAuthoritativePosition);
        void Render(D2DRenderer& inRenderer, const Camera& inCamera) const override;

    private:
        float skillEffectRemainingSeconds{};
        std::optional<SkillEffectDefinition> activeSkillEffect;
        double movementTimeSeconds{};
        RunState runState;
    };
}
