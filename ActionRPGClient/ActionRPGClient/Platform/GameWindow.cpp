#include "Platform/GameWindow.h"

#include <stdexcept>
#include <string>
#include <utility>

namespace ActionRPG
{
    GameWindow::GameWindow(const HINSTANCE inInstance, const std::wstring_view inTitle,
        const std::uint32_t inClientWidth, const std::uint32_t inClientHeight)
        : instance(inInstance)
        , clientWidth(inClientWidth)
        , clientHeight(inClientHeight)
    {
        WNDCLASSEXW windowClass{};
        windowClass.cbSize = sizeof(windowClass);
        windowClass.style = CS_HREDRAW | CS_VREDRAW;
        windowClass.lpfnWndProc = WindowProcedure;
        windowClass.hInstance = instance;
        windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        windowClass.lpszClassName = WINDOW_CLASS_NAME;

        if (RegisterClassExW(&windowClass) == 0)
        {
            throw std::runtime_error("Failed to register the Win32 window class.");
        }

        RECT windowRectangle{ 0, 0, static_cast<LONG>(clientWidth), static_cast<LONG>(clientHeight) };
        if (AdjustWindowRectEx(&windowRectangle, WS_OVERLAPPEDWINDOW, FALSE, 0) == FALSE)
        {
            UnregisterClassW(WINDOW_CLASS_NAME, instance);
            throw std::runtime_error("Failed to calculate the Win32 window size.");
        }

        const std::wstring windowTitle(inTitle);
        windowHandle = CreateWindowExW(
            0,
            WINDOW_CLASS_NAME,
            windowTitle.c_str(),
            WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            windowRectangle.right - windowRectangle.left,
            windowRectangle.bottom - windowRectangle.top,
            nullptr,
            nullptr,
            instance,
            this);

        if (windowHandle == nullptr)
        {
            UnregisterClassW(WINDOW_CLASS_NAME, instance);
            throw std::runtime_error("Failed to create the Win32 window.");
        }

        ShowWindow(windowHandle, SW_SHOWDEFAULT);
        UpdateWindow(windowHandle);
    }

    GameWindow::~GameWindow()
    {
        if (windowHandle != nullptr)
        {
            DestroyWindow(windowHandle);
        }

        UnregisterClassW(WINDOW_CLASS_NAME, instance);
    }

    bool GameWindow::ProcessMessages()
    {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE) != FALSE)
        {
            if (message.message == WM_QUIT)
            {
                return false;
            }

            TranslateMessage(&message);
            DispatchMessageW(&message);
        }

        return true;
    }

    bool GameWindow::ConsumeResize(std::uint32_t& outWidth, std::uint32_t& outHeight)
    {
        if (!resizePending)
        {
            return false;
        }

        resizePending = false;
        outWidth = clientWidth;
        outHeight = clientHeight;
        return true;
    }

    InputState GameWindow::ConsumeInputState()
    {
        const InputState inputState{
            .moveLeft = IsKeyDown(VK_LEFT),
            .moveRight = IsKeyDown(VK_RIGHT),
            .moveUp = IsKeyDown(VK_UP),
            .moveDown = IsKeyDown(VK_DOWN),
            .pressedKeys = std::move(pendingPressedKeys)
        };

        pendingPressedKeys.clear();
        return inputState;
    }

    LRESULT CALLBACK GameWindow::WindowProcedure(const HWND inWindowHandle, const UINT inMessage,
        const WPARAM inWParam, const LPARAM inLParam)
    {
        GameWindow* window = reinterpret_cast<GameWindow*>(GetWindowLongPtrW(inWindowHandle, GWLP_USERDATA));

        if (inMessage == WM_NCCREATE)
        {
            const auto* createStructure = reinterpret_cast<const CREATESTRUCTW*>(inLParam);
            window = static_cast<GameWindow*>(createStructure->lpCreateParams);
            window->windowHandle = inWindowHandle;
            SetWindowLongPtrW(inWindowHandle, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(window));
        }

        if (window != nullptr)
        {
            return window->HandleMessage(inMessage, inWParam, inLParam);
        }

        return DefWindowProcW(inWindowHandle, inMessage, inWParam, inLParam);
    }

    LRESULT GameWindow::HandleMessage(const UINT inMessage, const WPARAM inWParam, const LPARAM inLParam)
    {
        switch (inMessage)
        {
        case WM_ERASEBKGND:
            return 1;

        case WM_SIZE:
            clientWidth = static_cast<std::uint32_t>(LOWORD(inLParam));
            clientHeight = static_cast<std::uint32_t>(HIWORD(inLParam));
            isMinimized = inWParam == SIZE_MINIMIZED || clientWidth == 0 || clientHeight == 0;
            if (!isMinimized)
            {
                resizePending = true;
            }
            return 0;

        case WM_KEYDOWN:
            if (inWParam < keyStates.size())
            {
                const bool wasKeyDown = keyStates[inWParam];
                keyStates[inWParam] = true;
                if (!wasKeyDown)
                {
                    RecordPressedKey(inWParam);
                }
            }
            return 0;

        case WM_KEYUP:
            if (inWParam < keyStates.size())
            {
                keyStates[inWParam] = false;
            }
            return 0;

        case WM_KILLFOCUS:
            keyStates.fill(false);
            pendingPressedKeys.clear();
            return 0;

        case WM_CLOSE:
            DestroyWindow(windowHandle);
            return 0;

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;

        case WM_NCDESTROY:
        {
            const HWND destroyedWindowHandle = windowHandle;
            SetWindowLongPtrW(destroyedWindowHandle, GWLP_USERDATA, 0);
            windowHandle = nullptr;
            return DefWindowProcW(destroyedWindowHandle, inMessage, inWParam, inLParam);
        }

        default:
            return DefWindowProcW(windowHandle, inMessage, inWParam, inLParam);
        }
    }

    void GameWindow::RecordPressedKey(const WPARAM inVirtualKey)
    {
        switch (inVirtualKey)
        {
        case VK_LEFT:
            pendingPressedKeys.push_back(InputKey::MoveLeft);
            break;
        case VK_RIGHT:
            pendingPressedKeys.push_back(InputKey::MoveRight);
            break;
        case VK_UP:
            pendingPressedKeys.push_back(InputKey::MoveUp);
            break;
        case VK_DOWN:
            pendingPressedKeys.push_back(InputKey::MoveDown);
            break;
        case 'Z':
            pendingPressedKeys.push_back(InputKey::ActionZ);
            break;
        case 'C':
            pendingPressedKeys.push_back(InputKey::ActionC);
            break;
        case 'X':
            pendingPressedKeys.push_back(InputKey::ActionX);
            break;
        case 'V':
            pendingPressedKeys.push_back(InputKey::ActionV);
            break;
        default:
            break;
        }
    }

    bool GameWindow::IsKeyDown(const unsigned int inVirtualKey) const
    {
        return inVirtualKey < keyStates.size() && keyStates[inVirtualKey];
    }
}
