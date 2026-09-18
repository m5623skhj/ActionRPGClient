#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace ActionRPG
{
    class IniDocument final
    {
    public:
        explicit IniDocument(const std::filesystem::path& inFilePath);

        [[nodiscard]] const std::vector<std::string>& GetSectionNames() const { return sectionNames; }
        [[nodiscard]] const std::string& GetValue(std::string_view inSection, std::string_view inKey) const;
        [[nodiscard]] std::string GetOptionalValue(std::string_view inSection, std::string_view inKey,
            std::string_view inDefaultValue = {}) const;
        [[nodiscard]] bool HasValue(std::string_view inSection, std::string_view inKey) const;

    private:
        using KeyValues = std::unordered_map<std::string, std::string>;

        std::filesystem::path filePath;
        std::vector<std::string> sectionNames;
        std::unordered_map<std::string, KeyValues> sections;
    };
}
