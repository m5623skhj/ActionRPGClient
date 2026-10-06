#include "App/LoginFlow.h"
#include "Graphics/D2DRenderer.h"
#include "Network/DungeonClient.h"
#include "Network/TownClient.h"
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
        try
        {
            const auto name = AuthWideToUtf8(playerName);
            if (name.empty() || name.size() > 32 || std::any_of(playerName.begin(), playerName.end(),
                [](wchar_t value) { return value < 32 || value == 127; }))
                { message = L"캐릭터 이름은 UTF-8 기준 1~32바이트로 입력해 주세요."; return; }
        }
        catch (...) { message = L"캐릭터 이름을 확인해 주세요."; return; }
        nameFocused = false;
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
                    std::move(event.credential), AuthWideToUtf8(playerName), characterId, event.expiresAt);
            }
        }
    }

    void LoginFlow::Update(const InputState& inInput, const float inWidth, const float inHeight)
    {
        ProcessEvents();
        if (!gameToken.empty() && !HasToken() && state != State::Leaving)
        {
            BeginLeave(SessionMenuAction::Logout);
            message = L"로그인 유효 시간이 끝났습니다. 다시 로그인해 주세요.";
        }
        if (state == State::Town || state == State::Playing)
        {
            const auto info = town.GetConnectionInfo();
            if (info.attemptId == attempt)
            {
                if (state == State::Town)
                {
                    message = info.message;
                    if (info.state == TownConnectionState::Ready) state = State::Playing;
                    else if (info.state == TownConnectionState::Failed)
                    {
                        town.RequestStop();
                        state = HasToken() ? State::ServerSelection : State::Login;
                    }
                }
                else if (info.state == TownConnectionState::Failed)
                {
                    // A closed connection does not identify the cause (including account replacement).
                    BeginLeave(SessionMenuAction::Logout);
                    message = L"게임 연결이 종료됐습니다. 다시 로그인해 주세요.";
                }
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
                if (selecting) message = L"입장할 타운을 선택하세요. 타운 변경 시 현재 진행 상태는 유지되지 않습니다.";
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
        if (state != State::ServerSelection) return;
        const auto nameRect = layout.Rect(16, 176, 250, 36);
        if (inInput.leftMousePressed) nameFocused = layout.Click(inInput, nameRect);
        if (nameFocused)
        {
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
        }
        if (!nameFocused && !settings.servers.empty())
        {
            if ((inInput.WasPressed(InputKey::MoveUp) || inInput.mouseWheelDelta > 0) && selectedServer > 0) --selectedServer;
            if ((inInput.WasPressed(InputKey::MoveDown) || inInput.mouseWheelDelta < 0)
                && selectedServer + 1 < settings.servers.size()) ++selectedServer;
        }
        const auto page = selectedServer / 3;
        for (std::size_t row = 0; row < 3; ++row)
        {
            const auto index = page * 3 + row;
            if (index < settings.servers.size() && layout.Click(inInput, layout.Rect(16, 58 + row * 36.0f, 568, 32)))
                selectedServer = index;
        }
        if (layout.Click(inInput, layout.Rect(286, 176, 40, 36))) characterId = characterId == 1 ? 3 : characterId - 1;
        if (layout.Click(inInput, layout.Rect(452, 176, 40, 36))) characterId = characterId == 3 ? 1 : characterId + 1;
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
        inRenderer.DrawUiText(state == State::ServerSelection ? L"타운 선택" : L"Action RPG · Google 로그인",
            layout.Rect(16, 12, 568, 36), white, 24);
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
            const auto nameRect = layout.Rect(16, 176, 250, 36);
            Button(inRenderer, nameRect, playerName + (nameFocused ? L" |" : L""), true, nameFocused);
            Button(inRenderer, layout.Rect(286, 176, 40, 36), L"◀");
            inRenderer.DrawUiText(L"캐릭터 " + std::to_wstring(characterId), layout.Rect(330, 176, 118, 36),
                white, 16, false, true);
            Button(inRenderer, layout.Rect(452, 176, 40, 36), L"▶");
            inRenderer.DrawUiText(L"이름 입력 · ↑↓ 타운", layout.Rect(16, 154, 568, 20), white, 13);
            Button(inRenderer, layout.Rect(16, 228, 164, 36), L"입장 (Enter)", settings.townConfigurationError.empty());
            Button(inRenderer, layout.Rect(194, 228, 164, 36), L"계정 전환");
            Button(inRenderer, layout.Rect(372, 228, 164, 36), L"로그아웃 (Esc)");
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
                : L"인증과 입장이 끝나면 게임 화면이 열립니다.",
                layout.Rect(16, 66, 568, 76), white, 18, true);
            if (state != State::Leaving) Button(inRenderer, layout.Rect(16, 228, 164, 36), L"취소 (Esc)");
        }
        inRenderer.DrawUiText(message, layout.Rect(16, 276, 568, 64), white, 15, true);
    }
}