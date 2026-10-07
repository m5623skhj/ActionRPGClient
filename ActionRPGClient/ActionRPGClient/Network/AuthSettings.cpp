#include "Network/AuthSettings.h"
#include "Resources/AssetCatalog.h"

#include <Windows.h>
#include <winhttp.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <fstream>
#include <set>
#include <stdexcept>
#include <utility>

namespace
{
    struct GoogleDesktopCredentials
    {
        std::string clientId;
        std::string secret;
    };

    std::string DesktopCredentialAscii(const std::array<wchar_t, 1025>& inValue, const DWORD inLength)
    {
        if (inLength == 0 || inLength >= inValue.size())
            throw std::runtime_error("Invalid Google Desktop credentials.");
        std::string result;
        result.reserve(inLength);
        for (DWORD index = 0; index < inLength; ++index)
        {
            if (inValue[index] < L'!' || inValue[index] > L'~')
                throw std::runtime_error("Invalid Google Desktop credentials.");
            result.push_back(static_cast<char>(inValue[index]));
        }
        return result;
    }

    // Read once at startup and remove both values before the OAuth browser can inherit them.
    GoogleDesktopCredentials ReadGoogleDesktopCredentials()
    {
        constexpr wchar_t ID_NAME[] = L"ACTIONRPG_GOOGLE_CLIENT_ID";
        constexpr wchar_t SECRET_NAME[] = L"ACTIONRPG_GOOGLE_DESKTOP_CLIENT_SECRET";
        std::array<wchar_t, 1025> idBuffer{};
        std::array<wchar_t, 1025> secretBuffer{};
        SetLastError(ERROR_SUCCESS);
        const DWORD idLength = GetEnvironmentVariableW(ID_NAME, idBuffer.data(), static_cast<DWORD>(idBuffer.size()));
        const DWORD idError = GetLastError();
        SetLastError(ERROR_SUCCESS);
        const DWORD secretLength = GetEnvironmentVariableW(SECRET_NAME, secretBuffer.data(), static_cast<DWORD>(secretBuffer.size()));
        const DWORD secretError = GetLastError();
        const bool idCleared = SetEnvironmentVariableW(ID_NAME, nullptr) != FALSE
            || GetLastError() == ERROR_ENVVAR_NOT_FOUND;
        const bool secretCleared = SetEnvironmentVariableW(SECRET_NAME, nullptr) != FALSE
            || GetLastError() == ERROR_ENVVAR_NOT_FOUND;
        if (!idCleared || !secretCleared)
            throw std::runtime_error("Google Desktop credential cleanup failed.");
        if (secretLength == 0 && secretError == ERROR_ENVVAR_NOT_FOUND) return {};
        if ((idLength == 0 && idError != ERROR_SUCCESS)
            || (secretLength == 0 && secretError != ERROR_SUCCESS))
            throw std::runtime_error("Google Desktop credentials unavailable.");
        return { DesktopCredentialAscii(idBuffer, idLength), DesktopCredentialAscii(secretBuffer, secretLength) };
    }
}

