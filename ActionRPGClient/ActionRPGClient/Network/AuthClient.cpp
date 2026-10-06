#include "Network/AuthClient.h"
#include "Network/AuthHttps.h"

#include <asio.hpp>
#include <Windows.h>
#include <shellapi.h>
#include <objbase.h>
#include <bcrypt.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <stop_token>
#include <thread>
#include <utility>

namespace
{
    using Clock = std::chrono::steady_clock;
    using Json = nlohmann::json;
    struct HttpFailure { unsigned long status; };
    constexpr char BASE64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

    std::string Base64Url(const unsigned char* inData, std::size_t inSize)
    {
        std::string result;
        unsigned int bits{};
        int available{};
        for (std::size_t index = 0; index < inSize; ++index)
        {
            bits = (bits << 8) | inData[index];
            available += 8;
            while (available >= 6) { available -= 6; result += BASE64[(bits >> available) & 63]; }
        }
        if (available) result += BASE64[(bits << (6 - available)) & 63];
        return result;
    }

    std::string RandomString()
    {
        std::array<unsigned char, 32> bytes{};
        if (BCryptGenRandom(nullptr, bytes.data(), static_cast<ULONG>(bytes.size()),
            BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0) throw std::runtime_error("Random source failed.");
        return Base64Url(bytes.data(), bytes.size());
    }

    std::string PkceChallenge(const std::string& inVerifier)
    {
        BCRYPT_ALG_HANDLE algorithm{};
        if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
            throw std::runtime_error("SHA256 unavailable.");
        std::array<unsigned char, 32> hash{};
        const auto result = BCryptHash(algorithm, nullptr, 0,
            reinterpret_cast<PUCHAR>(const_cast<char*>(inVerifier.data())),
            static_cast<ULONG>(inVerifier.size()), hash.data(), static_cast<ULONG>(hash.size()));
        BCryptCloseAlgorithmProvider(algorithm, 0);
        if (result < 0) throw std::runtime_error("SHA256 failed.");
        return Base64Url(hash.data(), hash.size());
    }

    std::string Encode(const std::string_view inText)
    {
        constexpr char HEX[] = "0123456789ABCDEF";
        std::string result;
        for (const unsigned char value : inText)
        {
            if ((value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z')
                || (value >= '0' && value <= '9') || value == '-' || value == '_' || value == '.' || value == '~')
                result += static_cast<char>(value);
            else { result += '%'; result += HEX[value >> 4]; result += HEX[value & 15]; }
        }
        return result;
    }

    int Hex(const char inValue)
    {
        if (inValue >= '0' && inValue <= '9') return inValue - '0';
        if (inValue >= 'A' && inValue <= 'F') return inValue - 'A' + 10;
        if (inValue >= 'a' && inValue <= 'f') return inValue - 'a' + 10;
        return -1;
    }

    std::string Decode(const std::string_view inText)
    {
        std::string result;
        for (std::size_t index = 0; index < inText.size(); ++index)
        {
            const auto value = inText[index];
            if (value == '%')
            {
                if (index + 2 >= inText.size() || Hex(inText[index + 1]) < 0 || Hex(inText[index + 2]) < 0)
                    throw std::runtime_error("Invalid callback encoding.");
                result += static_cast<char>((Hex(inText[index + 1]) << 4) | Hex(inText[index + 2]));
                index += 2;
            }
            else result += value == '+' ? ' ' : value;
        }
        return result;
    }

    bool EqualState(const std::string& inLeft, const std::string& inRight)
    {
        if (inLeft.size() != inRight.size()) return false;
        unsigned char difference{};
        for (std::size_t index = 0; index < inLeft.size(); ++index)
            difference |= static_cast<unsigned char>(inLeft[index] ^ inRight[index]);
        return difference == 0;
    }

    std::string StringField(const Json& inJson, const char* inName, const std::size_t inLimit)
    {
        const auto value = inJson.at(inName).get<std::string>();
        if (value.empty() || value.size() > inLimit || value.find('\0') != std::string::npos)
            throw std::runtime_error("Invalid authentication response.");
        return value;
    }

    int Lifetime(const Json& inJson, const int inMaximum)
    {
        const auto& value = inJson.at("expiresIn");
        if (!value.is_number_integer() || value < 1 || value > inMaximum)
            throw std::runtime_error("Invalid credential lifetime.");
        return value.get<int>();
    }

    Json Post(const std::string& inOrigin, const std::wstring_view inPath, const Json& inBody,
        const std::string& inToken, const std::stop_token inStop)
    {
        const auto response = ActionRPG::AuthPost(inOrigin, inPath, inBody.dump(),
            L"application/json", inToken, inStop);
        if (response.status != 200) throw HttpFailure{ response.status };
        return Json::parse(response.body);
    }

    void Poll(const std::stop_token inStop, const Clock::time_point inDeadline)
    {
        if (inStop.stop_requested()) throw std::runtime_error("Authentication cancelled.");
        if (Clock::now() >= inDeadline) throw std::runtime_error("Authentication deadline exceeded.");
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // Only binds localhost. Code/state are consumed here and never logged or displayed.
    std::string GoogleIdToken(const ActionRPG::AuthSettings& inSettings, const std::string& inNonce,
        const Clock::time_point inDeadline, const bool inChooseAccount, const std::stop_token inStop)
    {
        asio::io_context context;
        asio::ip::tcp::acceptor acceptor(context,
            asio::ip::tcp::endpoint(asio::ip::address_v4::loopback(), 0));
        acceptor.non_blocking(true);
        const std::string redirect = "http://127.0.0.1:" + std::to_string(acceptor.local_endpoint().port())
            + "/oauth2/callback";
        const auto state = RandomString();
        const auto verifier = RandomString();
        auto url = std::string("https://accounts.google.com/o/oauth2/v2/auth?client_id=")
            + Encode(inSettings.googleClientId) + "&redirect_uri=" + Encode(redirect)
            + "&response_type=code&scope=openid&code_challenge_method=S256&code_challenge="
            + Encode(PkceChallenge(verifier)) + "&nonce=" + Encode(inNonce) + "&state=" + Encode(state);
        if (inChooseAccount) url += "&prompt=select_account";
        const auto wideUrl = ActionRPG::AuthUtf8ToWide(url);
        if (inStop.stop_requested()) throw std::runtime_error("Authentication cancelled.");
        if (reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", wideUrl.c_str(), nullptr, nullptr, SW_SHOWNORMAL)) <= 32)
            throw std::runtime_error("System browser unavailable.");
        std::string code;
        unsigned int invalidRequests{};
        while (code.empty())
        {
            Poll(inStop, inDeadline);
            asio::ip::tcp::socket socket(context);
            asio::error_code error;
            acceptor.accept(socket, error);
            if (error == asio::error::would_block || error == asio::error::try_again) continue;
            if (error) throw std::runtime_error("OAuth callback unavailable.");
            socket.non_blocking(true);
            const auto headerDeadline = std::min(inDeadline, Clock::now() + std::chrono::seconds(2));
            std::string request;
            std::array<char, 2048> bytes{};
            bool valid{};
            bool denied{};
            try
            {
                while (request.find("\r\n\r\n") == std::string::npos)
                {
                    Poll(inStop, headerDeadline);
                    const auto count = socket.read_some(asio::buffer(bytes), error);
                    if (error == asio::error::would_block || error == asio::error::try_again) continue;
                    if (error || request.size() + count > 8192) throw std::runtime_error("Invalid callback.");
                    request.append(bytes.data(), count);
                }
                const auto end = request.find(" HTTP/1.");
                constexpr std::string_view PREFIX = "GET /oauth2/callback?";
                if (!request.starts_with(PREFIX) || end == std::string::npos) throw std::runtime_error("Invalid callback.");
                std::map<std::string, std::string> parameters;
                auto query = std::string_view(request).substr(PREFIX.size(), end - PREFIX.size());
                while (!query.empty())
                {
                    const auto separator = query.find('&');
                    const auto field = query.substr(0, separator);
                    const auto equals = field.find('=');
                    if (equals == std::string_view::npos || !parameters.emplace(
                        Decode(field.substr(0, equals)), Decode(field.substr(equals + 1))).second)
                        throw std::runtime_error("Invalid callback.");
                    if (separator == std::string_view::npos) break;
                    query.remove_prefix(separator + 1);
                }
                if (!EqualState(parameters.at("state"), state)) throw std::runtime_error("OAuth state mismatch.");
                denied = parameters.contains("error");
                if (!denied)
                {
                    code = parameters.at("code");
                    if (code.empty() || code.size() > 4096) throw std::runtime_error("Invalid authorization code.");
                }
                valid = true;
            }
            catch (...)
            {
                if (inStop.stop_requested() || Clock::now() >= inDeadline) throw;
                code.clear();
                if (++invalidRequests > 16) throw std::runtime_error("Too many invalid OAuth callbacks.");
            }
            const std::string body = valid ? "Authentication response received. You may return to the game."
                : "Invalid authentication callback.";
            const auto response = std::string(valid ? "HTTP/1.1 200 OK\r\n" : "HTTP/1.1 400 Bad Request\r\n")
                + "Content-Type: text/plain; charset=utf-8\r\nCache-Control: no-store\r\n"
                  "Content-Security-Policy: default-src 'none'\r\nConnection: close\r\nContent-Length: "
                + std::to_string(body.size()) + "\r\n\r\n" + body;
            std::size_t sent{};
            const auto sendDeadline = std::min(inDeadline, Clock::now() + std::chrono::seconds(1));
            while (sent < response.size() && !inStop.stop_requested() && Clock::now() < sendDeadline)
            {
                const auto count = socket.write_some(asio::buffer(response.data() + sent, response.size() - sent), error);
                if (error == asio::error::would_block || error == asio::error::try_again)
                    { Poll(inStop, sendDeadline); continue; }
                if (error) break;
                sent += count;
            }
            if (denied) throw std::runtime_error("Google login declined.");
        }
        acceptor.close();
        if (inStop.stop_requested() || Clock::now() >= inDeadline)
            throw std::runtime_error("Authentication cancelled or expired.");
        const auto form = "client_id=" + Encode(inSettings.googleClientId) + "&code=" + Encode(code)
            + "&code_verifier=" + Encode(verifier) + "&grant_type=authorization_code&redirect_uri=" + Encode(redirect);
        const auto response = ActionRPG::AuthPost("https://oauth2.googleapis.com", L"/token", form,
            L"application/x-www-form-urlencoded", {}, inStop);
        if (response.status != 200) throw HttpFailure{ response.status };
        return StringField(Json::parse(response.body), "id_token", 16384);
    }
}

namespace ActionRPG
{
    struct AuthClient::Impl
    {
        struct Job
        {
            std::uint64_t attempt{};
            AuthOperation operation{};
            AuthSettings settings;
            std::string origin, token, serverId;
            bool chooseAccount{};
            std::stop_token stop;
        };
        std::mutex mutex;
        std::condition_variable_any changed;
        std::optional<Job> pending;
        std::stop_source cancellation;
        std::vector<AuthEvent> events;
        std::jthread worker;

        Impl() : worker([this](std::stop_token inStop) { Run(inStop); }) {}
        ~Impl()
        {
            Cancel();
            worker.request_stop();
            changed.notify_all();
            worker.join();
        }
        void Cancel()
        {
            std::scoped_lock lock(mutex);
            cancellation.request_stop();
            pending.reset();
            events.clear();
        }
        void Submit(Job inJob)
        {
            std::scoped_lock lock(mutex);
            cancellation.request_stop();
            cancellation = std::stop_source{};
            inJob.stop = cancellation.get_token();
            pending = std::move(inJob);
            events.clear();
            changed.notify_one();
        }
        void Emit(const Job& inJob, AuthEventKind inKind, std::wstring inMessage,
            std::string inCredential = {}, Clock::time_point inExpiry = {}, bool inReady = false)
        {
            std::scoped_lock lock(mutex);
            if (inJob.stop.stop_requested()) return;
            if (events.size() == 32) events.erase(events.begin());
            events.push_back({ inJob.attempt, inJob.operation, inKind, std::move(inMessage),
                std::move(inCredential), inExpiry, inReady });
        }
        void Execute(const Job& inJob)
        {
            if (inJob.operation == AuthOperation::Login)
            {
                Emit(inJob, AuthEventKind::Progress, L"서버 확인: 로그인 요청 준비");
                const auto challengeStarted = Clock::now();
                const auto challenge = Post(inJob.settings.authUrl, L"/v1/challenges", Json::object(), {}, inJob.stop);
                const auto challengeId = StringField(challenge, "challengeId", 1024);
                const auto nonce = StringField(challenge, "nonce", 1024);
                const auto deadline = challengeStarted + std::chrono::seconds(Lifetime(challenge, 300));
                Emit(inJob, AuthEventKind::Progress, L"Google 인증 대기: 브라우저에서 로그인해 주세요.");
                const auto idToken = GoogleIdToken(inJob.settings, nonce, deadline, inJob.chooseAccount, inJob.stop);
                if (Clock::now() >= deadline) throw std::runtime_error("Challenge expired.");
                Emit(inJob, AuthEventKind::Progress, L"서버 확인: Google 인증 검증");
                const auto loginStarted = Clock::now();
                const auto response = Post(inJob.settings.authUrl, L"/v1/login",
                    Json{ { "challengeId", challengeId }, { "idToken", idToken } }, {}, inJob.stop);
                const auto token = StringField(response, "gameToken", 64);
                if (token.size() != 64 || !std::all_of(token.begin(), token.end(), [](char value)
                    { return (value >= '0' && value <= '9') || (value >= 'a' && value <= 'f'); }))
                    throw std::runtime_error("Invalid game token.");
                Emit(inJob, AuthEventKind::Complete, L"로그인 완료. 입장할 타운을 선택하세요.", token,
                    loginStarted + std::chrono::seconds(Lifetime(response, 28800)));
            }
            else if (inJob.operation == AuthOperation::Ticket)
            {
                Emit(inJob, AuthEventKind::Progress, L"서버 확인: 타운 입장 티켓 발급");
                const auto ticketStarted = Clock::now();
                const auto response = Post(inJob.origin, L"/v1/tickets",
                    Json{ { "serverId", inJob.serverId } }, inJob.token, inJob.stop);
                const auto ticket = StringField(response, "ticket", 64);
                if (ticket.size() != 64 || !std::all_of(ticket.begin(), ticket.end(), [](char value)
                    { return (value >= '0' && value <= '9') || (value >= 'a' && value <= 'f'); }))
                    throw std::runtime_error("Invalid ticket.");
                const bool ready = response.at("ready").get<bool>();
                Emit(inJob, AuthEventKind::Complete,
                    ready ? L"타운 입장: 보안 연결 준비" : L"기존 접속 종료 대기. 잠시 후 입장을 다시 눌러 주세요.",
                    ready ? ticket : std::string{}, ticketStarted + std::chrono::seconds(Lifetime(response, 30)), ready);
            }
            else
            {
                Post(inJob.origin, L"/v1/logout", Json::object(), inJob.token, inJob.stop);
                Emit(inJob, AuthEventKind::Complete, L"로그아웃했습니다.");
            }
        }
        void Run(const std::stop_token inStop)
        {
            const HRESULT comResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            while (!inStop.stop_requested())
            {
                std::optional<Job> job;
                {
                    std::unique_lock lock(mutex);
                    if (!changed.wait(lock, inStop, [this] { return pending.has_value(); })) break;
                    job = std::move(pending);
                    pending.reset();
                }
                try { Execute(*job); }
                catch (const HttpFailure& failure)
                {
                    Emit(*job, AuthEventKind::Failed, job->operation == AuthOperation::Logout
                        ? L"로컬 로그아웃은 완료했습니다. 서버 로그아웃 요청은 거절되거나 처리되지 않았습니다."
                        : failure.status == 503
                        ? L"서버 준비 중입니다. 준비가 끝나면 새로 시도해 주세요."
                        : failure.status == 403
                        ? L"인증 또는 입장이 거절됐습니다. 설정과 계정을 확인한 뒤 새로 시도해 주세요."
                        : L"요청이 거절됐습니다. 새로 시도해 주세요.");
                }
                catch (...)
                {
                    Emit(*job, AuthEventKind::Failed, job->operation == AuthOperation::Logout
                        ? L"서버 로그아웃 완료 여부를 확인하지 못했습니다. 로컬 연결과 인증 정보는 정리했습니다."
                        : L"요청 완료 여부를 확인하지 못했습니다. 브라우저·연결·설정을 확인한 뒤 새로 시도해 주세요.");
                }
            }
            if (SUCCEEDED(comResult)) CoUninitialize();
        }
    };

    AuthClient::AuthClient() : impl(std::make_unique<Impl>()) {}
    AuthClient::~AuthClient() = default;
    void AuthClient::Login(const std::uint64_t inAttempt, AuthSettings inSettings, const bool inChooseAccount)
    {
        Impl::Job job; job.attempt = inAttempt; job.operation = AuthOperation::Login;
        job.settings = std::move(inSettings); job.chooseAccount = inChooseAccount;
        impl->Submit(std::move(job));
    }
    void AuthClient::RequestTicket(const std::uint64_t inAttempt, std::string inOrigin,
        std::string inGameToken, std::string inServerId)
    {
        Impl::Job job; job.attempt = inAttempt; job.operation = AuthOperation::Ticket;
        job.origin = std::move(inOrigin); job.token = std::move(inGameToken); job.serverId = std::move(inServerId);
        impl->Submit(std::move(job));
    }
    void AuthClient::Logout(const std::uint64_t inAttempt, std::string inOrigin, std::string inGameToken)
    {
        Impl::Job job; job.attempt = inAttempt; job.operation = AuthOperation::Logout;
        job.origin = std::move(inOrigin); job.token = std::move(inGameToken);
        impl->Submit(std::move(job));
    }
    void AuthClient::Cancel() { impl->Cancel(); }
    std::vector<AuthEvent> AuthClient::ConsumeEvents()
    {
        std::scoped_lock lock(impl->mutex);
        std::vector<AuthEvent> result;
        result.swap(impl->events);
        return result;
    }
}