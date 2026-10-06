#pragma once

#include "Game/Game.h"
#include "App/LoginFlow.h"
#include <memory>
#include "Graphics/D2DRenderer.h"
#include "Graphics/GraphicsDevice.h"
#include "Network/DungeonClient.h"
#include "Network/TownClient.h"
#include "Platform/GameWindow.h"
#include "Resources/AssetCatalog.h"

#include <Windows.h>

namespace ActionRPG
{
    class Application final
    {
    public:
        explicit Application(HINSTANCE inInstance);

        Application(const Application&) = delete;
        Application& operator=(const Application&) = delete;

        int Run();

    private:
        static constexpr std::uint32_t INITIAL_WIDTH = 1280;
        static constexpr std::uint32_t INITIAL_HEIGHT = 720;
        static constexpr double FIXED_UPDATE_SECONDS = 1.0 / 60.0;

        GameWindow window;
        GraphicsDevice graphicsDevice;
        D2DRenderer renderer;
        AssetCatalog assetCatalog;
        TownClient townClient;
        DungeonClient dungeonClient;
        LoginFlow loginFlow;
        std::unique_ptr<Game> game;
    };
}
