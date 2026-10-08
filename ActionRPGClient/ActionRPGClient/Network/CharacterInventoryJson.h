#pragma once

#include <Windows.h>
#include <bcrypt.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <stdexcept>
#include <string>

namespace ActionRPG::CharacterInventoryJson
{
    using Json = nlohmann::json;

    // Persistent IDs/revisions are decimal strings on the wire; never pass through floating point.
    [[nodiscard]] inline std::uint64_t UInt64(const Json& inValue)
    {
        if (!inValue.is_string()) throw std::runtime_error("Expected decimal identifier.");
        const auto& text = inValue.get_ref<const std::string&>();
        if (text.empty() || text.size() > 20 || (text.size() > 1 && text.front() == '0')
            || !std::all_of(text.begin(), text.end(), [](char value) { return value >= '0' && value <= '9'; }))
            throw std::runtime_error("Invalid decimal identifier.");
        std::uint64_t result{};
        const auto parsed = std::from_chars(text.data(), text.data() + text.size(), result);
        if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size())
            throw std::runtime_error("Decimal identifier out of range.");
        return result;
    }

    [[nodiscard]] inline std::uint32_t UInt32(const Json& inValue, std::uint32_t inMinimum, std::uint32_t inMaximum)
    {
        if (!inValue.is_number_integer() || inValue < inMinimum || inValue > inMaximum)
            throw std::runtime_error("Integer outside inventory contract.");
        return inValue.get<std::uint32_t>();
    }

    [[nodiscard]] inline std::string HexId(const Json& inValue, std::size_t inLength)
    {
        if (!inValue.is_string()) throw std::runtime_error("Expected opaque identifier.");
        const auto result = inValue.get<std::string>();
        if (result.size() != inLength || !std::all_of(result.begin(), result.end(), [](char value)
            { return (value >= '0' && value <= '9') || (value >= 'a' && value <= 'f'); }))
            throw std::runtime_error("Invalid opaque identifier.");
        return result;
    }

    [[nodiscard]] inline std::string NewRequestId()
    {
        std::array<unsigned char, 32> bytes{};
        if (BCryptGenRandom(nullptr, bytes.data(), static_cast<ULONG>(bytes.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0)
            throw std::runtime_error("Request ID generation failed.");
        constexpr char HEX[] = "0123456789abcdef";
        std::string result;
        result.reserve(64);
        for (const auto value : bytes) { result += HEX[value >> 4]; result += HEX[value & 15]; }
        return result;
    }
}
