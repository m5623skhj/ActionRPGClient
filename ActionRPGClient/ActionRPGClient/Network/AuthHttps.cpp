#include "Network/AuthHttps.h"
#include "Network/AuthSettings.h"

#include <Windows.h>
#include <winhttp.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <utility>

namespace
{
    struct InternetHandle
    {
        HINTERNET value{};
        explicit InternetHandle(HINTERNET inValue) : value(inValue)
        {
            if (!value) throw std::runtime_error("HTTPS handle unavailable.");
        }
        ~InternetHandle() { WinHttpCloseHandle(value); }
        InternetHandle(const InternetHandle&) = delete;
        InternetHandle& operator=(const InternetHandle&) = delete;
    };

    struct Completion { DWORD kind{}; DWORD size{}; };
    struct HttpState
    {
        std::mutex mutex;
        std::condition_variable changed;
        std::deque<Completion> notices;
        std::string body;
        std::wstring headers;
        std::array<char, 16384> buffer{};
    };

    // HANDLE_CLOSING releases the callback lease, including buffers still used by WinHTTP.
    void CALLBACK HttpCallback(HINTERNET, DWORD_PTR inContext, DWORD inStatus,
        void* inInformation, DWORD inSize)
    {
        if (!inContext) return;
        auto* lease = reinterpret_cast<std::shared_ptr<HttpState>*>(inContext);
        const auto state = *lease;
        if (inStatus == WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING)
        {
            delete lease;
            return;
        }
        DWORD size{};
        switch (inStatus)
        {
        case WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE:
        case WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE: break;
        case WINHTTP_CALLBACK_STATUS_DATA_AVAILABLE:
            if (inInformation && inSize == sizeof(DWORD)) size = *static_cast<DWORD*>(inInformation);
            break;
        case WINHTTP_CALLBACK_STATUS_READ_COMPLETE: size = inSize; break;
        case WINHTTP_CALLBACK_STATUS_REQUEST_ERROR: break;
        default: return;
        }
        {
            std::scoped_lock lock(state->mutex);
            state->notices.push_back({ inStatus, size });
        }
        state->changed.notify_one();
    }

    Completion Wait(const std::shared_ptr<HttpState>& inState, DWORD inExpected,
        std::stop_token inStop, std::chrono::steady_clock::time_point inDeadline)
    {
        std::unique_lock lock(inState->mutex);
        for (;;)
        {
            if (inStop.stop_requested()) throw std::runtime_error("HTTPS cancelled.");
            if (std::chrono::steady_clock::now() >= inDeadline)
                throw std::runtime_error("HTTPS deadline exceeded.");
            if (!inState->notices.empty())
            {
                const auto notice = inState->notices.front();
                inState->notices.pop_front();
                if (notice.kind == WINHTTP_CALLBACK_STATUS_REQUEST_ERROR || notice.kind != inExpected)
                    throw std::runtime_error("HTTPS request failed.");
                return notice;
            }
            inState->changed.wait_for(lock, std::chrono::milliseconds(50));
        }
    }

    void Check(BOOL inResult)
    {
        if (!inResult) throw std::runtime_error("HTTPS operation failed.");
    }
}

