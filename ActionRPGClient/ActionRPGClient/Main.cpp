#include "App/Application.h"

#include <Windows.h>
#include <objbase.h>

#include <exception>

int WINAPI wWinMain(HINSTANCE inInstance, HINSTANCE, PWSTR, int)
{
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
    }
    catch (const std::exception& exception)
    {
        MessageBoxA(nullptr, exception.what(), "ActionRPGClient", MB_OK | MB_ICONERROR);
        exitCode = 1;
    }

    if (shouldUninitializeCom)
    {
        CoUninitialize();
    }

    return exitCode;
}
