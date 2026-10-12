#include "Platform/ShutdownTrace.h"
#include "App/Application.h"

#include <Windows.h>
#include <objbase.h>

#include <exception>
#include <cwchar>

namespace
{
    // Report native faults before Windows handles termination. Do not recover or modify game state.
    LONG WINAPI ReportNativeFailure(EXCEPTION_POINTERS* inException)
    {
        static LONG reporting{};
        if (inException && inException->ExceptionRecord && InterlockedCompareExchange(&reporting, 1, 0) == 0)
        {
            wchar_t message[320]{};
            swprintf_s(message, L"ActionRPGClient native exception 0x%08lX at %p.\n"
                L"Copy this message and the last ActionRPGClient screen-change lines from Visual Studio Output.",
                inException->ExceptionRecord->ExceptionCode, inException->ExceptionRecord->ExceptionAddress);
            OutputDebugStringW(message);
            MessageBoxW(nullptr, message, L"ActionRPGClient - Native error", MB_OK | MB_ICONERROR);
        }
        return EXCEPTION_CONTINUE_SEARCH;
    }
}

int WINAPI wWinMain(HINSTANCE inInstance, HINSTANCE, PWSTR, int)
{
    SetUnhandledExceptionFilter(ReportNativeFailure);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    const HRESULT comResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool shouldUninitializeCom = SUCCEEDED(comResult);
    if (FAILED(comResult) && comResult != RPC_E_CHANGED_MODE)
    {
        MessageBoxW(nullptr, L"COM initialization failed.", L"ActionRPGClient", MB_OK | MB_ICONERROR);
        return 1;
    }

    int exitCode = 0;
    try
    {
        ActionRPG::Application application(inInstance);
        exitCode = application.Run();
        ActionRPG::TraceShutdown("ActionRPGClient: releasing application resources\n");
    }
    catch (const std::exception& exception)
    {
        OutputDebugStringA(exception.what());
        MessageBoxA(nullptr, exception.what(), "ActionRPGClient", MB_OK | MB_ICONERROR);
        exitCode = 1;
    }

    ActionRPG::TraceShutdown("ActionRPGClient: application resources released\n");
    if (shouldUninitializeCom)
    {
        ActionRPG::TraceShutdown("ActionRPGClient: uninitializing main-thread COM\n");
        CoUninitialize();
    }

    ActionRPG::TraceShutdown("ActionRPGClient: returning from wWinMain\n");
    return exitCode;
}
