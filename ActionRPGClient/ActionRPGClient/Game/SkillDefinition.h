#pragma once

#include "Input/InputState.h"

#include <string>
#include <vector>

namespace ActionRPG
{
    struct ColorValue
    {
        float red{};
        float green{};
        float blue{};
        float alpha{ 1.0f };
    };

    struct SkillEffectDefinition
    {
        std::string id;
        float durationSeconds{};
        ColorValue bodyColor;
        ColorValue auraColor;
        float auraScaleX{ 1.0f };
        float auraScaleY{ 1.0f };
        std::string imageAssetId;
        std::string audioAssetId;
    };

    struct SkillDefinition
    {
        std::string definitionId;
        std::string skillId;
        std::vector<InputKey> command;
        double maxStepSeconds{};
        SkillEffectDefinition effect;
    };

    struct SkillActivation
    {
        std::string skillId;
        SkillEffectDefinition effect;
    };
}
