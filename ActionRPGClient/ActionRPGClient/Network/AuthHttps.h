#pragma once

#include <stop_token>
#include <string>
#include <string_view>

namespace ActionRPG
{
    struct AuthHttpResponse
    {
        unsigned long status{};
        std::string body;
    };

    // Runs on the authentication worker; cancellation closes the asynchronous request.
    [[nodiscard]] AuthHttpResponse AuthPost(std::string_view inOrigin, std::wstring_view inPath,
        std::string inBody, std::wstring_view inContentType, std::string_view inBearer,
        std::stop_token inStop);
}