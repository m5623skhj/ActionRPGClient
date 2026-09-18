#pragma once

#include <d2d1_1.h>
#include <wrl/client.h>

#include <cstdint>
#include <string_view>

namespace ActionRPG
{
    class AssetCatalog;
    class D2DRenderer;
    class IniDocument;

    // Owns one externally configured sprite sheet and its playback position.
    class SpriteAnimation final
    {
    public:
        SpriteAnimation(D2DRenderer& inRenderer, const AssetCatalog& inAssetCatalog,
            const IniDocument& inDefinitions, std::string_view inSection);

        void Update(float inDeltaSeconds);
        void Reset();
        void Draw(D2DRenderer& inRenderer, float inCenterX, float inBottomY,
            bool inFlipHorizontal) const;

    private:
        Microsoft::WRL::ComPtr<ID2D1Bitmap1> bitmap;
        std::uint32_t columns{ 1 };
        std::uint32_t rows{ 1 };
        std::uint32_t frameCount{ 1 };
        std::uint32_t currentFrame{};
        float frameSeconds{ 1.0f };
        float elapsedSeconds{};
        float firstRowRatio{ 0.5f };
        float anchorY{ 1.0f };
        float renderWidth{};
        float renderHeight{};
    };
}
