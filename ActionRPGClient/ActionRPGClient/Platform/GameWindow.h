#pragma once

#include "Input/InputState.h"

#include <Windows.h>

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

namespace ActionRPG
{
    class GameWindow final
    {
    public:
        GameWindow(HINSTANCE inInstance, std::wstring_view inTitle, std::uint32_t inClientWidth,
            std::uint32_t inClientHeight);
        ~GameWindow();

        GameWindow(const GameWindow&) = delete;
        GameWindow& operator=(const GameWindow&) = delete;

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
        [[nodiscard]] bool IsKeyDown(unsigned int inVirtualKey) const;

    private:
        static constexpr wchar_t WINDOW_CLASS_NAME[] = L"ActionRPGClientWindow";

        HINSTANCE instance{};
        HWND windowHandle{};
        std::uint32_t clientWidth{};
        std::uint32_t clientHeight{};
        bool resizePending{};
        bool isMinimized{};
        std::array<bool, 256> keyStates{};
        std::vector<InputKey> pendingPressedKeys;
    };
}
