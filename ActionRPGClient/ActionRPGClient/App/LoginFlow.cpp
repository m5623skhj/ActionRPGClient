#include "App/LoginFlow.h"
#include "Graphics/D2DRenderer.h"
#include "Network/DungeonClient.h"
#include "Network/TownClient.h"
#include "Network/CharacterInventoryJson.h"
#include <unordered_set>
#include "Resources/AssetCatalog.h"
#include <d2d1_1helper.h>
#include <algorithm>
#include <utility>

namespace
{
    struct Layout
    {
        float left, top, right;
        explicit Layout(float inWidth, float inHeight)
            : left(std::max(12.0f, (inWidth - 600.0f) * 0.5f)),
              top(std::max(8.0f, (inHeight - 344.0f) * 0.5f)), right(inWidth - left) {}
        D2D1_RECT_F Rect(float inX, float inY, float inWidth, float inHeight) const
            { return D2D1::RectF(left + inX, top + inY, left + inX + inWidth, top + inY + inHeight); }
        bool Click(const ActionRPG::InputState& inInput, const D2D1_RECT_F& inRect) const
        {
            return inInput.leftMousePressed && inInput.clickX >= inRect.left && inInput.clickX <= inRect.right
                && inInput.clickY >= inRect.top && inInput.clickY <= inRect.bottom;
        }
    };
    void Button(ActionRPG::D2DRenderer& inRenderer, const D2D1_RECT_F& inRect,
        std::wstring_view inLabel, bool inEnabled = true, bool inSelected = false)
    {
        const auto background = inEnabled ? D2D1::ColorF(inSelected ? 0.16f : 0.10f, 0.23f, 0.32f)
            : D2D1::ColorF(0.10f, 0.11f, 0.13f);
        inRenderer.FillRectangle(inRect.left, inRect.top, inRect.right, inRect.bottom, background);
        inRenderer.DrawUiText(inLabel, inRect, D2D1::ColorF(inEnabled ? 0.95f : 0.45f, 0.95f, 0.95f),
            16.0f, false, true);
    }
}

namespace ActionRPG
{
    LoginFlow::LoginFlow(const AssetCatalog& inAssets, TownClient& inTown, DungeonClient& inDungeon)
        : settings(AuthSettings::Load(inAssets)), town(inTown), dungeon(inDungeon),
          message(settings.loginConfigurationError.empty() ? L"Google 계정으로 로그인해 주세요."
              : settings.loginConfigurationError), playerName(settings.playerName), characterId(settings.characterId) {}
    LoginFlow::~LoginFlow()
    {
        auth.Cancel();
        gameToken.clear();
        town.RequestStop();
        dungeon.RequestStop();
    }
    bool LoginFlow::HasToken() const
    {
        return !gameToken.empty() && std::chrono::steady_clock::now() < tokenExpiry;
    }
    bool LoginFlow::IsPlaying() const noexcept { return state == State::Playing; }
    bool LoginFlow::ConsumeExitRequested() noexcept { return std::exchange(exitRequested, false); }

