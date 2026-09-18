#pragma once

#include "Game/SkillDefinition.h"

#include <filesystem>
#include <optional>
#include <unordered_map>
#include <vector>

namespace ActionRPG
{
    class AssetCatalog;
    class InputCommandQueue;

    class SkillCommandSystem final
    {
    public:
        explicit SkillCommandSystem(const AssetCatalog& inAssetCatalog);

        [[nodiscard]] std::optional<SkillActivation> TryActivate(InputCommandQueue& inCommandQueue) const;

    private:
        void LoadEffects(const std::filesystem::path& inEffectsPath, const AssetCatalog& inAssetCatalog);
        void LoadSkills(const std::filesystem::path& inSkillsPath);

    private:
        std::unordered_map<std::string, SkillEffectDefinition> effects;
        std::vector<SkillDefinition> skills;
    };
}
