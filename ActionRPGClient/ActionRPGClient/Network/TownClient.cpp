#include "Platform/ShutdownTrace.h"
#include "Network/TownClient.h"
#include <nlohmann/json.hpp>
#include <asio.hpp>
#include <asio/ssl.hpp>
#include <Windows.h>
#include <openssl/ssl.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <deque>
#include <fstream>
#include <future>
#include <iterator>
#include <limits>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>
#include <utility>

namespace
{
    constexpr std::uint32_t MAX_PACKET_BODY_SIZE = 1024 * 1024;
    constexpr std::size_t MAX_QUEUED_SEND_BYTES = 1024 * 1024;
    using Clock = std::chrono::steady_clock;
    // Server admission waits at most 30s; allow 5s for its final status to reach the client.
    constexpr auto PRIOR_OWNER_WAIT_LIMIT = std::chrono::seconds(35);
    std::wstring SessionStatusMessage(const ActionRPG::TownSessionStatusCode inCode)
    {
        using Code = ActionRPG::TownSessionStatusCode;
        switch (inCode)
        {
        case Code::WaitingForPriorOwner: return L"기존 접속을 정리하는 중입니다.";
        case Code::Replaced: return L"다른 곳에서 접속하여 연결이 종료되었습니다.";
        case Code::AdmissionTimeout: return L"기존 접속 정리 대기 시간이 초과되었습니다. 다시 로그인해 주세요.";
        case Code::AuthUnavailable: return L"인증 서버에 연결할 수 없어 접속이 종료되었습니다. 잠시 후 다시 로그인해 주세요.";
        case Code::AuthorizationExpired: return L"인증 유효 시간이 끝나 연결이 종료되었습니다. 다시 로그인해 주세요.";
        case Code::LoggedOut: return L"로그아웃되어 연결이 종료되었습니다.";
        case Code::ServerTransfer: return L"다른 서버로 접속하여 연결이 종료되었습니다.";
        case Code::AccountUnavailable: return L"계정 상태를 확인할 수 없어 연결이 종료되었습니다. 다시 로그인해 주세요.";
        default: return L"타운 연결이 종료되었습니다.";
        }
    }

