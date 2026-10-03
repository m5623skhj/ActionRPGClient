#include "Network/DungeonClient.h"

#include "Network/DungeonProtocol.h"

#include <WinSock2.h>
#include <Windows.h>

#include <RUDPClientCore.h>

#include <array>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <utility>

namespace
{
    constexpr std::size_t MAX_IPV4_ADDRESS_LENGTH = 15;
    constexpr int MAX_PACKETS_PER_UPDATE = 64;

    [[nodiscard]] std::filesystem::path GetExecutableDirectory()
    {
        std::array<wchar_t, MAX_PATH> executablePath{};
        const DWORD length = GetModuleFileNameW(
            nullptr, executablePath.data(), static_cast<DWORD>(executablePath.size()));
        if (length == 0 || length == executablePath.size())
        {
            return {};
        }
        return std::filesystem::path(executablePath.data()).parent_path();
    }

    [[nodiscard]] std::optional<std::filesystem::path> WriteSessionBrokerOptions(
        const std::string& inAddress,
        const std::uint16_t inPort)
    {
        if (inAddress.empty() || inAddress.size() > MAX_IPV4_ADDRESS_LENGTH || inPort == 0)
        {
            return std::nullopt;
        }

        const std::filesystem::path path = std::filesystem::temp_directory_path()
            / (L"ActionRPGClient-SessionBroker-" + std::to_wstring(GetCurrentProcessId()) + L"-"
                + std::to_wstring(std::chrono::steady_clock::now().time_since_epoch().count()) + L".txt");
        const std::wstring address(inAddress.begin(), inAddress.end());
        const std::wstring contents = L"\xFEFF:SESSION_BROKER\r\n{\r\n\tIP = \"" + address
            + L"\"\r\n\tPORT = " + std::to_wstring(inPort)
            + L"\r\n}\r\n\r\n:SERIALIZEBUF\r\n{\r\n\tPACKET_CODE = 119\r\n"
              L"\tPACKET_KEY = 50\r\n}\r\n";

        static_assert(sizeof(wchar_t) == 2);
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        if (!output)
        {
            return std::nullopt;
        }
        output.write(reinterpret_cast<const char*>(contents.data()),
            static_cast<std::streamsize>(contents.size() * sizeof(wchar_t)));
        if (!output)
        {
            std::error_code ignoredError;
            std::filesystem::remove(path, ignoredError);
            return std::nullopt;
        }
        return path;
    }
}

namespace ActionRPG
{
    class DungeonClient::Impl final : public RUDPClientCore
    {
    public:
        ~Impl()
        {
            StopClient();
        }

        bool StartClient(std::string inSessionBrokerAddress, const std::uint16_t inSessionBrokerPort)
        {
            const DungeonConnectionState currentState = GetConnectionState();
            if (currentState != DungeonConnectionState::Stopped
                && currentState != DungeonConnectionState::Failed)
            {
                return false;
            }

            if (connectionThread.joinable())
            {
                connectionThread.join();
            }
            if (!IsStopped())
            {
                RUDPClientCore::Stop();
            }

            startCompleted.store(false, std::memory_order_release);
            state.store(DungeonConnectionState::Connecting, std::memory_order_release);
            try
            {
                connectionThread = std::thread(
                    [this, address = std::move(inSessionBrokerAddress), inSessionBrokerPort]()
                    {
                        std::optional<std::filesystem::path> brokerOptions;
                        bool started = false;
                        try
                        {
                            const std::filesystem::path executableDirectory = GetExecutableDirectory();
                            const std::filesystem::path coreOptions = executableDirectory
                                / L"ClientOptionFile" / L"CoreOption.txt";
                            brokerOptions = WriteSessionBrokerOptions(address, inSessionBrokerPort);
                            if (!executableDirectory.empty() && brokerOptions.has_value())
                            {
                                started = RUDPClientCore::Start(
                                    coreOptions.wstring(), brokerOptions->wstring(), false);
                            }
                        }
                        catch (...)
                        {
                            started = false;
                        }
                        if (brokerOptions.has_value())
                        {
                            std::error_code ignoredError;
                            std::filesystem::remove(*brokerOptions, ignoredError);
                        }
                        if (!started)
                        {
                            state.store(DungeonConnectionState::Failed, std::memory_order_release);
                        }
                        startCompleted.store(true, std::memory_order_release);
                    });
            }
            catch (...)
            {
                state.store(DungeonConnectionState::Failed, std::memory_order_release);
                startCompleted.store(true, std::memory_order_release);
                return false;
            }
            return true;
        }

