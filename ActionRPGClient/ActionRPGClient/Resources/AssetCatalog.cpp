#include "Resources/AssetCatalog.h"

#include <Windows.h>

#include <stdexcept>
#include <string>
#include <vector>

namespace
{
    std::wstring Utf8ToWide(const std::string_view inText)
    {
        if (inText.empty())
        {
            return {};
        }

        const int requiredCharacters = MultiByteToWideChar(
            CP_UTF8,
            MB_ERR_INVALID_CHARS,
            inText.data(),
            static_cast<int>(inText.size()),
            nullptr,
            0);
        if (requiredCharacters <= 0)
        {
            throw std::runtime_error("Asset path is not valid UTF-8.");
        }

        std::wstring convertedPath(static_cast<std::size_t>(requiredCharacters), L'\0');
        if (MultiByteToWideChar(
            CP_UTF8,
            MB_ERR_INVALID_CHARS,
            inText.data(),
            static_cast<int>(inText.size()),
            convertedPath.data(),
            requiredCharacters) != requiredCharacters)
        {
            throw std::runtime_error("Failed to convert an asset path from UTF-8.");
        }

        return convertedPath;
    }
}

namespace ActionRPG
{
    AssetCatalog::AssetCatalog()
        : assetRoot(GetExecutableDirectory() / L"Assets")
        , catalog(assetRoot / L"Data" / L"assets.ini")
    {
    }

    std::filesystem::path AssetCatalog::GetDataPath(const std::string_view inAssetId) const
    {
        const std::filesystem::path path = ResolvePath("Data", inAssetId);
        if (!std::filesystem::is_regular_file(path))
        {
            throw std::runtime_error("Data asset does not exist: " + path.string());
        }
        return path;
    }

    std::filesystem::path AssetCatalog::GetImagePath(const std::string_view inAssetId) const
    {
        const std::filesystem::path path = ResolvePath("Images", inAssetId);
        if (!std::filesystem::is_regular_file(path))
        {
            throw std::runtime_error("Image asset does not exist: " + path.string());
        }
        return path;
    }

    std::filesystem::path AssetCatalog::GetAudioPath(const std::string_view inAssetId) const
    {
        return ResolvePath("Audio", inAssetId);
    }

    std::filesystem::path AssetCatalog::GetAssetPath(const std::string_view inRelativePath) const
    {
        const std::filesystem::path path = ResolveRelativePath(inRelativePath);
        if (!std::filesystem::is_regular_file(path))
        {
            throw std::runtime_error("Asset does not exist: " + path.string());
        }
        return path;
    }

    bool AssetCatalog::HasImage(const std::string_view inAssetId) const
    {
        return catalog.HasValue("Images", inAssetId);
    }

    bool AssetCatalog::HasAudio(const std::string_view inAssetId) const
    {
        return catalog.HasValue("Audio", inAssetId);
    }

    std::filesystem::path AssetCatalog::ResolvePath(const std::string_view inSection,
        const std::string_view inAssetId) const
    {
        return ResolveRelativePath(catalog.GetValue(inSection, inAssetId));
    }

    std::filesystem::path AssetCatalog::ResolveRelativePath(const std::string_view inRelativePath) const
    {
        const std::filesystem::path relativePath = Utf8ToWide(inRelativePath);
        if (relativePath.empty() || relativePath.is_absolute())
        {
            throw std::runtime_error("Asset path must be a non-empty relative path: " + relativePath.string());
        }

        for (const auto& component : relativePath)
        {
            if (component == L"..")
            {
                throw std::runtime_error("Asset path cannot leave the Assets directory: " + relativePath.string());
            }
        }

        return (assetRoot / relativePath).lexically_normal();
    }

    std::filesystem::path AssetCatalog::GetExecutableDirectory()
    {
        std::vector<wchar_t> pathBuffer(MAX_PATH);
        while (true)
        {
            const DWORD length = GetModuleFileNameW(nullptr, pathBuffer.data(), static_cast<DWORD>(pathBuffer.size()));
            if (length == 0)
            {
                throw std::runtime_error("GetModuleFileNameW failed.");
            }
            if (length < pathBuffer.size() - 1)
            {
                return std::filesystem::path(std::wstring(pathBuffer.data(), length)).parent_path();
            }

            pathBuffer.resize(pathBuffer.size() * 2);
        }
    }
}
