#include "Core/IniDocument.h"

#include <cctype>
#include <fstream>
#include <stdexcept>

namespace
{
    std::string Trim(std::string_view inText)
    {
        std::size_t first = 0;
        while (first < inText.size() && std::isspace(static_cast<unsigned char>(inText[first])) != 0)
        {
            ++first;
        }

        std::size_t last = inText.size();
        while (last > first && std::isspace(static_cast<unsigned char>(inText[last - 1])) != 0)
        {
            --last;
        }

        return std::string(inText.substr(first, last - first));
    }

    std::runtime_error MakeParseError(const std::filesystem::path& inPath, const std::size_t inLineNumber,
        const std::string_view inReason)
    {
        return std::runtime_error(
            inPath.string() + ":" + std::to_string(inLineNumber) + ": " + std::string(inReason));
    }
}

namespace ActionRPG
{
    IniDocument::IniDocument(const std::filesystem::path& inFilePath)
        : filePath(inFilePath)
    {
        std::ifstream input(filePath);
        if (!input)
        {
            throw std::runtime_error("Failed to open INI file: " + filePath.string());
        }

        std::string currentSection;
        std::string line;
        std::size_t lineNumber = 0;
        while (std::getline(input, line))
        {
            ++lineNumber;
            if (lineNumber == 1 && line.size() >= 3
                && static_cast<unsigned char>(line[0]) == 0xEF
                && static_cast<unsigned char>(line[1]) == 0xBB
                && static_cast<unsigned char>(line[2]) == 0xBF)
            {
                line.erase(0, 3);
            }

            const std::string trimmedLine = Trim(line);
            if (trimmedLine.empty() || trimmedLine.front() == ';' || trimmedLine.front() == '#')
            {
                continue;
            }

            if (trimmedLine.front() == '[')
            {
                if (trimmedLine.back() != ']')
                {
                    throw MakeParseError(filePath, lineNumber, "section header is missing ']'.");
                }

                currentSection = Trim(std::string_view(trimmedLine).substr(1, trimmedLine.size() - 2));
                if (currentSection.empty())
                {
                    throw MakeParseError(filePath, lineNumber, "section name is empty.");
                }
                if (sections.contains(currentSection))
                {
                    throw MakeParseError(filePath, lineNumber, "duplicate section: " + currentSection);
                }

                sections.emplace(currentSection, KeyValues{});
                sectionNames.push_back(currentSection);
                continue;
            }

            if (currentSection.empty())
            {
                throw MakeParseError(filePath, lineNumber, "key/value appears before a section.");
            }

            const std::size_t equalsPosition = trimmedLine.find('=');
            if (equalsPosition == std::string::npos)
            {
                throw MakeParseError(filePath, lineNumber, "key/value is missing '='.");
            }

            const std::string key = Trim(std::string_view(trimmedLine).substr(0, equalsPosition));
            const std::string value = Trim(std::string_view(trimmedLine).substr(equalsPosition + 1));
            if (key.empty())
            {
                throw MakeParseError(filePath, lineNumber, "key is empty.");
            }

            auto& keyValues = sections.at(currentSection);
            if (!keyValues.emplace(key, value).second)
            {
                throw MakeParseError(filePath, lineNumber, "duplicate key: " + key);
            }
        }
    }

    const std::string& IniDocument::GetValue(const std::string_view inSection, const std::string_view inKey) const
    {
        const auto sectionIterator = sections.find(std::string(inSection));
        if (sectionIterator == sections.end())
        {
            throw std::runtime_error("Missing INI section [" + std::string(inSection) + "] in " + filePath.string());
        }

        const auto valueIterator = sectionIterator->second.find(std::string(inKey));
        if (valueIterator == sectionIterator->second.end())
        {
            throw std::runtime_error(
                "Missing INI key '" + std::string(inKey) + "' in [" + std::string(inSection) + "] in "
                + filePath.string());
        }

        return valueIterator->second;
    }

    std::string IniDocument::GetOptionalValue(const std::string_view inSection, const std::string_view inKey,
        const std::string_view inDefaultValue) const
    {
        if (!HasValue(inSection, inKey))
        {
            return std::string(inDefaultValue);
        }

        return GetValue(inSection, inKey);
    }

    bool IniDocument::HasValue(const std::string_view inSection, const std::string_view inKey) const
    {
        const auto sectionIterator = sections.find(std::string(inSection));
        return sectionIterator != sections.end() && sectionIterator->second.contains(std::string(inKey));
    }
}
