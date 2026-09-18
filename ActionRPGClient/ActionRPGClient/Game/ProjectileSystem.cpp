#include "Game/ProjectileSystem.h"

#include "Core/IniDocument.h"
#include "Game/GameplayMap.h"
#include "Resources/AssetCatalog.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
    std::vector<std::string> Split(const std::string_view inText)
    {
        std::vector<std::string> values;
        std::size_t begin{};
        while (begin <= inText.size())
        {
            const std::size_t comma = inText.find(',', begin);
            const std::size_t end = comma == std::string_view::npos ? inText.size() : comma;
            values.emplace_back(inText.substr(begin, end - begin));
            if (comma == std::string_view::npos)
            {
                break;
            }
            begin = comma + 1;
        }
        return values;
    }

    float ParseFloat(const std::string& inText, const std::string_view inFieldName)
    {
        std::size_t parsedCharacters{};
        float value{};
        try
        {
            value = std::stof(inText, &parsedCharacters);
        }
        catch (const std::exception&)
        {
            throw std::runtime_error("Invalid number for " + std::string(inFieldName) + ": " + inText);
        }

        if (parsedCharacters != inText.size() || !std::isfinite(value))
        {
            throw std::runtime_error("Invalid number for " + std::string(inFieldName) + ": " + inText);
        }
        return value;
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

    ActionRPG::ProjectileMotionType ParseMotionType(const std::string_view inText)
    {
        if (inText == "Straight") return ActionRPG::ProjectileMotionType::Straight;
        if (inText == "Arc") return ActionRPG::ProjectileMotionType::Arc;
        throw std::runtime_error("Unknown projectile motion type: " + std::string(inText));
    }
}

namespace ActionRPG
{
    ProjectileSystem::ProjectileSystem(const AssetCatalog& inAssetCatalog)
    {
        const IniDocument document(inAssetCatalog.GetDataPath("Projectiles"));
        for (const std::string& section : document.GetSectionNames())
        {
            ProjectileDefinition definition;
            definition.id = section;
            definition.motionType = ParseMotionType(document.GetValue(section, "motion"));
            definition.speed = ParseFloat(document.GetValue(section, "speed"), "speed");
            definition.lifetimeSeconds = ParseFloat(
                document.GetValue(section, "lifetime_seconds"), "lifetime_seconds");
            definition.spawnForward = ParseFloat(document.GetValue(section, "spawn_forward"), "spawn_forward");
            definition.spawnHeight = ParseFloat(document.GetValue(section, "spawn_height"), "spawn_height");
            definition.verticalSpeed = ParseFloat(document.GetValue(section, "vertical_speed"), "vertical_speed");
            definition.gravity = ParseFloat(document.GetValue(section, "gravity"), "gravity");
            definition.radiusX = ParseFloat(document.GetValue(section, "radius_x"), "radius_x");
            definition.radiusY = ParseFloat(document.GetValue(section, "radius_y"), "radius_y");
            definition.bodyColor = ParseColor(document.GetValue(section, "body_color"), "body_color");
            definition.highlightColor = ParseColor(
                document.GetValue(section, "highlight_color"), "highlight_color");

            if (definition.speed <= 0.0f || definition.lifetimeSeconds <= 0.0f
                || definition.spawnHeight < 0.0f || definition.radiusX <= 0.0f || definition.radiusY <= 0.0f)
            {
                throw std::runtime_error("Projectile values must be positive in [" + section + "].");
            }
            if (definition.motionType == ProjectileMotionType::Arc
                && (definition.verticalSpeed <= 0.0f || definition.gravity <= 0.0f))
            {
                throw std::runtime_error("Arc projectile requires positive vertical_speed and gravity in ["
                    + section + "].");
            }

            definitions.emplace(definition.id, std::move(definition));
        }

        if (definitions.empty())
        {
            throw std::runtime_error("No projectile definitions were loaded.");
        }
    }

    void ProjectileSystem::Spawn(const std::string_view inDefinitionId, const Vector2 inThrowerPosition,
        const float inThrowerHeight, Vector2 inDirection)
    {
        const auto definitionIterator = definitions.find(std::string(inDefinitionId));
        if (definitionIterator == definitions.end())
        {
            throw std::runtime_error("Unknown projectile definition: " + std::string(inDefinitionId));
        }

        const float directionLength = std::sqrt(inDirection.x * inDirection.x + inDirection.y * inDirection.y);
        if (directionLength <= 0.0f)
        {
            throw std::runtime_error("Projectile direction cannot be zero.");
        }
        inDirection.x /= directionLength;
        inDirection.y /= directionLength;

        projectiles.emplace_back(definitionIterator->second, inThrowerPosition, inThrowerHeight, inDirection);
    }

    void ProjectileSystem::Update(const float inDeltaSeconds, const GameplayMap& inGameplayMap)
    {
        constexpr float CULL_MARGIN = 200.0f;
        for (Projectile& projectile : projectiles)
        {
            projectile.Update(inDeltaSeconds);
            const Vector2 position = projectile.GetGroundPosition();
            if (position.x < -CULL_MARGIN || position.x > inGameplayMap.GetWorldWidth() + CULL_MARGIN
                || position.y < -CULL_MARGIN || position.y > inGameplayMap.GetWorldHeight() + CULL_MARGIN)
            {
                projectile.Expire();
            }
        }

        std::erase_if(projectiles, [](const Projectile& inProjectile)
        {
            return !inProjectile.IsAlive();
        });
    }

    void ProjectileSystem::Render(D2DRenderer& inRenderer, const Camera& inCamera) const
    {
        for (const Projectile& projectile : projectiles)
        {
            projectile.Render(inRenderer, inCamera);
        }
    }
}
