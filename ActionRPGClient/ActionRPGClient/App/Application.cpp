#include "App/Application.h"

#include <d2d1_1helper.h>

#include <algorithm>
#include <chrono>
#include <string>
#include <vector>

namespace ActionRPG
{
    Application::Application(const HINSTANCE inInstance)
        : window(inInstance, L"Action RPG Client", INITIAL_WIDTH, INITIAL_HEIGHT)
        , graphicsDevice(window.GetHandle(), window.GetClientWidth(), window.GetClientHeight())
        , renderer(graphicsDevice)
        , game(static_cast<float>(window.GetClientWidth()), static_cast<float>(window.GetClientHeight()),
            assetCatalog, renderer, townClient, dungeonClient)
    {
        townClient.Start("127.0.0.1", 7777,
            "Player-" + std::to_string(GetCurrentProcessId()), 1);
    }

    int Application::Run()
    {
        using Clock = std::chrono::steady_clock;

        auto previousTime = Clock::now();
        double accumulatedSeconds = 0.0;
        std::vector<InputKey> pendingPressedKeys;
        std::wstring pendingTextInput;
        int pendingMouseWheelDelta = 0;
        bool pendingLeftMousePressed = false;
        float pendingClickX = 0.0f;
        float pendingClickY = 0.0f;

        while (window.ProcessMessages())
        {
            std::uint32_t resizedWidth{};
            std::uint32_t resizedHeight{};
            if (window.ConsumeResize(resizedWidth, resizedHeight))
            {
                graphicsDevice.Resize(resizedWidth, resizedHeight);
                game.Resize(static_cast<float>(resizedWidth), static_cast<float>(resizedHeight));
            }

            if (window.IsMinimized())
            {
                WaitMessage();
                previousTime = Clock::now();
                continue;
            }

            const auto currentTime = Clock::now();
            const std::chrono::duration<double> elapsedTime = currentTime - previousTime;
            previousTime = currentTime;

            // A breakpoint or window drag must not create an excessively long catch-up update.
            accumulatedSeconds += std::min(elapsedTime.count(), 0.25);

            const InputState inputState = window.ConsumeInputState();
            pendingPressedKeys.insert(
                pendingPressedKeys.end(),
                inputState.pressedKeys.begin(),
                inputState.pressedKeys.end());
            pendingTextInput += inputState.textInput;
            pendingMouseWheelDelta += inputState.mouseWheelDelta;
            if (inputState.leftMousePressed && !pendingLeftMousePressed)
            {
                pendingClickX = inputState.clickX;
                pendingClickY = inputState.clickY;
            }
            pendingLeftMousePressed = pendingLeftMousePressed || inputState.leftMousePressed;
            while (accumulatedSeconds >= FIXED_UPDATE_SECONDS)
            {
                InputState updateInput = inputState;
                updateInput.pressedKeys = std::move(pendingPressedKeys);
                updateInput.textInput = std::move(pendingTextInput);
                updateInput.mouseWheelDelta = pendingMouseWheelDelta;
                updateInput.leftMousePressed = pendingLeftMousePressed;
                updateInput.clickX = pendingClickX;
                updateInput.clickY = pendingClickY;
                game.Update(static_cast<float>(FIXED_UPDATE_SECONDS), updateInput);
                pendingPressedKeys.clear();
                pendingTextInput.clear();
                pendingMouseWheelDelta = 0;
                pendingLeftMousePressed = false;
                accumulatedSeconds -= FIXED_UPDATE_SECONDS;
            }

            if (game.ConsumeExitRequested())
            {
                return 0;
            }

            renderer.BeginFrame(D2D1::ColorF(0.04f, 0.05f, 0.07f));
            game.Render(renderer);
            renderer.EndFrame();
        }

        return 0;
    }
}
