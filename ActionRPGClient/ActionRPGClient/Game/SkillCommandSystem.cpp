#include "Game/SkillCommandSystem.h"

#include "Core/IniDocument.h"
#include "Game/InputCommandQueue.h"
#include "Resources/AssetCatalog.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <stdexcept>
#include <string_view>

namespace
{
    std::string Trim(std::string_view inText)
    {
        std::size_t first = 0;
        while (first < inText.size() && std::isspace(static_cast<unsigned char>(inText[first])) != 0)
        {
            ++first;
        }

        std::size_t last = inText.size();
        while (last > first && std::isspace(static_cast<unsigned char>(inText[last - 1])) != 0)
        {
            --last;
        }

        return std::string(inText.substr(first, last - first));
    }

    std::vector<std::string> Split(const std::string_view inText)
    {
        std::vector<std::string> values;
        std::size_t begin = 0;
        while (begin <= inText.size())
        {
            const std::size_t comma = inText.find(',', begin);
            const std::size_t end = comma == std::string_view::npos ? inText.size() : comma;
            values.push_back(Trim(inText.substr(begin, end - begin)));
            if (comma == std::string_view::npos)
            {
                break;
            }
            begin = comma + 1;
        }
        return values;
    }

    double ParseDouble(const std::string& inText, const std::string_view inFieldName)
    {
        std::size_t parsedCharacters = 0;
        double value{};
        try
        {
            value = std::stod(inText, &parsedCharacters);
        }
        catch (const std::exception&)
        {
            throw std::runtime_error("Invalid number for " + std::string(inFieldName) + ": " + inText);
        }

        if (parsedCharacters != inText.size())
        {
            throw std::runtime_error("Invalid number for " + std::string(inFieldName) + ": " + inText);
        }
        return value;
    }

    float ParseFloat(const std::string& inText, const std::string_view inFieldName)
    {
        return static_cast<float>(ParseDouble(inText, inFieldName));
    }

    ActionRPG::InputKey ParseInputKey(const std::string_view inToken)
    {
        using ActionRPG::InputKey;

        if (inToken == "Left") return InputKey::MoveLeft;
        if (inToken == "Right") return InputKey::MoveRight;
        if (inToken == "Up") return InputKey::MoveUp;
        if (inToken == "Down") return InputKey::MoveDown;
        if (inToken == "Z") return InputKey::ActionZ;
        if (inToken == "C") return InputKey::ActionC;
        throw std::runtime_error("Unknown command input token: " + std::string(inToken));
    }

    std::vector<ActionRPG::InputKey> ParseCommand(const std::string& inText)
    {
        std::vector<ActionRPG::InputKey> command;
        for (const std::string& token : Split(inText))
        {
            if (token.empty())
            {
                throw std::runtime_error("Command contains an empty input token: " + inText);
            }
            command.push_back(ParseInputKey(token));
        }

        if (command.empty())
        {
            throw std::runtime_error("Command cannot be empty.");
        }
        return command;
    }

    ActionRPG::ColorValue ParseColor(const std::string& inText, const std::string_view inFieldName)
    {
        const std::vector<std::string> values = Split(inText);
        if (values.size() != 4)
        {
            throw std::runtime_error(std::string(inFieldName) + " must contain red, green, blue, alpha.");
        }

        ActionRPG::ColorValue color{
            ParseFloat(values[0], inFieldName),
            ParseFloat(values[1], inFieldName),
            ParseFloat(values[2], inFieldName),
            ParseFloat(values[3], inFieldName)
        };

        constexpr std::array channels{ &ActionRPG::ColorValue::red, &ActionRPG::ColorValue::green,
            &ActionRPG::ColorValue::blue, &ActionRPG::ColorValue::alpha };
        for (const auto channel : channels)
        {
            if (color.*channel < 0.0f || color.*channel > 1.0f)
            {
                throw std::runtime_error(std::string(inFieldName) + " channels must be between 0 and 1.");
            }
        }
        return color;
    }
}

namespace ActionRPG
{
    SkillCommandSystem::SkillCommandSystem(const AssetCatalog& inAssetCatalog)
    {
        LoadEffects(inAssetCatalog.GetDataPath("Effects"), inAssetCatalog);
        LoadSkills(inAssetCatalog.GetDataPath("Skills"));

        std::stable_sort(skills.begin(), skills.end(), [](const SkillDefinition& inLeft,
            const SkillDefinition& inRight)
        {
            return inLeft.command.size() > inRight.command.size();
        });
    }

