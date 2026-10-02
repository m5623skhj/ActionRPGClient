#include "Resources/SpriteAnimation.h"

#include "Core/IniDocument.h"
#include "Graphics/D2DRenderer.h"
#include "Resources/AssetCatalog.h"

#include <d2d1_1helper.h>

#include <cctype>
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>
#include <utility>

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

    float ParseAnchorY(const ActionRPG::IniDocument& inDocument, const std::string_view inSection,
        const std::string_view inKey)
    {
        const std::string text = inDocument.GetValue(inSection, inKey);
        std::size_t parsedCharacters{};
        const float parsedValue = std::stof(text, &parsedCharacters);
        if (parsedCharacters != text.size() || parsedValue <= 0.0f || parsedValue > 1.0f
            || !std::isfinite(parsedValue))
        {
            throw std::runtime_error("Invalid " + std::string(inKey) + " '" + text
                + "' in [" + std::string(inSection) + "].");
        }

        return parsedValue;
    }

    std::string Trim(const std::string_view inText)
    {
        std::size_t first{};
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

    std::vector<float> ParseFrameAnchorXs(const ActionRPG::IniDocument& inDocument,
        const std::string_view inSection, const std::uint32_t inFrameCount)
    {
        if (!inDocument.HasValue(inSection, "anchor_xs"))
        {
            return std::vector<float>(inFrameCount, 0.5f);
        }

        const std::string& text = inDocument.GetValue(inSection, "anchor_xs");
        std::vector<float> anchors;
        std::size_t begin{};
        while (begin <= text.size())
        {
            const std::size_t comma = text.find(',', begin);
            const std::size_t end = comma == std::string::npos ? text.size() : comma;
            const std::string valueText = Trim(std::string_view(text).substr(begin, end - begin));
            std::size_t parsedCharacters{};
            const float value = std::stof(valueText, &parsedCharacters);
            if (parsedCharacters != valueText.size() || value < 0.0f || value > 1.0f
                || !std::isfinite(value))
            {
                throw std::runtime_error("Invalid anchor_xs value '" + valueText + "' in ["
                    + std::string(inSection) + "].");
            }
            anchors.push_back(value);

            if (comma == std::string::npos)
            {
                break;
            }
            begin = comma + 1;
        }

        if (anchors.size() != inFrameCount)
        {
            throw std::runtime_error("anchor_xs must contain one value per frame in ["
                + std::string(inSection) + "].");
        }
        return anchors;
    }
}

namespace ActionRPG
{
    SpriteAnimation::SpriteAnimation(Microsoft::WRL::ComPtr<ID2D1Bitmap1> inBitmap,
        std::vector<SpriteFrame> inFrames, const float inFrameSeconds, const float inScale)
        : bitmap(std::move(inBitmap)), frameSeconds(inFrameSeconds),
          frames(std::move(inFrames)), scale(inScale)
    {
        if (!bitmap || frames.empty() || frames.size() > 4096
            || !std::isfinite(frameSeconds) || frameSeconds <= 0.0f
            || !std::isfinite(scale) || scale <= 0.0f)
            throw std::runtime_error("Invalid explicit sprite animation.");
        frameCount = static_cast<std::uint32_t>(frames.size());
        const auto size = bitmap->GetPixelSize();
        for (const auto& frame : frames)
        {
            const auto& rect = frame.sourceRect;
            if (!std::isfinite(rect.left) || !std::isfinite(rect.top)
                || !std::isfinite(rect.right) || !std::isfinite(rect.bottom)
                || !std::isfinite(frame.pivot.x) || !std::isfinite(frame.pivot.y)
                || rect.left < 0.0f || rect.top < 0.0f || rect.right <= rect.left
                || rect.bottom <= rect.top || rect.right > size.width || rect.bottom > size.height
                || frame.pivot.x < 0.0f || frame.pivot.y < 0.0f
                || frame.pivot.x > rect.right - rect.left || frame.pivot.y > rect.bottom - rect.top)
                throw std::runtime_error("Sprite rectangle or pivot is outside the image.");
        }
    }

    SpriteAnimation::SpriteAnimation(D2DRenderer& inRenderer, const AssetCatalog& inAssetCatalog,
        const IniDocument& inDefinitions, const std::string_view inSection)
        : bitmap(inRenderer.LoadBitmap(
            inAssetCatalog.GetImagePath(inDefinitions.GetValue(inSection, "image"))))
        , columns(ParsePositiveInteger(inDefinitions, inSection, "columns"))
        , rows(ParsePositiveInteger(inDefinitions, inSection, "rows"))
        , frameCount(ParsePositiveInteger(inDefinitions, inSection, "frame_count"))
        , frameSeconds(ParsePositiveFloat(inDefinitions, inSection, "frame_seconds"))
        , firstRowRatio(ParseRowRatio(inDefinitions, inSection))
        , anchorY(ParseAnchorY(inDefinitions, inSection, "anchor_y"))
        , renderWidth(ParsePositiveFloat(inDefinitions, inSection, "render_width"))
        , renderHeight(ParsePositiveFloat(inDefinitions, inSection, "render_height"))
    {
        secondRowAnchorY = inDefinitions.HasValue(inSection, "second_row_anchor_y")
            ? ParseAnchorY(inDefinitions, inSection, "second_row_anchor_y")
            : anchorY;
        frameAnchorXs = ParseFrameAnchorXs(inDefinitions, inSection, frameCount);

        const std::uint64_t availableCells = static_cast<std::uint64_t>(columns) * rows;
        if (frameCount > availableCells)
        {
            throw std::runtime_error("Sprite frame_count exceeds the configured sheet cells in ["
                + std::string(inSection) + "].");
        }
    }

