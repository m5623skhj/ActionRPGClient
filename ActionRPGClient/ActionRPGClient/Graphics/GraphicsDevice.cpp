#include "Graphics/GraphicsDevice.h"

#include <d2d1_1helper.h>

#include <iterator>
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
    GraphicsDevice::GraphicsDevice(const HWND inWindowHandle, const std::uint32_t inWidth,
        const std::uint32_t inHeight)
    {
        CreateD3DDevice();
        CreateD2DDevice();
        CreateSwapChain(inWindowHandle, inWidth, inHeight);
        CreateRenderTarget();
    }

    void GraphicsDevice::Resize(const std::uint32_t inWidth, const std::uint32_t inHeight)
    {
        if (inWidth == 0 || inHeight == 0)
        {
            return;
        }

        d2dContext->SetTarget(nullptr);
        d2dTargetBitmap.Reset();
        d2dContext->Flush();

        ThrowIfFailed(
            swapChain->ResizeBuffers(2, inWidth, inHeight, DXGI_FORMAT_B8G8R8A8_UNORM, 0),
            "IDXGISwapChain1::ResizeBuffers");

        CreateRenderTarget();
    }

    void GraphicsDevice::RecreateRenderTarget()
    {
        d2dContext->SetTarget(nullptr);
        d2dTargetBitmap.Reset();
        CreateRenderTarget();
    }

    void GraphicsDevice::Present()
    {
        ThrowIfFailed(swapChain->Present(1, 0), "IDXGISwapChain1::Present");
    }

    void GraphicsDevice::CreateD3DDevice()
    {
        UINT deviceFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#if defined(_DEBUG)
        deviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

        constexpr D3D_FEATURE_LEVEL featureLevels[] = {
            D3D_FEATURE_LEVEL_11_1,
            D3D_FEATURE_LEVEL_11_0
        };

        D3D_FEATURE_LEVEL createdFeatureLevel{};
        HRESULT result = D3D11CreateDevice(
            nullptr,
            D3D_DRIVER_TYPE_HARDWARE,
            nullptr,
            deviceFlags,
            featureLevels,
            static_cast<UINT>(std::size(featureLevels)),
            D3D11_SDK_VERSION,
            &d3dDevice,
            &createdFeatureLevel,
            &d3dContext);

#if defined(_DEBUG)
        if (result == DXGI_ERROR_SDK_COMPONENT_MISSING)
        {
            deviceFlags &= ~D3D11_CREATE_DEVICE_DEBUG;
            result = D3D11CreateDevice(
                nullptr,
                D3D_DRIVER_TYPE_HARDWARE,
                nullptr,
                deviceFlags,
                featureLevels,
                static_cast<UINT>(std::size(featureLevels)),
                D3D11_SDK_VERSION,
                &d3dDevice,
                &createdFeatureLevel,
                &d3dContext);
        }
#endif

        ThrowIfFailed(result, "D3D11CreateDevice");
    }

    void GraphicsDevice::CreateD2DDevice()
    {
        D2D1_FACTORY_OPTIONS factoryOptions{};
#if defined(_DEBUG)
        factoryOptions.debugLevel = D2D1_DEBUG_LEVEL_INFORMATION;
#endif

        ThrowIfFailed(
            D2D1CreateFactory(
                D2D1_FACTORY_TYPE_SINGLE_THREADED,
                factoryOptions,
                d2dFactory.GetAddressOf()),
            "D2D1CreateFactory");

        Microsoft::WRL::ComPtr<IDXGIDevice> dxgiDevice;
        ThrowIfFailed(d3dDevice.As(&dxgiDevice), "ID3D11Device::QueryInterface(IDXGIDevice)");
        ThrowIfFailed(d2dFactory->CreateDevice(dxgiDevice.Get(), &d2dDevice), "ID2D1Factory1::CreateDevice");
        ThrowIfFailed(
            d2dDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &d2dContext),
            "ID2D1Device::CreateDeviceContext");

        ThrowIfFailed(
            DWriteCreateFactory(
                DWRITE_FACTORY_TYPE_SHARED,
                __uuidof(IDWriteFactory),
                reinterpret_cast<IUnknown**>(dwriteFactory.GetAddressOf())),
            "DWriteCreateFactory");
    }

    void GraphicsDevice::CreateSwapChain(const HWND inWindowHandle, const std::uint32_t inWidth,
        const std::uint32_t inHeight)
    {
        Microsoft::WRL::ComPtr<IDXGIDevice> dxgiDevice;
        Microsoft::WRL::ComPtr<IDXGIAdapter> dxgiAdapter;
        Microsoft::WRL::ComPtr<IDXGIFactory2> dxgiFactory;

        ThrowIfFailed(d3dDevice.As(&dxgiDevice), "ID3D11Device::QueryInterface(IDXGIDevice)");
        ThrowIfFailed(dxgiDevice->GetAdapter(&dxgiAdapter), "IDXGIDevice::GetAdapter");
        ThrowIfFailed(
            dxgiAdapter->GetParent(IID_PPV_ARGS(&dxgiFactory)),
            "IDXGIAdapter::GetParent(IDXGIFactory2)");

        DXGI_SWAP_CHAIN_DESC1 swapChainDescription{};
        swapChainDescription.Width = inWidth;
        swapChainDescription.Height = inHeight;
        swapChainDescription.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        swapChainDescription.SampleDesc.Count = 1;
        swapChainDescription.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swapChainDescription.BufferCount = 2;
        swapChainDescription.Scaling = DXGI_SCALING_STRETCH;
        swapChainDescription.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        swapChainDescription.AlphaMode = DXGI_ALPHA_MODE_IGNORE;

        ThrowIfFailed(
            dxgiFactory->CreateSwapChainForHwnd(
                d3dDevice.Get(),
                inWindowHandle,
                &swapChainDescription,
                nullptr,
                nullptr,
                &swapChain),
            "IDXGIFactory2::CreateSwapChainForHwnd");

        ThrowIfFailed(
            dxgiFactory->MakeWindowAssociation(inWindowHandle, DXGI_MWA_NO_ALT_ENTER),
            "IDXGIFactory::MakeWindowAssociation");
    }

    void GraphicsDevice::CreateRenderTarget()
    {
        Microsoft::WRL::ComPtr<IDXGISurface> backBuffer;
        ThrowIfFailed(swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer)), "IDXGISwapChain1::GetBuffer");

        constexpr float DIPS_PER_INCH = 96.0f;
        const D2D1_BITMAP_PROPERTIES1 bitmapProperties = D2D1::BitmapProperties1(
            D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE),
            DIPS_PER_INCH,
            DIPS_PER_INCH);

        ThrowIfFailed(
            d2dContext->CreateBitmapFromDxgiSurface(backBuffer.Get(), &bitmapProperties, &d2dTargetBitmap),
            "ID2D1DeviceContext::CreateBitmapFromDxgiSurface");

        d2dContext->SetTarget(d2dTargetBitmap.Get());
        d2dContext->SetDpi(DIPS_PER_INCH, DIPS_PER_INCH);
    }
}
