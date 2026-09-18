#pragma once

#include <Windows.h>
#include <d2d1_1.h>
#include <d3d11_1.h>
#include <dwrite.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include <cstdint>

namespace ActionRPG
{
    class GraphicsDevice final
    {
    public:
        GraphicsDevice(HWND inWindowHandle, std::uint32_t inWidth, std::uint32_t inHeight);

        GraphicsDevice(const GraphicsDevice&) = delete;
        GraphicsDevice& operator=(const GraphicsDevice&) = delete;

        void Resize(std::uint32_t inWidth, std::uint32_t inHeight);
        void RecreateRenderTarget();
        void Present();

        [[nodiscard]] ID2D1DeviceContext* GetD2DContext() const { return d2dContext.Get(); }
        [[nodiscard]] IDWriteFactory* GetDWriteFactory() const { return dwriteFactory.Get(); }

    private:
        void CreateD3DDevice();
        void CreateD2DDevice();
        void CreateSwapChain(HWND inWindowHandle, std::uint32_t inWidth, std::uint32_t inHeight);
        void CreateRenderTarget();

    private:
        Microsoft::WRL::ComPtr<ID3D11Device> d3dDevice;
        Microsoft::WRL::ComPtr<ID3D11DeviceContext> d3dContext;
        Microsoft::WRL::ComPtr<IDXGISwapChain1> swapChain;

        Microsoft::WRL::ComPtr<ID2D1Factory1> d2dFactory;
        Microsoft::WRL::ComPtr<ID2D1Device> d2dDevice;
        Microsoft::WRL::ComPtr<ID2D1DeviceContext> d2dContext;
        Microsoft::WRL::ComPtr<ID2D1Bitmap1> d2dTargetBitmap;
        Microsoft::WRL::ComPtr<IDWriteFactory> dwriteFactory;
    };
}