    void SpriteAnimation::Update(const float inDeltaSeconds, const bool inLoop)
    {
        if (!inLoop)
        {
            (void)AdvanceOnce(inDeltaSeconds);
            return;
        }

        if (frameCount <= 1 || isFinished)
        {
            return;
        }

        elapsedSeconds += inDeltaSeconds;
        while (elapsedSeconds >= frameSeconds)
        {
            elapsedSeconds -= frameSeconds;
            if (currentFrame + 1 < frameCount)
            {
                ++currentFrame;
            }
            else
            {
                currentFrame = 0;
            }
        }
    }

    void SpriteAnimation::Reset()
    {
        currentFrame = 0;
        elapsedSeconds = 0.0f;
        isFinished = false;
    }

    void SpriteAnimation::Seek(const float inSeconds, const bool inLoop)
    {
        if (!std::isfinite(inSeconds) || inSeconds < 0.0f)
            throw std::runtime_error("Invalid animation presentation time.");
        const double frame = std::floor(static_cast<double>(inSeconds) / frameSeconds);
        currentFrame = inLoop ? static_cast<std::uint32_t>(std::fmod(frame, frameCount))
            : static_cast<std::uint32_t>(std::min(frame, static_cast<double>(frameCount - 1)));
        elapsedSeconds = std::fmod(inSeconds, frameSeconds);
        isFinished = !inLoop && frame >= frameCount;
    }

    float SpriteAnimation::AdvanceOnce(const float inDeltaSeconds)
    {
        if (isFinished)
        {
            return inDeltaSeconds;
        }

        float remainingSeconds = inDeltaSeconds;
        while (remainingSeconds >= frameSeconds - elapsedSeconds)
        {
            remainingSeconds -= frameSeconds - elapsedSeconds;
            elapsedSeconds = 0.0f;
            if (currentFrame + 1 < frameCount)
            {
                ++currentFrame;
            }
            else
            {
                isFinished = true;
                return remainingSeconds;
            }
        }
        elapsedSeconds += remainingSeconds;
        return 0.0f;
    }

    void SpriteAnimation::Draw(D2DRenderer& inRenderer, const float inCenterX, const float inBottomY,
        const bool inFlipHorizontal) const
    {
        if (!bitmap) return;
        if (!frames.empty())
        {
            const auto& frame = frames[currentFrame];
            const float width = frame.sourceRect.right - frame.sourceRect.left;
            const float height = frame.sourceRect.bottom - frame.sourceRect.top;
            // Reflect the local pivot along with the image to keep its ground point fixed.
            const float pivotX = inFlipHorizontal ? width - frame.pivot.x : frame.pivot.x;
            const float left = inCenterX - pivotX * scale;
            const float top = inBottomY - frame.pivot.y * scale;
            // Metadata uses pixels; Direct2D's source rectangle uses bitmap DIPs.
            const auto pixelSize = bitmap->GetPixelSize();
            const auto dipSize = bitmap->GetSize();
            const float dipX = dipSize.width / static_cast<float>(pixelSize.width);
            const float dipY = dipSize.height / static_cast<float>(pixelSize.height);
            const auto source = D2D1::RectF(frame.sourceRect.left * dipX, frame.sourceRect.top * dipY,
                frame.sourceRect.right * dipX, frame.sourceRect.bottom * dipY);
            inRenderer.DrawBitmap(bitmap.Get(), source,
                D2D1::RectF(left, top, left + width * scale, top + height * scale), inFlipHorizontal);
            return;
        }
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
        const float currentAnchorY = rows == 2 && row == 1 ? secondRowAnchorY : anchorY;
        const float destinationTop = inBottomY - renderHeight * currentAnchorY;
        const float sourceAnchorX = frameAnchorXs[currentFrame];
        const float destinationAnchorX = inFlipHorizontal ? 1.0f - sourceAnchorX : sourceAnchorX;
        const float destinationLeft = inCenterX - renderWidth * destinationAnchorX;
        const D2D1_RECT_F destinationRectangle = D2D1::RectF(
            destinationLeft,
            destinationTop,
            destinationLeft + renderWidth,
            destinationTop + renderHeight);

        inRenderer.DrawBitmap(bitmap.Get(), sourceRectangle, destinationRectangle, inFlipHorizontal);
    }
}
