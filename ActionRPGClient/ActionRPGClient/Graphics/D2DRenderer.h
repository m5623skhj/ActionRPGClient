#pragma once

#include "Graphics/GraphicsDevice.h"

#include <d2d1_1.h>
#include <dwrite.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <filesystem>
#include <string_view>
#include <map>
#include <tuple>
#include <string>

namespace ActionRPG
{
    class D2DRenderer final
    {
    public:
        // Uniform UI geometry/text scaling on the render thread; restores the letterbox transform via RAII.
        class UiScaleScope final
        {
        public:
            UiScaleScope(D2DRenderer& inRenderer, float inScale);
            ~UiScaleScope();
            UiScaleScope(const UiScaleScope&) = delete;
            UiScaleScope& operator=(const UiScaleScope&) = delete;
        private:
            ID2D1DeviceContext* context{};
            D2D1_MATRIX_3X2_F previous{};
        };

        class UiTranslationScope final
        {
        public:
            UiTranslationScope(D2DRenderer& inRenderer, float inX, float inY);
            ~UiTranslationScope();
            UiTranslationScope(const UiTranslationScope&) = delete;
            UiTranslationScope& operator=(const UiTranslationScope&) = delete;
        private:
            ID2D1DeviceContext* context{};
            D2D1_MATRIX_3X2_F previous{};
        };

        explicit D2DRenderer(GraphicsDevice& inGraphicsDevice);

        D2DRenderer(const D2DRenderer&) = delete;
        D2DRenderer& operator=(const D2DRenderer&) = delete;

        void SetViewport(std::uint32_t inPhysicalWidth, std::uint32_t inPhysicalHeight,
            std::uint32_t inLogicalWidth, std::uint32_t inLogicalHeight);
        void BeginFrame(const D2D1_COLOR_F& inClearColor);
        void EndFrame();

        void FillRectangle(float inLeft, float inTop, float inRight, float inBottom,
            const D2D1_COLOR_F& inColor);
        void DrawRectangle(float inLeft, float inTop, float inRight, float inBottom,
            const D2D1_COLOR_F& inColor, float inStrokeWidth = 1.0f);
        void DrawLine(float inStartX, float inStartY, float inEndX, float inEndY,
            const D2D1_COLOR_F& inColor, float inStrokeWidth = 1.0f);
        void FillEllipse(float inCenterX, float inCenterY, float inRadiusX, float inRadiusY,
            const D2D1_COLOR_F& inColor);
        // Screen-space direction; the trail grows only over distance already traveled.
        void DrawLuminousProjectile(float inX, float inY, float inDirectionX, float inDirectionY,
            float inRadius, float inTravelDistance, const D2D1_COLOR_F& inBodyColor,
            const D2D1_COLOR_F& inHighlightColor);
        void PushAxisAlignedClip(const D2D1_RECT_F& inRectangle);
        void PopAxisAlignedClip();
        [[nodiscard]] Microsoft::WRL::ComPtr<ID2D1Bitmap1> LoadBitmap(
            const std::filesystem::path& inFilePath) const;
        void DrawBitmap(ID2D1Bitmap1* inBitmap, const D2D1_RECT_F& inSourceRectangle,
            const D2D1_RECT_F& inDestinationRectangle, bool inFlipHorizontal = false);
        // Four translated, solid-color alpha silhouettes; draw the original bitmap afterwards.
        void DrawBitmapOutline(ID2D1Bitmap1* inBitmap, const D2D1_RECT_F& inSourceRectangle,
            const D2D1_RECT_F& inDestinationRectangle, const D2D1_COLOR_F& inColor, float inThickness);
        void SetUiFontFamily(std::wstring_view inFamily);
        void DrawUiText(std::wstring_view inText, const D2D1_RECT_F& inRect, const D2D1_COLOR_F& inColor,
            float inSize, bool inWrap = false, bool inCentered = false);
        [[nodiscard]] D2D1_SIZE_F MeasureUiText(std::wstring_view inText, float inWidth, float inSize) const;
        void DrawUiIcon(ID2D1Bitmap1* inBitmap, const D2D1_RECT_F& inRect, bool inGray);
        void DrawText(std::wstring_view inText, float inLeft, float inTop, float inRight, float inBottom,
            const D2D1_COLOR_F& inColor);

    private:
        void InitializeProjectileLight();

        float logicalWidth = 1280.0f, logicalHeight = 720.0f;
        float viewportScale = 1.0f, viewportLeft{}, viewportTop{};
        GraphicsDevice& graphicsDevice;
        ID2D1DeviceContext* d2dContext{};
        Microsoft::WRL::ComPtr<IWICImagingFactory2> wicFactory;
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> solidColorBrush;
        Microsoft::WRL::ComPtr<ID2D1RadialGradientBrush> projectileGlowBrush;
        Microsoft::WRL::ComPtr<ID2D1LinearGradientBrush> projectileTrailBrush;
        Microsoft::WRL::ComPtr<ID2D1PathGeometry> projectileTrailGeometry;
        float projectileLightPhase{};
        Microsoft::WRL::ComPtr<IDWriteTextFormat> defaultTextFormat;
        Microsoft::WRL::ComPtr<ID2D1Effect> grayscaleEffect;
        Microsoft::WRL::ComPtr<ID2D1Effect> silhouetteEffect;
        std::wstring uiFontFamily;
        std::map<std::tuple<float,bool,bool>, Microsoft::WRL::ComPtr<IDWriteTextFormat>> uiTextFormats;
    };
}
