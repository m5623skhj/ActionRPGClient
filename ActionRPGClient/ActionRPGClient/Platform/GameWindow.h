#pragma once

#include "Input/InputState.h"

#include <Windows.h>

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

namespace ActionRPG
{
    struct ScreenSettings
    {
        std::uint32_t width = 1280;
        std::uint32_t height = 720;
        bool fullscreen{};
    };

    class GameWindow final
    {
    public:
        GameWindow(HINSTANCE inInstance, std::wstring_view inTitle, std::uint32_t inClientWidth,
            std::uint32_t inClientHeight);
        ~GameWindow();

        GameWindow(const GameWindow&) = delete;
        GameWindow& operator=(const GameWindow&) = delete;

        [[nodiscard]] static ScreenSettings GetScreenSettings();
        [[nodiscard]] static bool CanUseWindowed(std::uint32_t inWidth, std::uint32_t inHeight);
        [[nodiscard]] static bool RequestScreenSettings(const ScreenSettings& inSettings);
        [[nodiscard]] bool ConsumePresentationChange();
        [[nodiscard]] static bool IsScreenSettingsPending();
        static void ConsumeUiPointer(InputState& inInput);
        static void ResetInput();
        static void SetChatInputMode(bool inEnabled, float inX = 0, float inY = 0);
        [[nodiscard]] bool ProcessMessages();
        [[nodiscard]] bool ConsumeResize(std::uint32_t& outWidth, std::uint32_t& outHeight);
        [[nodiscard]] InputState ConsumeInputState();
        [[nodiscard]] HWND GetHandle() const { return windowHandle; }
        [[nodiscard]] std::uint32_t GetClientWidth() const { return clientWidth; }
        [[nodiscard]] std::uint32_t GetClientHeight() const { return clientHeight; }
        [[nodiscard]] bool IsMinimized() const { return isMinimized; }

    private:
        static LRESULT CALLBACK WindowProcedure(HWND inWindowHandle, UINT inMessage, WPARAM inWParam,
            LPARAM inLParam);
        LRESULT HandleMessage(UINT inMessage, WPARAM inWParam, LPARAM inLParam);
        void RecordPressedKey(WPARAM inVirtualKey);
        void ClearInput();
        void MapPointer(float& inX, float& inY) const;
        [[nodiscard]] bool ApplyPresentation(HMONITOR inMonitor);
        [[nodiscard]] bool IsKeyDown(unsigned int inVirtualKey) const;

    private:
        static constexpr wchar_t WINDOW_CLASS_NAME[] = L"ActionRPGClientWindow";

        inline static GameWindow* activeWindow{};
        HINSTANCE instance{};
        HWND windowHandle{};
        std::uint32_t clientWidth{};
        std::uint32_t clientHeight{};
        bool resizePending{};
        bool closeRequested{};
        bool isMinimized{};
        float mouseX{};
        float mouseY{};
        float pendingClickX{};
        float pendingClickY{};
        int pendingMouseWheelDelta{};
        bool pendingLeftMousePressed{};
        bool leftMouseDown{}, pendingLeftMouseReleased{}, pendingRightMousePressed{}, pendingCancelDrag{};
        float pendingRightClickX{}, pendingRightClickY{};
        ScreenSettings screenSettings;
        ScreenSettings requestedSettings;
        HMONITOR requestedMonitor{};
        bool presentationPending{}, presentationChanged{}, applyingPresentation{};
        std::wstring pendingTextInput;
        std::vector<TextEditEvent> pendingTextEdits;
        std::wstring composition;
        bool chatInputMode{}, imeComposing{}, imeCandidateOpen{}, chatSubmitRequested{};
        std::array<bool, 256> keyStates{};
        std::vector<InputKey> pendingPressedKeys;
    };
}
