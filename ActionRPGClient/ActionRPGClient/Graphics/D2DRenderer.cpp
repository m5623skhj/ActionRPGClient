#include "Graphics/D2DRenderer.h"

#include <d2d1_1helper.h>

#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace
{
    void ThrowIfFailed(const HRESULT inResult, const char* const inOperation)
    {
        if (SUCCEEDED(inResult))
        {
            return;
        }

        std::ostringstream message;
        message << inOperation << " failed with HRESULT 0x"
            << std::hex << std::uppercase << static_cast<unsigned long>(inResult) << '.';
        throw std::runtime_error(message.str());
    }
}

namespace ActionRPG
{
    D2DRenderer::D2DRenderer(GraphicsDevice& inGraphicsDevice)
        : graphicsDevice(inGraphicsDevice)
        , d2dContext(inGraphicsDevice.GetD2DContext())
    {
        ThrowIfFailed(
            CoCreateInstance(
                CLSID_WICImagingFactory2,
                nullptr,
                CLSCTX_INPROC_SERVER,
                IID_PPV_ARGS(&wicFactory)),
            "CoCreateInstance(CLSID_WICImagingFactory2)");

        ThrowIfFailed(
            d2dContext->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::White), &solidColorBrush),
            "ID2D1DeviceContext::CreateSolidColorBrush");

        ThrowIfFailed(
            graphicsDevice.GetDWriteFactory()->CreateTextFormat(
                L"Segoe UI",
                nullptr,
                DWRITE_FONT_WEIGHT_NORMAL,
                DWRITE_FONT_STYLE_NORMAL,
                DWRITE_FONT_STRETCH_NORMAL,
                18.0f,
                L"ko-KR",
                &defaultTextFormat),
            "IDWriteFactory::CreateTextFormat");

        defaultTextFormat->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    }

    void D2DRenderer::BeginFrame(const D2D1_COLOR_F& inClearColor)
    {
        d2dContext->BeginDraw();
        d2dContext->SetTransform(D2D1::Matrix3x2F::Identity());
        d2dContext->Clear(inClearColor);
    }

    void D2DRenderer::EndFrame()
    {
        const HRESULT drawResult = d2dContext->EndDraw();
        if (drawResult == D2DERR_RECREATE_TARGET)
        {
            graphicsDevice.RecreateRenderTarget();
            return;
        }

        ThrowIfFailed(drawResult, "ID2D1DeviceContext::EndDraw");
        graphicsDevice.Present();
    }

    void D2DRenderer::FillRectangle(const float inLeft, const float inTop, const float inRight,
        const float inBottom, const D2D1_COLOR_F& inColor)
    {
        solidColorBrush->SetColor(inColor);
        d2dContext->FillRectangle(D2D1::RectF(inLeft, inTop, inRight, inBottom), solidColorBrush.Get());
    }

    void D2DRenderer::DrawRectangle(const float inLeft, const float inTop, const float inRight,
        const float inBottom, const D2D1_COLOR_F& inColor, const float inStrokeWidth)
    {
        solidColorBrush->SetColor(inColor);
        d2dContext->DrawRectangle(
            D2D1::RectF(inLeft, inTop, inRight, inBottom),
            solidColorBrush.Get(),
            inStrokeWidth);
    }

    void D2DRenderer::FillEllipse(const float inCenterX, const float inCenterY, const float inRadiusX,
        const float inRadiusY, const D2D1_COLOR_F& inColor)
    {
        solidColorBrush->SetColor(inColor);
        d2dContext->FillEllipse(
            D2D1::Ellipse(D2D1::Point2F(inCenterX, inCenterY), inRadiusX, inRadiusY),
            solidColorBrush.Get());
    }

    Microsoft::WRL::ComPtr<ID2D1Bitmap1> D2DRenderer::LoadBitmap(
        const std::filesystem::path& inFilePath) const
    {
        Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder;
        ThrowIfFailed(
            wicFactory->CreateDecoderFromFilename(
                inFilePath.c_str(),
                nullptr,
                GENERIC_READ,
                WICDecodeMetadataCacheOnLoad,
                &decoder),
            "IWICImagingFactory::CreateDecoderFromFilename");

        Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame;
        ThrowIfFailed(decoder->GetFrame(0, &frame), "IWICBitmapDecoder::GetFrame");

        Microsoft::WRL::ComPtr<IWICFormatConverter> converter;
        ThrowIfFailed(wicFactory->CreateFormatConverter(&converter), "IWICImagingFactory::CreateFormatConverter");
        ThrowIfFailed(
            converter->Initialize(
                frame.Get(),
                GUID_WICPixelFormat32bppPBGRA,
                WICBitmapDitherTypeNone,
                nullptr,
                0.0,
                WICBitmapPaletteTypeCustom),
            "IWICFormatConverter::Initialize");

        Microsoft::WRL::ComPtr<ID2D1Bitmap1> bitmap;
        ThrowIfFailed(
            d2dContext->CreateBitmapFromWicBitmap(converter.Get(), nullptr, &bitmap),
            "ID2D1DeviceContext::CreateBitmapFromWicBitmap");
        return bitmap;
    }

    void D2DRenderer::DrawBitmap(ID2D1Bitmap1* const inBitmap, const D2D1_RECT_F& inSourceRectangle,
        const D2D1_RECT_F& inDestinationRectangle, const bool inFlipHorizontal)
    {
        D2D1_MATRIX_3X2_F originalTransform{};
        d2dContext->GetTransform(&originalTransform);

        if (inFlipHorizontal)
        {
            const float centerX = (inDestinationRectangle.left + inDestinationRectangle.right) * 0.5f;
            d2dContext->SetTransform(
                D2D1::Matrix3x2F::Scale(
                    D2D1::SizeF(-1.0f, 1.0f),
                    D2D1::Point2F(centerX, 0.0f)) * originalTransform);
        }

        d2dContext->DrawBitmap(
            inBitmap,
            &inDestinationRectangle,
            1.0f,
            D2D1_INTERPOLATION_MODE_NEAREST_NEIGHBOR,
            &inSourceRectangle,
            nullptr);

        if (inFlipHorizontal)
        {
            d2dContext->SetTransform(originalTransform);
        }
    }

    void D2DRenderer::DrawText(const std::wstring_view inText, const float inLeft, const float inTop,
        const float inRight, const float inBottom, const D2D1_COLOR_F& inColor)
    {
        solidColorBrush->SetColor(inColor);
        d2dContext->DrawTextW(
            inText.data(),
            static_cast<UINT32>(inText.size()),
            defaultTextFormat.Get(),
            D2D1::RectF(inLeft, inTop, inRight, inBottom),
            solidColorBrush.Get(),
            D2D1_DRAW_TEXT_OPTIONS_CLIP);
    }
}
