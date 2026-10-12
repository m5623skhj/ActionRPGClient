#pragma once

#include <WinSock2.h>
#include <Windows.h>
#include <cwchar>
#include <string_view>

namespace ActionRPG
{
    // Fixed shutdown stages only; available without a debugger or console.
    // Each process uses one temp log, and diagnostics never block resource cleanup
    // on a worker or depend on the network logger's lifetime.
    inline void TraceShutdown(const char* inStage) noexcept
    {
        OutputDebugStringA(inStage);
        static const auto path = []
        {
            struct LogPath { wchar_t value[MAX_PATH]{}; } result;
            wchar_t directory[MAX_PATH]{};
            const DWORD length = GetTempPathW(MAX_PATH, directory);
            if (length == 0 || length + 64 >= MAX_PATH) return result;
            const int written = swprintf_s(result.value,
                L"%lsActionRPGClient-shutdown-%lu-%llu.log", directory,
                GetCurrentProcessId(), GetTickCount64());
            if (written < 0) result.value[0] = L'\0';
            return result;
        }();
        if (!path.value[0]) return;
        const HANDLE file = CreateFileW(path.value, FILE_APPEND_DATA,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
            FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) return;
        const std::string_view stage(inStage);
        DWORD written{};
        WriteFile(file, stage.data(), static_cast<DWORD>(stage.size()), &written, nullptr);
        CloseHandle(file);
    }
}
