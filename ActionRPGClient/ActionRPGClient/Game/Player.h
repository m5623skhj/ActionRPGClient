#pragma once

#include "Game/Character.h"
#include "Game/RunState.h"
#include "Game/SkillDefinition.h"
#include "Input/InputState.h"

#include <cstdint>
#include <optional>
#include <string>

namespace ActionRPG
{
    class PlayerSkillPresentation;
    class Player final : public Character
    {
    public:
        Player(Vector2 inInitialPosition, const AssetCatalog& inAssetCatalog, D2DRenderer& inRenderer);

        void Update(float inDeltaSeconds, const InputState& inInput, const GameplayMap& inGameplayMap);
        void ApplyHit(CharacterHitType inType) override;
        void ResetActionState() override;
        void SetRunningEnabled(bool inEnabled) override;
        void ActivateCommandSkill(const SkillEffectDefinition& inEffect);
        void SetSkillPresentation(const PlayerSkillPresentation* inPresentation) { skillPresentation = inPresentation; }
        void ReconcileGroundPosition(Vector2 inAuthoritativePosition);
        void Render(D2DRenderer& inRenderer, const Camera& inCamera) const override;

    private:
        float skillEffectRemainingSeconds{};
        std::optional<SkillEffectDefinition> activeSkillEffect;
        double movementTimeSeconds{};
        RunState runState;
        const PlayerSkillPresentation* skillPresentation{};
        struct SkillPlayback
        {
            std::uint32_t sequence{};
            std::string id;
            bool airborne{};
            float seconds{};
        };
        // Per-actor render cache, accessed only on the game thread and reset with the actor.
        mutable std::optional<SkillPlayback> skillPlayback;
    };
}
