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
        explicit D2DRenderer(GraphicsDevice& inGraphicsDevice);

        D2DRenderer(const D2DRenderer&) = delete;
        D2DRenderer& operator=(const D2DRenderer&) = delete;

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
        void PushAxisAlignedClip(const D2D1_RECT_F& inRectangle);
        void PopAxisAlignedClip();
        [[nodiscard]] Microsoft::WRL::ComPtr<ID2D1Bitmap1> LoadBitmap(
            const std::filesystem::path& inFilePath) const;
        void DrawBitmap(ID2D1Bitmap1* inBitmap, const D2D1_RECT_F& inSourceRectangle,
            const D2D1_RECT_F& inDestinationRectangle, bool inFlipHorizontal = false);
        void SetUiFontFamily(std::wstring_view inFamily);
        void DrawUiText(std::wstring_view inText, const D2D1_RECT_F& inRect, const D2D1_COLOR_F& inColor,
            float inSize, bool inWrap = false, bool inCentered = false);
        void DrawUiIcon(ID2D1Bitmap1* inBitmap, const D2D1_RECT_F& inRect, bool inGray);
        void DrawText(std::wstring_view inText, float inLeft, float inTop, float inRight, float inBottom,
            const D2D1_COLOR_F& inColor);

    private:
        GraphicsDevice& graphicsDevice;
        ID2D1DeviceContext* d2dContext{};
        Microsoft::WRL::ComPtr<IWICImagingFactory2> wicFactory;
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> solidColorBrush;
        Microsoft::WRL::ComPtr<IDWriteTextFormat> defaultTextFormat;
        Microsoft::WRL::ComPtr<ID2D1Effect> grayscaleEffect;
        std::wstring uiFontFamily;
        std::map<std::tuple<float,bool,bool>, Microsoft::WRL::ComPtr<IDWriteTextFormat>> uiTextFormats;
    };
}
