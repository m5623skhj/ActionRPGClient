#pragma once

#include "Network/AuthSettings.h"
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace ActionRPG
{
    enum class AuthOperation { Login, Ticket, Logout };
    enum class AuthEventKind { Progress, Complete, Failed };
    struct AuthEvent
    {
        std::uint64_t attemptId{};
        AuthOperation operation{};
        AuthEventKind kind{};
        std::wstring message;
        std::string credential;
        std::chrono::steady_clock::time_point expiresAt{};
        bool ready{};
    };

    class AuthClient final
    {
    public:
        AuthClient();
        ~AuthClient();
        AuthClient(const AuthClient&) = delete;
        AuthClient& operator=(const AuthClient&) = delete;
        void Login(std::uint64_t inAttempt, AuthSettings inSettings, bool inChooseAccount);
        void RequestTicket(std::uint64_t inAttempt, std::string inOrigin,
            std::string inGameToken, std::string inServerId);
        void Logout(std::uint64_t inAttempt, std::string inOrigin, std::string inGameToken);
        void Cancel();
        [[nodiscard]] std::vector<AuthEvent> ConsumeEvents();
    private:
        struct Impl;
        std::unique_ptr<Impl> impl;
    };
}