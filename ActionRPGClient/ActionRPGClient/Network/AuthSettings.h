#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace ActionRPG
{
    class AssetCatalog;

    struct TownServerSettings
    {
        std::string serverId;
        std::wstring label;
        std::string hostname;
        std::uint16_t port{};
    };

    struct AuthSettings
    {
        std::string authUrl;
        std::string googleClientId;
        std::string googleDesktopClientSecret; // Optional launcher credential; memory only.
        std::filesystem::path townCaFile;
        std::wstring playerName;
        std::uint32_t characterId{ 1 };
        std::vector<TownServerSettings> servers;
        std::wstring loginConfigurationError;
        std::wstring townConfigurationError;

        [[nodiscard]] static AuthSettings Load(const AssetCatalog& inAssets);
    };

    [[nodiscard]] std::wstring AuthUtf8ToWide(std::string_view inText);
    [[nodiscard]] std::string AuthWideToUtf8(std::wstring_view inText);
}