        void StopClient()
        {
            if (connectionThread.joinable())
            {
                connectionThread.join();
            }
            RUDPClientCore::Stop();
            startCompleted.store(false, std::memory_order_release);
            state.store(DungeonConnectionState::Stopped, std::memory_order_release);
        }

        [[nodiscard]] DungeonConnectionState GetConnectionState() const
        {
            const DungeonConnectionState currentState = state.load(std::memory_order_acquire);
            if (currentState == DungeonConnectionState::Stopped
                || currentState == DungeonConnectionState::Failed)
            {
                return currentState;
            }
            if (IsConnected())
            {
                return DungeonConnectionState::Connected;
            }
            if (startCompleted.load(std::memory_order_acquire) && IsStopped())
            {
                return DungeonConnectionState::Failed;
            }
            return DungeonConnectionState::Connecting;
        }

    private:
        std::thread connectionThread;
        std::atomic<DungeonConnectionState> state = DungeonConnectionState::Stopped;
        std::atomic_bool startCompleted{};
    };

    DungeonClient::DungeonClient()
        : impl(std::make_unique<Impl>())
    {
    }

    DungeonClient::~DungeonClient()
    {
        Stop();
    }

    bool DungeonClient::Start(
        std::string inSessionBrokerAddress,
        const std::uint16_t inSessionBrokerPort)
    {
        if (!IsStopComplete()) return false;
        const DungeonConnectionState connectionState = impl->GetConnectionState();
        if (connectionState != DungeonConnectionState::Stopped
            && connectionState != DungeonConnectionState::Failed)
        {
            return false;
        }
        impl = std::make_unique<Impl>();
        return impl->StartClient(std::move(inSessionBrokerAddress), inSessionBrokerPort);
    }

    void DungeonClient::Stop()
    {
        if (stopTask.valid()) stopTask.get();
        impl->StopClient();
        ResetRealtime();
        worldJson.clear(); worldBytes = 0; receivingWorld = false;
        combatJson.clear(); combatSnapshotId = combatBytes = combatOffset = 0;
        lastCompletedCombatId = 0;
        combatPolling = combatRequestPending = false;
    }

    // Game-thread API: only the worker touches impl while this future is active.
    void DungeonClient::RequestStop()
    {
        if (stopTask.valid()) return;
        ResetRealtime();
        combatPolling = combatRequestPending = receivingWorld = false;
        stopTask = std::async(std::launch::async, [this]() { impl->StopClient(); });
    }

