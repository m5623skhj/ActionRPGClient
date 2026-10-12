#include "App/Application.h"

#include <d2d1_1helper.h>

#include <algorithm>
#include <chrono>
#include <string>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace ActionRPG
{
    Application::Application(const HINSTANCE inInstance)
        : window(inInstance, L"Action RPG Client", INITIAL_WIDTH, INITIAL_HEIGHT)
        , graphicsDevice(window.GetHandle(), window.GetClientWidth(), window.GetClientHeight())
        , renderer(graphicsDevice)
        , loginFlow(assetCatalog, townClient, dungeonClient)
    {
        renderer.SetUiFontFamily(L"Segoe UI");
        const auto settings = GameWindow::GetScreenSettings();
        renderer.SetViewport(window.GetClientWidth(), window.GetClientHeight(), settings.width, settings.height);
    }

    int Application::Run()
    {
        using Clock = std::chrono::steady_clock;

        auto previousTime = Clock::now();
        double accumulatedSeconds = 0.0;
        std::vector<InputKey> pendingPressedKeys;
        std::wstring pendingTextInput;
        int pendingMouseWheelDelta = 0;
        auto appliedSettings = GameWindow::GetScreenSettings();
        bool pendingLeftMousePressed = false;
        float pendingClickX = 0.0f;
        float pendingClickY = 0.0f;

        while (window.ProcessMessages())
        {
            audio.Update();
            std::uint32_t resizedWidth{};
            std::uint32_t resizedHeight{};
            const bool presentationChanged = window.ConsumePresentationChange();
            const bool physicalResize = window.ConsumeResize(resizedWidth, resizedHeight);
            const auto requestedSettings = GameWindow::GetScreenSettings();
            const bool resolutionChanged = requestedSettings.width != appliedSettings.width
                || requestedSettings.height != appliedSettings.height;
            const bool modeChanged = requestedSettings.fullscreen != appliedSettings.fullscreen;
            if (physicalResize || presentationChanged || resolutionChanged || modeChanged)
            {
                std::ostringstream transition;
                transition << "Screen change ("
                    << (resolutionChanged && modeChanged ? "resolution + mode" : resolutionChanged ? "resolution" : modeChanged ? "mode" : "physical viewport")
                    << "): logical " << appliedSettings.width << 'x' << appliedSettings.height
                    << (appliedSettings.fullscreen ? " fullscreen" : " windowed") << " -> "
                    << requestedSettings.width << 'x' << requestedSettings.height
                    << (requestedSettings.fullscreen ? " fullscreen" : " windowed")
                    << ", physical " << window.GetClientWidth() << 'x' << window.GetClientHeight();
                const auto context = transition.str();
                OutputDebugStringA(("ActionRPGClient: " + context + "\n").c_str());
                try
                {
                    // Borderless mode and logical resolution are independent. Only a real
                    // client-size change resizes buffers; only logical changes resize game UI.
                    if (physicalResize) graphicsDevice.Resize(resizedWidth, resizedHeight);
                    OutputDebugStringA("ActionRPGClient: updating logical render viewport\n");
                    renderer.SetViewport(window.GetClientWidth(), window.GetClientHeight(),
                        requestedSettings.width, requestedSettings.height);
                    if (game && resolutionChanged)
                    {
                        OutputDebugStringA("ActionRPGClient: updating logical game/UI bounds\n");
                        game->Resize(static_cast<float>(requestedSettings.width), static_cast<float>(requestedSettings.height));
                    }
                    appliedSettings = requestedSettings;
                    OutputDebugStringA("ActionRPGClient: screen change completed\n");
                }
                catch (const std::exception& error)
                {
                    const std::string message = context + "\n" + error.what();
                    OutputDebugStringA((message + "\n").c_str());
                    // A failed user selection must not terminate an otherwise usable game.
                    // Restore window, back buffer, and logical input/render coordinates together.
                    try
                    {
                        if (!GameWindow::RequestScreenSettings(appliedSettings))
                            throw std::runtime_error("Previous screen settings cannot be restored.");
                        if (!window.ProcessMessages()) return 0;
                        const auto restored = GameWindow::GetScreenSettings();
                        if (restored.width != appliedSettings.width || restored.height != appliedSettings.height
                            || restored.fullscreen != appliedSettings.fullscreen)
                            throw std::runtime_error("Previous window mode could not be restored.");
                        graphicsDevice.Resize(window.GetClientWidth(), window.GetClientHeight());
                        renderer.SetViewport(window.GetClientWidth(), window.GetClientHeight(), restored.width, restored.height);
                        if (game && resolutionChanged)
                            game->Resize(static_cast<float>(restored.width), static_cast<float>(restored.height));
                        (void)window.ConsumeResize(resizedWidth, resizedHeight);
                        (void)window.ConsumePresentationChange();
                    }
                    catch (const std::exception& recoveryError)
                    {
                        throw std::runtime_error(message + "\nScreen recovery failed: " + recoveryError.what());
                    }
                    MessageBoxA(window.GetHandle(), message.c_str(), "ActionRPGClient - Screen settings", MB_OK | MB_ICONERROR);
                    GameWindow::ResetInput();
                }
            }

            if (presentationChanged || resolutionChanged || modeChanged || window.IsMinimized())
            {
                pendingPressedKeys.clear();
                pendingTextInput.clear();
                pendingMouseWheelDelta = 0;
                pendingLeftMousePressed = false;
                accumulatedSeconds = 0;
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

            const auto settings = GameWindow::GetScreenSettings();
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
                loginFlow.Update(updateInput, static_cast<float>(settings.width),
                    static_cast<float>(settings.height));
                if (!loginFlow.IsPlaying()) game.reset();
                else
                {
                    const bool created = !game;
                    if (created)
                        game = std::make_unique<Game>(static_cast<float>(settings.width),
                            static_cast<float>(settings.height), assetCatalog, renderer, townClient, dungeonClient, audio);
                    // The Enter/click that completed admission must not become a gameplay command.
                    game->Update(static_cast<float>(FIXED_UPDATE_SECONDS), created ? InputState{} : updateInput);
                    const auto action = game->ConsumeSessionAction();
                    if (action != SessionMenuAction::None)
                    {
                        loginFlow.BeginLeave(action);
                        game.reset();
                    }
                }
                pendingPressedKeys.clear();
                pendingTextInput.clear();
                pendingMouseWheelDelta = 0;
                pendingLeftMousePressed = false;
                accumulatedSeconds -= FIXED_UPDATE_SECONDS;
            }

            if ((game && game->ConsumeExitRequested()) || loginFlow.ConsumeExitRequested())
            {
                OutputDebugStringA("ActionRPGClient: game/login requested normal exit\n");
                return 0;
            }

            renderer.BeginFrame(D2D1::ColorF(0.04f, 0.05f, 0.07f));
            if (game) game->Render(renderer);
            else loginFlow.Render(renderer, static_cast<float>(settings.width),
                static_cast<float>(settings.height));
            renderer.EndFrame();
        }

        return 0;
    }
}
