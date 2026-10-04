#include "Graphics/D2DRenderer.h"

#include <d2d1_1helper.h>
#include <initguid.h>
#include <d2d1effects_2.h>

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

    void D2DRenderer::DrawLine(const float inStartX, const float inStartY, const float inEndX,
        const float inEndY, const D2D1_COLOR_F& inColor, const float inStrokeWidth)
    {
        solidColorBrush->SetColor(inColor);
        d2dContext->DrawLine(D2D1::Point2F(inStartX, inStartY), D2D1::Point2F(inEndX, inEndY),
            solidColorBrush.Get(), inStrokeWidth);
    }

    void D2DRenderer::FillEllipse(const float inCenterX, const float inCenterY, const float inRadiusX,
        const float inRadiusY, const D2D1_COLOR_F& inColor)
    {
        solidColorBrush->SetColor(inColor);
        d2dContext->FillEllipse(
            D2D1::Ellipse(D2D1::Point2F(inCenterX, inCenterY), inRadiusX, inRadiusY),
            solidColorBrush.Get());
    }

    void D2DRenderer::PushAxisAlignedClip(const D2D1_RECT_F& inRectangle)
    {
        d2dContext->PushAxisAlignedClip(inRectangle, D2D1_ANTIALIAS_MODE_ALIASED);
    }

    void D2DRenderer::PopAxisAlignedClip()
    {
        d2dContext->PopAxisAlignedClip();
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

    void D2DRenderer::SetUiFontFamily(std::wstring_view inFamily)
    { uiFontFamily=inFamily; uiTextFormats.clear(); }

    void D2DRenderer::DrawUiText(std::wstring_view inText,const D2D1_RECT_F& inRect,const D2D1_COLOR_F& inColor,
        float inSize,bool inWrap,bool inCentered)
    {
        const auto key=std::make_tuple(inSize,inWrap,inCentered);
        auto found=uiTextFormats.find(key);
        if (found==uiTextFormats.end())
        {
            Microsoft::WRL::ComPtr<IDWriteTextFormat> format;
            ThrowIfFailed(graphicsDevice.GetDWriteFactory()->CreateTextFormat(uiFontFamily.c_str(),nullptr,
                DWRITE_FONT_WEIGHT_NORMAL,DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,inSize,L"ko-KR",&format),"Create UI text format");
            format->SetWordWrapping(inWrap ? DWRITE_WORD_WRAPPING_EMERGENCY_BREAK:DWRITE_WORD_WRAPPING_NO_WRAP);
            format->SetTextAlignment(inCentered ? DWRITE_TEXT_ALIGNMENT_CENTER:DWRITE_TEXT_ALIGNMENT_LEADING);
            format->SetParagraphAlignment(inCentered ? DWRITE_PARAGRAPH_ALIGNMENT_CENTER:DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
            found=uiTextFormats.emplace(key,std::move(format)).first;
        }
        solidColorBrush->SetColor(inColor);
        d2dContext->DrawTextW(inText.data(),static_cast<UINT32>(inText.size()),found->second.Get(),inRect,
            solidColorBrush.Get(),D2D1_DRAW_TEXT_OPTIONS_CLIP);
    }

    // One reusable grayscale effect; sprites continue using the original nearest-neighbor renderer.
    void D2DRenderer::DrawUiIcon(ID2D1Bitmap1* inBitmap,const D2D1_RECT_F& inRect,bool inGray)
    {
        if (!inBitmap || inRect.right<=inRect.left || inRect.bottom<=inRect.top) return;
        const auto size=inBitmap->GetSize();
        if (!inGray)
        {
            const auto source=D2D1::RectF(0,0,size.width,size.height);
            d2dContext->DrawBitmap(inBitmap,&inRect,1.0f,D2D1_INTERPOLATION_MODE_LINEAR,&source,nullptr);
            return;
        }
        if (!grayscaleEffect) ThrowIfFailed(d2dContext->CreateEffect(CLSID_D2D1Grayscale,&grayscaleEffect),"Create grayscale UI effect");
        grayscaleEffect->SetInput(0,inBitmap);
        D2D1_MATRIX_3X2_F previous; d2dContext->GetTransform(&previous);
        d2dContext->SetTransform(D2D1::Matrix3x2F::Scale((inRect.right-inRect.left)/size.width,(inRect.bottom-inRect.top)/size.height)
            *D2D1::Matrix3x2F::Translation(inRect.left,inRect.top)*previous);
        d2dContext->DrawImage(grayscaleEffect.Get(),nullptr,nullptr,D2D1_INTERPOLATION_MODE_LINEAR);
        d2dContext->SetTransform(previous);
        grayscaleEffect->SetInput(0,nullptr);
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
