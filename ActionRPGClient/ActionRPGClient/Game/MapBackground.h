#pragma once

namespace ActionRPG
{
    class Camera;
    class D2DRenderer;

    // Draws decorative scenery only. It never participates in movement or collision.
    class MapBackground final
    {
    public:
        MapBackground(float inWorldWidth, float inBackgroundHeight);

        void Render(D2DRenderer& inRenderer, const Camera& inCamera) const;

    private:
        float worldWidth{};
        float backgroundHeight{};
    };
}
