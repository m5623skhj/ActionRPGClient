#include "Network/TownClient.h"
#include <asio.hpp>
#include <asio/ssl.hpp>
#include <openssl/ssl.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <deque>
#include <fstream>
#include <future>
#include <iterator>
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
        std::thread worker;
        Impl() : worker([this] { ioContext.run(); }) {}
        ~Impl();
        void SetStatus(std::uint64_t inAttempt, TownConnectionState inState, std::wstring inMessage);
        void PushEvent(std::uint64_t inAttempt, TownEvent inEvent);
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
        std::string ticket, playerName;
        std::uint32_t characterId;
        TownConnectionState state{ TownConnectionState::Connecting };
        bool closed{};
        std::array<std::uint8_t, 4> receiveHeader{};
        std::vector<std::uint8_t> receiveBody;
        std::deque<std::shared_ptr<std::vector<std::uint8_t>>> sendQueue;

        Session(Impl& inOwner, const std::uint64_t inAttempt, TownServerSettings inServer,
            const std::filesystem::path& inCa, std::string inTicket, std::string inName,
            const std::uint32_t inCharacter)
            : owner(inOwner), attemptId(inAttempt), tlsContext(MakeTlsContext(inCa)),
              socket(owner.strand, tlsContext), resolver(owner.strand), deadline(owner.strand),
              server(std::move(inServer)), ticket(std::move(inTicket)), playerName(std::move(inName)),
              characterId(inCharacter) {}

        bool IsCurrent() const { return !closed && owner.attempt.load() == attemptId; }
        void SetState(const TownConnectionState inState, std::wstring inMessage)
        {
            state = inState;
            owner.SetStatus(attemptId, state, std::move(inMessage));
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
        void HandleDisconnect()
        {
            if (!IsCurrent()) { Close(); return; }
            SetState(TownConnectionState::Failed,
                L"타운 연결이 종료되거나 입장이 완료되지 않았습니다. 새 티켓으로 다시 시도해 주세요.");
            Close();
        }
        void Start(const Clock::time_point inExpiry)
        {
            if (!IsCurrent() || Clock::now() >= inExpiry || ticket.size() != 64
                || !std::all_of(ticket.begin(), ticket.end(), [](char value)
                    { return (value >= '0' && value <= '9') || (value >= 'a' && value <= 'f'); })
                || playerName.empty() || playerName.size() > 32 || characterId < 1 || characterId > 3)
                { HandleDisconnect(); return; }
            socket.set_verify_callback(asio::ssl::host_name_verification(server.hostname));
            if (SSL_set_tlsext_host_name(socket.native_handle(), server.hostname.c_str()) != 1)
                { HandleDisconnect(); return; }
            deadline.expires_at(std::min(inExpiry, Clock::now() + std::chrono::seconds(15)));
            deadline.async_wait([self = shared_from_this()](const asio::error_code& inError)
                { if (!inError) self->HandleDisconnect(); });
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
        asio::post(strand, [this]
        {
            if (session) session->Close();
            session.reset();
            workGuard.reset();
            ioContext.stop();
        });
        if (worker.joinable()) worker.join();
    }
    void TownClient::Impl::SetStatus(const std::uint64_t inAttempt,
        const TownConnectionState inState, std::wstring inMessage)
    {
        std::scoped_lock lock(mutex);
        if (attempt.load() != inAttempt) return;
        status = { inAttempt, inState, std::move(inMessage) };
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
    TownClient::TownClient() : impl(std::make_unique<Impl>()) {}
    TownClient::~TownClient() = default;

    void TownClient::Start(const std::uint64_t inAttempt, TownServerSettings inServer,
        std::filesystem::path inCa, std::string inTicket, std::string inName,
        const std::uint32_t inCharacter, const Clock::time_point inExpiry)
    {
        {
            std::scoped_lock lock(impl->mutex);
            impl->attempt.store(inAttempt);
            impl->connected.store(false);
            impl->events.clear();
            impl->status = { inAttempt, TownConnectionState::Connecting, L"타운 입장: TLS 연결 중" };
        }
        asio::post(impl->strand, [this, inAttempt, server = std::move(inServer), ca = std::move(inCa),
            ticket = std::move(inTicket), name = std::move(inName), inCharacter, inExpiry]() mutable
        {
            if (impl->session) impl->session->Close();
            impl->session.reset();
            if (impl->attempt.load() != inAttempt) return;
            try
            {
                impl->session = std::make_shared<Impl::Session>(*impl, inAttempt, std::move(server), ca,
                    std::move(ticket), std::move(name), inCharacter);
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
        {
            std::scoped_lock lock(impl->mutex);
            impl->attempt.store(0);
            impl->connected.store(false);
            impl->events.clear();
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

        if (state == TownConnectionState::AdmissionPending)
        {
            const auto result = TownProtocol::DecodeAdmissionResult(receiveBody);
            if (!result || result->result != 0) { HandleDisconnect(); return; }
            SetState(TownConnectionState::EnteringTown, L"타운 입장: 캐릭터 생성 대기");
            QueuePacket(TownProtocol::Encode(TownProtocol::EnterTownRequest{ playerName, characterId }));
            return;
        }
        if (state == TownConnectionState::EnteringTown && *type != TownProtocol::PacketType::EnterTownResponse)
            { HandleDisconnect(); return; }
        if (state == TownConnectionState::Ready && *type == TownProtocol::PacketType::EnterTownResponse)
            { HandleDisconnect(); return; }

        switch (*type)
        {
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