    void LoginFlow::StartLogin()
    {
        if (!settings.loginConfigurationError.empty())
            { message = settings.loginConfigurationError; return; }
        ++attempt;
        state = State::GoogleLogin;
        message = L"서버 확인: Google 로그인 준비";
        auth.Login(attempt, settings, chooseAccount);
    }
    void LoginFlow::EnterSelectedTown()
    {
        if (!HasToken()) { gameToken.clear(); state = State::Login; message = L"다시 로그인해 주세요."; return; }
        if (!settings.townConfigurationError.empty())
            { message = settings.townConfigurationError; return; }
        if (selectedServer >= settings.servers.size()) return;
        nameFocused = false;
        ClearCharacters();
        ++attempt;
        state = State::Ticket;
        message = L"서버 확인: 새 입장 티켓 요청";
        auth.RequestTicket(attempt, settings.authUrl, gameToken, settings.servers[selectedServer].serverId);
    }
    void LoginFlow::CancelPending()
    {
        ++attempt;
        auth.Cancel();
        town.RequestStop();
        ClearCharacters();
        state = HasToken() ? State::ServerSelection : State::Login;
        message = L"취소했습니다. 다시 시도하면 새 인증 요청 또는 입장 티켓을 발급합니다.";
    }
    void LoginFlow::BeginLeave(const SessionMenuAction inAction)
    {
        if (inAction == SessionMenuAction::None || state == State::Leaving) return;
        ++attempt;
        auth.Cancel();
        town.RequestStop();
        dungeon.RequestStop();
        ClearCharacters();
        nameFocused = false;
        state = State::Leaving;
        leaveAction = inAction;
        message = L"기존 연결을 종료하는 중입니다.";
        logoutPending = false;
        if (inAction != SessionMenuAction::SelectTown)
        {
            chooseAccount = inAction == SessionMenuAction::SwitchAccount;
            auto token = std::move(gameToken);
            gameToken.clear();
            tokenExpiry = {};
            if (!token.empty())
            {
                logoutPending = true;
                auth.Logout(attempt, settings.authUrl, std::move(token));
            }
        }
    }
    void LoginFlow::ProcessEvents()
    {
        for (auto& event : auth.ConsumeEvents())
        {
            if (event.attemptId != attempt) continue;
            if (event.operation == AuthOperation::Logout && state == State::Leaving)
            {
                if (event.kind != AuthEventKind::Progress) { logoutPending = false; message = std::move(event.message); }
                continue;
            }
            if ((event.operation == AuthOperation::Login && state != State::GoogleLogin)
                || (event.operation == AuthOperation::Ticket && state != State::Ticket)) continue;
            message = std::move(event.message);
            if (event.kind == AuthEventKind::Progress) continue;
            if (event.kind == AuthEventKind::Failed)
            {
                state = HasToken() ? State::ServerSelection : State::Login;
                continue;
            }
            if (event.operation == AuthOperation::Login)
            {
                gameToken = std::move(event.credential);
                tokenExpiry = event.expiresAt;
                chooseAccount = false;
                state = State::ServerSelection;
                if (!settings.townConfigurationError.empty()) message = settings.townConfigurationError;
            }
            else if (event.operation == AuthOperation::Ticket)
            {
                if (!event.ready) { state = State::ServerSelection; continue; }
                if (!HasToken() || std::chrono::steady_clock::now() >= event.expiresAt)
                {
                    state = State::ServerSelection;
                    message = L"입장 티켓이 만료됐습니다. 입장을 다시 눌러 주세요.";
                    continue;
                }
                state = State::Town;
                town.Start(attempt, settings.servers.at(selectedServer), settings.townCaFile,
                    std::move(event.credential), event.expiresAt);
            }
        }
    }

    void LoginFlow::ClearCharacters()
    {
        characters.clear();
        characterBatches.clear();
        characterBatchCount = 0;
        selectedCharacter = 0;
        characterRequest = CharacterRequest::None;
        characterRequestId.clear();
        pendingCharacter.reset();
        charactersCurrent = characterAccepted = false;
    }

    void LoginFlow::RequestCharacters()
    {
        if (characterRequest != CharacterRequest::None) return;
        try
        {
            characterRequestId = CharacterInventoryJson::NewRequestId();
            characterRequest = CharacterRequest::List;
            characterBatches.clear();
            characterBatchCount = 0;
            charactersCurrent = false;
            characterDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
            state = State::CharacterSelection;
            message = L"저장된 캐릭터를 조회하는 중입니다.";
            town.RequestCharacterList(characterRequestId);
        }
        catch (...) { characterRequest = CharacterRequest::None; message = L"캐릭터 조회 요청을 만들지 못했습니다."; }
    }

