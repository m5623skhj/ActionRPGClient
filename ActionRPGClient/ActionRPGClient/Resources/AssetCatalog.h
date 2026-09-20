#pragma once

#include "Core/IniDocument.h"

#include <filesystem>
#include <string_view>

namespace ActionRPG
{
    class AssetCatalog final
    {
    public:
        AssetCatalog();

        [[nodiscard]] std::filesystem::path GetDataPath(std::string_view inAssetId) const;
        [[nodiscard]] std::filesystem::path GetImagePath(std::string_view inAssetId) const;
        [[nodiscard]] std::filesystem::path GetAudioPath(std::string_view inAssetId) const;
        [[nodiscard]] std::filesystem::path GetAssetPath(std::string_view inRelativePath) const;
        [[nodiscard]] bool HasImage(std::string_view inAssetId) const;
        [[nodiscard]] bool HasAudio(std::string_view inAssetId) const;

    private:
        [[nodiscard]] std::filesystem::path ResolvePath(std::string_view inSection,
            std::string_view inAssetId) const;
        [[nodiscard]] std::filesystem::path ResolveRelativePath(std::string_view inRelativePath) const;
        [[nodiscard]] static std::filesystem::path GetExecutableDirectory();

    private:
        std::filesystem::path assetRoot;
        IniDocument catalog;
    };
}