    std::uint32_t DecodeBodySize(const std::array<std::uint8_t, 4>& inHeader)
    {
        return (static_cast<std::uint32_t>(inHeader[0]) << 24)
            | (static_cast<std::uint32_t>(inHeader[1]) << 16)
            | (static_cast<std::uint32_t>(inHeader[2]) << 8) | inHeader[3];
    }
    std::vector<std::uint8_t> FramePacket(std::vector<std::uint8_t> inBody)
    {
        const auto size = static_cast<std::uint32_t>(inBody.size());
        std::vector<std::uint8_t> result(4 + inBody.size());
        result[0] = static_cast<std::uint8_t>(size >> 24);
        result[1] = static_cast<std::uint8_t>(size >> 16);
        result[2] = static_cast<std::uint8_t>(size >> 8);
        result[3] = static_cast<std::uint8_t>(size);
        std::copy(inBody.begin(), inBody.end(), result.begin() + 4);
        return result;
    }
    asio::ssl::context MakeTlsContext(const std::filesystem::path& inCa)
    {
        asio::ssl::context context(asio::ssl::context::tls_client);
        if (SSL_CTX_set_min_proto_version(context.native_handle(), TLS1_2_VERSION) != 1)
            throw std::runtime_error("TLS configuration failed.");
        if (!std::filesystem::is_regular_file(inCa) || std::filesystem::file_size(inCa) > MAX_PACKET_BODY_SIZE)
            throw std::runtime_error("CA file unavailable.");
        std::ifstream input(inCa, std::ios::binary);
        if (!input) throw std::runtime_error("CA file unavailable.");
        const std::string certificates((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
        if (certificates.empty()) throw std::runtime_error("Empty CA file.");
        context.add_certificate_authority(asio::buffer(certificates));
        context.set_verify_mode(asio::ssl::verify_peer);
        return context;
    }
}

namespace ActionRPG
{
    struct TownClient::Impl
    {
        struct Session;
        asio::io_context ioContext;
        asio::strand<asio::io_context::executor_type> strand{ asio::make_strand(ioContext) };
        asio::executor_work_guard<asio::io_context::executor_type> workGuard{ asio::make_work_guard(ioContext) };
        std::shared_ptr<Session> session; // Only the strand accesses the active session.
        std::atomic_uint64_t attempt{};
        std::atomic_bool connected{};
        mutable std::mutex mutex;
        TownConnectionInfo status;
        std::vector<TownEvent> events;
        std::vector<CharacterEvent> characterEvents;
        std::thread worker;
        Impl() : worker([this] { ioContext.run(); }) {}
        ~Impl();
        void SetStatus(std::uint64_t inAttempt, TownConnectionState inState, std::wstring inMessage,
            TownSessionStatusCode inCode = TownSessionStatusCode::None, std::uint32_t inRetryAfterMs = 0);
        void PushEvent(std::uint64_t inAttempt, TownEvent inEvent);
        void PushCharacterEvent(std::uint64_t inAttempt, CharacterEvent inEvent);
    };

    // Each attempt owns its stream and buffers until its final callbacks have returned.
    struct TownClient::Impl::Session : std::enable_shared_from_this<Session>
    {
        Impl& owner;
        std::uint64_t attemptId;
        asio::ssl::context tlsContext;
        asio::ssl::stream<asio::ip::tcp::socket> socket;
        asio::ip::tcp::resolver resolver;
        asio::steady_timer deadline;
        TownServerSettings server;
        std::string ticket;
        bool selectionRequested{}, priorOwnerWaitStarted{};
        TownConnectionState state{ TownConnectionState::Connecting };
        bool closed{};
        std::array<std::uint8_t, 4> receiveHeader{};
        std::vector<std::uint8_t> receiveBody;
        std::deque<std::shared_ptr<std::vector<std::uint8_t>>> sendQueue;

        Session(Impl& inOwner, const std::uint64_t inAttempt, TownServerSettings inServer,
            const std::filesystem::path& inCa, std::string inTicket)
            : owner(inOwner), attemptId(inAttempt), tlsContext(MakeTlsContext(inCa)),
              socket(owner.strand, tlsContext), resolver(owner.strand), deadline(owner.strand),
              server(std::move(inServer)), ticket(std::move(inTicket)) {}

        bool IsCurrent() const { return !closed && owner.attempt.load() == attemptId; }
        void SetState(const TownConnectionState inState, std::wstring inMessage,
            const TownSessionStatusCode inCode = TownSessionStatusCode::None, const std::uint32_t inRetryAfterMs = 0)
        {
            state = inState;
            owner.SetStatus(attemptId, state, std::move(inMessage), inCode, inRetryAfterMs);
        }
        void Close()
        {
            if (closed) return;
            closed = true;
            asio::error_code ignored;
            deadline.cancel();
            resolver.cancel();
            socket.lowest_layer().cancel(ignored);
            socket.lowest_layer().close(ignored);
            ticket.clear();
            sendQueue.clear();
        }
        void Fail(std::wstring inMessage, const TownSessionStatusCode inCode = TownSessionStatusCode::None)
        {
            if (!IsCurrent()) { Close(); return; }
            SetState(TownConnectionState::Failed, std::move(inMessage), inCode);
            Close(); // Cancelling read/write/timer callbacks cannot overwrite the terminal reason.
        }
        void HandleDisconnect()
        {
            if (state == TownConnectionState::WaitingForPriorOwner)
                Fail(L"기존 접속 정리를 기다리던 연결이 종료되었습니다. 다시 로그인해 주세요.",
                    TownSessionStatusCode::WaitingForPriorOwner);
            else Fail(L"타운 연결이 종료되거나 입장이 완료되지 않았습니다. 새 티켓으로 다시 시도해 주세요.");
        }
        void Start(const Clock::time_point inExpiry)
        {
            if (!IsCurrent() || Clock::now() >= inExpiry || ticket.size() != 64
                || !std::all_of(ticket.begin(), ticket.end(), [](char value)
                    { return (value >= '0' && value <= '9') || (value >= 'a' && value <= 'f'); }))
                { HandleDisconnect(); return; }
            socket.set_verify_callback(asio::ssl::host_name_verification(server.hostname));
            if (SSL_set_tlsext_host_name(socket.native_handle(), server.hostname.c_str()) != 1)
                { HandleDisconnect(); return; }
            deadline.expires_at(std::min(inExpiry, Clock::now() + std::chrono::seconds(15)));
            deadline.async_wait([self = shared_from_this()](const asio::error_code& inError)
            {
                if (!inError && self->IsCurrent() && (self->state == TownConnectionState::Connecting
                    || self->state == TownConnectionState::AdmissionPending)) self->HandleDisconnect();
            });
            resolver.async_resolve(server.hostname, std::to_string(server.port),
                [self = shared_from_this()](const asio::error_code& inError,
                    const asio::ip::tcp::resolver::results_type& inResults)
            {
                if (!self->IsCurrent()) return;
                if (inError) { self->HandleDisconnect(); return; }
                asio::async_connect(self->socket.next_layer(), inResults,
                    [self](const asio::error_code& inConnectError, const asio::ip::tcp::endpoint&)
                {
                    if (!self->IsCurrent()) return;
                    if (inConnectError) { self->HandleDisconnect(); return; }
                    asio::error_code error;
                    self->socket.next_layer().set_option(asio::ip::tcp::no_delay(true), error);
                    if (error) { self->HandleDisconnect(); return; }
                    self->socket.async_handshake(asio::ssl::stream_base::client,
                        [self](const asio::error_code& inHandshakeError)
                    {
                        if (!self->IsCurrent()) return;
                        if (inHandshakeError) { self->HandleDisconnect(); return; }
                        self->SetState(TownConnectionState::AdmissionPending, L"타운 입장: 티켓 승인 대기");
                        self->QueuePacket(TownProtocol::Encode(TownProtocol::AdmissionTicketRequest{ self->ticket }));
                        self->ticket.clear();
                        self->ReadHeader();
                    });
                });
            });
        }
        void ReadHeader()
        {
            if (!IsCurrent()) return;
            asio::async_read(socket, asio::buffer(receiveHeader),
                [self = shared_from_this()](const asio::error_code& inError, std::size_t)
            {
                if (!self->IsCurrent()) return;
                if (inError) { self->HandleDisconnect(); return; }
                const auto size = DecodeBodySize(self->receiveHeader);
                if (size < 2 || size > MAX_PACKET_BODY_SIZE) { self->HandleDisconnect(); return; }
                self->ReadBody(size);
            });
        }
        void ReadBody(const std::uint32_t inSize)
        {
            receiveBody.resize(inSize);
            asio::async_read(socket, asio::buffer(receiveBody),
                [self = shared_from_this()](const asio::error_code& inError, std::size_t)
            {
                if (!self->IsCurrent()) return;
                if (inError) { self->HandleDisconnect(); return; }
                self->HandlePacket();
                self->ReadHeader();
            });
        }
        void HandlePacket();
        void PushEvent(TownEvent inEvent) { owner.PushEvent(attemptId, std::move(inEvent)); }
        void QueuePacket(std::vector<std::uint8_t> inBody)
        {
            if (!IsCurrent()) return;
            auto packet = std::make_shared<std::vector<std::uint8_t>>(FramePacket(std::move(inBody)));
            std::size_t queuedBytes{};
            for (const auto& queued : sendQueue) queuedBytes += queued->size();
            if (queuedBytes + packet->size() > MAX_QUEUED_SEND_BYTES) { HandleDisconnect(); return; }
            const bool writing = !sendQueue.empty();
            sendQueue.push_back(std::move(packet));
            if (!writing) WriteNext();
        }
        void WriteNext()
        {
            if (!IsCurrent() || sendQueue.empty()) return;
            const auto packet = sendQueue.front();
            asio::async_write(socket, asio::buffer(*packet),
                [self = shared_from_this(), packet](const asio::error_code& inError, std::size_t)
            {
                if (!self->IsCurrent()) return;
                if (inError) { self->HandleDisconnect(); return; }
                self->sendQueue.pop_front();
                self->WriteNext();
            });
        }
    };

    TownClient::Impl::~Impl()
    {
        attempt.store(0);
        connected.store(false);
        TraceShutdown("ActionRPGClient: stopping town I/O worker\n");
        // stop() is thread-safe and wakes run() directly. Do not depend on a
        // queued handler to stop the executor we are about to join.
        ioContext.stop();
        if (worker.joinable()) worker.join();
        // No callback can access the session now; retain its buffers until join.
        if (session) session->Close();
        session.reset();
        workGuard.reset();
        TraceShutdown("ActionRPGClient: town I/O worker stopped\n");
    }
    void TownClient::Impl::SetStatus(const std::uint64_t inAttempt,
        const TownConnectionState inState, std::wstring inMessage,
        const TownSessionStatusCode inCode, const std::uint32_t inRetryAfterMs)
    {
        std::scoped_lock lock(mutex);
        if (attempt.load() != inAttempt) return;
        status = { inAttempt, inState, std::move(inMessage), inCode, inRetryAfterMs };
        connected.store(inState == TownConnectionState::Ready);
    }
    void TownClient::Impl::PushEvent(const std::uint64_t inAttempt, TownEvent inEvent)
    {
        std::scoped_lock lock(mutex);
        if (attempt.load() != inAttempt) return;
        if (events.size() >= 4096)
        {
            // Bounded queue; fail the connection rather than silently dropping authority events.
            asio::post(strand, [this, inAttempt]
            {
                if (session && attempt.load() == inAttempt) session->HandleDisconnect();
            });
            return;
        }
        events.push_back(std::move(inEvent));
    }
    void TownClient::Impl::PushCharacterEvent(const std::uint64_t inAttempt, CharacterEvent inEvent)
    {
        std::scoped_lock lock(mutex);
        if (attempt.load() != inAttempt) return;
        if (characterEvents.size() >= 256)
        {
            asio::post(strand, [this, inAttempt]
            {
                if (session && attempt.load() == inAttempt) session->HandleDisconnect();
            });
            return;
        }
        characterEvents.push_back(std::move(inEvent));
    }

    TownClient::TownClient() : impl(std::make_unique<Impl>()) {}
    TownClient::~TownClient() = default;

    void TownClient::Start(const std::uint64_t inAttempt, TownServerSettings inServer,
        std::filesystem::path inCa, std::string inTicket, const Clock::time_point inExpiry)
    {
        selectedCharacterId = 0;
        chatSequence = 0;
        {
            std::scoped_lock lock(impl->mutex);
            impl->attempt.store(inAttempt);
            impl->connected.store(false);
            impl->events.clear();
            impl->characterEvents.clear();
            impl->status = { inAttempt, TownConnectionState::Connecting, L"타운 입장: TLS 연결 중" };
        }
        asio::post(impl->strand, [this, inAttempt, server = std::move(inServer), ca = std::move(inCa),
            ticket = std::move(inTicket), inExpiry]() mutable
        {
            if (impl->session) impl->session->Close();
            impl->session.reset();
            if (impl->attempt.load() != inAttempt) return;
            try
            {
                impl->session = std::make_shared<Impl::Session>(*impl, inAttempt, std::move(server), ca,
                    std::move(ticket));
                impl->session->Start(inExpiry);
            }
            catch (...)
            {
                impl->SetStatus(inAttempt, TownConnectionState::Failed,
                    L"타운 TLS 설정을 확인해 주세요. 신뢰 CA와 인증서 호스트명이 필요합니다.");
            }
        });
    }
    void TownClient::RequestStop()
    {
        selectedCharacterId = 0;
        {
            std::scoped_lock lock(impl->mutex);
            impl->attempt.store(0);
            impl->connected.store(false);
            impl->events.clear();
            impl->characterEvents.clear();
            impl->status = {};
        }
        asio::post(impl->strand, [this]
        {
            if (impl->session) impl->session->Close();
            impl->session.reset();
        });
    }
    void TownClient::Stop()
    {
        RequestStop();
        auto stopped = std::make_shared<std::promise<void>>();
        auto completion = stopped->get_future();
        asio::post(impl->strand, [stopped] { stopped->set_value(); });
        completion.get();
    }
    TownConnectionInfo TownClient::GetConnectionInfo() const
    {
        std::scoped_lock lock(impl->mutex);
        return impl->status;
    }
    std::vector<TownEvent> TownClient::ConsumeEvents()
    {
        std::scoped_lock lock(impl->mutex);
        std::vector<TownEvent> result;
        result.swap(impl->events);
        return result;
    }
    bool TownClient::IsConnected() const noexcept { return impl->connected.load(); }
    void TownClient::QueuePacket(std::vector<std::uint8_t> inBody)
    {
        if (impl->session && impl->session->state == TownConnectionState::Ready)
            impl->session->QueuePacket(std::move(inBody));
    }
    void TownClient::QueueCharacterPacket(std::vector<std::uint8_t> inBody, const bool inSelecting)
    {
        asio::post(impl->strand, [this, attempt = impl->attempt.load(), packet = std::move(inBody), inSelecting]() mutable
        {
            if (attempt != impl->attempt.load() || !impl->session) return;
            auto& session = *impl->session;
            if (session.state != TownConnectionState::CharacterSelection
                && session.state != TownConnectionState::EnteringTown) return;
            if (inSelecting)
            {
                session.selectionRequested = true;
                session.SetState(TownConnectionState::EnteringTown, L"저장된 캐릭터 입장 대기");
            }
            session.QueuePacket(std::move(packet));
        });
    }

    void TownClient::RequestCharacterList(std::string inRequestId)
    {
        QueueCharacterPacket(TownProtocol::Encode(TownProtocol::CharacterListRequest{
            nlohmann::json{{"requestId", std::move(inRequestId)}}.dump()}));
    }

    void TownClient::CreateCharacter(std::string inRequestId, std::string inName, const std::uint32_t inDefinitionId)
    {
        QueueCharacterPacket(TownProtocol::Encode(TownProtocol::CharacterCreateRequest{
            nlohmann::json{{"requestId", std::move(inRequestId)}, {"name", std::move(inName)},
                {"characterDefinitionId", inDefinitionId}}.dump()}));
    }

    void TownClient::SelectCharacter(std::string inRequestId, const std::uint64_t inCharacterId,
        const std::uint64_t inOwnerGeneration)
    {
        QueueCharacterPacket(TownProtocol::Encode(TownProtocol::CharacterSelectRequest{
            nlohmann::json{{"requestId", std::move(inRequestId)}, {"characterId", std::to_string(inCharacterId)},
                {"expectedOwnerGeneration", std::to_string(inOwnerGeneration)}}.dump()}), true);
    }

    void TownClient::RequestInventoryState(std::string inRequestId)
    {
        asio::post(impl->strand, [this, attempt = impl->attempt.load(),
            packet = TownProtocol::Encode(TownProtocol::InventoryStateRequest{
                nlohmann::json{{"requestId", std::move(inRequestId)}}.dump()})]() mutable
        {
            if (impl->connected.load() && attempt == impl->attempt.load()) QueuePacket(std::move(packet));
        });
    }

    void TownClient::ChangeInventory(std::string inRequestId, const std::uint64_t inRevision, std::string inAction,
        std::string inInstanceId, const std::uint32_t inCount, const std::optional<bool> inFacingLeft)
    {
        nlohmann::json body{{"requestId", std::move(inRequestId)}, {"revision", std::to_string(inRevision)},
            {"action", std::move(inAction)}, {"instanceId", std::move(inInstanceId)}, {"count", inCount}};
        if (inFacingLeft) body["facingLeft"] = *inFacingLeft;
        asio::post(impl->strand, [this, attempt = impl->attempt.load(),
            packet = TownProtocol::Encode(TownProtocol::InventoryOperationRequest{body.dump()})]() mutable
        {
            if (impl->connected.load() && attempt == impl->attempt.load()) QueuePacket(std::move(packet));
        });
    }

    void TownClient::RequestUseStatus(std::string inRequestId)
    {
        asio::post(impl->strand, [this, attempt = impl->attempt.load(),
            packet = TownProtocol::Encode(TownProtocol::InventoryOperationRequest{
                nlohmann::json{{"action", "UseStatus"}, {"requestId", std::move(inRequestId)}}.dump()})]() mutable
        {
            if (impl->connected.load() && attempt == impl->attempt.load()) QueuePacket(std::move(packet));
        });
    }

    std::vector<CharacterEvent> TownClient::ConsumeCharacterEvents()
    {
        std::scoped_lock lock(impl->mutex);
        std::vector<CharacterEvent> result;
        result.swap(impl->characterEvents);
        return result;
    }

    void TownClient::SendMovement(const TownProtocol::MoveInput& inInput)
    {
        asio::post(impl->strand, [this, attempt = impl->attempt.load(), packet = TownProtocol::Encode(inInput)]() mutable
        {
            if (impl->connected.load() && attempt == impl->attempt.load())
            {
                QueuePacket(std::move(packet));
            }
        });
    }

    void TownClient::RequestDungeon(std::string inZoneId, const std::uint32_t inDungeonId)
    {
        asio::post(impl->strand, [this, attempt = impl->attempt.load(), packet = TownProtocol::Encode(
            TownProtocol::EnterDungeonRequest{ std::move(inZoneId), inDungeonId })]() mutable
        {
            if (impl->connected.load() && attempt == impl->attempt.load())
            {
                QueuePacket(std::move(packet));
            }
        });
    }

    void TownClient::RequestDungeonCompletion(const std::uint64_t inRoomId, const bool inRetry)
    {
        asio::post(impl->strand, [this, attempt = impl->attempt.load(), packet = TownProtocol::Encode(
            TownProtocol::DungeonCompletionRequest{inRoomId, inRetry})]() mutable
        {
            if (impl->connected.load() && attempt == impl->attempt.load()) QueuePacket(std::move(packet));
        });
    }

    void TownClient::RequestQuestState(std::string inRequestId)
    {
        const auto json = nlohmann::json{{"requestId", std::move(inRequestId)}}.dump();
        asio::post(impl->strand, [this, attempt = impl->attempt.load(),
            packet = TownProtocol::Encode(TownProtocol::QuestStateRequest{json})]() mutable
        { if (impl->connected.load() && attempt == impl->attempt.load()) QueuePacket(std::move(packet)); });
    }
    void TownClient::SendQuestAction(std::string inJson)
    {
        asio::post(impl->strand, [this, attempt = impl->attempt.load(),
            packet = TownProtocol::Encode(TownProtocol::QuestActionRequest{std::move(inJson)})]() mutable
        { if (impl->connected.load() && attempt == impl->attempt.load()) QueuePacket(std::move(packet)); });
    }

    void TownClient::RequestSkillState()
    {
        asio::post(impl->strand,[this, attempt = impl->attempt.load(), packet=TownProtocol::Encode(TownProtocol::SkillStateRequest{})]() mutable
        { if (impl->connected.load() && attempt == impl->attempt.load()) QueuePacket(std::move(packet)); });
    }
    void TownClient::LearnSkill(std::string inSkillId,std::uint32_t inExpectedSkillLevel)
    {
        asio::post(impl->strand,[this, attempt = impl->attempt.load(), packet=TownProtocol::Encode(TownProtocol::LearnSkillRequest{std::move(inSkillId),inExpectedSkillLevel})]() mutable
        { if (impl->connected.load() && attempt == impl->attempt.load()) QueuePacket(std::move(packet)); });
    }

    void TownClient::ConfirmDungeonJoin(
        const std::uint64_t inRoomId,
        const std::uint64_t inChallenge)
    {
        asio::post(impl->strand, [this, attempt = impl->attempt.load(), packet = TownProtocol::Encode(
            TownProtocol::ConfirmDungeonJoin{ inRoomId, inChallenge })]() mutable
        {
            if (impl->connected.load() && attempt == impl->attempt.load())
            {
                QueuePacket(std::move(packet));
            }
        });
    }

    void TownClient::InviteToParty(const std::uint64_t inTargetPlayerId)
    {
        asio::post(impl->strand, [this, attempt = impl->attempt.load(), packet = TownProtocol::Encode(
            TownProtocol::PartyInviteRequest{ inTargetPlayerId })]() mutable
        {
            if (impl->connected.load() && attempt == impl->attempt.load())
            {
                QueuePacket(std::move(packet));
            }
        });
    }

    void TownClient::AnswerPartyInvitation(const std::uint64_t inInvitationId,
        const bool inAccepted)
    {
        asio::post(impl->strand, [this, attempt = impl->attempt.load(), packet = TownProtocol::Encode(
            TownProtocol::PartyInviteAnswer{ inInvitationId, inAccepted })]() mutable
        {
            if (impl->connected.load() && attempt == impl->attempt.load())
            {
                QueuePacket(std::move(packet));
            }
        });
    }

    void TownClient::LeaveParty()
    {
        asio::post(impl->strand, [this, attempt = impl->attempt.load(), packet = TownProtocol::Encode(
            TownProtocol::PartyLeaveRequest{})]() mutable
        {
            if (impl->connected.load() && attempt == impl->attempt.load())
            {
                QueuePacket(std::move(packet));
            }
        });
    }

    void TownClient::KickPartyMember(const std::uint64_t inTargetPlayerId)
    {
        asio::post(impl->strand, [this, attempt = impl->attempt.load(), packet = TownProtocol::Encode(
            TownProtocol::PartyKickRequest{ inTargetPlayerId })]() mutable
        {
            if (impl->connected.load() && attempt == impl->attempt.load())
            {
                QueuePacket(std::move(packet));
            }
        });
    }

    void TownClient::UpdatePartySettings(std::string inTitle, const bool inIsPublic)
    {
        asio::post(impl->strand, [this, attempt = impl->attempt.load(), packet = TownProtocol::Encode(
            TownProtocol::PartySettingsRequest{ std::move(inTitle), inIsPublic })]() mutable
        {
            if (impl->connected.load() && attempt == impl->attempt.load())
            {
                QueuePacket(std::move(packet));
            }
        });
    }

    void TownClient::CreateParty(std::string inTitle, const bool inIsPublic)
    {
        asio::post(impl->strand, [this, attempt = impl->attempt.load(), packet = TownProtocol::Encode(
            TownProtocol::PartyCreateRequest{ std::move(inTitle), inIsPublic })]() mutable
        {
            if (impl->connected.load() && attempt == impl->attempt.load())
            {
                QueuePacket(std::move(packet));
            }
        });
    }

    void TownClient::RequestPartyDirectoryPage(const std::uint32_t inPage)
    {
        asio::post(impl->strand, [this, attempt = impl->attempt.load(), packet = TownProtocol::Encode(
            TownProtocol::PartyDirectoryPageRequest{ inPage })]() mutable
        {
            if (impl->connected.load() && attempt == impl->attempt.load())
            {
                QueuePacket(std::move(packet));
            }
        });
    }

    void TownClient::UnsubscribePartyDirectory()
    {
        asio::post(impl->strand, [this, attempt = impl->attempt.load(), packet = TownProtocol::Encode(
            TownProtocol::PartyDirectoryUnsubscribe{})]() mutable
        {
            if (impl->connected.load() && attempt == impl->attempt.load())
            {
                QueuePacket(std::move(packet));
            }
        });
    }

    std::optional<std::uint32_t> TownClient::SendChat(const TownProtocol::ChatChannel inChannel, std::string inMessage)
    {
        if(!IsConnected() || chatSequence==std::numeric_limits<std::uint32_t>::max())return std::nullopt;
        const auto sequence=++chatSequence;
        asio::post(impl->strand,[this,attempt=impl->attempt.load(),packet=TownProtocol::Encode(
            TownProtocol::ChatRequest{sequence,inChannel,std::move(inMessage)})]() mutable
        {
            if(impl->connected.load() && attempt==impl->attempt.load())QueuePacket(std::move(packet));
        });
        return sequence;
    }

    void TownClient::RequestPartyDetail(const std::uint64_t inPartyId)
    {
        asio::post(impl->strand, [this, attempt = impl->attempt.load(), packet = TownProtocol::Encode(
            TownProtocol::PartyDetailRequest{ inPartyId })]() mutable
        {
            if (impl->connected.load() && attempt == impl->attempt.load()) QueuePacket(std::move(packet));
        });
    }

    void TownClient::RequestPartyJoin(const std::uint64_t inPartyId)
    {
        asio::post(impl->strand, [this, attempt = impl->attempt.load(), packet = TownProtocol::Encode(
            TownProtocol::PartyJoinRequest{ inPartyId })]() mutable
        {
            if (impl->connected.load() && attempt == impl->attempt.load()) QueuePacket(std::move(packet));
        });
    }

    void TownClient::AnswerPartyJoin(const std::uint64_t inRequestId, const bool inAccepted)
    {
        asio::post(impl->strand, [this, attempt = impl->attempt.load(), packet = TownProtocol::Encode(
            TownProtocol::PartyJoinAnswer{ inRequestId, inAccepted })]() mutable
        {
            if (impl->connected.load() && attempt == impl->attempt.load()) QueuePacket(std::move(packet));
        });
    }

    void TownClient::Impl::Session::HandlePacket()
    {
        const std::optional<TownProtocol::PacketType> type = TownProtocol::ReadPacketType(receiveBody);
        if (!type.has_value())
        {
            HandleDisconnect();
            return;
        }

        // SessionStatus is legal before admission and during play; handle it before phase packet guards.
        if (*type == TownProtocol::PacketType::SessionStatus)
        {
            const auto packet = TownProtocol::DecodeSessionStatus(receiveBody);
            if (!packet) { HandleDisconnect(); return; }
            const auto code = static_cast<TownSessionStatusCode>(packet->code);
            if (code == TownSessionStatusCode::WaitingForPriorOwner)
            {
                if (state != TownConnectionState::AdmissionPending && state != TownConnectionState::WaitingForPriorOwner)
                    { HandleDisconnect(); return; }
                if (!priorOwnerWaitStarted)
                {
                    priorOwnerWaitStarted = true;
                    deadline.expires_after(PRIOR_OWNER_WAIT_LIMIT);
                    deadline.async_wait([self = shared_from_this()](const asio::error_code& error)
                    {
                        if (!error && self->IsCurrent() && self->state == TownConnectionState::WaitingForPriorOwner)
                            self->Fail(SessionStatusMessage(TownSessionStatusCode::AdmissionTimeout),
                                TownSessionStatusCode::AdmissionTimeout);
                    });
                }
                // Heartbeats update the notice, never the bounded deadline or admission permission.
                SetState(TownConnectionState::WaitingForPriorOwner, SessionStatusMessage(code), code, packet->retryAfterMs);
            }
            else Fail(SessionStatusMessage(code), code);
            return;
        }
        if (state == TownConnectionState::AdmissionPending || state == TownConnectionState::WaitingForPriorOwner)
        {
            const auto result = TownProtocol::DecodeAdmissionResult(receiveBody);
            if (!result || result->result != 0) { HandleDisconnect(); return; }
            deadline.cancel();
            SetState(TownConnectionState::CharacterSelection, L"저장된 캐릭터를 선택해 주세요");
            return;
        }
        if ((state == TownConnectionState::CharacterSelection || state == TownConnectionState::EnteringTown)
            && *type != TownProtocol::PacketType::CharacterListResponse
            && *type != TownProtocol::PacketType::CharacterCreateResponse
            && *type != TownProtocol::PacketType::CharacterSelectResponse
            && (*type != TownProtocol::PacketType::EnterTownResponse || !selectionRequested))
            { HandleDisconnect(); return; }
        if (state == TownConnectionState::Ready && *type == TownProtocol::PacketType::EnterTownResponse)
            { HandleDisconnect(); return; }

        switch (*type)
        {
        case TownProtocol::PacketType::CharacterListResponse:
            if (auto packet = TownProtocol::DecodeCharacterListResponse(receiveBody))
                owner.PushCharacterEvent(attemptId, {CharacterEvent::Kind::List, std::move(packet->json)});
            else HandleDisconnect();
            break;
        case TownProtocol::PacketType::CharacterCreateResponse:
            if (auto packet = TownProtocol::DecodeCharacterCreateResponse(receiveBody))
                owner.PushCharacterEvent(attemptId, {CharacterEvent::Kind::Create, std::move(packet->json)});
            else HandleDisconnect();
            break;
        case TownProtocol::PacketType::CharacterSelectResponse:
            if (auto packet = TownProtocol::DecodeCharacterSelectResponse(receiveBody))
            {
                owner.PushCharacterEvent(attemptId, {CharacterEvent::Kind::Select, std::move(packet->json)});
                if (state != TownConnectionState::Ready)
                    SetState(TownConnectionState::CharacterSelection, L"캐릭터 선택 응답 확인");
            }
            else HandleDisconnect();
            break;
        case TownProtocol::PacketType::InventoryStateResponse:
            if (auto packet = TownProtocol::DecodeInventoryStateResponse(receiveBody))
                PushEvent(InventoryEvent{InventoryEvent::Kind::State, std::move(packet->json)});
            else HandleDisconnect();
            break;
        case TownProtocol::PacketType::InventoryOperationResponse:
            if (auto packet = TownProtocol::DecodeInventoryOperationResponse(receiveBody))
                PushEvent(InventoryEvent{InventoryEvent::Kind::Operation, std::move(packet->json)});
            else HandleDisconnect();
            break;
        case TownProtocol::PacketType::ItemDefinitionsResponse:
            if (auto packet = TownProtocol::DecodeItemDefinitionsResponse(receiveBody))
                PushEvent(InventoryEvent{InventoryEvent::Kind::Definitions, std::move(packet->json)});
            else HandleDisconnect();
            break;
        case TownProtocol::PacketType::QuestStatePage:
            if (auto packet = TownProtocol::DecodeQuestStatePage(receiveBody))
                PushEvent(QuestEvent{QuestEvent::Kind::StatePage, std::move(packet->json)});
            else HandleDisconnect();
            break;
        case TownProtocol::PacketType::QuestActionResult:
            if (auto packet = TownProtocol::DecodeQuestActionResult(receiveBody))
                PushEvent(QuestEvent{QuestEvent::Kind::ActionResult, std::move(packet->json)});
            else HandleDisconnect();
            break;
        case TownProtocol::PacketType::SkillStateResponse:
            if (auto packet=TownProtocol::DecodeSkillStateResponse(receiveBody)) PushEvent(SkillStateEvent{std::move(packet->payload)});
            else HandleDisconnect();
            break;
        case TownProtocol::PacketType::EnterTownResponse:
            if (auto packet = TownProtocol::DecodeEnterTownResponse(receiveBody))
            {
                PushEvent(std::move(*packet));
                deadline.cancel();
                SetState(TownConnectionState::Ready, L"타운 입장 완료");
            }
            else HandleDisconnect();
            break;
        case TownProtocol::PacketType::PlayerAppear:
            if (auto packet = TownProtocol::DecodePlayerAppear(receiveBody)) PushEvent(std::move(*packet));
            else HandleDisconnect();
            break;
        case TownProtocol::PacketType::PlayerMove:
            if (auto packet = TownProtocol::DecodePlayerMove(receiveBody)) PushEvent(std::move(*packet));
            else HandleDisconnect();
            break;
        case TownProtocol::PacketType::PlayerDisappear:
            if (auto packet = TownProtocol::DecodePlayerDisappear(receiveBody)) PushEvent(std::move(*packet));
            else HandleDisconnect();
            break;
        case TownProtocol::PacketType::EnterDungeonResponse:
            if (auto packet = TownProtocol::DecodeEnterDungeonResponse(receiveBody)) PushEvent(std::move(*packet));
            else HandleDisconnect();
            break;
        case TownProtocol::PacketType::DungeonCompletionResponse:
            if (auto packet = TownProtocol::DecodeDungeonCompletionResponse(receiveBody)) PushEvent(std::move(*packet));
            else HandleDisconnect();
            break;
        case TownProtocol::PacketType::MapChanged:
            if (auto packet = TownProtocol::DecodeMapChanged(receiveBody)) PushEvent(std::move(*packet));
            else HandleDisconnect();
            break;
        case TownProtocol::PacketType::DungeonSelectionOpen:
            if (auto packet = TownProtocol::DecodeDungeonSelectionOpen(receiveBody)) PushEvent(std::move(*packet));
            else HandleDisconnect();
            break;
        case TownProtocol::PacketType::ChatResult:
            if(auto packet=TownProtocol::DecodeChatResult(receiveBody))PushEvent(std::move(*packet));
            else HandleDisconnect();
            break;
        case TownProtocol::PacketType::ChatMessage:
            if(auto packet=TownProtocol::DecodeChatMessage(receiveBody))PushEvent(std::move(*packet));
            else HandleDisconnect();
            break;
        case TownProtocol::PacketType::PartyInvitation:
            if (auto packet = TownProtocol::DecodePartyInvitation(receiveBody)) PushEvent(std::move(*packet));
            else HandleDisconnect();
            break;
        case TownProtocol::PacketType::PartySnapshot:
            if (auto packet = TownProtocol::DecodePartySnapshot(receiveBody)) PushEvent(std::move(*packet));
            else HandleDisconnect();
            break;
        case TownProtocol::PacketType::PartyOperationResult:
            if (auto packet = TownProtocol::DecodePartyOperationResult(receiveBody)) PushEvent(std::move(*packet));
            else HandleDisconnect();
            break;
        case TownProtocol::PacketType::PartyDirectoryPage:
            if (auto packet = TownProtocol::DecodePartyDirectoryPage(receiveBody)) PushEvent(std::move(*packet));
            else HandleDisconnect();
            break;
        case TownProtocol::PacketType::PartyDetailResponse:
            if (auto packet = TownProtocol::DecodePartyDetailResponse(receiveBody)) PushEvent(std::move(*packet));
            else HandleDisconnect();
            break;
        case TownProtocol::PacketType::PartyJoinRequestUpdate:
            if (auto packet = TownProtocol::DecodePartyJoinRequestUpdate(receiveBody)) PushEvent(std::move(*packet));
            else HandleDisconnect();
            break;
        case TownProtocol::PacketType::PartyKicked:
            if (auto packet = TownProtocol::DecodePartyKicked(receiveBody)) PushEvent(std::move(*packet));
            else HandleDisconnect();
            break;
        case TownProtocol::PacketType::PartyDirectoryChanged:
            if (auto packet = TownProtocol::DecodePartyDirectoryChanged(receiveBody)) PushEvent(std::move(*packet));
            else HandleDisconnect();
            break;
        default:
            HandleDisconnect();
            break;
        }
    }

}
