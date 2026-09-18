#include "Resources/SpriteAnimation.h"

#include "Core/IniDocument.h"
#include "Graphics/D2DRenderer.h"
#include "Resources/AssetCatalog.h"

#include <d2d1_1helper.h>

#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>

namespace
{
    std::uint32_t ParsePositiveInteger(const ActionRPG::IniDocument& inDocument,
        const std::string_view inSection, const std::string_view inKey)
    {
        const std::string& text = inDocument.GetValue(inSection, inKey);
        std::size_t parsedCharacters{};
        const unsigned long parsedValue = std::stoul(text, &parsedCharacters);
        if (parsedCharacters != text.size() || parsedValue == 0
            || parsedValue > std::numeric_limits<std::uint32_t>::max())
        {
            throw std::runtime_error("Invalid positive integer '" + text + "' for "
                + std::string(inSection) + "." + std::string(inKey) + ".");
        }

        return static_cast<std::uint32_t>(parsedValue);
    }

    float ParsePositiveFloat(const ActionRPG::IniDocument& inDocument,
        const std::string_view inSection, const std::string_view inKey)
    {
        const std::string& text = inDocument.GetValue(inSection, inKey);
        std::size_t parsedCharacters{};
        const float parsedValue = std::stof(text, &parsedCharacters);
        if (parsedCharacters != text.size() || parsedValue <= 0.0f || !std::isfinite(parsedValue))
        {
            throw std::runtime_error("Invalid positive number '" + text + "' for "
                + std::string(inSection) + "." + std::string(inKey) + ".");
        }

        return parsedValue;
    }

    float ParseRowRatio(const ActionRPG::IniDocument& inDocument,
        const std::string_view inSection)
    {
        const std::string text = inDocument.GetOptionalValue(inSection, "first_row_ratio", "0.5");
        std::size_t parsedCharacters{};
        const float parsedValue = std::stof(text, &parsedCharacters);
        if (parsedCharacters != text.size() || parsedValue <= 0.0f || parsedValue >= 1.0f
            || !std::isfinite(parsedValue))
        {
            throw std::runtime_error("Invalid first_row_ratio '" + text + "' in ["
                + std::string(inSection) + "].");
        }

        return parsedValue;
    }

    float ParseAnchorY(const ActionRPG::IniDocument& inDocument,
        const std::string_view inSection)
    {
        const std::string text = inDocument.GetOptionalValue(inSection, "anchor_y", "1.0");
        std::size_t parsedCharacters{};
        const float parsedValue = std::stof(text, &parsedCharacters);
        if (parsedCharacters != text.size() || parsedValue <= 0.0f || parsedValue > 1.0f
            || !std::isfinite(parsedValue))
        {
            throw std::runtime_error("Invalid anchor_y '" + text + "' in ["
                + std::string(inSection) + "].");
        }

        return parsedValue;
    }
}

namespace ActionRPG
{
    SpriteAnimation::SpriteAnimation(D2DRenderer& inRenderer, const AssetCatalog& inAssetCatalog,
        const IniDocument& inDefinitions, const std::string_view inSection)
        : bitmap(inRenderer.LoadBitmap(
            inAssetCatalog.GetImagePath(inDefinitions.GetValue(inSection, "image"))))
        , columns(ParsePositiveInteger(inDefinitions, inSection, "columns"))
        , rows(ParsePositiveInteger(inDefinitions, inSection, "rows"))
        , frameCount(ParsePositiveInteger(inDefinitions, inSection, "frame_count"))
        , frameSeconds(ParsePositiveFloat(inDefinitions, inSection, "frame_seconds"))
        , firstRowRatio(ParseRowRatio(inDefinitions, inSection))
        , anchorY(ParseAnchorY(inDefinitions, inSection))
        , renderWidth(ParsePositiveFloat(inDefinitions, inSection, "render_width"))
        , renderHeight(ParsePositiveFloat(inDefinitions, inSection, "render_height"))
    {
        const std::uint64_t availableCells = static_cast<std::uint64_t>(columns) * rows;
        if (frameCount > availableCells)
        {
            throw std::runtime_error("Sprite frame_count exceeds the configured sheet cells in ["
                + std::string(inSection) + "].");
        }
    }

    void SpriteAnimation::Update(const float inDeltaSeconds)
    {
        if (frameCount <= 1)
        {
            return;
        }

        elapsedSeconds += inDeltaSeconds;
        while (elapsedSeconds >= frameSeconds)
        {
            elapsedSeconds -= frameSeconds;
            currentFrame = (currentFrame + 1) % frameCount;
        }
    }

    void SpriteAnimation::Reset()
    {
        currentFrame = 0;
        elapsedSeconds = 0.0f;
    }

    void SpriteAnimation::Draw(D2DRenderer& inRenderer, const float inCenterX, const float inBottomY,
        const bool inFlipHorizontal) const
    {
        const D2D1_SIZE_F bitmapSize = bitmap->GetSize();
        const float frameWidth = bitmapSize.width / static_cast<float>(columns);
        const std::uint32_t column = currentFrame % columns;
        const std::uint32_t row = currentFrame / columns;
        float sourceTop{};
        float sourceBottom{};
        if (rows == 2)
        {
            const float rowBoundary = bitmapSize.height * firstRowRatio;
            sourceTop = row == 0 ? 0.0f : rowBoundary;
            sourceBottom = row == 0 ? rowBoundary : bitmapSize.height;
        }
        else
        {
            const float frameHeight = bitmapSize.height / static_cast<float>(rows);
            sourceTop = static_cast<float>(row) * frameHeight;
            sourceBottom = static_cast<float>(row + 1) * frameHeight;
        }

        const D2D1_RECT_F sourceRectangle = D2D1::RectF(
            static_cast<float>(column) * frameWidth,
            sourceTop,
            static_cast<float>(column + 1) * frameWidth,
            sourceBottom);
        const float destinationTop = inBottomY - renderHeight * anchorY;
        const D2D1_RECT_F destinationRectangle = D2D1::RectF(
            inCenterX - renderWidth * 0.5f,
            destinationTop,
            inCenterX + renderWidth * 0.5f,
            destinationTop + renderHeight);

        inRenderer.DrawBitmap(bitmap.Get(), sourceRectangle, destinationRectangle, inFlipHorizontal);
    }
}