    bool DungeonClient::IsStopComplete()
    {
        if (!stopTask.valid()) return true;
        if (stopTask.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return false;
        stopTask.get();
        return true;
    }

    void DungeonClient::RequestWorld()
    {
        worldJson.clear(); worldBytes = 0; receivingWorld = true;
        worldDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        DungeonProtocol::DungeonWorldRequest request; request.offset = 0; SendReliable(request);
    }

    DungeonConnectionState DungeonClient::GetConnectionState() const
    {
        if (stopTask.valid()) return DungeonConnectionState::Stopped;
        if (receivingWorld && std::chrono::steady_clock::now() > worldDeadline) return DungeonConnectionState::Failed;
        if (combatPolling && !HasFreshRealtime() && std::chrono::steady_clock::now() > combatProgressDeadline) return DungeonConnectionState::Failed;
        return impl->GetConnectionState();
    }

    void DungeonClient::StartCombatPolling()
    {
        combatJson.clear(); combatSnapshotId = combatBytes = combatOffset = 0;
        lastCompletedCombatId = 0;
        combatPolling = true; combatRequestPending = false;
        combatNextRequest = std::chrono::steady_clock::now();
        combatProgressDeadline = combatNextRequest + std::chrono::seconds(30);
    }

    void DungeonClient::ResetRealtime()
    {
        realtimeAssemblies.clear();
        realtimeChallenge = realtimeRoomId = realtimeSequence = realtimeTick = realtimeTime = 0;
        realtimeDungeonId = realtimeMapEpoch = 0;
        realtimeRequested = realtimeSeen = realtimeFallbackPending = false; realtimeLastFrame = {};
    }

    void DungeonClient::StartRealtime(const std::uint64_t inRoomId)
    {
        if (inRoomId == 0 || realtimeChallenge == 0 || GetConnectionState() != DungeonConnectionState::Connected) return;
        realtimeRoomId = inRoomId; realtimeRequested = true;
        DungeonProtocol::DungeonRealtimeRequest request;
        request.version = 1; request.enabled = 1; request.challenge = realtimeChallenge;
        SendReliable(request);
    }

    bool DungeonClient::HasFreshRealtime() const
    {
        return realtimeSeen && std::chrono::steady_clock::now() - realtimeLastFrame < std::chrono::milliseconds(500);
    }

    // The RUDP worker owns its queues; ConsumeEvents owns parsing/history on the game thread.
    // Incomplete frames never reach GameWorld. Keep at most two 48KiB assemblies.
    void DungeonClient::ReceiveRealtime(DungeonProtocol::DungeonRealtimeChunk packet, std::vector<DungeonEvent>& outEvents)
    {
        constexpr std::uint32_t MAX_REALTIME_BYTES = 48 * 1024;
        constexpr std::uint32_t REALTIME_CHUNK_BYTES = 768;
        if (!realtimeRequested || receivingWorld || packet.version != 1 || packet.challenge != realtimeChallenge
            || packet.roomId != realtimeRoomId || packet.dungeonId == 0 || packet.mapEpoch == 0
            || packet.snapshotSequence == 0 || packet.serverTimeMs == 0 || packet.state > 3
            || packet.mapId.empty() || packet.mapId.size() > 64 || packet.mapId.find('\0') != std::string::npos
            || packet.totalBytes < 6 || packet.totalBytes > MAX_REALTIME_BYTES
            || packet.offset >= packet.totalBytes || packet.offset % REALTIME_CHUNK_BYTES != 0
            || packet.payload.size() != std::min(REALTIME_CHUNK_BYTES, packet.totalBytes - packet.offset)
            || (realtimeDungeonId != 0 && realtimeDungeonId != packet.dungeonId)
            || packet.mapEpoch < realtimeMapEpoch || packet.snapshotSequence <= realtimeSequence) return;
        const auto now = std::chrono::steady_clock::now();
        std::erase_if(realtimeAssemblies, [now](const auto& assembly)
            { return now - assembly.created > std::chrono::milliseconds(500); });
        if (packet.mapEpoch > realtimeMapEpoch)
        {
            realtimeMapEpoch = packet.mapEpoch;
            realtimeAssemblies.clear();
            outEvents.emplace_back(DungeonRealtimeResetEvent{packet.mapEpoch});
        }
        auto assembly = std::find_if(realtimeAssemblies.begin(), realtimeAssemblies.end(), [&packet](const auto& value)
            { return value.metadata.snapshotSequence == packet.snapshotSequence; });
        if (assembly == realtimeAssemblies.end())
        {
            if (realtimeAssemblies.size() == 2 && packet.snapshotSequence < realtimeAssemblies.front().metadata.snapshotSequence) return;
            RealtimeAssembly value;
            value.metadata = packet; value.metadata.payload.clear(); value.created = now;
            value.bytes.resize(packet.totalBytes);
            value.received.resize((packet.totalBytes + REALTIME_CHUNK_BYTES - 1) / REALTIME_CHUNK_BYTES);
            const auto position = std::find_if(realtimeAssemblies.begin(), realtimeAssemblies.end(), [&packet](const auto& current)
                { return current.metadata.snapshotSequence > packet.snapshotSequence; });
            realtimeAssemblies.insert(position, std::move(value));
            if (realtimeAssemblies.size() > 2) realtimeAssemblies.pop_front();
            assembly = std::find_if(realtimeAssemblies.begin(), realtimeAssemblies.end(), [&packet](const auto& current)
                { return current.metadata.snapshotSequence == packet.snapshotSequence; });
            if (assembly == realtimeAssemblies.end()) return;
        }
        const auto& metadata = assembly->metadata;
        if (metadata.dungeonId != packet.dungeonId || metadata.mapId != packet.mapId || metadata.mapEpoch != packet.mapEpoch || metadata.totalBytes != packet.totalBytes
            || metadata.serverTick != packet.serverTick || metadata.serverTimeMs != packet.serverTimeMs || metadata.state != packet.state)
        { realtimeAssemblies.erase(assembly); return; }
        const auto index = packet.offset / REALTIME_CHUNK_BYTES;
        if (assembly->received[index]) return;
        assembly->bytes.replace(packet.offset, packet.payload.size(), packet.payload);
        assembly->received[index] = true;
        if (++assembly->receivedCount != assembly->received.size()) return;
        if (packet.serverTick < realtimeTick || packet.serverTimeMs < realtimeTime)
        { realtimeAssemblies.erase(assembly); return; }
        try
        {
            auto snapshot = DungeonCombatSnapshot::ParseRealtime(assembly->bytes);
            constexpr const char* STATES[] = {"WaitingForPlayers", "Running", "Cleared", "Stopped"};
            snapshot.roomId = packet.roomId; snapshot.mapEpoch = packet.mapEpoch; snapshot.mapId = packet.mapId;
            snapshot.serverTick = packet.serverTick; snapshot.serverTimeMs = packet.serverTimeMs;
            snapshot.snapshotSequence = packet.snapshotSequence; snapshot.state = STATES[packet.state]; snapshot.cleared = packet.state == 2;
            realtimeDungeonId = packet.dungeonId;
            realtimeSequence = packet.snapshotSequence; realtimeTick = packet.serverTick; realtimeTime = packet.serverTimeMs;
            realtimeSeen = realtimeFallbackPending = true; realtimeLastFrame = now;
            combatProgressDeadline = now + std::chrono::seconds(30);
            outEvents.emplace_back(DungeonRealtimeEvent{std::move(snapshot)});
            while (!realtimeAssemblies.empty() && realtimeAssemblies.front().metadata.snapshotSequence <= realtimeSequence)
                realtimeAssemblies.pop_front();
        }
        catch (const std::exception&)
        {
            realtimeAssemblies.erase(assembly);
        }
    }

    void DungeonClient::SendAction(const std::uint32_t inSequence, const std::uint8_t inAction, const bool inFacingLeft)
    {
        if (!combatPolling || receivingWorld || inSequence == 0 || (inAction != 1 && inAction != 2)) return;
        DungeonProtocol::DungeonActionInput request;
        request.sequence = inSequence; request.action = inAction; request.facingLeft = inFacingLeft ? 1 : 0;
        SendReliable(request);
    }

    // Called only by ConsumeEvents on the game thread. Exactly one chunk request is outstanding.
    void DungeonClient::UpdateCombatPolling()
    {
        if (!combatPolling || GetConnectionState() != DungeonConnectionState::Connected) return;
        const auto now = std::chrono::steady_clock::now();
        if (combatRequestPending)
        {
            if (now < combatResponseDeadline) return;
            combatRequestPending = false;
        }
        if (!HasFreshRealtime() && realtimeFallbackPending && !combatRequestPending)
        {
            combatNextRequest = std::min(combatNextRequest, now);
            realtimeFallbackPending = false;
        }
        if (now < combatNextRequest) return;
        DungeonProtocol::DungeonCombatStateRequest request;
        request.snapshotId = combatSnapshotId; request.offset = combatOffset;
        SendReliable(request);
        combatRequestPending = true;
        combatResponseDeadline = now + std::chrono::seconds(5);
    }

    void DungeonClient::SendReliable(IPacket& inPacket)
    {
        if (GetConnectionState() == DungeonConnectionState::Connected)
        {
            impl->SendPacket(inPacket);
        }
    }

    bool DungeonClient::SendUnreliable(IPacket& inPacket)
    {
        return GetConnectionState() == DungeonConnectionState::Connected
            && impl->SendUnreliablePacket(inPacket);
    }

    std::vector<DungeonEvent> DungeonClient::ConsumeEvents()
    {
        if (stopTask.valid()) return {};
        const auto now = std::chrono::steady_clock::now();
        std::erase_if(realtimeAssemblies, [now](const auto& assembly)
            { return now - assembly.created > std::chrono::milliseconds(500); });
        std::vector<DungeonEvent> events;
        bool invalidPacket = false;

        for (int packetCount = 0; packetCount < MAX_PACKETS_PER_UPDATE; ++packetCount)
        {
            NetBuffer* const receivedBuffer = impl->GetReceivedPacket();
            if (receivedBuffer == nullptr)
            {
                break;
            }
            const std::unique_ptr<NetBuffer, decltype(&NetBuffer::Free)> buffer(
                receivedBuffer, &NetBuffer::Free);

            ::PacketId packetId{};
            *buffer >> packetId;
            switch (static_cast<DungeonProtocol::PacketType>(packetId))
            {
            case DungeonProtocol::PacketType::DUNGEON_CHALLENGE:
            {
                DungeonProtocol::DungeonChallenge packet;
                packet.BufferToPacket(*buffer);
                if (packet.challenge == 0)
                {
                    invalidPacket = true;
                }
                else
                {
                    realtimeChallenge = packet.challenge;
                    events.emplace_back(DungeonChallengeEvent{ packet.challenge });
                }
                break;
            }
            case DungeonProtocol::PacketType::DUNGEON_AUTH_RESULT:
            {
                DungeonProtocol::DungeonAuthResult packet;
                packet.BufferToPacket(*buffer);
                if (packet.succeeded > 1)
                {
                    invalidPacket = true;
                }
                else
                {
                    events.emplace_back(DungeonAuthResultEvent{ packet.succeeded != 0 });
                }
                break;
            }
            case DungeonProtocol::PacketType::DUNGEON_REALTIME_RESULT:
            {
                DungeonProtocol::DungeonRealtimeResult packet; packet.BufferToPacket(*buffer);
                if (buffer->GetBufferError() != 0 || buffer->GetUseSize() != 0) { invalidPacket = true; break; }
                if (!realtimeRequested || packet.challenge != realtimeChallenge || packet.roomId != realtimeRoomId) break;
                if (packet.version != 1 || packet.accepted != 1 || packet.dungeonId == 0
                    || packet.tickIntervalMs != 50 || packet.snapshotIntervalMs < 50 || packet.snapshotIntervalMs > 100
                    || (realtimeDungeonId != 0 && realtimeDungeonId != packet.dungeonId))
                { realtimeRequested = realtimeSeen = false; realtimeAssemblies.clear(); break; }
                realtimeDungeonId = packet.dungeonId;
                break;
            }
            case DungeonProtocol::PacketType::DUNGEON_WORLD_CHUNK:
            {
                DungeonProtocol::DungeonWorldChunk packet;
                packet.BufferToPacket(*buffer);
                if (!receivingWorld || packet.totalBytes == 0 || packet.totalBytes > 4 * 1024 * 1024
                    || (worldBytes != 0 && worldBytes != packet.totalBytes) || packet.offset != worldJson.size()
                    || packet.payload.empty() || packet.payload.size() > 768
                    || packet.payload.size() > packet.totalBytes - std::min(packet.offset, packet.totalBytes)
                    || buffer->GetBufferError() != 0 || buffer->GetUseSize() != 0)
                { invalidPacket = true; break; }
                worldBytes = packet.totalBytes; worldJson += packet.payload;
                worldDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
                if (worldJson.size() == worldBytes)
                {
                    receivingWorld = false;
                    events.emplace_back(DungeonWorldEvent{std::move(worldJson)});
                    worldJson.clear();
                }
                else
                {
                    DungeonProtocol::DungeonWorldRequest request;
                    request.offset = static_cast<std::uint32_t>(worldJson.size()); SendReliable(request);
                }
                break;
            }
            case DungeonProtocol::PacketType::DUNGEON_PLAYER_STATE:
            {
                DungeonProtocol::DungeonPlayerState packet;
                packet.BufferToPacket(*buffer);
                if (receivingWorld || packet.sequence == 0 || packet.mapId.empty() || packet.mapId.size() > 64
                    || !std::isfinite(packet.x) || !std::isfinite(packet.y) || std::abs(packet.x) > 1000000 || std::abs(packet.y) > 1000000)
                    invalidPacket = true;
                else events.emplace_back(DungeonPlayerStateEvent{packet.sequence, std::move(packet.mapId), packet.x, packet.y});
                break;
            }
            case DungeonProtocol::PacketType::DUNGEON_ACTION_RESULT:
            {
                DungeonProtocol::DungeonActionResult packet; packet.BufferToPacket(*buffer);
                if (!combatPolling || packet.sequence == 0 || packet.accepted > 1) invalidPacket = true;
                else events.emplace_back(DungeonActionResultEvent{packet.sequence, packet.accepted != 0, packet.serverTick});
                break;
            }
            case DungeonProtocol::PacketType::DUNGEON_COMBAT_STATE_CHUNK:
            {
                DungeonProtocol::DungeonCombatStateChunk packet; packet.BufferToPacket(*buffer);
                if (!combatPolling || packet.status > 3 || packet.totalBytes > 512 * 1024
                    || packet.payload.size() > 768 || packet.retryAfterMs > 60000
                    || buffer->GetBufferError() != 0 || buffer->GetUseSize() != 0)
                { invalidPacket = true; break; }
                const auto now = std::chrono::steady_clock::now();
                if (packet.snapshotId != 0 && packet.snapshotId == lastCompletedCombatId) break;
                if (packet.status == 2 || packet.status == 3)
                {
                    combatJson.clear(); combatSnapshotId = combatBytes = combatOffset = 0;
                    combatRequestPending = false; combatNextRequest = now + std::chrono::milliseconds(200);
                    break;
                }
                // A delayed retransmission for an older completed stream cannot replace the active one.
                if (combatSnapshotId != 0 && packet.snapshotId != combatSnapshotId) break;
                if (packet.status == 1)
                {
                    if (!packet.payload.empty() || packet.offset != combatOffset) break;
                    if (packet.snapshotId != 0) combatSnapshotId = packet.snapshotId;
                    combatRequestPending = false;
                    combatNextRequest = now + std::chrono::milliseconds(std::max(1U, packet.retryAfterMs));
                    combatProgressDeadline = combatNextRequest + std::chrono::seconds(30);
                    break;
                }
                if (packet.offset < combatOffset) break;
                if (!combatRequestPending || packet.snapshotId == 0 || packet.totalBytes == 0
                    || packet.offset != combatOffset || packet.payload.empty()
                    || packet.offset > packet.totalBytes || packet.payload.size() > packet.totalBytes - packet.offset
                    || (combatBytes != 0 && combatBytes != packet.totalBytes))
                { invalidPacket = true; break; }
                combatSnapshotId = packet.snapshotId; combatBytes = packet.totalBytes;
                combatJson += packet.payload; combatOffset = static_cast<std::uint32_t>(combatJson.size());
                combatRequestPending = false; combatNextRequest = now;
                combatProgressDeadline = now + std::chrono::seconds(30);
                if (combatOffset == combatBytes)
                {
                    lastCompletedCombatId = combatSnapshotId;
                    events.emplace_back(DungeonCombatEvent{std::move(combatJson)});
                    combatJson.clear(); combatSnapshotId = combatBytes = combatOffset = 0;
                    combatNextRequest = now + std::chrono::milliseconds(HasFreshRealtime() ? 1000 : 200);
                }
                break;
            }
            default:
                invalidPacket = true;
                break;
            }

            if (buffer->GetBufferError() != 0 || buffer->GetUseSize() != 0 || invalidPacket)
            {
                invalidPacket = true;
                break;
            }
        }

        for (int packetCount = 0; packetCount < MAX_PACKETS_PER_UPDATE; ++packetCount)
        {
            NetBuffer* const received = impl->GetReceivedUnreliablePacket();
            if (received == nullptr) break;
            const std::unique_ptr<NetBuffer, decltype(&NetBuffer::Free)> buffer(received, &NetBuffer::Free);
            ::PacketId packetId{}; *buffer >> packetId;
            if (static_cast<DungeonProtocol::PacketType>(packetId) != DungeonProtocol::PacketType::DUNGEON_REALTIME_CHUNK) continue;
            DungeonProtocol::DungeonRealtimeChunk packet; packet.BufferToPacket(*buffer);
            // Unreliable malformed/stale data is discarded; reliable recovery remains available.
            if (buffer->GetBufferError() != 0 || buffer->GetUseSize() != 0) continue;
            ReceiveRealtime(std::move(packet), events);
        }

        if (invalidPacket)
        {
            Stop();
            events.clear();
        }
        else UpdateCombatPolling();
        return events;
    }
}