    void LoginFlow::CreateCharacter()
    {
        if (characterRequest != CharacterRequest::None) return;
        try
        {
            const auto name = AuthWideToUtf8(playerName);
            if (name.empty() || name.size() > 32 || std::any_of(playerName.begin(), playerName.end(),
                [](wchar_t value) { return value < 32 || value == 127; }))
                { message = L"캐릭터 이름은 UTF-8 기준 1~32바이트로 입력해 주세요."; return; }
            characterRequestId = CharacterInventoryJson::NewRequestId();
            characterRequest = CharacterRequest::Create;
            charactersCurrent = false;
            characterDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
            message = L"캐릭터 생성 결과를 기다리는 중입니다.";
            town.CreateCharacter(characterRequestId, name, characterId);
        }
        catch (...) { characterRequest = CharacterRequest::None; message = L"캐릭터 이름 또는 생성 요청을 확인해 주세요."; }
    }

    void LoginFlow::SelectCharacter()
    {
        if (!charactersCurrent || characterRequest != CharacterRequest::None || selectedCharacter >= characters.size()) return;
        try
        {
            pendingCharacter = characters[selectedCharacter];
            characterRequestId = CharacterInventoryJson::NewRequestId();
            characterRequest = CharacterRequest::Select;
            characterDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
            message = L"캐릭터의 저장 상태와 소유권을 확인하는 중입니다.";
            town.SelectCharacter(characterRequestId, pendingCharacter->id, pendingCharacter->generation);
        }
        catch (...) { pendingCharacter.reset(); characterRequest = CharacterRequest::None; message = L"입장 요청을 만들지 못했습니다."; }
    }

