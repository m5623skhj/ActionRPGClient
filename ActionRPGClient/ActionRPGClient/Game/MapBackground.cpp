#include "Game/MapBackground.h"

#include "Game/Camera.h"
#include "Game/Vector2.h"
#include "Graphics/D2DRenderer.h"
#include "Resources/AssetCatalog.h"

#include <d2d1_1helper.h>

#include <algorithm>
#include <limits>

namespace ActionRPG
{
    MapBackground::MapBackground(const AssetCatalog& inAssetCatalog)
        : assetCatalog(inAssetCatalog)
    {
    }

    void MapBackground::Configure(const TownProtocol::MapInfo& inMap)
    {
        images = inMap.images;
        worldLeft = inMap.worldLeft;
        worldTop = inMap.worldTop;
        worldRight = inMap.worldRight;
        worldBottom = inMap.worldBottom;
        bitmapCache.clear();
    }

    void MapBackground::Render(D2DRenderer& inRenderer, const Camera& inCamera) const
    {
        ++frameNumber;
        if (images.empty())
        {
            const Vector2 topLeft = inCamera.WorldToScreen({ worldLeft, worldTop });
            const Vector2 bottomRight = inCamera.WorldToScreen({ worldRight, worldBottom });
            inRenderer.FillRectangle(topLeft.x, topLeft.y, bottomRight.x, bottomRight.y,
                D2D1::ColorF(0.08f, 0.11f, 0.17f));
            return;
        }

        for (const TownProtocol::MapImage& image : images)
        {
            const Vector2 topLeft = inCamera.WorldToScreen({ image.x, image.y });
            const Vector2 bottomRight = inCamera.WorldToScreen({ image.x + image.width, image.y + image.height });
            if (bottomRight.x <= 0.0f || bottomRight.y <= 0.0f
                || topLeft.x >= inCamera.GetViewportWidth() || topLeft.y >= inCamera.GetViewportHeight())
            {
                continue;
            }

            CachedBitmap& cached = bitmapCache[image.asset];
            cached.lastUsedFrame = frameNumber;
            if (!cached.bitmap && !cached.loadFailed)
            {
                try
                {
                    cached.bitmap = inRenderer.LoadBitmap(assetCatalog.GetAssetPath(image.asset));
                }
                catch (...)
                {
                    cached.loadFailed = true;
                }
            }
            if (!cached.bitmap)
            {
                inRenderer.FillRectangle(topLeft.x, topLeft.y, bottomRight.x, bottomRight.y,
                    D2D1::ColorF(0.25f, 0.08f, 0.10f));
                continue;
            }

            const D2D1_SIZE_F bitmapSize = cached.bitmap->GetSize();
            inRenderer.DrawBitmap(cached.bitmap.Get(),
                D2D1::RectF(0.0f, 0.0f, bitmapSize.width, bitmapSize.height),
                D2D1::RectF(topLeft.x, topLeft.y, bottomRight.x, bottomRight.y));
        }

        constexpr std::size_t MAX_CACHED_BITMAPS = 64;
        while (bitmapCache.size() > MAX_CACHED_BITMAPS)
        {
            auto oldest = bitmapCache.end();
            std::uint64_t oldestFrame = std::numeric_limits<std::uint64_t>::max();
            for (auto iterator = bitmapCache.begin(); iterator != bitmapCache.end(); ++iterator)
            {
                if (iterator->second.lastUsedFrame < oldestFrame)
                {
                    oldest = iterator;
                    oldestFrame = iterator->second.lastUsedFrame;
                }
            }
            if (oldest == bitmapCache.end() || oldestFrame == frameNumber)
            {
                break;
            }
            bitmapCache.erase(oldest);
        }
    }
}
