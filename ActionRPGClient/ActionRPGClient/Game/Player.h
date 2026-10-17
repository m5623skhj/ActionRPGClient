#pragma once

#include "Game/Character.h"
#include "Game/RunState.h"
#include "Game/SkillDefinition.h"
#include "Input/InputState.h"

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>

namespace ActionRPG
{
    class PlayerSkillPresentation;
    class Player final : public Character
    {
    public:
        Player(Vector2 inInitialPosition, const AssetCatalog& inAssetCatalog, D2DRenderer& inRenderer,
            std::uint32_t inDefinitionId = 1);
        void ConfigureCharacter(std::uint32_t inDefinitionId, const AssetCatalog& inAssets, D2DRenderer& inRenderer);
        [[nodiscard]] std::uint32_t GetDefinitionId() const { return definitionId; }

        void Update(float inDeltaSeconds, const InputState& inInput, const GameplayMap& inGameplayMap);
        void ApplyHit(CharacterHitType inType) override;
        void ResetActionState() override;
        void SetRunningEnabled(bool inEnabled) override;
        [[nodiscard]] bool IsDungeonRunInput() const { return IsRunningEnabled() && !IsHitReacting() && runState.IsRunning(); }
        void ActivateCommandSkill(const SkillEffectDefinition& inEffect);
        [[nodiscard]] bool BoostCatalogSkill(const std::string& inId);
        [[nodiscard]] bool ActivateCatalogSkill(const std::string& inId, const std::unordered_map<std::string,std::uint32_t>& inSkillLevels);
        [[nodiscard]] const std::unordered_map<std::string,float>& GetTownSkillCooldowns() const { return townSkillCooldowns; }
        [[nodiscard]] bool IsLocallyCasting() const { return townSkill.has_value(); }
        void SetSkillPresentation(const PlayerSkillPresentation* inPresentation) { skillPresentation = inPresentation; }
        void ReconcileGroundPosition(Vector2 inAuthoritativePosition);
        void ReconcileCombatGroundPosition(const CombatPlayerState& inState, const GameplayMap& inMap);
        void ClearTownPositionCorrection() { pendingTownCorrection = {}; }
        void ReconcileTownGroundPosition(Vector2 inAuthoritativePosition, Vector2 inVelocity,
            float inSnapshotDelaySeconds, const GameplayMap& inGameplayMap);
        void Render(D2DRenderer& inRenderer, const Camera& inCamera) const override;

    private:
        void UpdateTownPositionCorrection(float inDeltaSeconds, const GameplayMap& inGameplayMap);
        Vector2 pendingTownCorrection{};
        std::uint32_t definitionId{1};
        // Local town preview only. Dungeon damage and cooldowns remain server authoritative.
        std::optional<CombatPlayerState> townSkill;
        float townSkillDuration{};
        std::unordered_map<std::string, float> townSkillCooldowns;
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
            std::uint32_t hitstopSequence{};
        };
        // Per-actor render cache, accessed only on the game thread and reset with the actor.
        mutable std::optional<SkillPlayback> skillPlayback;
    };
}
