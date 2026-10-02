#include <Windows.h>
#include <shellapi.h>

#include <filesystem>
#include <stdexcept>
#include <vector>

namespace
{
    /**
     * Locate bundled editor files relative to this executable, independently of cwd.
     */
    std::filesystem::path GetExecutableDirectory()
    {
        constexpr DWORD INITIAL_PATH_CAPACITY = 512;
        constexpr DWORD MAX_PATH_CAPACITY = 32768;
        std::vector<wchar_t> buffer(INITIAL_PATH_CAPACITY);
        while (buffer.size() <= MAX_PATH_CAPACITY)
        {
            const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
            if (length == 0)
            {
                throw std::runtime_error("Unable to locate CharacterEditor.exe.");
            }
            if (length < buffer.size())
            {
                return std::filesystem::path(buffer.data()).parent_path();
            }
            if (buffer.size() == MAX_PATH_CAPACITY)
            {
                break;
            }
            buffer.resize(buffer.size() * 2);
        }
        throw std::runtime_error("The executable path exceeds the supported length.");
    }
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, const int inShowCommand)
{
    try
    {
        const std::filesystem::path editorPath = GetExecutableDirectory() / L"CharacterEditor" / L"index.html";
        if (!std::filesystem::is_regular_file(editorPath))
        {
            throw std::runtime_error("CharacterEditor/index.html is missing. Build the CharacterEditor project or open its source index.html in a browser.");
        }
        const HINSTANCE result = ShellExecuteW(nullptr, L"open", editorPath.c_str(), nullptr, nullptr, inShowCommand);
        if (reinterpret_cast<INT_PTR>(result) <= 32)
        {
            throw std::runtime_error("Unable to open the editor. Associate .html files with a browser or open index.html in the browser manually.");
        }
        return 0;
    }
    catch (const std::exception& inException)
    {
        MessageBoxA(nullptr, inException.what(), "Character Editor", MB_OK | MB_ICONERROR);
        return 1;
    }
}