namespace ActionRPG
{
    AuthHttpResponse AuthPost(const std::string_view inOrigin, const std::wstring_view inPath,
        std::string inBody, const std::wstring_view inContentType, const std::string_view inBearer,
        const std::stop_token inStop)
    {
        if (inStop.stop_requested()) throw std::runtime_error("HTTPS cancelled.");
        if (inBody.size() > 65536 || inOrigin.size() > 4096 || inBearer.size() > 4096
            || inBearer.find_first_of("\r\n") != std::string_view::npos)
            throw std::runtime_error("Invalid HTTPS request.");
        const auto origin = AuthUtf8ToWide(inOrigin);
        URL_COMPONENTS parts{};
        parts.dwStructSize = sizeof(parts);
        parts.dwHostNameLength = parts.dwUserNameLength = parts.dwPasswordLength =
            parts.dwUrlPathLength = parts.dwExtraInfoLength = static_cast<DWORD>(-1);
        Check(WinHttpCrackUrl(origin.c_str(), 0, 0, &parts));
        if (parts.nScheme != INTERNET_SCHEME_HTTPS || !parts.dwHostNameLength
            || parts.dwUserNameLength || parts.dwPasswordLength || parts.dwExtraInfoLength
            || (parts.dwUrlPathLength && std::wstring_view(parts.lpszUrlPath, parts.dwUrlPathLength) != L"/"))
            throw std::runtime_error("HTTPS origin required.");
        const std::wstring hostname(parts.lpszHostName, parts.dwHostNameLength);
        InternetHandle session(WinHttpOpen(L"ActionRPGClient/1",
            WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS,
            WINHTTP_FLAG_ASYNC));
        Check(WinHttpSetTimeouts(session.value, 3000, 5000, 5000, 5000));
        DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
        Check(WinHttpSetOption(session.value, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols)));
        InternetHandle connection(WinHttpConnect(session.value, hostname.c_str(), parts.nPort, 0));
        const std::wstring path(inPath);
        InternetHandle request(WinHttpOpenRequest(connection.value, L"POST", path.c_str(), nullptr,
            WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE));
        DWORD disabled = WINHTTP_DISABLE_REDIRECTS;
        Check(WinHttpSetOption(request.value, WINHTTP_OPTION_DISABLE_FEATURE, &disabled, sizeof(disabled)));
        const auto state = std::make_shared<HttpState>();
        state->body = std::move(inBody);
        state->headers = L"Content-Type: " + std::wstring(inContentType) + L"\r\nAccept: application/json\r\n";
        if (!inBearer.empty()) state->headers += L"Authorization: Bearer " + AuthUtf8ToWide(inBearer) + L"\r\n";
        if (WinHttpSetStatusCallback(request.value, HttpCallback,
            WINHTTP_CALLBACK_FLAG_ALL_COMPLETIONS | WINHTTP_CALLBACK_FLAG_HANDLES, 0) == WINHTTP_INVALID_STATUS_CALLBACK)
            throw std::runtime_error("HTTPS callback unavailable.");
        auto* lease = new std::shared_ptr<HttpState>(state);
        auto context = reinterpret_cast<DWORD_PTR>(lease);
        if (!WinHttpSetOption(request.value, WINHTTP_OPTION_CONTEXT_VALUE,
            &context, sizeof(context)))
        {
            delete lease;
            throw std::runtime_error("HTTPS context unavailable.");
        }
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
        Check(WinHttpSendRequest(request.value, state->headers.c_str(), static_cast<DWORD>(-1),
            state->body.empty() ? WINHTTP_NO_REQUEST_DATA : state->body.data(),
            static_cast<DWORD>(state->body.size()), static_cast<DWORD>(state->body.size()), context));
        Wait(state, WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE, inStop, deadline);
        Check(WinHttpReceiveResponse(request.value, nullptr));
        Wait(state, WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE, inStop, deadline);
        AuthHttpResponse response;
        DWORD statusLength = sizeof(response.status);
        Check(WinHttpQueryHeaders(request.value, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX, &response.status, &statusLength, WINHTTP_NO_HEADER_INDEX));
        for (;;)
        {
            Check(WinHttpQueryDataAvailable(request.value, nullptr));
            const auto available = Wait(state, WINHTTP_CALLBACK_STATUS_DATA_AVAILABLE, inStop, deadline).size;
            if (!available) break;
            if (response.body.size() + available > 65536) throw std::runtime_error("HTTPS response too large.");
            Check(WinHttpReadData(request.value, state->buffer.data(),
                static_cast<DWORD>(std::min<std::size_t>(available, state->buffer.size())), nullptr));
            const auto count = Wait(state, WINHTTP_CALLBACK_STATUS_READ_COMPLETE, inStop, deadline).size;
            if (!count) break;
            if (count > state->buffer.size()) throw std::runtime_error("Invalid HTTPS response.");
            response.body.append(state->buffer.data(), count);
        }
        return response;
    }
}