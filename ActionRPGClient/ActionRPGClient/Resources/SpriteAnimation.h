#pragma once

#include <d2d1_1.h>
#include <wrl/client.h>

#include <cstdint>
#include <string_view>
#include <vector>

namespace ActionRPG
{
    struct SpriteFrame
    {
        D2D1_RECT_F sourceRect{};
        D2D1_POINT_2F pivot{};
    };

    class AssetCatalog;
    class D2DRenderer;
    class IniDocument;

    // Owns one externally configured sprite sheet and its playback position.
    class SpriteAnimation final
    {
    public:
        SpriteAnimation() = default;
        SpriteAnimation(D2DRenderer& inRenderer, const AssetCatalog& inAssetCatalog,
            const IniDocument& inDefinitions, std::string_view inSection);
        SpriteAnimation(Microsoft::WRL::ComPtr<ID2D1Bitmap1> inBitmap,
            std::vector<SpriteFrame> inFrames, float inFrameSeconds, float inScale,
            std::vector<float> inFrameDurationsSeconds = {});

        void Update(float inDeltaSeconds, bool inLoop = true);
        // Advance one playthrough and return update time left after the last frame's duration.
        [[nodiscard]] float AdvanceOnce(float inDeltaSeconds);
        void Reset();
        // Select a frame directly from authoritative elapsed time without advancing gameplay.
        void Seek(float inSeconds, bool inLoop = false);
        void Draw(D2DRenderer& inRenderer, float inCenterX, float inBottomY,
            bool inFlipHorizontal) const;

        [[nodiscard]] std::uint32_t GetCurrentFrame() const { return currentFrame; }
        [[nodiscard]] std::uint32_t GetFrameCount() const { return frameCount; }
        [[nodiscard]] float GetDuration() const { return frameEndSeconds.back(); }
        [[nodiscard]] std::uint32_t GetFrameAt(float inSeconds, bool inLoop = false) const;
        [[nodiscard]] float GetFrameStartSeconds(std::uint32_t inIndex) const;
        [[nodiscard]] bool IsFinished() const { return isFinished; }

    private:
        void BuildTimeline(const std::vector<float>& inFrameDurationsSeconds);
        void SetTime(double inSeconds, bool inLoop);
        [[nodiscard]] std::uint32_t FrameAt(double inSeconds, bool inLoop) const;
        Microsoft::WRL::ComPtr<ID2D1Bitmap1> bitmap;
        std::uint32_t columns{ 1 };
        std::uint32_t rows{ 1 };
        std::uint32_t frameCount{ 1 };
        std::uint32_t currentFrame{};
        bool isFinished{};
        float frameSeconds{ 1.0f };
        double elapsedSeconds{};
        std::vector<float> frameEndSeconds{1.0f};
        float firstRowRatio{ 0.5f };
        float anchorY{ 1.0f };
        float secondRowAnchorY{ 1.0f };
        float renderWidth{};
        float renderHeight{};
        std::vector<float> frameAnchorXs;
        std::vector<SpriteFrame> frames;
        float scale{ 1.0f };
    };
}