namespace ActionRPG
{
    std::wstring AuthUtf8ToWide(const std::string_view inText)
    {
        if (inText.empty()) return {};
        const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
            inText.data(), static_cast<int>(inText.size()), nullptr, 0);
        if (length <= 0) throw std::runtime_error("Invalid UTF-8.");
        std::wstring result(static_cast<std::size_t>(length), L'\0');
        if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, inText.data(),
            static_cast<int>(inText.size()), result.data(), length) != length)
            throw std::runtime_error("UTF-8 conversion failed.");
        return result;
    }

    std::string AuthWideToUtf8(const std::wstring_view inText)
    {
        if (inText.empty()) return {};
        const int length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
            inText.data(), static_cast<int>(inText.size()), nullptr, 0, nullptr, nullptr);
        if (length <= 0) throw std::runtime_error("Invalid Unicode.");
        std::string result(static_cast<std::size_t>(length), '\0');
        if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, inText.data(),
            static_cast<int>(inText.size()), result.data(), length, nullptr, nullptr) != length)
            throw std::runtime_error("Unicode conversion failed.");
        return result;
    }

    AuthSettings AuthSettings::Load(const AssetCatalog& inAssets)
    {
        AuthSettings settings;
        settings.playerName = L"Player-" + std::to_wstring(GetCurrentProcessId());
        try
        {
            auto desktopCredentials = ReadGoogleDesktopCredentials();
            const auto path = inAssets.GetAssetPath("Data/AuthClient.json");
            if (!std::filesystem::is_regular_file(path) || std::filesystem::file_size(path) > 32768)
                throw std::runtime_error("Authentication settings unavailable.");
            std::ifstream input(path, std::ios::binary);
            const auto document = nlohmann::json::parse(input);
            if (!document.is_object()) throw std::runtime_error("Invalid authentication settings.");
            settings.authUrl = document.value("authUrl", std::string{});
            settings.googleClientId = document.value("googleClientId", std::string{});
            if (!desktopCredentials.secret.empty())
            {
                if (desktopCredentials.clientId != settings.googleClientId)
                    throw std::runtime_error("Google Desktop credential ID mismatch.");
                settings.googleDesktopClientSecret = std::move(desktopCredentials.secret);
            }
            const auto configuredName = document.value("playerName", std::string{});
            if (!configuredName.empty()) settings.playerName = AuthUtf8ToWide(configuredName);
            const auto character = document.value("characterId", nlohmann::json(1));
            if (!character.is_number_integer() || character < 1 || character > 3
                || AuthWideToUtf8(settings.playerName).size() > 32)
                throw std::runtime_error("Invalid game profile settings.");
            settings.characterId = character.get<std::uint32_t>();

            const auto url = AuthUtf8ToWide(settings.authUrl);
            URL_COMPONENTS parts{};
            parts.dwStructSize = sizeof(parts);
            parts.dwHostNameLength = parts.dwUrlPathLength = parts.dwExtraInfoLength =
                parts.dwUserNameLength = parts.dwPasswordLength = static_cast<DWORD>(-1);
            const bool validOrigin = !url.empty() && url.size() <= 4096 && url.find(L'\0') == std::wstring::npos
                && WinHttpCrackUrl(url.c_str(), 0, 0, &parts)
                && parts.nScheme == INTERNET_SCHEME_HTTPS && parts.dwHostNameLength != 0
                && parts.dwUserNameLength == 0 && parts.dwPasswordLength == 0 && parts.dwExtraInfoLength == 0
                && (parts.dwUrlPathLength == 0 || (parts.dwUrlPathLength == 1 && *parts.lpszUrlPath == L'/'));
            const bool validClientId = settings.googleClientId.ends_with(".apps.googleusercontent.com")
                && settings.googleClientId.size() <= 256
                && std::all_of(settings.googleClientId.begin(), settings.googleClientId.end(), [](unsigned char value)
                { return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z')
                    || (value >= '0' && value <= '9') || value == '.' || value == '-' || value == '_'; });
            if (!validOrigin || !validClientId)
                settings.loginConfigurationError = L"Auth HTTPS 주소와 Google Desktop client ID 설정이 필요합니다.";
            while (!settings.authUrl.empty() && settings.authUrl.back() == '/') settings.authUrl.pop_back();

            const auto caFile = document.value("townCaFile", std::string{});
            if (!caFile.empty())
            {
                settings.townCaFile = std::filesystem::path(AuthUtf8ToWide(caFile));
                if (settings.townCaFile.is_relative()) settings.townCaFile = inAssets.GetAssetPath(caFile);
            }
            const auto servers = document.find("servers");
            if (servers == document.end() || !servers->is_array() || servers->size() > 128)
                throw std::runtime_error("Invalid town list.");
            std::set<std::string> serverIds;
            for (const auto& item : *servers)
            {
                TownServerSettings server;
                server.serverId = item.at("serverId").get<std::string>();
                server.label = AuthUtf8ToWide(item.at("name").get<std::string>());
                server.hostname = item.at("hostname").get<std::string>();
                const auto& port = item.at("port");
                if (!port.is_number_integer() || port < 1 || port > 65535)
                    throw std::runtime_error("Invalid town port.");
                const bool validHost = !server.hostname.empty() && server.hostname.size() <= 253
                    && std::all_of(server.hostname.begin(), server.hostname.end(), [](unsigned char value)
                    { return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z')
                        || (value >= '0' && value <= '9') || value == '.' || value == '-' || value == ':'; });
                if (server.serverId.empty() || server.serverId.size() > 128 || server.label.empty()
                    || server.label.size() > 80 || !validHost
                    || !serverIds.insert(server.serverId).second)
                    throw std::runtime_error("Invalid town entry.");
                server.port = port.get<std::uint16_t>();
                settings.servers.push_back(std::move(server));
            }
            if (settings.servers.empty() || settings.townCaFile.empty()
                || !std::filesystem::is_regular_file(settings.townCaFile))
                settings.townConfigurationError = L"실제 타운 목록과 신뢰 CA 파일 설정이 필요합니다.";
        }
        catch (...)
        {
            settings.servers.clear();
            settings.googleDesktopClientSecret.clear();
            settings.loginConfigurationError = L"Assets/Data/AuthClient.json과 런처의 Google Desktop 자격 증명 설정을 확인해 주세요.";
            settings.townConfigurationError = settings.loginConfigurationError;
        }
        return settings;
    }
}