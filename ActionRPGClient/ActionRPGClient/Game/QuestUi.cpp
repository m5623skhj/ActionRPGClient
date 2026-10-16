#include "Game/QuestUi.h"
#include "Core/IniDocument.h"
#include "Game/Camera.h"
#include "Game/HudLayout.h"
#include "Graphics/D2DRenderer.h"
#include "Network/CharacterInventoryJson.h"
#include "Resources/AssetCatalog.h"

#include <wincodec.h>
#include <algorithm>
#include <stdexcept>
#include <cmath>
#include <utility>

namespace ActionRPG
{
    namespace
    {
        using Json = nlohmann::json;
        constexpr const char* CATEGORIES[]{"Scenario", "Normal", "Event"};
        constexpr const wchar_t* CATEGORY_NAMES[]{L"시나리오", L"일반", L"이벤트"};
        bool Contains(const D2D1_RECT_F& r, float x, float y)
        { return x >= r.left && x < r.right && y >= r.top && y < r.bottom; }
        std::wstring Wide(std::string_view text)
        {
            if (text.empty()) return {};
            const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
            if (count <= 0) return L"표시할 수 없는 문자열";
            std::wstring result(count, L'\0');
            (void)MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), result.data(), count);
            return result;
        }
        std::wstring Reason(const std::string& result)
        {
            if (result.empty()) return {};
            if (result == "Succeeded") return L"서버 처리 완료 · 상태 갱신 중";
            if (result == "RequirementsNotMet") return L"조건을 충족하지 못했습니다. 제출 아이템과 수락 조건을 확인하세요.";
            if (result == "NotInRange") return L"퀘스트 게시판 가까이에서 다시 시도하세요.";
            if (result == "NotComplete") return L"현재 단계의 목표를 먼저 완료하세요.";
            if (result == "InvalidChoice") return L"선택 보상에서 정확히 한 묶음을 선택하세요.";
            if (result == "InventoryFull") return L"가방 공간이 부족합니다. 제출·보상은 적용되지 않았으며 보상 대기 상태를 유지합니다.";
            if (result == "EventClosed") return L"퀘스트 처리 기간이 종료되었습니다.";
            if (result == "AlreadyCompleted") return L"이미 완료한 퀘스트입니다. 최신 상태를 확인합니다.";
            if (result == "RevisionConflict") return L"캐릭터 상태가 변경되었습니다. 최신 상태를 다시 조회합니다.";
            if (result == "Busy" || result == "Pending") return L"서버가 처리 중입니다. 같은 요청의 결과를 기다립니다.";
            if (result == "RateLimited") return L"요청이 많습니다. 잠시 후 다시 시도하세요.";
            if (result == "DatabaseUnavailable") return L"저장 결과를 확인하지 못했습니다. 새 요청 없이 결과를 확인하세요.";
            if (result == "QuestLimit") return L"진행·미수령 퀘스트는 최대 16개입니다.";
            if (result == "StaleOwner" || result == "NotEntered") return L"접속과 캐릭터 소유 상태를 다시 확인해야 합니다.";
            return L"퀘스트 요청을 처리할 수 없습니다. 최신 상태를 확인하세요.";
        }
        std::wstring Status(const std::string& state)
        {
            if (state == "Active") return L"진행 중";
            if (state == "ReadyToReport") return L"NPC 보고 가능";
            if (state == "RewardPending") return L"보상 대기";
            if (state == "Completed") return L"완료";
            if (state == "Abandoned") return L"포기";
            if (state == "Expired") return L"기간 종료";
            if (state == "Unavailable") return L"수락 불가";
            return L"수락 전";
        }
        bool Live(const QuestEntryView& q)
        { return q.state == "Active" || q.state == "ReadyToReport" || q.state == "RewardPending"; }
        float ChoiceHeight(D2DRenderer& renderer, const std::wstring& text, float width)
        { return std::max(48.0f, renderer.MeasureUiText(text, std::max(40.0f, width - 72), 13).height + 16); }
    }

    QuestUi::QuestUi(const AssetCatalog& inAssets, D2DRenderer& inRenderer) : assets(inAssets), renderer(inRenderer)
    { LoadNpcVisuals(); }

    // Cache the alpha mask and visible bounds once. Rendering and hit testing share
    // the same cropped source and foot anchor; transparent PNG padding is never a target.
    void QuestUi::LoadNpcVisuals()
    {
        const IniDocument definitions(assets.GetDataPath("Npcs"));
        Microsoft::WRL::ComPtr<IWICImagingFactory2> factory;
        const auto check = [](HRESULT result)
        { if (FAILED(result)) throw std::runtime_error("Unable to load NPC alpha mask."); };
        check(CoCreateInstance(CLSID_WICImagingFactory2, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)));
        for (const auto& id : definitions.GetSectionNames())
        {
            NpcVisual visual;
            const auto path = assets.GetImagePath(definitions.GetValue(id, "image_asset"));
            visual.bitmap = renderer.LoadBitmap(path);
            std::size_t parsed{};
            const auto& height = definitions.GetValue(id, "render_height");
            visual.height = std::stof(height, &parsed);
            if (parsed != height.size() || !std::isfinite(visual.height) || visual.height < 32 || visual.height > 512)
                throw std::runtime_error("Invalid NPC render height.");
            const auto& interaction = definitions.GetValue(id, "interaction");
            if (interaction != "quests" && interaction != "none") throw std::runtime_error("Invalid NPC interaction.");
            visual.quests = interaction == "quests";
            Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder;
            Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame;
            Microsoft::WRL::ComPtr<IWICFormatConverter> converter;
            check(factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &decoder));
            check(decoder->GetFrame(0, &frame));
            check(factory->CreateFormatConverter(&converter));
            check(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone,
                nullptr, 0, WICBitmapPaletteTypeCustom));
            UINT width{}, heightPixels{};
            check(converter->GetSize(&width, &heightPixels));
            if (!width || !heightPixels || width > 4096 || heightPixels > 4096)
                throw std::runtime_error("Invalid NPC image size.");
            std::vector<BYTE> pixels(static_cast<std::size_t>(width)*heightPixels*4);
            check(converter->CopyPixels(nullptr, width*4, static_cast<UINT>(pixels.size()), pixels.data()));
            visual.pixelWidth = width;
            visual.alpha.resize(static_cast<std::size_t>(width)*heightPixels);
            auto& bounds = visual.opaquePixels;
            bounds = {width,heightPixels,0,0};
            for (UINT y=0; y<heightPixels; ++y) for (UINT x=0; x<width; ++x)
            {
                const auto index = static_cast<std::size_t>(y)*width+x;
                visual.alpha[index] = pixels[index*4+3];
                if (visual.alpha[index] == 0) continue;
                bounds.left = std::min(bounds.left,x); bounds.right = std::max(bounds.right,x+1);
                bounds.top = std::min(bounds.top,y); bounds.bottom = std::max(bounds.bottom,y+1);
            }
            if (bounds.right <= bounds.left || bounds.bottom <= bounds.top) throw std::runtime_error("Empty NPC image.");
            const auto dips = visual.bitmap->GetSize();
            visual.source = {bounds.left*dips.width/width,bounds.top*dips.height/heightPixels,
                bounds.right*dips.width/width,bounds.bottom*dips.height/heightPixels};
            npcVisuals.emplace(id,std::move(visual));
        }
    }

    D2D1_RECT_F QuestUi::NpcBounds(const QuestNpcView& inNpc, const NpcVisual& inVisual, const Camera& inCamera) const
    {
        const auto p = inCamera.WorldToScreen({inNpc.x,inNpc.y});
        const float width = inVisual.height*(inVisual.opaquePixels.right-inVisual.opaquePixels.left)
            /(inVisual.opaquePixels.bottom-inVisual.opaquePixels.top);
        return {p.x-width*0.5f,p.y-inVisual.height,p.x+width*0.5f,p.y};
    }

    const QuestNpcView* QuestUi::PickNpc(const Camera& inCamera, float inX, float inY) const
    {
        const QuestNpcView* selected{};
        for (const auto& npc : state.Get().npcs)
        {
            if (npc.mapId != mapId) continue;
            const auto found = npcVisuals.find(npc.id);
            if (found == npcVisuals.end()) continue;
            const auto& visual = found->second;
            const auto rect = NpcBounds(npc,visual,inCamera);
            if (!Contains(rect,inX,inY)) continue;
            const auto& pixels = visual.opaquePixels;
            const auto x = pixels.left + std::min(pixels.right-pixels.left-1,
                static_cast<UINT>((inX-rect.left)/(rect.right-rect.left)*(pixels.right-pixels.left)));
            const auto y = pixels.top + std::min(pixels.bottom-pixels.top-1,
                static_cast<UINT>((inY-rect.top)/(rect.bottom-rect.top)*(pixels.bottom-pixels.top)));
            if (visual.alpha[static_cast<std::size_t>(y)*visual.pixelWidth+x] >= 32
                && (!selected || npc.y >= selected->y)) selected = &npc;
        }
        return selected;
    }
    void QuestUi::SetCharacterContext(std::uint64_t inCharacterId)
    {
        if (state.GetCharacterId() == inCharacterId) return;
        state.Reset(inCharacterId); icons.clear(); npcId.clear(); hoveredNpcId.clear(); selectedId.clear(); selectedCycle.clear(); choiceId.clear();
        queryId.clear(); pendingAction.clear(); pendingJson.clear(); outgoingJson.clear(); notice.clear();
        querySeconds = refreshCooldown = pendingSeconds = actionCooldown = detailScroll = listScroll = 0;
        wantRefresh = true; confirmAbandon = refreshProgression = false; observedRevision = 0; category = 0;
        trackerDragging=trackerHovered=trackerHidden=false;
    }
    void QuestUi::SetConnected(bool inConnected)
    {
        if (connected && !inConnected)
        {
            trackerDragging=trackerHovered=false;
            state.Invalidate(); queryId.clear(); outgoingJson.clear(); wantRefresh = true;
            notice = pendingJson.empty() ? L"연결을 확인해 주세요." : L"연결이 끊겨 요청 결과를 확인하지 못했습니다.";
        }
        connected = inConnected;
    }
    void QuestUi::SetMapContext(std::string inMapId)
    {
        if (mapId != inMapId) { trackerDragging=trackerHovered=false; mapId = std::move(inMapId); npcId.clear(); confirmAbandon = false; state.Invalidate(); RequestRefresh(); }
    }
    void QuestUi::OpenAll() { trackerHidden = false; npcId.clear(); confirmAbandon = false; state.Invalidate(); RequestRefresh(); }
    void QuestUi::Close() { trackerDragging=false; confirmAbandon = false; hoveredNpcId.clear(); }
    void QuestUi::RequestRefresh() { wantRefresh = true; }
    void QuestUi::ObserveRevision(std::uint64_t inRevision)
    {
        if (inRevision <= observedRevision) return;
        observedRevision = inRevision;
        state.RequireRevision(inRevision); RequestRefresh();
    }
    void QuestUi::InvalidResponse()
    {
        state.Invalidate(); queryId.clear(); querySeconds = 0; wantRefresh = true;
        refreshCooldown = std::max(refreshCooldown, 1.0f);
        notice = L"퀘스트 상태를 확인하지 못했습니다. 기존 표시를 보존하고 다시 조회합니다.";
    }
    void QuestUi::ApplyPage(std::string_view inPayload)
    {
        const auto previous = Selected();
        const auto oldGeneration = previous ? previous->acceptGeneration : 0;
        const auto oldVersion = previous ? previous->definitionVersion : 0;
        const auto oldStage = previous ? previous->stageIndex : 0;
        if (!state.ApplyPage(inPayload)) return;
        const auto current = Selected();
        if (current && (current->acceptGeneration != oldGeneration || current->definitionVersion != oldVersion || current->stageIndex != oldStage))
        { choiceId.clear(); confirmAbandon = false; detailScroll = 0; }
        observedRevision = std::max(observedRevision, state.Get().revision);
        queryId.clear(); querySeconds = 0; wantRefresh = false;
        LoadIcons();
        if (!Selected()) { selectedId.clear(); selectedCycle.clear(); choiceId.clear(); detailScroll = 0; confirmAbandon = false; }
    }
    void QuestUi::ApplyResult(std::string_view inPayload)
    {
        using namespace CharacterInventoryJson;
        const auto value = QuestState::ParseJson(inPayload, 14 * 1024);
        if (UInt64(value.at("characterId")) != state.GetCharacterId() || pendingJson.empty()) return;
        const auto request = HexId(value.at("requestId"), 64);
        if (request != Json::parse(pendingJson).at("requestId").get<std::string>()) return;
        if (!value.at("result").is_string() || value.at("result").get_ref<const std::string&>().size() > 64)
            throw std::runtime_error("Invalid quest result.");
        const auto result = value.at("result").get<std::string>();
        const auto revision = UInt64(value.at("revision"));
        observedRevision = std::max(observedRevision, revision);
        state.RequireRevision(revision); RequestRefresh(); queryId.clear();
        notice = Reason(result);
        if (result == "Pending" || result == "Busy" || result == "DatabaseUnavailable")
        { pendingSeconds = std::max(pendingSeconds, 10.0f); return; }
        if ((result == "Succeeded" || result == "AlreadyCompleted") && pendingAction == "Claim")
            refreshProgression = true;
        pendingJson.clear(); pendingAction.clear(); pendingSeconds = 0; actionCooldown = 0.5f;
        confirmAbandon = false;
    }
    bool QuestUi::CanAct() const
    { return connected && state.IsReady() && !state.IsAssembling() && pendingJson.empty() && actionCooldown <= 0; }
    bool QuestUi::NpcInRange(const std::string& inId) const
    {
        for (const auto& npc : state.Get().npcs)
            if (npc.id == inId && npc.mapId == mapId)
            { const float dx = npc.x-playerPosition.x, dy = npc.y-playerPosition.y; return dx*dx + dy*dy <= 96.0f*96.0f; }
        return false;
    }
    bool QuestUi::NpcRelevant(const QuestEntryView& q) const
    {
        if (npcId.empty()) return true;
        for (const auto* key : {"accept", "complete"}) if (q.definition.at(key).at("npcId") == npcId) return true;
        const auto& steps = q.definition.at("steps");
        if (q.stageIndex < steps.size()) for (const auto& goal : steps.at(q.stageIndex).at("objectives"))
            if (goal.at("type") == "TalkNpc" && goal.at("target").at("npcId") == npcId) return true;
        return false;
    }
    bool QuestUi::IsListed(const QuestEntryView& inQuest) const
    {
        // Keep accepted quests visible even when reporting/claiming is temporarily blocked.
        return inQuest.state != "Completed" && (Live(inQuest) || inQuest.canAccept)
            && inQuest.definition.at("category") == CATEGORIES[category] && NpcRelevant(inQuest);
    }
    std::vector<const QuestEntryView*> QuestUi::Filtered() const
    {
        std::vector<const QuestEntryView*> result;
        for (const auto& q : state.Get().quests)
            if (IsListed(q)) result.push_back(&q);
        return result;
    }
    const QuestEntryView* QuestUi::Selected() const
    {
        for (const auto& q : state.Get().quests)
            if (q.id == selectedId && q.cycleKey == selectedCycle && IsListed(q)) return &q;
        return nullptr;
    }
    std::vector<const QuestEntryView*> QuestUi::Tracked() const
    {
        std::vector<const QuestEntryView*> result;
        for (const auto& id : state.Get().trackedQuestIds)
        {
            const QuestEntryView* selected = nullptr;
            for (const auto& q : state.Get().quests)
                if (q.id == id && Live(q) && (!selected || selected->acceptGeneration < q.acceptGeneration)) selected = &q;
            if (selected) result.push_back(selected);
        }
        return result;
    }
    QuestUi::Layout QuestUi::CalculateLayout(float width, float height, float hudTop) const
    {
        const float panelWidth = std::min(880.0f, width - 32), panelHeight = std::min(500.0f, hudTop - 36);
        const float left = (width-panelWidth)/2 + windowOffset.x;
        const float top = std::max(16.0f, std::min(60.0f, (height-panelHeight)/2)) + windowOffset.y;
        Layout l;
        l.panel = {left,top,left+panelWidth,top+panelHeight};
        for (int i = 0; i < 3; ++i) l.tabs[i] = {left+16+i*144.0f,top+42,left+152+i*144.0f,top+72};
        l.list = {left+16,top+84,left+272,l.panel.bottom-88};
        l.detail = {left+284,top+84,l.panel.right-16,l.panel.bottom-88};
        l.track = {left+16,l.panel.bottom-78,left+168,l.panel.bottom-42};
        l.abandon = {l.panel.right-294,l.panel.bottom-78,l.panel.right-166,l.panel.bottom-42};
        l.primary = {l.panel.right-154,l.panel.bottom-78,l.panel.right-16,l.panel.bottom-42};
        l.retry = l.primary; l.npcTalk = {l.panel.right-126,top+42,l.panel.right-16,top+72};
        return l;
    }
    D2D1_RECT_F QuestUi::GetWindowBounds(float inWidth, float inHeight, float inHudTop) const
    {
        const float scale = HudLayout::Scale(inWidth);
        return HudLayout::ScaleRectangle(CalculateLayout(inWidth/scale,inHeight/scale,inHudTop/scale).panel,scale);
    }
    D2D1_RECT_F QuestUi::TrackerBounds(float width, float height, float hudTop) const
    {
        const float panelHeight = std::max(100.0f,std::min(396.0f,hudTop-172)-128);
        const float left = std::clamp(width-320+trackerOffset.x,8.0f,std::max(8.0f,width-312));
        const float top = std::clamp(128+trackerOffset.y,8.0f,std::max(8.0f,height-panelHeight-8));
        return {left,top,left+304,top+panelHeight};
    }
    bool QuestUi::IsTrackerAt(float inX,float inY,float inWidth,float inHeight,float inHudTop) const
    {
        if (trackerHidden || Tracked().empty()) return false;
        const float scale=HudLayout::Scale(inWidth);
        return Contains(TrackerBounds(inWidth/scale,inHeight/scale,inHudTop/scale),inX/scale,inY/scale);
    }
    void QuestUi::BeginAction(std::string inAction, const QuestEntryView* q, std::vector<std::string> inTracked)
    {
        if (!CanAct()) return;
        Json body{{"requestId",CharacterInventoryJson::NewRequestId()}, {"expectedRevision",std::to_string(state.Get().revision)}, {"action",inAction}};
        if (inAction == "Talk")
        { if (!NpcInRange(npcId)) { notice = Reason("NotInRange"); return; } body["npcId"] = npcId; }
        else if (inAction == "Track") { trackerHidden=false;body["trackedQuestIds"] = std::move(inTracked); }
        else
        {
            if (!q) return;
            body["questId"] = q->id; body["cycleKey"] = q->cycleKey;
            const auto& rule = q->definition.at(inAction == "Accept" ? "accept" : "complete");
            if (inAction != "Abandon" && !rule.at("npcId").is_null())
            {
                const auto id = rule.at("npcId").get<std::string>();
                if (!NpcInRange(id)) { notice = Reason("NotInRange"); return; } body["npcId"] = id;
            }
            if (inAction == "Claim" && !q->definition.at("choiceRewards").empty())
            {
                if (choiceId.empty()) { notice = Reason("InvalidChoice"); return; }
                body["choiceId"] = choiceId;
            }
        }
        pendingAction = std::move(inAction); pendingJson = body.dump(); outgoingJson = pendingJson;
        pendingSeconds = 0; actionCooldown = 0.5f; confirmAbandon = false; notice = L"처리 중입니다.";
    }
    void QuestUi::LoadIcons()
    {
        std::erase_if(icons, [this](const auto& icon)
        {
            return std::none_of(state.Get().itemDefinitions.begin(), state.Get().itemDefinitions.end(),
                [&](const auto& row) { return row.second.at("icon") == icon.first; });
        });
        for (const auto& [id, definition] : state.Get().itemDefinitions)
        {
            const auto path = definition.at("icon").get<std::string>();
            if (path.empty() || icons.contains(path)) continue;
            auto& bitmap = icons[path];
            try { bitmap = renderer.LoadBitmap(assets.GetAssetPath(path)); } catch (const std::exception&) { /* Text remains readable without optional art. */ }
        }
    }
    std::wstring QuestUi::ItemName(const std::string& id) const
    {
        const auto found = state.Get().itemDefinitions.find(id);
        return found == state.Get().itemDefinitions.end() ? Wide(id) : Wide(found->second.at("name").get<std::string>());
    }
    std::wstring QuestUi::ObjectiveText(const QuestEntryView& q, const QuestObjectiveView& objective) const
    {
        const auto& steps = q.definition.at("steps");
        std::wstring label = L"목표";
        std::string description;
        for (const auto& step : steps) for (const auto& goal : step.at("objectives"))
        {
            if (goal.at("id") != objective.id) continue;
            if (goal.contains("description")) description = goal.at("description").get<std::string>();
            const auto type = goal.at("type").get<std::string>(); const auto& target = goal.at("target");
            if (type == "KillMonster") label = L"몬스터 " + std::to_wstring(target.at("monsterDataId").get<unsigned>()) + L" 처치";
            else if (type == "ClearDungeon") label = L"던전 " + std::to_wstring(target.at("dungeonId").get<unsigned>()) + L" 클리어";
            else if (type == "HoldItem") label = ItemName(target.at("itemDefinitionId")) + L" 보유 · 수령 시 제출";
            else if (type == "AcquireItem") label = ItemName(target.at("itemDefinitionId")) + L" 신규 획득 · 차감 없음";
            else if (type == "TalkNpc")
            {
                const auto id = target.at("npcId").get<std::string>(); label = Wide(id) + L" 대화";
                for (const auto& npc : state.Get().npcs) if (npc.id == id) label = Wide(npc.name) + L" 대화";
            }
            else if (type == "Reach") label = Wide(target.at("mapId").get<std::string>()) + L" 도달";
            break;
        }
        if (!description.empty()) label = Wide(description);
        else if (!objective.description.empty()) label = Wide(objective.description);
        return (objective.done ? L"✓ " : L"• ") + label + L"  " + std::to_wstring(objective.current) + L"/" + std::to_wstring(objective.required);
    }
    std::wstring QuestUi::RewardText(const Json& rewards) const
    {
        std::wstring text;
        for (const auto& reward : rewards)
        {
            if (!text.empty()) text += L" / ";
            if (reward.at("type") == "Item") text += ItemName(reward.at("definitionId")) + L" ×" + std::to_wstring(reward.at("count").get<unsigned>());
            else if (reward.at("type")=="Experience")
                text += L"경험치 +" + std::to_wstring(CharacterInventoryJson::UInt64(reward.at("amount")));
            else text += L"스킬 포인트 +" + std::to_wstring(reward.at("amount").get<unsigned>());
        }
        return text.empty() ? L"없음" : text;
    }
    std::wstring QuestUi::Details(const QuestEntryView& q) const
    {
        const auto& d = q.definition;
        std::wstring text = Wide(d.at("title").get<std::string>()) + L"\n" + Status(q.state);
        const auto repeat = d.at("repeat").get<std::string>();
        text += repeat == "Daily" ? L" · 일일" : repeat == "Weekly" ? L" · 주간" : L" · 일회";
        text += L"\n" + Wide(d.at("description").get<std::string>());
        const auto& accept = d.at("accept");
        text += L"\n수락: 레벨 " + std::to_wstring(accept.at("minLevel").get<unsigned>()) + L" 이상";
        text += accept.at("mode") == "Automatic" ? L" · 자동" : accept.at("mode") == "Npc" ? L" · NPC" : L" · 목록";
        if (!accept.at("prerequisites").empty())
        {
            text += L"\n선행 퀘스트: ";
            for (const auto& id : accept.at("prerequisites"))
            {
                auto title = id.get<std::string>();
                for (const auto& other : state.Get().quests) if (other.id == title) { title = other.definition.at("title"); break; }
                text += Wide(title) + L" ";
            }
        }
        if (!accept.at("mapIds").empty())
        { text += L"\n수락 지역: "; for (const auto& map : accept.at("mapIds")) text += Wide(map.get<std::string>()) + L" "; }
        if (q.state == "Completed") text += L"\n\n완료 기록";
        else if (Live(q)) text += L"\n\n현재 단계 " + std::to_wstring(std::min<std::size_t>(q.stageIndex+1,d.at("steps").size()))
            + L"/" + std::to_wstring(d.at("steps").size()) + L" (단계 안 목표는 함께 진행)";
        else text += L"\n\n첫 단계 목표 · 수락 후 진행";
        if (q.state == "Completed") text += L"\n이미 완료한 퀘스트입니다.";
        else for (const auto& objective : q.objectives) text += L"\n" + ObjectiveText(q, objective);
        if (q.objectives.empty()) text += L"\n" + (Live(q) ? Status(q.state) : L"수락 후 목표가 표시됩니다.");
        if (q.state == "ReadyToReport") text += L"\n퀘스트 게시판에 보고한 뒤 보상을 수령하세요.";
        if (!q.turnInItems.empty())
        {
            text += L"\n\n수령 시 제출 (장착 아이템 제외):";
            for (const auto& item : q.turnInItems) text += L"\n" + ItemName(item.definitionId) + L" ×" + std::to_wstring(item.count);
            text += L"\n제출 아이템이 부족하면 보상을 받을 수 없습니다.";
        }
        if (!d.at("event").is_null())
        {
            const auto& event = d.at("event");
            const char* keys[]{"visibility","acceptance","progress","reward"};
            const wchar_t* labels[]{L"노출",L"수락",L"진행",L"보상"};
            text += L"\n\n이벤트 기간 (UTC):";
            for (int i=0;i<4;++i) text += L"\n" + std::wstring(labels[i]) + L": " + Wide(event.at(keys[i]).at("startUtc").get<std::string>()) + L" ~ " + Wide(event.at(keys[i]).at("endUtc").get<std::string>());
        }
        if (!q.blockedReason.empty()) text += L"\n\n" + Reason(q.blockedReason);
        text += L"\n\n확정 보상";
        return text;
    }
    float QuestUi::DetailsHeight(const QuestEntryView& q, float width) const
    { return renderer.MeasureUiText(Details(q), width-24, 14).height + 12 + q.definition.at("fixedRewards").size()*40.0f; }

    QuestUiAction QuestUi::Update(float delta, const InputState& rawInput, float width, float height, float hudTop,
        bool visible, bool worldInteractive, bool trackerVisible, Vector2 inPlayer, const Camera& camera)
    {
        QuestUiAction result;
        hoveredNpcId.clear();
        const float worldHudTop = hudTop;
        playerPosition = inPlayer;
        querySeconds += delta; refreshCooldown = std::max(0.0f,refreshCooldown-delta); actionCooldown = std::max(0.0f,actionCooldown-delta);
        if (!pendingJson.empty())
        {
            pendingSeconds += delta;
            if (pendingSeconds >= 10)
            { notice = L"처리 결과를 확인하지 못했습니다. ‘결과 확인’을 눌러 주세요."; wantRefresh = true; }
        }
        if (!queryId.empty() && querySeconds >= 10) { queryId.clear(); wantRefresh = true; }
        if (connected && state.GetCharacterId() != 0 && (wantRefresh || !state.IsReady()) && queryId.empty() && refreshCooldown <= 0)
        {
            try { queryId = CharacterInventoryJson::NewRequestId(); result.stateRequestId = queryId; querySeconds = 0; refreshCooldown = 1.1f; }
            catch (const std::exception&) { notice = L"퀘스트 조회 요청을 만들지 못했습니다."; refreshCooldown = 2; }
        }
        const float scale = HudLayout::Scale(width);
        const auto input = HudLayout::ScaleInput(rawInput,1.0f/scale);
        width /= scale; height /= scale; hudTop /= scale;
        const auto trackerBounds = TrackerBounds(width,height,hudTop);
        const bool hasTracker = trackerVisible && !trackerHidden && !Tracked().empty();
        trackerHovered = hasTracker && Contains(trackerBounds,input.mouseX,input.mouseY);
        if (rawInput.cancelDrag || !hasTracker) trackerDragging = false;
        if (trackerDragging)
        {
            if (input.leftMouseDown)
            {
                trackerOffset = {trackerDragOffset.x+input.mouseX-trackerDragMouse.x,
                    trackerDragOffset.y+input.mouseY-trackerDragMouse.y};
                result.consumePointer = true;
            }
            else trackerDragging = false;
        }
        if (trackerHovered && input.leftMousePressed && input.clickY < trackerBounds.top+28)
        {
            result.consumePointer = true;
            if (Contains({trackerBounds.right-24,trackerBounds.top+2,trackerBounds.right-2,trackerBounds.top+25},input.clickX,input.clickY))
            { trackerHidden = true; trackerDragging = false; }
            else if (input.clickY < trackerBounds.top+28)
            {
                trackerDragging = true; trackerDragMouse = {input.mouseX,input.mouseY};
                trackerOffset={};
                const auto origin=TrackerBounds(width,height,hudTop);
                trackerOffset={trackerBounds.left-origin.left,trackerBounds.top-origin.top};
                trackerDragOffset=trackerOffset;
            }
        }
        if (worldInteractive && connected && rawInput.mouseY < worldHudTop
            && !(hasTracker && Contains(trackerBounds,input.mouseX,input.mouseY)))
            if (const auto npc = PickNpc(camera,rawInput.mouseX,rawInput.mouseY)) hoveredNpcId = npc->id;
        const auto panel=CalculateLayout(width,height,hudTop).panel;
        const bool pointerInPanel=visible && Contains(panel,input.mouseX,input.mouseY);
        if (visible)
        {
            const auto l = CalculateLayout(width,height,hudTop);
            if (input.mouseWheelDelta && Contains(l.list,input.mouseX,input.mouseY)) listScroll -= input.mouseWheelDelta/120.0f*52;
            if (input.mouseWheelDelta && Contains(l.detail,input.mouseX,input.mouseY)) detailScroll -= input.mouseWheelDelta/120.0f*42;
            if (input.leftMousePressed && Contains(l.panel,input.clickX,input.clickY))
            {
                result.consumePointer = true;
                for (int i=0;i<3;++i) if (Contains(l.tabs[i],input.clickX,input.clickY) && !confirmAbandon)
                { category=i; selectedId.clear(); selectedCycle.clear(); choiceId.clear(); listScroll=detailScroll=0; }
            }
            const auto entries = Filtered();
            const float listHeight=l.list.bottom-l.list.top;
            listScroll=std::clamp(listScroll,0.0f,std::max(0.0f,entries.size()*56.0f-listHeight));
            if (selectedId.empty() && !entries.empty()) { selectedId=entries.front()->id; selectedCycle=entries.front()->cycleKey; }
            if (input.leftMousePressed && Contains(l.list,input.clickX,input.clickY) && !confirmAbandon)
            {
                const auto index=static_cast<std::size_t>((input.clickY-l.list.top+listScroll)/56);
                if (index<entries.size() && (selectedId!=entries[index]->id || selectedCycle!=entries[index]->cycleKey))
                { selectedId=entries[index]->id;selectedCycle=entries[index]->cycleKey;choiceId.clear();detailScroll=0; }
            }
            if(input.leftMousePressed && !confirmAbandon)
            {
                if(!pendingJson.empty() && pendingSeconds>=10 && connected && actionCooldown<=0 && Contains(l.retry,input.clickX,input.clickY))
                { outgoingJson=pendingJson;pendingSeconds=0;actionCooldown=1.0f;notice=L"같은 요청의 결과를 확인합니다."; }
                else if(!npcId.empty() && Contains(l.npcTalk,input.clickX,input.clickY)) BeginAction("Talk");
            }
            if (const auto q=Selected())
            {
                float content=DetailsHeight(*q,l.detail.right-l.detail.left)+28;
                for(const auto& choice:q->definition.at("choiceRewards")) content+=ChoiceHeight(renderer,RewardText(choice.at("rewards")),l.detail.right-l.detail.left-24)+8;
                detailScroll=std::clamp(detailScroll,0.0f,std::max(0.0f,content-(l.detail.bottom-l.detail.top)));
                if(input.leftMousePressed)
                {
                    if (confirmAbandon)
                    {
                        if(Contains(l.abandon,input.clickX,input.clickY)) confirmAbandon=false;
                        else if(q->canAbandon && Contains(l.primary,input.clickX,input.clickY)) BeginAction("Abandon",q);
                    }
                    else
                    {
                        float y=l.detail.top+DetailsHeight(*q,l.detail.right-l.detail.left)+28-detailScroll;
                        if(Contains(l.detail,input.clickX,input.clickY)) for(const auto& choice:q->definition.at("choiceRewards"))
                        {
                            const float ch=ChoiceHeight(renderer,RewardText(choice.at("rewards")),l.detail.right-l.detail.left-24);
                            if(CanAct() && Contains({l.detail.left+12,y,l.detail.right-12,y+ch},input.clickX,input.clickY)) choiceId=choice.at("id").get<std::string>();
                            y+=ch+8;
                        }
                        if(CanAct() && Contains(l.abandon,input.clickX,input.clickY) && q->canAbandon) confirmAbandon=true;
                        if(CanAct() && Contains(l.primary,input.clickX,input.clickY))
                        {
                            if(q->canClaim) BeginAction("Claim",q);
                            else if(q->canReport) BeginAction("Report",q);
                            else if(q->canAccept) BeginAction("Accept",q);
                        }
                        if(CanAct() && Contains(l.track,input.clickX,input.clickY) && Live(*q))
                        {
                            auto tracked=state.Get().trackedQuestIds;
                            const auto found=std::find(tracked.begin(),tracked.end(),q->id);
                            if(found!=tracked.end()) tracked.erase(found);
                            else if(tracked.size()<3) tracked.push_back(q->id);
                            else notice=L"퀘스트는 최대 3개까지 추적할 수 있습니다.";
                            if(tracked!=state.Get().trackedQuestIds) BeginAction("Track",nullptr,std::move(tracked));
                        }
                    }
                }
            }
        }
        if ((worldInteractive || hasTracker) && input.leftMousePressed && !trackerDragging && !pointerInPanel)
        {
            if (!result.consumePointer && worldInteractive && connected && rawInput.clickY < worldHudTop
                && !(hasTracker && Contains(trackerBounds,input.clickX,input.clickY)))
                if (const auto npc = PickNpc(camera,rawInput.clickX,rawInput.clickY))
                {
                    result.consumePointer = true;
                    if (npcVisuals.at(npc->id).quests)
                    {
                        npcId=npc->id;selectedId.clear();selectedCycle.clear();choiceId.clear();detailScroll=listScroll=0;
                        result.openWindow=true;
                        hoveredNpcId.clear();
                        if(!NpcInRange(npc->id)) notice=Reason("NotInRange");
                        else BeginAction("Talk");
                    }
                }
            if (!result.consumePointer && hasTracker)
            {
                const auto rect=TrackerBounds(width,height,hudTop);
                if(Contains(rect,input.clickX,input.clickY))
                {
                    const auto tracked=Tracked(); const int index=static_cast<int>((input.clickY-rect.top-28)/76);
                    if(input.clickY>=rect.top+28 && index>=0 && static_cast<std::size_t>(index)<tracked.size())
                    {
                        const auto q=tracked[index]; selectedId=q->id;selectedCycle=q->cycleKey;
                        for(int i=0;i<3;++i) if(q->definition.at("category")==CATEGORIES[i]) category=i;
                        npcId.clear();detailScroll=0;result.openWindow=result.consumePointer=true;
                    }
                }
            }
        }
        result.operationJson=std::exchange(outgoingJson,{});
        result.refreshProgression=std::exchange(refreshProgression,false);
        return result;
    }

    void QuestUi::Render(D2DRenderer& draw, float width, float height, float hudTop) const
    {
        const float scale=HudLayout::Scale(width);
        const D2DRenderer::UiScaleScope scope(draw,scale);
        width/=scale;height/=scale;hudTop/=scale;
        const auto l=CalculateLayout(width,height,hudTop);
        const auto text=D2D1::ColorF(0xF0EAE0), muted=D2D1::ColorF(0xA5AFBD), gold=D2D1::ColorF(0xEACB7C);
        const auto button=[&](D2D1_RECT_F r,const std::wstring& label,bool enabled)
        {
            draw.FillRectangle(r.left,r.top,r.right,r.bottom,D2D1::ColorF(enabled?0x304451:0x202731));
            draw.DrawRectangle(r.left,r.top,r.right,r.bottom,D2D1::ColorF(enabled?0xBDA46C:0x525A65));
            draw.DrawUiText(label,r,enabled?text:muted,14,false,true);
        };
        draw.FillRectangle(l.panel.left,l.panel.top,l.panel.right,l.panel.bottom,D2D1::ColorF(0.035f,0.05f,0.075f,0.98f));
        draw.DrawRectangle(l.panel.left,l.panel.top,l.panel.right,l.panel.bottom,D2D1::ColorF(0xBDA46C));
        std::wstring title=L"퀘스트";
        if(!npcId.empty()) for(const auto& npc:state.Get().npcs) if(npc.id==npcId) title+=L" · "+Wide(npc.name);
        draw.DrawUiText(title,{l.panel.left+16,l.panel.top+9,l.panel.right-190,l.panel.top+36},gold,20);
        draw.DrawUiText(L"ESC · 닫기",{l.panel.right-174,l.panel.top+10,l.panel.right-36,l.panel.top+34},muted,13,false,true);
        for(int i=0;i<3;++i)
        {
            button(l.tabs[i],CATEGORY_NAMES[i],!confirmAbandon);
            if(category==i) draw.DrawRectangle(l.tabs[i].left+1,l.tabs[i].top+1,l.tabs[i].right-1,l.tabs[i].bottom-1,gold,2);
        }
        if(!npcId.empty()) button(l.npcTalk,npcId=="TownGuide"?L"게시판 확인":L"대화",CanAct() && NpcInRange(npcId) && !confirmAbandon);
        draw.FillRectangle(l.list.left,l.list.top,l.list.right,l.list.bottom,D2D1::ColorF(0x111923));
        draw.PushAxisAlignedClip(l.list);
        const auto entries=Filtered();
        float y=l.list.top-listScroll;
        for(const auto q:entries)
        {
            if(y+56>=l.list.top && y<l.list.bottom)
            {
                if(q->id==selectedId && q->cycleKey==selectedCycle) draw.FillRectangle(l.list.left,y,l.list.right,y+54,D2D1::ColorF(0x304451));
                draw.DrawUiText(Wide(q->definition.at("title").get<std::string>()),{l.list.left+8,y+4,l.list.right-8,y+29},text,14);
                std::wstring line=Status(q->state);
                if(q->definition.at("repeat")!="Once") line+=L" · "+Wide(q->cycleKey);
                draw.DrawUiText(line,{l.list.left+8,y+30,l.list.right-8,y+51},Live(*q)?gold:muted,12);
            }
            y+=56;
        }
        if(entries.empty()) draw.DrawUiText(state.IsReady()?L"해당 퀘스트가 없습니다.":L"퀘스트 상태를 조회하고 있습니다.",l.list,muted,14,true);
        draw.PopAxisAlignedClip();
        const auto q=Selected();
        if(q)
        {
            draw.PushAxisAlignedClip(l.detail);
            const float innerWidth=l.detail.right-l.detail.left-24;
            const auto details=Details(*q);
            const float textHeight=draw.MeasureUiText(details,innerWidth,14).height;
            y=l.detail.top-detailScroll;
            draw.DrawUiText(details,{l.detail.left+12,y,l.detail.right-12,y+textHeight+2},text,14,true);
            y+=textHeight+12;
            const auto rewardIcon=[&](const Json& reward,float atY)
            {
                const auto rect=D2D1::RectF(l.detail.left+12,atY+3,l.detail.left+46,atY+37);
                if(reward.at("type")=="Item")
                {
                    const auto found=state.Get().itemDefinitions.find(reward.at("definitionId").get<std::string>());
                    if(found!=state.Get().itemDefinitions.end())
                    {
                        const auto bitmap=icons.find(found->second.at("icon").get<std::string>());
                        if(bitmap!=icons.end() && bitmap->second) draw.DrawUiIcon(bitmap->second.Get(),rect,false);
                    }
                }
                else draw.DrawUiText(reward.at("type")=="Experience" ? L"EXP":L"SP",rect,gold,14,false,true);
            };
            for(const auto& reward:q->definition.at("fixedRewards"))
            {
                rewardIcon(reward,y);
                draw.DrawUiText(RewardText(Json::array({reward})),{l.detail.left+54,y,l.detail.right-12,y+40},text,14,true);
                y+=40;
            }
            const auto& choices=q->definition.at("choiceRewards");
            if(!choices.empty()) draw.DrawUiText(L"선택 보상 · 아래에서 정확히 한 묶음 선택",{l.detail.left+12,y,l.detail.right-12,y+25},gold,14);
            y+=28;
            for(const auto& choice:choices)
            {
                const auto label=RewardText(choice.at("rewards"));
                const float ch=ChoiceHeight(draw,label,l.detail.right-l.detail.left-24);
                const bool selected=choice.at("id")==choiceId;
                draw.FillRectangle(l.detail.left+12,y,l.detail.right-12,y+ch,D2D1::ColorF(selected?0x344A4D:0x1B2731));
                draw.DrawRectangle(l.detail.left+12,y,l.detail.right-12,y+ch,selected?gold:muted);
                if(!choice.at("rewards").empty()) rewardIcon(choice.at("rewards").front(),y+(ch-40)/2);
                draw.DrawUiText((selected?L"✓ ":L"○ ")+label,{l.detail.left+58,y+8,l.detail.right-20,y+ch-8},text,13,true);
                y+=ch+8;
            }
            draw.PopAxisAlignedClip();
            const bool tracked=std::find(state.Get().trackedQuestIds.begin(),state.Get().trackedQuestIds.end(),q->id)!=state.Get().trackedQuestIds.end();
            button(l.track,tracked?L"추적 해제":L"목표 추적",CanAct() && Live(*q) && !confirmAbandon);
            if(confirmAbandon)
            {
                draw.FillRectangle(l.panel.left+16,l.panel.bottom-118,l.panel.right-16,l.panel.bottom-84,D2D1::ColorF(0x502B24));
                draw.DrawUiText(L"포기하면 진행도가 초기화됩니다. 포기하시겠습니까?",{l.panel.left+24,l.panel.bottom-114,l.panel.right-24,l.panel.bottom-86},text,14);
                button(l.abandon,L"취소",true);button(l.primary,L"포기 확인",CanAct() && q->canAbandon);
            }
            else
            {
                button(l.abandon,L"포기",CanAct() && q->canAbandon);
                const bool choiceReady=choices.empty() || !choiceId.empty();
                const std::wstring primary=q->state=="RewardPending"?L"보상 수령":q->state=="ReadyToReport"?L"완료 보고":L"수락";
                button(l.primary,primary,CanAct() && (q->canAccept || q->canReport || (q->canClaim && choiceReady)));
            }
        }
        if(!pendingJson.empty() && pendingSeconds>=10) button(l.retry,L"결과 확인",connected && actionCooldown<=0);
        std::wstring message=notice;
        if(!connected) message=L"연결을 확인해 주세요. 퀘스트 처리는 중지되었습니다.";
        else if(!state.IsReady() && pendingJson.empty()) message=L"최신 퀘스트 상태를 조회하고 있습니다.";
        draw.DrawUiText(message,{l.panel.left+16,l.panel.bottom-36,l.panel.right-16,l.panel.bottom-3},muted,12,true);
    }
    void QuestUi::RenderTracker(D2DRenderer& draw,float width,float height,float hudTop) const
    {
        const auto quests=Tracked();
        if(quests.empty() || trackerHidden) return;
        const float scale=HudLayout::Scale(width);
        const D2DRenderer::UiScaleScope scope(draw,scale);
        const auto rect=TrackerBounds(width/scale,height/scale,hudTop/scale);
        if (trackerHovered || trackerDragging)
        {
            draw.FillRectangle(rect.left,rect.top,rect.right,rect.bottom,D2D1::ColorF(0.025f,0.04f,0.06f,0.58f));
            draw.DrawUiText(L"X",{rect.right-24,rect.top+2,rect.right-2,rect.top+25},D2D1::ColorF(0xEAEFF5),14,false,true);
        }
        draw.DrawUiText(state.IsReady()?L"목표 추적":L"목표 추적 · 상태 확인 중",{rect.left+10,rect.top+4,rect.right-28,rect.top+25},D2D1::ColorF(0xEACB7C),14);
        draw.PushAxisAlignedClip(rect);
        float y=rect.top+28;
        for(const auto q:quests)
        {
            draw.DrawUiText(Wide(q->definition.at("title").get<std::string>()),{rect.left+10,y,rect.right-10,y+22},D2D1::ColorF(0xF0EAE0),14);
            std::wstring summary;
            if(q->state!="Active") summary=Status(q->state);
            else
            {
                const auto done=std::count_if(q->objectives.begin(),q->objectives.end(),[](const auto& o){return o.done;});
                summary=L"단계 "+std::to_wstring(q->stageIndex+1)+L" · 목표 "+std::to_wstring(done)+L"/"+std::to_wstring(q->objectives.size());
                if(!q->objectives.empty()) summary+=L"\n"+ObjectiveText(*q,q->objectives.front());
            }
            draw.DrawUiText(summary,{rect.left+10,y+23,rect.right-10,y+70},D2D1::ColorF(0xC9D4DF),12,true);
            y+=76;
        }
        draw.PopAxisAlignedClip();
    }
    void QuestUi::RenderNpcs(D2DRenderer& draw,const Camera& camera, float inPlayerDepth, bool inForeground) const
    {
        for (const auto& npc : state.Get().npcs)
        {
            if (npc.mapId != mapId || (npc.y > inPlayerDepth) != inForeground) continue;
            const auto found = npcVisuals.find(npc.id);
            if (found == npcVisuals.end()) continue;
            const auto& visual = found->second;
            const auto rect = NpcBounds(npc,visual,camera);
            const auto p = camera.WorldToScreen({npc.x,npc.y});
            const auto color = D2D1::ColorF(npc.canReport||npc.canClaim?0x7EE0EE:npc.canAccept?0xF1D26F:0xE4E9EF);
            draw.FillEllipse(p.x,p.y,(rect.right-rect.left)*0.28f,7,D2D1::ColorF(0.04f,0.05f,0.07f,0.5f));
            if (hoveredNpcId == npc.id)
                draw.DrawBitmapOutline(visual.bitmap.Get(),visual.source,rect,D2D1::ColorF(0xA8FF60),2.0f);
            draw.DrawBitmap(visual.bitmap.Get(),visual.source,rect);
            const auto name = Wide(npc.name);
            const float nameWidth = std::max(92.0f,draw.MeasureUiText(name,280,14).width+16);
            const D2D1_RECT_F label{p.x-nameWidth*0.5f,rect.top-30,p.x+nameWidth*0.5f,rect.top-6};
            draw.FillRectangle(label.left,label.top,label.right,label.bottom,D2D1::ColorF(0.03f,0.05f,0.08f,0.8f));
            draw.DrawUiText(name,label,color,14,false,true);
            if (npc.canReport || npc.canClaim || npc.canAccept)
                draw.DrawUiText(npc.canReport||npc.canClaim?L"?":L"!",
                    {p.x-18,label.top-35,p.x+18,label.top},color,26,false,true);
        }
    }
}
