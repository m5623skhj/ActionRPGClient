#include "Graphics/D2DRenderer.h"

#include <d2d1_1helper.h>
#include <initguid.h>
#include <d2d1effects.h>
#include <d2d1effects_2.h>

#include <algorithm>
#include <chrono>
#include <cmath>
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

    D2DRenderer::UiScaleScope::UiScaleScope(D2DRenderer& inRenderer, float inScale)
        : context(inRenderer.d2dContext)
    {
        context->GetTransform(&previous);
        context->SetTransform(D2D1::Matrix3x2F::Scale(inScale,inScale)*previous);
    }
    D2DRenderer::UiScaleScope::~UiScaleScope() { context->SetTransform(previous); }

    void D2DRenderer::SetViewport(const std::uint32_t inPhysicalWidth, const std::uint32_t inPhysicalHeight,
        const std::uint32_t inLogicalWidth, const std::uint32_t inLogicalHeight)
    {
        if (!inPhysicalWidth || !inPhysicalHeight || !inLogicalWidth || !inLogicalHeight) return;
        logicalWidth = static_cast<float>(inLogicalWidth);
        logicalHeight = static_cast<float>(inLogicalHeight);
        viewportScale = std::min(inPhysicalWidth / logicalWidth, inPhysicalHeight / logicalHeight);
        viewportLeft = (inPhysicalWidth - logicalWidth * viewportScale) * 0.5f;
        viewportTop = (inPhysicalHeight - logicalHeight * viewportScale) * 0.5f;
    }

    void D2DRenderer::BeginFrame(const D2D1_COLOR_F& inClearColor)
    {
        // A cosmetic frame clock keeps the flowing light smooth between network snapshots.
        const double lightSeconds = std::chrono::duration<double>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        projectileLightPhase = static_cast<float>(std::fmod(lightSeconds * 6.0, 1.0));
        d2dContext->BeginDraw();
        d2dContext->SetTransform(D2D1::Matrix3x2F::Identity());
        d2dContext->Clear(D2D1::ColorF(D2D1::ColorF::Black));
        d2dContext->SetTransform(D2D1::Matrix3x2F::Scale(viewportScale, viewportScale)
            * D2D1::Matrix3x2F::Translation(viewportLeft, viewportTop));
        PushAxisAlignedClip(D2D1::RectF(0, 0, logicalWidth, logicalHeight));
        FillRectangle(0, 0, logicalWidth, logicalHeight, inClearColor);
    }

    void D2DRenderer::EndFrame()
    {
        PopAxisAlignedClip();
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

    void D2DRenderer::InitializeProjectileLight()
    {
        // Render-thread resources are shared by every bullet, rather than allocated per shot.
        const D2D1_GRADIENT_STOP glowStops[]{
            {0.0f, D2D1::ColorF(1.0f, 0.95f, 0.65f, 0.62f)},
            {0.35f, D2D1::ColorF(1.0f, 0.63f, 0.12f, 0.24f)},
            {1.0f, D2D1::ColorF(1.0f, 0.32f, 0.02f, 0.0f)}};
        Microsoft::WRL::ComPtr<ID2D1GradientStopCollection> glowCollection;
        ThrowIfFailed(d2dContext->CreateGradientStopCollection(glowStops, 3,
            D2D1_GAMMA_2_2, D2D1_EXTEND_MODE_CLAMP, &glowCollection), "Create bullet glow stops");
        ThrowIfFailed(d2dContext->CreateRadialGradientBrush(
            D2D1::RadialGradientBrushProperties(D2D1::Point2F(0, 0), D2D1::Point2F(0, 0), 1, 1),
            glowCollection.Get(), &projectileGlowBrush), "Create bullet glow brush");

        const D2D1_GRADIENT_STOP trailStops[]{
            {0.0f, D2D1::ColorF(1.0f, 0.35f, 0.02f, 0.0f)},
            {0.4f, D2D1::ColorF(1.0f, 0.48f, 0.06f, 0.12f)},
            {0.8f, D2D1::ColorF(1.0f, 0.72f, 0.20f, 0.48f)},
            {1.0f, D2D1::ColorF(1.0f, 0.94f, 0.66f, 0.9f)}};
        Microsoft::WRL::ComPtr<ID2D1GradientStopCollection> trailCollection;
        ThrowIfFailed(d2dContext->CreateGradientStopCollection(trailStops, 4,
            D2D1_GAMMA_2_2, D2D1_EXTEND_MODE_CLAMP, &trailCollection), "Create bullet trail stops");
        ThrowIfFailed(d2dContext->CreateLinearGradientBrush(
            D2D1::LinearGradientBrushProperties(D2D1::Point2F(-1, 0), D2D1::Point2F(0, 0)),
            trailCollection.Get(), &projectileTrailBrush), "Create bullet trail brush");

        Microsoft::WRL::ComPtr<ID2D1Factory> factory;
        d2dContext->GetFactory(&factory);
        ThrowIfFailed(factory->CreatePathGeometry(&projectileTrailGeometry), "Create bullet trail geometry");
        Microsoft::WRL::ComPtr<ID2D1GeometrySink> sink;
        ThrowIfFailed(projectileTrailGeometry->Open(&sink), "Open bullet trail geometry");
        sink->BeginFigure(D2D1::Point2F(0, -1), D2D1_FIGURE_BEGIN_FILLED);
        sink->AddQuadraticBezier(D2D1::QuadraticBezierSegment(D2D1::Point2F(-0.45f, -0.65f), D2D1::Point2F(-1, 0)));
        sink->AddQuadraticBezier(D2D1::QuadraticBezierSegment(D2D1::Point2F(-0.45f, 0.65f), D2D1::Point2F(0, 1)));
        sink->EndFigure(D2D1_FIGURE_END_CLOSED);
        ThrowIfFailed(sink->Close(), "Close bullet trail geometry");
    }

    /** Draw a tapered gradient wake, two flowing filaments and a bright core in flight direction. */
    void D2DRenderer::DrawLuminousProjectile(const float inX, const float inY, const float inDirectionX,
        const float inDirectionY, const float inRadius, const float inTravelDistance,
        const D2D1_COLOR_F& inBodyColor, const D2D1_COLOR_F& inHighlightColor)
    {
        if (inRadius <= 0.0f) return;
        if (!projectileTrailGeometry) InitializeProjectileLight();
        const float directionLength = std::hypot(inDirectionX, inDirectionY);
        const float directionX = directionLength > 0.0001f ? inDirectionX / directionLength : 1.0f;
        const float directionY = directionLength > 0.0001f ? inDirectionY / directionLength : 0.0f;
        constexpr float MAX_TRAIL_LENGTH = 64.0f;
        const float trailLength = std::min(std::clamp(inRadius * 12.0f, 24.0f, MAX_TRAIL_LENGTH),
            std::max(0.0f, inTravelDistance));
        D2D1_MATRIX_3X2_F previous{};
        d2dContext->GetTransform(&previous);
        const auto flightTransform = D2D1::Matrix3x2F(directionX, directionY, -directionY, directionX, inX, inY) * previous;
        if (trailLength > 0.01f)
        {
            d2dContext->SetTransform(D2D1::Matrix3x2F::Scale(trailLength, inRadius * 0.95f) * flightTransform);
            d2dContext->FillGeometry(projectileTrailGeometry.Get(), projectileTrailBrush.Get());
            d2dContext->SetTransform(D2D1::Matrix3x2F::Scale(trailLength * 0.82f, inRadius * 0.28f) * flightTransform);
            d2dContext->FillGeometry(projectileTrailGeometry.Get(), projectileTrailBrush.Get());
            d2dContext->SetTransform(flightTransform);
            for (int index = 0; index < 2; ++index)
            {
                const float phase = std::fmod(projectileLightPhase + index * 0.5f, 1.0f);
                const float start = -trailLength * phase;
                const float end = std::min(0.0f, start + trailLength * 0.16f);
                const float offset = (index == 0 ? -1.0f : 1.0f) * inRadius * 0.18f;
                DrawLine(start, offset, end, offset, D2D1::ColorF(inHighlightColor.r, inHighlightColor.g,
                    inHighlightColor.b, inHighlightColor.a * (1.0f - phase) * 0.75f), inRadius * 0.22f);
            }
        }
        d2dContext->SetTransform(D2D1::Matrix3x2F::Scale(inRadius * 3.0f, inRadius * 1.8f) * flightTransform);
        d2dContext->FillEllipse(D2D1::Ellipse(D2D1::Point2F(0, 0), 1, 1), projectileGlowBrush.Get());
        d2dContext->SetTransform(flightTransform);
        FillEllipse(0, 0, inRadius * 1.65f, inRadius * 0.48f, inBodyColor);
        FillEllipse(inRadius * 0.45f, 0, inRadius * 1.15f, inRadius * 0.22f, inHighlightColor);
        d2dContext->SetTransform(previous);
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

    D2D1_SIZE_F D2DRenderer::MeasureUiText(const std::wstring_view inText, const float inWidth, const float inSize) const
    {
        Microsoft::WRL::ComPtr<IDWriteTextFormat> format;
        ThrowIfFailed(graphicsDevice.GetDWriteFactory()->CreateTextFormat(uiFontFamily.c_str(), nullptr,
            DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
            inSize, L"ko-KR", &format), "Create chat text format");
        format->SetWordWrapping(DWRITE_WORD_WRAPPING_EMERGENCY_BREAK);
        Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
        ThrowIfFailed(graphicsDevice.GetDWriteFactory()->CreateTextLayout(inText.data(),
            static_cast<UINT32>(inText.size()), format.Get(), std::max(1.0f, inWidth), 10000.0f,
            &layout), "Measure chat text");
        DWRITE_TEXT_METRICS metrics{};
        ThrowIfFailed(layout->GetMetrics(&metrics), "Get chat text metrics");
        return D2D1::SizeF(metrics.widthIncludingTrailingWhitespace, metrics.height);
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

    D2DRenderer::UiTranslationScope::UiTranslationScope(D2DRenderer& inRenderer, float inX, float inY)
        : context(inRenderer.d2dContext)
    {
        context->GetTransform(&previous);
        context->SetTransform(D2D1::Matrix3x2F::Translation(inX,inY)*previous);
    }
    D2DRenderer::UiTranslationScope::~UiTranslationScope() { context->SetTransform(previous); }

    void D2DRenderer::DrawBitmapOutline(ID2D1Bitmap1* inBitmap, const D2D1_RECT_F& inSourceRectangle,
        const D2D1_RECT_F& inDestinationRectangle, const D2D1_COLOR_F& inColor, const float inThickness)
    {
        const float sourceWidth = inSourceRectangle.right - inSourceRectangle.left;
        const float sourceHeight = inSourceRectangle.bottom - inSourceRectangle.top;
        if (!inBitmap || sourceWidth <= 0 || sourceHeight <= 0 || inThickness <= 0) return;
        if (!silhouetteEffect)
            ThrowIfFailed(d2dContext->CreateEffect(CLSID_D2D1ColorMatrix, &silhouetteEffect), "Create NPC silhouette effect");
        // PREMULTIPLIED mode unpremultiplies input, applies this constant RGB, then
        // premultiplies by the preserved alpha. Transparent pixels stay transparent.
        const D2D1_MATRIX_5X4_F matrix{
            0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,inColor.a, inColor.r,inColor.g,inColor.b,0};
        ThrowIfFailed(silhouetteEffect->SetValue(D2D1_COLORMATRIX_PROP_COLOR_MATRIX, matrix), "Set silhouette color");
        ThrowIfFailed(silhouetteEffect->SetValue(D2D1_COLORMATRIX_PROP_ALPHA_MODE,
            D2D1_COLORMATRIX_ALPHA_MODE_PREMULTIPLIED), "Set silhouette alpha mode");
        silhouetteEffect->SetInput(0, inBitmap);
        D2D1_MATRIX_3X2_F previous{};
        d2dContext->GetTransform(&previous);
        const D2D1_POINT_2F offsets[]{{-inThickness,0},{inThickness,0},{0,-inThickness},{0,inThickness}};
        const D2D1_POINT_2F origin{};
        for (const auto& offset : offsets)
        {
            d2dContext->SetTransform(D2D1::Matrix3x2F::Scale(
                (inDestinationRectangle.right-inDestinationRectangle.left)/sourceWidth,
                (inDestinationRectangle.bottom-inDestinationRectangle.top)/sourceHeight)
                * D2D1::Matrix3x2F::Translation(inDestinationRectangle.left+offset.x, inDestinationRectangle.top+offset.y)
                * previous);
            d2dContext->DrawImage(silhouetteEffect.Get(), &origin, &inSourceRectangle, D2D1_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
        }
        d2dContext->SetTransform(previous);
        silhouetteEffect->SetInput(0, nullptr);
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