    std::optional<SkillActivation> SkillCommandSystem::TryActivate(InputCommandQueue& inCommandQueue) const
    {
        for (const SkillDefinition& skill : skills)
        {
            if (inCommandQueue.TryConsume(std::span<const InputKey>(skill.command), skill.maxStepSeconds))
            {
                return SkillActivation{ skill.skillId, skill.effect };
            }
        }

        return std::nullopt;
    }

    void SkillCommandSystem::LoadEffects(const std::filesystem::path& inEffectsPath,
        const AssetCatalog& inAssetCatalog)
    {
        const IniDocument document(inEffectsPath);
        for (const std::string& section : document.GetSectionNames())
        {
            SkillEffectDefinition effect;
            effect.id = section;
            effect.durationSeconds = ParseFloat(document.GetValue(section, "duration_seconds"), "duration_seconds");
            effect.bodyColor = ParseColor(document.GetValue(section, "body_color"), "body_color");
            effect.auraColor = ParseColor(document.GetValue(section, "aura_color"), "aura_color");

            const std::vector<std::string> auraScale = Split(document.GetValue(section, "aura_scale"));
            if (auraScale.size() != 2)
            {
                throw std::runtime_error("aura_scale must contain x and y values in effect: " + section);
            }
            effect.auraScaleX = ParseFloat(auraScale[0], "aura_scale");
            effect.auraScaleY = ParseFloat(auraScale[1], "aura_scale");
            effect.imageAssetId = document.GetOptionalValue(section, "image_asset");
            effect.audioAssetId = document.GetOptionalValue(section, "audio_asset");

            if (effect.durationSeconds <= 0.0f || effect.durationSeconds > 60.0f)
            {
                throw std::runtime_error("duration_seconds must be between 0 and 60 in effect: " + section);
            }
            if (effect.auraScaleX <= 0.0f || effect.auraScaleY <= 0.0f
                || effect.auraScaleX > 10.0f || effect.auraScaleY > 10.0f)
            {
                throw std::runtime_error("aura_scale must be greater than 0 and at most 10 in effect: " + section);
            }
            if (!effect.imageAssetId.empty() && !inAssetCatalog.HasImage(effect.imageAssetId))
            {
                throw std::runtime_error("Unknown image asset '" + effect.imageAssetId + "' in effect: " + section);
            }
            if (!effect.audioAssetId.empty() && !inAssetCatalog.HasAudio(effect.audioAssetId))
            {
                throw std::runtime_error("Unknown audio asset '" + effect.audioAssetId + "' in effect: " + section);
            }

            effects.emplace(effect.id, std::move(effect));
        }

        if (effects.empty())
        {
            throw std::runtime_error("No skill effects were loaded from: " + inEffectsPath.string());
        }
    }

    void SkillCommandSystem::LoadSkills(const std::filesystem::path& inSkillsPath)
    {
        const IniDocument document(inSkillsPath);
        for (const std::string& section : document.GetSectionNames())
        {
            SkillDefinition skill;
            skill.definitionId = section;
            skill.skillId = document.GetValue(section, "skill");
            skill.command = ParseCommand(document.GetValue(section, "command"));
            skill.maxStepSeconds = ParseDouble(document.GetValue(section, "max_step_seconds"),
                "max_step_seconds");

            const std::string& effectId = document.GetValue(section, "effect");
            const auto effectIterator = effects.find(effectId);
            if (effectIterator == effects.end())
            {
                throw std::runtime_error("Unknown effect '" + effectId + "' in skill definition: " + section);
            }
            skill.effect = effectIterator->second;

            if (skill.skillId.empty())
            {
                throw std::runtime_error("skill cannot be empty in definition: " + section);
            }
            if (skill.maxStepSeconds <= 0.0 || skill.maxStepSeconds > 5.0)
            {
                throw std::runtime_error("max_step_seconds must be between 0 and 5 in definition: " + section);
            }

            skills.push_back(std::move(skill));
        }

        if (skills.empty())
        {
            throw std::runtime_error("No skill commands were loaded from: " + inSkillsPath.string());
        }
    }
}
