#include "Platform/GameWindow.h"

#include <Windowsx.h>

#include <algorithm>
#include <stdexcept>
#include <string>
#include <utility>

namespace
{
    constexpr DWORD WINDOWED_STYLE = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    constexpr DWORD FULLSCREEN_STYLE = WS_POPUP | WS_SYSMENU | WS_MINIMIZEBOX;

    bool GetWindowRectangle(const std::uint32_t inWidth, const std::uint32_t inHeight,
        const UINT inDpi, RECT& outRect)
    {
        outRect = {0, 0, static_cast<LONG>(inWidth), static_cast<LONG>(inHeight)};
        return AdjustWindowRectExForDpi(&outRect, WINDOWED_STYLE, FALSE, 0, inDpi) != FALSE;
    }
}

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
        if (AdjustWindowRectExForDpi(&windowRectangle, WINDOWED_STYLE, FALSE, 0, GetDpiForSystem()) == FALSE)
        {
            UnregisterClassW(WINDOW_CLASS_NAME, instance);
            throw std::runtime_error("Failed to calculate the Win32 window size.");
        }

        const std::wstring windowTitle(inTitle);
        windowHandle = CreateWindowExW(
            0,
            WINDOW_CLASS_NAME,
            windowTitle.c_str(),
            WINDOWED_STYLE,
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

        activeWindow = this;
        screenSettings = {inClientWidth, inClientHeight, false};
        if (!CanUseWindowed(inClientWidth, inClientHeight)) screenSettings.fullscreen = true;
        if (!ApplyPresentation(MonitorFromWindow(windowHandle, MONITOR_DEFAULTTONEAREST)))
        {
            DestroyWindow(windowHandle);
            UnregisterClassW(WINDOW_CLASS_NAME, instance);
            throw std::runtime_error("Failed to initialize the fixed game viewport.");
        }
        ShowWindow(windowHandle, SW_SHOWDEFAULT);
        UpdateWindow(windowHandle);
    }

    GameWindow::~GameWindow()
    {
        if (activeWindow == this) activeWindow = nullptr;
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

        if (presentationPending && windowHandle != nullptr && !isMinimized)
        {
            presentationPending = false;
            const auto previousSettings = screenSettings;
            screenSettings = requestedSettings;
            if (!ApplyPresentation(requestedMonitor))
            {
                screenSettings = previousSettings;
                presentationPending = false;
            }
            requestedMonitor = nullptr;
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

    ScreenSettings GameWindow::GetScreenSettings()
    {
        return activeWindow ? activeWindow->screenSettings : ScreenSettings{};
    }

    bool GameWindow::CanUseWindowed(const std::uint32_t inWidth, const std::uint32_t inHeight)
    {
        if (!activeWindow || !activeWindow->windowHandle) return false;
        MONITORINFO monitor{sizeof(MONITORINFO)};
        const auto& window = *activeWindow;
        RECT rect{};
        return GetMonitorInfoW(MonitorFromWindow(window.windowHandle, MONITOR_DEFAULTTONEAREST), &monitor)
            && GetWindowRectangle(inWidth, inHeight, GetDpiForWindow(window.windowHandle), rect)
            && rect.right - rect.left <= monitor.rcWork.right - monitor.rcWork.left
            && rect.bottom - rect.top <= monitor.rcWork.bottom - monitor.rcWork.top;
    }

    bool GameWindow::RequestScreenSettings(const ScreenSettings& inSettings)
    {
        if (!activeWindow || !((inSettings.width == 1280 && inSettings.height == 720)
            || (inSettings.width == 1920 && inSettings.height == 1080))) return false;
        if (!inSettings.fullscreen && !CanUseWindowed(inSettings.width, inSettings.height)) return false;
        activeWindow->requestedSettings = inSettings;
        activeWindow->requestedMonitor = MonitorFromWindow(activeWindow->windowHandle, MONITOR_DEFAULTTONEAREST);
        activeWindow->presentationPending = true;
        return true;
    }

    bool GameWindow::IsScreenSettingsPending()
    {
        return activeWindow && activeWindow->presentationPending;
    }

    bool GameWindow::ConsumePresentationChange()
    {
        return std::exchange(presentationChanged, false);
    }

    void GameWindow::ResetInput()
    {
        if (activeWindow) activeWindow->ClearInput();
    }

    void GameWindow::ClearInput()
    {
        keyStates.fill(false);
        pendingPressedKeys.clear();
        pendingTextInput.clear();
        pendingMouseWheelDelta = 0;
        pendingLeftMousePressed = pendingLeftMouseReleased = pendingRightMousePressed = leftMouseDown = false;
        pendingCancelDrag = true;
        mouseX = mouseY = pendingClickX = pendingClickY = pendingRightClickX = pendingRightClickY = -1.0f;
        if (GetCapture() == windowHandle) ReleaseCapture();
    }

    // Rendering and pointer picking share the same centered, aspect-preserving viewport.
    void GameWindow::MapPointer(float& inX, float& inY) const
    {
        if (clientWidth == 0 || clientHeight == 0 || isMinimized) { inX = inY = -1.0f; return; }
        const float scale = std::min(static_cast<float>(clientWidth) / screenSettings.width,
            static_cast<float>(clientHeight) / screenSettings.height);
        const float left = (clientWidth - screenSettings.width * scale) * 0.5f;
        const float top = (clientHeight - screenSettings.height * scale) * 0.5f;
        inX = (inX - left) / scale;
        inY = (inY - top) / scale;
        if (inX < 0 || inY < 0 || inX >= screenSettings.width || inY >= screenSettings.height)
            inX = inY = -1.0f;
    }

    // Called on the window/game thread between updates; no network thread touches presentation state.
    bool GameWindow::ApplyPresentation(HMONITOR inMonitor)
    {
        if (!inMonitor) inMonitor = MonitorFromWindow(windowHandle, MONITOR_DEFAULTTONEAREST);
        MONITORINFO monitor{sizeof(MONITORINFO)};
        if (!GetMonitorInfoW(inMonitor, &monitor)) return false;
        RECT rect{};
        if (!GetWindowRectangle(screenSettings.width, screenSettings.height, GetDpiForWindow(windowHandle), rect))
            return false;
        // Display/DPI changes may invalidate a previously valid windowed selection. Preserve its
        // logical resolution by switching to borderless rather than clipping or shrinking it.
        if (rect.right - rect.left > monitor.rcWork.right - monitor.rcWork.left
            || rect.bottom - rect.top > monitor.rcWork.bottom - monitor.rcWork.top)
            screenSettings.fullscreen = true;
        const LONG width = screenSettings.fullscreen ? monitor.rcMonitor.right - monitor.rcMonitor.left : rect.right - rect.left;
        const LONG height = screenSettings.fullscreen ? monitor.rcMonitor.bottom - monitor.rcMonitor.top : rect.bottom - rect.top;
        const LONG left = screenSettings.fullscreen ? monitor.rcMonitor.left : monitor.rcWork.left
            + (monitor.rcWork.right - monitor.rcWork.left - width) / 2;
        const LONG top = screenSettings.fullscreen ? monitor.rcMonitor.top : monitor.rcWork.top
            + (monitor.rcWork.bottom - monitor.rcWork.top - height) / 2;
        const LONG_PTR oldStyle = GetWindowLongPtrW(windowHandle, GWL_STYLE);
        RECT oldRect{};
        if (!GetWindowRect(windowHandle, &oldRect)) return false;
        applyingPresentation = true;
        SetLastError(ERROR_SUCCESS);
        if (SetWindowLongPtrW(windowHandle, GWL_STYLE, screenSettings.fullscreen ? FULLSCREEN_STYLE : WINDOWED_STYLE) == 0
            && GetLastError() != ERROR_SUCCESS)
        {
            applyingPresentation = false;
            return false;
        }
        const bool applied = SetWindowPos(windowHandle, nullptr, left, top, width, height,
            SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED) != FALSE;
        if (!applied)
        {
            SetWindowLongPtrW(windowHandle, GWL_STYLE, oldStyle);
            SetWindowPos(windowHandle, nullptr, oldRect.left, oldRect.top,
                oldRect.right - oldRect.left, oldRect.bottom - oldRect.top,
                SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        }
        applyingPresentation = false;
        if (applied)
        {
            ClearInput();
            resizePending = presentationChanged = true;
        }
        return applied;
    }

    // Window dispatch and GameWorld run on the same thread. Consume once per game tick.
    void GameWindow::ConsumeUiPointer(InputState& input)
    {
        if (!activeWindow) { input.cancelDrag=true; return; }
        auto& window=*activeWindow;
        input.mouseX=window.mouseX; input.mouseY=window.mouseY;
        input.leftMouseDown=window.leftMouseDown; input.leftMouseReleased=window.pendingLeftMouseReleased;
        input.rightMousePressed=window.pendingRightMousePressed; input.cancelDrag=window.pendingCancelDrag;
        input.rightClickX=window.pendingRightClickX; input.rightClickY=window.pendingRightClickY;
        window.MapPointer(input.mouseX, input.mouseY);
        window.MapPointer(input.rightClickX, input.rightClickY);
        input.shiftHeld=window.IsKeyDown(VK_SHIFT);
        window.pendingLeftMouseReleased=window.pendingRightMousePressed=window.pendingCancelDrag=false;
    }

    InputState GameWindow::ConsumeInputState()
    {
        InputState inputState{
            .moveLeft = IsKeyDown(VK_LEFT),
            .moveRight = IsKeyDown(VK_RIGHT),
            .moveUp = IsKeyDown(VK_UP),
            .moveDown = IsKeyDown(VK_DOWN),
            .mouseX = mouseX,
            .mouseY = mouseY,
            .clickX = pendingClickX,
            .clickY = pendingClickY,
            .mouseWheelDelta = pendingMouseWheelDelta,
            .leftMousePressed = pendingLeftMousePressed,
            .leftMouseDown = leftMouseDown, .leftMouseReleased = pendingLeftMouseReleased,
            .rightMousePressed = pendingRightMousePressed, .cancelDrag = pendingCancelDrag,
            .shiftHeld = IsKeyDown(VK_SHIFT), .rightClickX = pendingRightClickX, .rightClickY = pendingRightClickY,
            .textInput = std::move(pendingTextInput),
            .pressedKeys = std::move(pendingPressedKeys)
        };

        MapPointer(inputState.mouseX, inputState.mouseY);
        MapPointer(inputState.clickX, inputState.clickY);
        MapPointer(inputState.rightClickX, inputState.rightClickY);
        pendingPressedKeys.clear();
        pendingMouseWheelDelta = 0;
        pendingLeftMousePressed = false;
        pendingTextInput.clear();
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

        case WM_SYSCOMMAND:
            if ((inWParam & 0xFFF0) == SC_SIZE || (inWParam & 0xFFF0) == SC_MAXIMIZE
                || (screenSettings.fullscreen && (inWParam & 0xFFF0) == SC_MOVE)) return 0;
            return DefWindowProcW(windowHandle, inMessage, inWParam, inLParam);

        case WM_NCLBUTTONDBLCLK:
            if (inWParam == HTCAPTION) return 0;
            return DefWindowProcW(windowHandle, inMessage, inWParam, inLParam);

        case WM_GETMINMAXINFO:
        {
            RECT rect{};
            if (!applyingPresentation && !screenSettings.fullscreen && GetWindowRectangle(screenSettings.width, screenSettings.height,
                GetDpiForWindow(windowHandle), rect))
            {
                auto* info = reinterpret_cast<MINMAXINFO*>(inLParam);
                info->ptMinTrackSize = info->ptMaxTrackSize = {rect.right - rect.left, rect.bottom - rect.top};
                return 0;
            }
            return DefWindowProcW(windowHandle, inMessage, inWParam, inLParam);
        }

        case WM_WINDOWPOSCHANGING:
        {
            auto* position = reinterpret_cast<WINDOWPOS*>(inLParam);
            if (!applyingPresentation && !IsIconic(windowHandle) && !screenSettings.fullscreen)
            {
                RECT rect{};
                if (GetWindowRectangle(screenSettings.width, screenSettings.height, GetDpiForWindow(windowHandle), rect))
                {
                    if (!(position->flags & SWP_NOSIZE))
                    {
                        position->cx = rect.right - rect.left;
                        position->cy = rect.bottom - rect.top;
                    }
                    if (!(position->flags & SWP_NOMOVE))
                    {
                        RECT proposed{position->x, position->y, position->x + rect.right - rect.left,
                            position->y + rect.bottom - rect.top};
                        MONITORINFO monitor{sizeof(MONITORINFO)};
                        const auto targetMonitor = MonitorFromRect(&proposed, MONITOR_DEFAULTTONEAREST);
                        if (GetMonitorInfoW(targetMonitor, &monitor))
                        {
                            const bool fits = rect.right - rect.left <= monitor.rcWork.right - monitor.rcWork.left
                                && rect.bottom - rect.top <= monitor.rcWork.bottom - monitor.rcWork.top;
                            if (!fits || targetMonitor != MonitorFromWindow(windowHandle, MONITOR_DEFAULTTONEAREST))
                            {
                                requestedSettings = screenSettings;
                                requestedMonitor = targetMonitor;
                                presentationPending = true;
                                ClearInput();
                            }
                            position->x = std::clamp<LONG>(position->x, monitor.rcWork.left,
                                std::max(monitor.rcWork.left, monitor.rcWork.right - (rect.right - rect.left)));
                            position->y = std::clamp<LONG>(position->y, monitor.rcWork.top,
                                std::max(monitor.rcWork.top, monitor.rcWork.bottom - (rect.bottom - rect.top)));
                        }
                    }
                }
            }
            else if (!applyingPresentation && !IsIconic(windowHandle) && screenSettings.fullscreen)
            {
                RECT proposed{};
                GetWindowRect(windowHandle, &proposed);
                if (!(position->flags & SWP_NOMOVE))
                {
                    OffsetRect(&proposed, position->x - proposed.left, position->y - proposed.top);
                }
                requestedMonitor = MonitorFromRect(&proposed, MONITOR_DEFAULTTONEAREST);
                requestedSettings = screenSettings;
                presentationPending = true;
                ClearInput();
            }
            return 0;
        }

        case WM_DPICHANGED:
            requestedMonitor = MonitorFromRect(reinterpret_cast<const RECT*>(inLParam), MONITOR_DEFAULTTONEAREST);
            if (!presentationPending) requestedSettings = screenSettings;
            presentationPending = true;
            ClearInput();
            return 0;

        case WM_SETTINGCHANGE:
            if (inWParam != SPI_SETWORKAREA)
                return DefWindowProcW(windowHandle, inMessage, inWParam, inLParam);
            [[fallthrough]];
        case WM_DISPLAYCHANGE:
            requestedMonitor = MonitorFromWindow(windowHandle, MONITOR_DEFAULTTONEAREST);
            if (!presentationPending) requestedSettings = screenSettings;
            presentationPending = true;
            ClearInput();
            return 0;

        case WM_SIZE:
            isMinimized = inWParam == SIZE_MINIMIZED || LOWORD(inLParam) == 0 || HIWORD(inLParam) == 0;
            if (!isMinimized)
            {
                clientWidth = static_cast<std::uint32_t>(LOWORD(inLParam));
                clientHeight = static_cast<std::uint32_t>(HIWORD(inLParam));
                resizePending = true;
            }
            else ClearInput();
            return 0;

        case WM_KEYDOWN:
            if (inWParam < keyStates.size())
            {
                const bool wasKeyDown = keyStates[inWParam];
                keyStates[inWParam] = true;
                if (!wasKeyDown && (inLParam & (static_cast<LPARAM>(1) << 30)) == 0)
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

        case WM_MOUSEMOVE:
        {
            mouseX = static_cast<float>(GET_X_LPARAM(inLParam));
            mouseY = static_cast<float>(GET_Y_LPARAM(inLParam));
            TRACKMOUSEEVENT mouseTracking{sizeof(TRACKMOUSEEVENT), TME_LEAVE, windowHandle, 0};
            TrackMouseEvent(&mouseTracking);
            return 0;
        }

        case WM_MOUSELEAVE:
            // Do not keep hover information at the last in-window position.
            mouseX = mouseY = -1.0f;
            return 0;

        case WM_LBUTTONDOWN:
            mouseX = static_cast<float>(GET_X_LPARAM(inLParam));
            mouseY = static_cast<float>(GET_Y_LPARAM(inLParam));
            if (!pendingLeftMousePressed)
            {
                pendingClickX = mouseX;
                pendingClickY = mouseY;
            }
            pendingLeftMousePressed = true;
            leftMouseDown = true;
            SetCapture(windowHandle);
            return 0;

        case WM_LBUTTONUP:
            mouseX = static_cast<float>(GET_X_LPARAM(inLParam));
            mouseY = static_cast<float>(GET_Y_LPARAM(inLParam));
            leftMouseDown = false; pendingLeftMouseReleased = true;
            if (GetCapture() == windowHandle) ReleaseCapture();
            return 0;

        case WM_RBUTTONDOWN:
            mouseX = pendingRightClickX = static_cast<float>(GET_X_LPARAM(inLParam));
            mouseY = pendingRightClickY = static_cast<float>(GET_Y_LPARAM(inLParam));
            pendingRightMousePressed = true;
            return 0;

        case WM_CAPTURECHANGED:
            if (leftMouseDown) { leftMouseDown = false; pendingCancelDrag = true; pendingLeftMousePressed = false; }
            return 0;

        case WM_MOUSEWHEEL:
            pendingMouseWheelDelta += GET_WHEEL_DELTA_WPARAM(inWParam);
            return 0;

        case WM_CHAR:
            if (inWParam <= 0xFFFF)
            {
                pendingTextInput.push_back(static_cast<wchar_t>(inWParam));
            }
            return 0;

        case WM_KILLFOCUS:
            ClearInput();
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
            if (activeWindow == this) activeWindow = nullptr;
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
        case 'A': pendingPressedKeys.push_back(InputKey::SkillA); break;
        case 'S': pendingPressedKeys.push_back(InputKey::SkillS); break;
        case 'D': pendingPressedKeys.push_back(InputKey::SkillD); break;
        case 'F': pendingPressedKeys.push_back(InputKey::SkillF); break;
        case 'G': pendingPressedKeys.push_back(InputKey::SkillG); break;
        case 'H': pendingPressedKeys.push_back(InputKey::SkillH); break;
        case '1': pendingPressedKeys.push_back(InputKey::Item1); break;
        case '2': pendingPressedKeys.push_back(InputKey::Item2); break;
        case '3': pendingPressedKeys.push_back(InputKey::Item3); break;
        case '4': pendingPressedKeys.push_back(InputKey::Item4); break;
        case VK_RETURN:
            pendingPressedKeys.push_back(InputKey::ConfirmSelection);
            break;
        case VK_ESCAPE:
            pendingPressedKeys.push_back(InputKey::ToggleSystemMenu);
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