    void LoginFlow::ProcessCharacterEvents()
    {
        using namespace CharacterInventoryJson;
        for (auto& event : town.ConsumeCharacterEvents())
        {
            try
            {
                const auto body = Json::parse(event.payload);
                const auto requestId = HexId(body.at("requestId"), 64);
                if (requestId != characterRequestId || characterRequest == CharacterRequest::None) continue;
                if ((event.kind == CharacterEvent::Kind::List && characterRequest != CharacterRequest::List)
                    || (event.kind == CharacterEvent::Kind::Create && characterRequest != CharacterRequest::Create)
                    || (event.kind == CharacterEvent::Kind::Select && characterRequest != CharacterRequest::Select)) continue;
                const auto id = UInt64(body.at("characterId"));
                const auto revision = UInt64(body.at("revision"));
                const auto result = body.at("result").get<std::string>();
                if (result != "Succeeded")
                {
                    characterRequest = CharacterRequest::None;
                    pendingCharacter.reset();
                    characterBatches.clear();
                    charactersCurrent = false;
                    message = result == "NameTaken" ? L"이미 사용 중인 이름입니다."
                        : result == "StaleOwner" ? L"캐릭터 상태가 변경됐습니다. 목록을 새로 조회해 주세요."
                        : result == "CharacterNotFound" ? L"캐릭터를 찾지 못했습니다. 목록을 새로 조회해 주세요."
                        : result == "Busy" ? L"이전 요청이 처리 중입니다. 잠시 후 다시 조회해 주세요."
                        : L"저장된 캐릭터 정보를 확인하지 못했습니다. 잠시 후 다시 조회해 주세요.";
                    continue;
                }
                if (event.kind == CharacterEvent::Kind::List)
                {
                    const auto count = UInt32(body.at("batchCount"), 1, UINT32_MAX);
                    const auto index = UInt32(body.at("batchIndex"), 0, count - 1);
                    const auto& rows = body.at("characters");
                    if (!rows.is_array() || rows.size() > 64
                        || (characterBatchCount != 0 && characterBatchCount != count))
                        throw std::runtime_error("Invalid character list batch.");
                    characterBatchCount = count;
                    const auto found = characterBatches.find(index);
                    if (found != characterBatches.end() && found->second != rows)
                        throw std::runtime_error("Conflicting character list batch.");
                    characterBatches[index] = rows;
                    if (characterBatches.size() != count) continue;
                    std::vector<Character> next;
                    std::unordered_set<std::uint64_t> seen;
                    for (std::uint32_t batch = 0; batch < count; ++batch)
                        for (const auto& row : characterBatches.at(batch))
                        {
                            Character value;
                            value.id = UInt64(row.at("characterId"));
                            value.generation = UInt64(row.at("ownerGeneration"));
                            value.revision = UInt64(row.at("revision"));
                            value.definitionId = UInt32(row.at("characterDefinitionId"), 1, 1000000);
                            value.level = UInt32(row.at("level"), 1, 1000000);
                            const auto name = row.at("name").get<std::string>();
                            if (value.id == 0 || name.empty() || name.size() > 32
                                || !seen.emplace(value.id).second) throw std::runtime_error("Invalid character row.");
                            value.name = AuthUtf8ToWide(name);
                            if (std::any_of(value.name.begin(), value.name.end(), [](wchar_t c) { return c < 32 || c == 127; }))
                                throw std::runtime_error("Invalid character name.");
                            next.push_back(std::move(value));
                        }
                    characters = std::move(next);
                    selectedCharacter = characters.empty() ? 0 : std::min(selectedCharacter, characters.size() - 1);
                    charactersCurrent = true;
                    characterBatches.clear();
                    characterRequest = CharacterRequest::None;
                    message = characters.empty() ? L"저장된 캐릭터가 없습니다. 새 캐릭터를 생성해 주세요."
                        : L"입장할 캐릭터를 선택해 주세요. ↑↓ 선택 · Enter 입장";
                }
                else if (event.kind == CharacterEvent::Kind::Create)
                {
                    if (id == 0) throw std::runtime_error("Missing created character.");
                    characterRequest = CharacterRequest::None;
                    nameFocused = false;
                    RequestCharacters(); // Acknowledge creation, then read the authoritative roster.
                }
                else
                {
                    if (!pendingCharacter || id != pendingCharacter->id || revision < pendingCharacter->revision)
                        throw std::runtime_error("Invalid selected character.");
                    town.SetSelectedCharacterId(id);
                    characterAccepted = true;
                    characterRequest = CharacterRequest::None;
                    state = State::Town;
                    characterDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
                    message = L"캐릭터 복원이 완료됐습니다. 월드 입장을 기다리는 중입니다.";
                }
            }
            catch (...)
            {
                if (characterRequest == CharacterRequest::Select)
                {
                    CancelPending();
                    message = L"캐릭터 입장 결과를 확인하지 못했습니다. 새 연결로 다시 시도해 주세요.";
                }
                else
                {
                    characterRequest = CharacterRequest::None;
                    characterBatches.clear();
                    charactersCurrent = false;
                    message = L"캐릭터 응답 형식을 확인하지 못했습니다. 다시 조회해 주세요.";
                }
            }
        }
    }
    void LoginFlow::Update(const InputState& inInput, const float inWidth, const float inHeight)
    {
        ProcessEvents();
        ProcessCharacterEvents();
        const auto now = std::chrono::steady_clock::now();
        if (!gameToken.empty() && !HasToken() && state != State::Leaving)
        {
            BeginLeave(SessionMenuAction::Logout);
            message = L"로그인 유효 시간이 끝났습니다. 다시 로그인해 주세요.";
        }
        if (state == State::Town || state == State::CharacterSelection || state == State::CharacterCreate || state == State::Playing)
        {
            const auto info = town.GetConnectionInfo();
            if (info.attemptId == attempt)
            {
                if (info.state == TownConnectionState::Failed)
                {
                    if (state == State::Playing)
                    {
                        BeginLeave(SessionMenuAction::Logout);
                        message = L"게임 연결이 종료됐습니다. 다시 로그인해 주세요.";
                    }
                    else
                    {
                        town.RequestStop();
                        ClearCharacters();
                        state = HasToken() ? State::ServerSelection : State::Login;
                        message = L"타운 연결이 종료됐습니다. 새 입장 티켓으로 다시 시도해 주세요.";
                    }
                }
                else if (state == State::Town)
                {
                    if (info.state == TownConnectionState::Ready && characterAccepted
                        && town.GetSelectedCharacterId() != 0) state = State::Playing;
                    else if (info.state == TownConnectionState::CharacterSelection && !characterAccepted)
                        RequestCharacters();
                    else if (!characterAccepted) message = info.message;
                }
            }
            if (characterRequest != CharacterRequest::None && now >= characterDeadline)
            {
                if (characterRequest == CharacterRequest::Select)
                {
                    CancelPending(); // Selection may have committed. Fence it by closing this session.
                    message = L"입장 결과를 확인하지 못했습니다. 새 연결로 다시 시도해 주세요.";
                }
                else
                {
                    const bool created = characterRequest == CharacterRequest::Create;
                    characterRequest = CharacterRequest::None;
                    characterBatches.clear();
                    charactersCurrent = false;
                    if (created) RequestCharacters(); // Never replay an uncertain creation.
                    else message = L"목록 조회가 지연되고 있습니다. 다시 조회해 주세요.";
                }
            }
            else if (state == State::Town && characterAccepted && now >= characterDeadline)
            {
                CancelPending();
                message = L"월드 입장이 지연되고 있습니다. 새 연결로 다시 시도해 주세요.";
            }
        }
        if (state == State::Playing) return;
        const Layout layout(inWidth, inHeight);
        const bool confirm = inInput.WasPressed(InputKey::ConfirmSelection);
        const bool escape = inInput.WasPressed(InputKey::ToggleSystemMenu);
        if (state == State::Leaving)
        {
            if (dungeon.IsStopComplete() && !logoutPending)
            {
                const bool selecting = leaveAction == SessionMenuAction::SelectTown && HasToken();
                state = selecting ? State::ServerSelection : State::Login;
                if (selecting) message = L"입장할 타운을 선택하세요. 저장된 캐릭터를 다시 선택할 수 있습니다.";
                leaveAction = SessionMenuAction::None;
            }
            return;
        }
        if (state == State::GoogleLogin || state == State::Ticket || state == State::Town)
        {
            if (escape || layout.Click(inInput, layout.Rect(16, 228, 164, 36))) CancelPending();
            return;
        }
        if (state == State::Login)
        {
            if (confirm || layout.Click(inInput, layout.Rect(16, 228, 260, 36))) StartLogin();
            if (escape || layout.Click(inInput, layout.Rect(290, 228, 150, 36))) exitRequested = true;
            return;
        }
        if (state == State::CharacterSelection || state == State::CharacterCreate)
        {
            if (characterRequest != CharacterRequest::None)
            {
                if (escape) CancelPending();
                return;
            }
            if (state == State::CharacterCreate)
            {
                const auto nameRect = layout.Rect(16, 100, 350, 36);
                if (inInput.leftMousePressed) nameFocused = layout.Click(inInput, nameRect);
                if (nameFocused)
                    for (const auto value : inInput.textInput)
                    {
                        if (value == L'\b' && !playerName.empty())
                        {
                            const auto last = playerName.back();
                            playerName.pop_back();
                            if (last >= 0xDC00 && last <= 0xDFFF && !playerName.empty()
                                && playerName.back() >= 0xD800 && playerName.back() <= 0xDBFF) playerName.pop_back();
                        }
                        else if (value >= 32 && value != 127 && playerName.size() < 32) playerName += value;
                    }
                if (layout.Click(inInput, layout.Rect(16, 154, 40, 36))) characterId = characterId <= 1 ? 3 : characterId - 1;
                if (layout.Click(inInput, layout.Rect(182, 154, 40, 36))) characterId = characterId >= 3 ? 1 : characterId + 1;
                if (confirm || layout.Click(inInput, layout.Rect(16, 228, 164, 36))) CreateCharacter();
                else if (escape || layout.Click(inInput, layout.Rect(194, 228, 164, 36))) RequestCharacters();
                return;
            }
            if (charactersCurrent && !characters.empty())
            {
                if ((inInput.WasPressed(InputKey::MoveUp) || inInput.mouseWheelDelta > 0) && selectedCharacter > 0) --selectedCharacter;
                if ((inInput.WasPressed(InputKey::MoveDown) || inInput.mouseWheelDelta < 0)
                    && selectedCharacter + 1 < characters.size()) ++selectedCharacter;
                for (std::size_t row = 0; row < 3; ++row)
                {
                    const auto index = (selectedCharacter / 3) * 3 + row;
                    if (index < characters.size() && layout.Click(inInput, layout.Rect(16, 58 + row * 36.0f, 568, 32)))
                        selectedCharacter = index;
                }
            }
            if (confirm || layout.Click(inInput, layout.Rect(16, 228, 164, 36))) SelectCharacter();
            else if (layout.Click(inInput, layout.Rect(194, 228, 164, 36)))
            {
                state = State::CharacterCreate;
                playerName.clear();
                nameFocused = true;
                characterId = std::clamp(characterId, 1u, 3u);
                message = L"새 캐릭터의 이름과 종류를 선택해 주세요.";
            }
            else if (layout.Click(inInput, layout.Rect(372, 228, 164, 36))) RequestCharacters();
            else if (escape) CancelPending();
            return;
        }
        if (state != State::ServerSelection) return;
        if (!settings.servers.empty())
        {
            if ((inInput.WasPressed(InputKey::MoveUp) || inInput.mouseWheelDelta > 0) && selectedServer > 0) --selectedServer;
            if ((inInput.WasPressed(InputKey::MoveDown) || inInput.mouseWheelDelta < 0)
                && selectedServer + 1 < settings.servers.size()) ++selectedServer;
        }
        for (std::size_t row = 0; row < 3; ++row)
        {
            const auto index = (selectedServer / 3) * 3 + row;
            if (index < settings.servers.size() && layout.Click(inInput, layout.Rect(16, 58 + row * 36.0f, 568, 32)))
                selectedServer = index;
        }
        if (confirm || layout.Click(inInput, layout.Rect(16, 228, 164, 36))) EnterSelectedTown();
        else if (layout.Click(inInput, layout.Rect(194, 228, 164, 36))) BeginLeave(SessionMenuAction::SwitchAccount);
        else if (escape || layout.Click(inInput, layout.Rect(372, 228, 164, 36))) BeginLeave(SessionMenuAction::Logout);
    }
    void LoginFlow::Render(D2DRenderer& inRenderer, const float inWidth, const float inHeight) const
    {
        const Layout layout(inWidth, inHeight);
        inRenderer.FillRectangle(layout.left, layout.top, layout.right, layout.top + 344,
            D2D1::ColorF(0.06f, 0.08f, 0.12f));
        const auto white = D2D1::ColorF(0.94f, 0.94f, 0.94f);
        const wchar_t* title = state == State::ServerSelection ? L"타운 선택"
            : state == State::CharacterSelection ? L"캐릭터 선택"
            : state == State::CharacterCreate ? L"캐릭터 생성" : L"Action RPG · Google 로그인";
        inRenderer.DrawUiText(title, layout.Rect(16, 12, 568, 36), white, 24);
        if (state == State::ServerSelection)
        {
            if (settings.servers.empty())
                inRenderer.DrawUiText(L"설정된 타운이 없습니다.", layout.Rect(16, 58, 568, 96), white, 18, true);
            for (std::size_t row = 0; row < 3; ++row)
            {
                const auto index = (selectedServer / 3) * 3 + row;
                if (index < settings.servers.size()) Button(inRenderer, layout.Rect(16, 58 + row * 36.0f, 568, 32),
                    settings.servers[index].label, true, selectedServer == index);
            }
            inRenderer.DrawUiText(L"타운 인증 후 저장된 캐릭터를 선택합니다. ↑↓ 타운 선택",
                layout.Rect(16, 176, 568, 36), white, 15, true);
            Button(inRenderer, layout.Rect(16, 228, 164, 36), L"연결 (Enter)",
                settings.townConfigurationError.empty() && !settings.servers.empty());
            Button(inRenderer, layout.Rect(194, 228, 164, 36), L"계정 전환");
            Button(inRenderer, layout.Rect(372, 228, 164, 36), L"로그아웃 (Esc)");
        }
        else if (state == State::CharacterSelection)
        {
            const bool waiting = characterRequest != CharacterRequest::None;
            if (characters.empty())
                inRenderer.DrawUiText(charactersCurrent ? L"저장된 캐릭터가 없습니다." : L"캐릭터 목록을 확인해야 합니다.",
                    layout.Rect(16, 58, 568, 96), white, 18, true);
            for (std::size_t row = 0; row < 3; ++row)
            {
                const auto index = (selectedCharacter / 3) * 3 + row;
                if (index < characters.size())
                {
                    const auto& character = characters[index];
                    Button(inRenderer, layout.Rect(16, 58 + row * 36.0f, 568, 32),
                        character.name + L"  · Lv." + std::to_wstring(character.level)
                        + L"  · 캐릭터 " + std::to_wstring(character.definitionId),
                        charactersCurrent && !waiting, selectedCharacter == index);
                }
            }
            inRenderer.DrawUiText(waiting ? L"서버 응답 대기 중 · Esc 연결 취소"
                : L"↑↓ 또는 휠로 선택 · Esc 타운 선택으로 돌아가기", layout.Rect(16, 176, 568, 36), white, 15, true);
            Button(inRenderer, layout.Rect(16, 228, 164, 36), L"입장 (Enter)",
                charactersCurrent && !waiting && selectedCharacter < characters.size());
            Button(inRenderer, layout.Rect(194, 228, 164, 36), L"새 캐릭터", !waiting);
            Button(inRenderer, layout.Rect(372, 228, 164, 36), L"새로 조회", !waiting);
        }
        else if (state == State::CharacterCreate)
        {
            const bool waiting = characterRequest != CharacterRequest::None;
            inRenderer.DrawUiText(L"이름 (UTF-8 1~32바이트)", layout.Rect(16, 66, 568, 24), white, 16);
            Button(inRenderer, layout.Rect(16, 100, 350, 36), playerName + (nameFocused ? L" |" : L""),
                !waiting, nameFocused);
            Button(inRenderer, layout.Rect(16, 154, 40, 36), L"◀", !waiting);
            inRenderer.DrawUiText(L"캐릭터 " + std::to_wstring(characterId), layout.Rect(60, 154, 118, 36),
                white, 16, false, true);
            Button(inRenderer, layout.Rect(182, 154, 40, 36), L"▶", !waiting);
            Button(inRenderer, layout.Rect(16, 228, 164, 36), L"생성 (Enter)", !waiting);
            Button(inRenderer, layout.Rect(194, 228, 164, 36), L"목록으로 (Esc)", !waiting);
        }
        else if (state == State::Login)
        {
            inRenderer.DrawUiText(L"브라우저에서 Google 인증을 완료한 뒤 게임으로 돌아오세요.",
                layout.Rect(16, 66, 568, 76), white, 18, true);
            Button(inRenderer, layout.Rect(16, 228, 260, 36), L"Google 로그인 (Enter)",
                settings.loginConfigurationError.empty());
            Button(inRenderer, layout.Rect(290, 228, 150, 36), L"종료 (Esc)");
        }
        else
        {
            inRenderer.DrawUiText(state == State::Leaving ? L"연결을 정리하고 있습니다."
                : L"타운 인증과 캐릭터 선택을 완료하면 게임 화면이 열립니다.",
                layout.Rect(16, 66, 568, 76), white, 18, true);
            if (state != State::Leaving) Button(inRenderer, layout.Rect(16, 228, 164, 36), L"취소 (Esc)");
        }
        inRenderer.DrawUiText(message, layout.Rect(16, 276, 568, 64), white, 15, true);
    }
}
