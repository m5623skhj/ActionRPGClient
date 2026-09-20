#pragma once

#include "Network/TownProtocol.h"

#include <d2d1_1.h>
#include <wrl/client.h>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace ActionRPG
{
    class AssetCatalog;
    class Camera;
    class D2DRenderer;

    // Renders authored town image tiles and keeps only a bounded visible bitmap cache.
    class MapBackground final
    {
    public:
        explicit MapBackground(const AssetCatalog& inAssetCatalog);

        void Configure(const TownProtocol::MapInfo& inMap);
        void Render(D2DRenderer& inRenderer, const Camera& inCamera) const;

    private:
        struct CachedBitmap
        {
            Microsoft::WRL::ComPtr<ID2D1Bitmap1> bitmap;
            std::uint64_t lastUsedFrame{};
            bool loadFailed{};
        };

        const AssetCatalog& assetCatalog;
        std::vector<TownProtocol::MapImage> images;
        float worldLeft{};
        float worldTop{};
        float worldRight = 2400.0f;
        float worldBottom = 1400.0f;
        mutable std::unordered_map<std::string, CachedBitmap> bitmapCache;
        mutable std::uint64_t frameNumber{};
    };
}
