#include "Game/SkillUi.h"
#include "Graphics/D2DRenderer.h"
#include "Resources/AssetCatalog.h"
#include "Platform/GameWindow.h"
#include <Windows.h>
#include <d2d1_1helper.h>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <unordered_set>

namespace
{
    using Json = nlohmann::json;
    bool Contains(const D2D1_RECT_F& rect, float x, float y)
    { return x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom; }
    bool Overlaps(const D2D1_RECT_F& a, const D2D1_RECT_F& b)
    { return a.left < b.right && a.right > b.left && a.top < b.bottom && a.bottom > b.top; }
    std::wstring Wide(const std::string& text)
    {
        if (text.empty()) return {};
        const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
        if (count <= 0) throw std::runtime_error("Invalid skill UI text.");
        std::wstring result(count, L'\0');
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), result.data(), count);
        return result;
    }
    D2D1_RECT_F Offset(const D2D1_RECT_F& rect, float x, float y, float width, float height)
    { return D2D1::RectF(rect.left+x, rect.top+y, rect.left+x+width, rect.top+y+height); }
    std::wstring Reason(const std::string& value)
    {
        if (value == "Succeeded") return L"사용 가능";
        if (value == "RequiredLevel") return L"요구 캐릭터 레벨 부족";
        if (value == "InsufficientSp") return L"SP 부족";
        if (value == "Prerequisite") return L"선행 스킬 조건 미충족";
        if (value == "MaximumSkillLevel") return L"최대 스킬 레벨";
        if (value == "WrongCharacter") return L"다른 캐릭터의 스킬";
        if (value == "StaleSkillLevel") return L"습득 상태가 변경되었습니다";
        if (value == "InDungeon") return L"던전에서는 스킬을 습득·강화할 수 없습니다";
        if (value == "LevelAdvanced") return L"캐릭터 레벨과 SP가 갱신되었습니다";
        return Wide(value);
    }
    std::wstring CooldownText(float seconds)
    {
        std::wostringstream text;
        if (seconds < 60) text << std::fixed << std::setprecision(1) << std::ceil(seconds*10)/10 << L"초";
        else text << std::max<std::uint32_t>(1,static_cast<std::uint32_t>(std::floor(seconds/60))) << L"m";
        return text.str();
    }
}

namespace ActionRPG
{
    SkillUi::SkillUi(const AssetCatalog& inAssets, D2DRenderer& inRenderer) : assets(inAssets), renderer(inRenderer)
    {
        const auto path = assets.GetDataPath("SkillUi");
        PlayerSkills::Catalog::Require(std::filesystem::file_size(path) <= 64*1024, "Oversized SkillUi configuration.");
        std::ifstream input(path); input >> settings;
        PlayerSkills::Catalog::Require(settings.at("format") == "SkillUi" && settings.at("schemaVersion") == 1, "Unsupported SkillUi configuration.");
        const auto check = [this](const char* section, std::initializer_list<const char*> keys)
        { for (const auto key : keys) Number(settings.at(section), key); };
        check("layout", {"referenceWidth","referenceHeight","minimumWidth","minimumHeight","compactWidth","compactHeight","stackedWidth","dragThreshold","learnTimeoutSeconds"});
        for (const auto section : {"normalBar","compactBar"})
        {
            check(section, {"slotWidth","slotHeight","iconLeft","iconTop","iconSize","keyLeft","keyTop","keyWidth","keyHeight","cooldownLeft","cooldownTop","cooldownWidth","cooldownHeight","gap","padding","bottomMargin","keyFontSize","cooldownFontSize"});
            const auto& bar = settings.at(section);
            PlayerSkills::Catalog::Require(Number(bar,"iconLeft")+Number(bar,"iconSize") <= Number(bar,"slotWidth")
                && Number(bar,"iconTop")+Number(bar,"iconSize") <= Number(bar,"keyTop")
                && Number(bar,"keyTop")+Number(bar,"keyHeight") <= Number(bar,"slotHeight"), "Skill slot content exceeds its frame.");
        }
        for (const auto section : {"normalWindow","compactWindow"}) check(section, {"width","height","margin","barGap","padding","headerHeight","footerHeight","contentGap","columnGap","treeRatio","tabHeight","detailIconSize","learnButtonHeight"});
        for (const auto section : {"node","compactNode"}) check(section, {"width","height","iconLeft","iconTop","iconSize","nameLeft","nameTop","nameWidth","nameHeight","columnGap","rowGap"});
        check("fonts", {"title","body","small","lineHeight"});
        renderer.SetUiFontFamily(Wide(settings.at("fonts").at("family").get<std::string>()));
        for (const auto name : {"panel","border","selected","text","cooldownText","cooldownBacking","locked"}) Color(name);
        const auto [minimumWidth,minimumHeight]=GetMinimumClientSize();
        GameWindow::SetMinimumClientSize(minimumWidth,minimumHeight);
        catalog = PlayerSkills::Catalog::Load(assets.GetDataPath("PlayerSkills"));
        status = L"스킬 상태를 기다리는 중...";
    }

    float SkillUi::Number(const Json& object, const char* key) const
    { return static_cast<float>(PlayerSkills::Catalog::Number(object.at(key), 0.001, 16384)); }
    D2D1_COLOR_F SkillUi::Color(const char* key) const
    {
        const auto& color = settings.at("colors").at(key);
        PlayerSkills::Catalog::Require(color.is_array() && color.size() == 4, "Invalid UI color.");
        return D2D1::ColorF(static_cast<float>(PlayerSkills::Catalog::Number(color[0],0,1)), static_cast<float>(PlayerSkills::Catalog::Number(color[1],0,1)),
            static_cast<float>(PlayerSkills::Catalog::Number(color[2],0,1)), static_cast<float>(PlayerSkills::Catalog::Number(color[3],0,1)));
    }
    std::pair<std::uint32_t,std::uint32_t> SkillUi::GetMinimumClientSize() const
    { return {static_cast<std::uint32_t>(Number(settings.at("layout"),"minimumWidth")), static_cast<std::uint32_t>(Number(settings.at("layout"),"minimumHeight"))}; }

    void SkillUi::ApplyState(std::string_view inPayload, std::uint32_t inCharacterId)
    {
        PlayerSkills::Catalog::Require(inPayload.size() <= 1024*1024, "Oversized skill state.");
        const auto value = Json::parse(inPayload);
        PlayerSkills::Catalog::Keys(value, {"characterId","progression","skillTrees","playerSkills","result"});
        const auto newCharacter = PlayerSkills::Catalog::Integer(value.at("characterId"),1,1000000);
        PlayerSkills::Catalog::Require(newCharacter == inCharacterId, "Skill state belongs to another character.");
        const auto newCatalog = PlayerSkills::Catalog::Parse(value.at("playerSkills"));
        PlayerSkills::Catalog::Require(newCatalog.source == catalog.source, "Client/server skill catalogs differ.");
        auto newTrees = PlayerSkills::SkillTreeCatalog::Parse(value.at("skillTrees"),newCatalog);
        auto newProgression = PlayerSkills::CharacterProgression::Parse(value.at("progression"));
        newTrees.ValidateProgression(newCatalog,newProgression,newCharacter);
        const auto result = value.at("result").get<std::string>();
        PlayerSkills::Catalog::Require(result.size() <= 128,"Invalid skill result.");
        const bool changed = characterId != newCharacter || !ready;
        characterId = newCharacter; trees = std::move(newTrees); progression = std::move(newProgression);
        ready = true; learnPending = false; learnSeconds = 0; CancelDrag();
        if (changed) { slots.fill({}); LoadSlots(); treeScrollX = treeScrollY = detailScroll = 0; }
        for (const auto& [id,node] : trees.nodes)
        {
            if (!icons.contains(id))
            {
                try { icons.emplace(id,renderer.LoadBitmap(assets.GetAssetPath(node.at("icon").get<std::string>()))); }
                catch (const std::exception&) { /* Graphics may arrive later; keep a readable placeholder. */ }
            }
        }
        for (auto& id : slots) if (!id.empty() && (!IsOwned(id) || progression.GetSkillLevel(id) == 0)) id.clear();
        const auto ordered = OrderedSkills();
        if (selected.empty() || !IsOwned(selected)) selected = ordered.empty() ? "" : ordered.front();
        status = result == "Succeeded" ? L"습득 상태를 업데이트했습니다" : (result == "State" || result.empty() ? L"습득한 스킬을 아래 슬롯으로 드래그하세요" : Reason(result));
    }
    void SkillUi::ApplyCombatState(const CombatPlayerState& inState)
    {
        // Realtime SKL1 has no progression/cooldown fields. Never replace trusted JSON with defaults.
        if (!ready || inState.characterId != characterId || !inState.hasSkillState) return;
        PlayerSkills::CharacterProgression next{inState.level,inState.skillPoints,inState.skillLevels};
        trees.ValidateProgression(catalog,next,characterId);
        // Town owns progression. Room JSON may predate a TCP learning response, so
        // validate its progression without overwriting the latest Town permission state.
        cooldowns = inState.skillCooldowns; combatReady = true;
    }
    void SkillUi::AcceptSkill(const std::string& inId)
    { if (ready && IsOwned(inId)) cooldowns[inId]=catalog.skills.at(inId).at("cooldownSeconds").get<float>(); }
    void SkillUi::ApplyTownCooldowns(const std::unordered_map<std::string,float>& inCooldowns) { cooldowns = inCooldowns; }
    void SkillUi::ResetCombatState() { combatReady = false; cooldowns.clear(); CancelDrag(); }
    void SkillUi::Invalidate() { ready = false; learnPending = false; ResetCombatState(); status = L"스킬 상태를 다시 요청해야 합니다"; }
    void SkillUi::CancelDrag() { dragging.clear(); dragCandidate.clear(); }
    bool SkillUi::IsOwned(const std::string& id) const
    { return trees.nodes.contains(id) && catalog.skills.contains(id) && catalog.characterIds.at(catalog.skills.at(id).at("characterId").get<std::string>()) == characterId; }
    float SkillUi::Cooldown(const std::string& id) const
    { const auto found = cooldowns.find(id); return found == cooldowns.end() ? 0 : found->second; }
    bool SkillUi::CanUse(const std::string& id, bool airborne, bool dungeon) const
    {
        return ready && IsOwned(id) && progression.GetSkillLevel(id) > 0 && Cooldown(id) <= 0
            && !catalog.skills.at(id).at(airborne ? "air" : "ground").is_null()
            && (dungeon ? combatReady : connected);
    }
    std::string SkillUi::Hotkey(const InputState& input) const
    {
        for (const auto key : input.pressedKeys)
        {
            const auto index = static_cast<int>(key) - static_cast<int>(InputKey::SkillA);
            if (index >= 0 && index < static_cast<int>(slots.size()) && !slots[index].empty()) return slots[index];
        }
        return {};
    }
    std::vector<std::string> SkillUi::OrderedSkills() const
    {
        std::vector<std::string> result;
        for (const auto& [id,node] : trees.nodes) if (IsOwned(id)) result.push_back(id);
        std::sort(result.begin(),result.end(),[this](const auto& a,const auto& b)
        {
            const auto& first = trees.nodes.at(a); const auto& second = trees.nodes.at(b);
            if (first.at("row") != second.at("row")) return first.at("row") < second.at("row");
            if (first.at("column") != second.at("column")) return first.at("column") < second.at("column");
            return a < b;
        });
        return result;
    }
    SkillUi::Layout SkillUi::CalculateLayout(float width,float height) const
    {
        Layout result;
        result.compact = width < Number(settings.at("layout"),"compactWidth") || height < Number(settings.at("layout"),"compactHeight");
        result.stacked = width < Number(settings.at("layout"),"stackedWidth");
        const auto& bar = settings.at(result.compact ? "compactBar" : "normalBar");
        const auto& window = settings.at(result.compact ? "compactWindow" : "normalWindow");
        const float padding = Number(bar,"padding"), gap = Number(bar,"gap");
        const float barWidth = slots.size()*Number(bar,"slotWidth")+(slots.size()-1)*gap+2*padding;
        const float barHeight = Number(bar,"slotHeight")+2*padding;
        result.bar = D2D1::RectF((width-barWidth)/2,height-Number(bar,"bottomMargin")-barHeight,(width+barWidth)/2,height-Number(bar,"bottomMargin"));
        for (std::size_t i = 0;i < slots.size();++i) result.slots[i] = Offset(result.bar,padding+i*(Number(bar,"slotWidth")+gap),padding,Number(bar,"slotWidth"),Number(bar,"slotHeight"));
        const float margin = Number(window,"margin"), available = std::max(0.0f,result.bar.top-Number(window,"barGap")-margin);
        const float panelWidth = std::min(Number(window,"width"),std::max(0.0f,width-2*margin)), panelHeight = std::min(Number(window,"height"),available);
        result.panel = D2D1::RectF((width-panelWidth)/2,margin+(available-panelHeight)/2,(width+panelWidth)/2,margin+(available+panelHeight)/2);
        const float inner = Number(window,"padding"), contentGap = Number(window,"contentGap");
        const float left = result.panel.left+inner,right = result.panel.right-inner;
        const float top = result.panel.top+inner+Number(window,"headerHeight")+contentGap;
        const float bottom = result.panel.bottom-inner-Number(window,"footerHeight")-contentGap;
        const float treeWidth = (right-left)*Number(window,"treeRatio");
        result.tree = D2D1::RectF(left,top,left+treeWidth,bottom);
        result.detail = D2D1::RectF(result.tree.right+Number(window,"columnGap"),top,right,bottom);
        if (result.stacked)
        {
            const float tabHeight = Number(window,"tabHeight");
            result.treeTab = D2D1::RectF(left,top,(left+right)/2,top+tabHeight);
            result.detailTab = D2D1::RectF((left+right)/2,top,right,top+tabHeight);
            result.tree = result.detail = D2D1::RectF(left,top+tabHeight+contentGap,right,bottom);
        }
        result.learnButton = D2D1::RectF(result.detail.left,result.detail.bottom-Number(window,"learnButtonHeight"),result.detail.right,result.detail.bottom);
        return result;
    }
    D2D1_RECT_F SkillUi::NodeRectangle(const Layout& layout,const std::string& id) const
    {
        const auto& node = settings.at(layout.compact ? "compactNode":"node"); const auto& definition = trees.nodes.at(id);
        return Offset(layout.tree,definition.at("column").get<float>()*(Number(node,"width")+Number(node,"columnGap"))-treeScrollX,
            definition.at("row").get<float>()*(Number(node,"height")+Number(node,"rowGap"))-treeScrollY,Number(node,"width"),Number(node,"height"));
    }
    std::wstring SkillUi::LearnReason(const std::string& id) const
    { return !learningAllowed ? L"던전에서는 습득·강화 불가" : !connected ? L"마을 서버 연결을 기다리는 중" : Reason(trees.CanLearn(catalog,progression,characterId,id,progression.GetSkillLevel(id))); }
    std::wstring SkillUi::Details(const std::string& id) const
    {
        const auto& skill = catalog.skills.at(id); const auto& node = trees.nodes.at(id);
        const auto current = progression.GetSkillLevel(id), next = current+1;
        std::wstring text = Wide(skill.at("name").get<std::string>())+L"\n"+Wide(node.at("description").get<std::string>())+L"\n\n";
        text += L"스킬 레벨: "+std::to_wstring(current)+L" / "+(node.at("maxSkillLevel").is_null() ? L"상한 없음" : std::to_wstring(node.at("maxSkillLevel").get<std::uint32_t>()))+L"\n";
        std::wostringstream cooldown; cooldown << skill.at("cooldownSeconds").get<float>();
        text += L"쿨타임: "+cooldown.str()+L"초\n";
        if (skill.at("execution").contains("damage"))
            text += L"공격력: "+std::to_wstring(trees.Damage(catalog,id,current ? current : 1))+(current ? L"" : L" (습득 시)")+L"\n다음 단계 공격력: "+std::to_wstring(trees.Damage(catalog,id,next))+L"\n";
        else text += L"버프: "+Wide(skill.at("execution").at("stat").get<std::string>())+L" × "+std::to_wstring(skill.at("execution").at("multiplier").get<float>())+L"\n";
        text += L"다음 단계 요구 레벨: "+std::to_wstring(trees.RequiredLevel(id,next))+L"\n필요 SP: "+std::to_wstring(node.at("spCost").get<std::uint32_t>())+L" (보유 "+std::to_wstring(progression.skillPoints)+L")\n";
        text += L"사용 위치: "+std::wstring(skill.at("air").is_null() ? L"지상" : skill.at("ground").is_null() ? L"공중" : L"지상/공중")+L"\n커맨드: ";
        for (const auto& key : skill.at("input").at("command")) text += Wide(key.get<std::string>())+L" ";
        text += L"\n입력 간격: "+std::to_wstring(skill.at("input").at("maxStepSeconds").get<float>())+L"초\n선행 조건:\n";
        if (node.at("prerequisites").empty()) text += L"없음\n";
        for (const auto& prerequisite : node.at("prerequisites"))
        {
            const auto requiredId = prerequisite.at("skillId").get<std::string>();
            text += Wide(catalog.skills.at(requiredId).at("name").get<std::string>())+L" Lv."+std::to_wstring(prerequisite.at("skillLevel").get<std::uint32_t>())+L" (현재 "+std::to_wstring(progression.GetSkillLevel(requiredId))+L")\n";
        }
        return text+L"\n"+LearnReason(id);
    }

    std::filesystem::path SkillUi::SlotPath() const
    {
        const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA",nullptr,0);
        if (length == 0) return {};
        std::wstring directory(length,L'\0');
        if (GetEnvironmentVariableW(L"LOCALAPPDATA",directory.data(),length) == 0) return {};
        directory.resize(length-1);
        return std::filesystem::path(directory)/L"ActionRPGClient"/(L"skill-slots-Character"+std::to_wstring(characterId)+L".json");
    }
    void SkillUi::LoadSlots()
    {
        const auto path = SlotPath();
        try
        {
            if (path.empty() || !std::filesystem::is_regular_file(path) || std::filesystem::file_size(path) > 8192) return;
            std::ifstream file(path); Json value; file >> value;
            PlayerSkills::Catalog::Keys(value,{"format","schemaVersion","characterId","slots"});
            PlayerSkills::Catalog::Require(value.at("format") == "SkillSlots" && value.at("schemaVersion") == 1
                && value.at("characterId") == characterId && value.at("slots").is_array() && value.at("slots").size() == slots.size(),"Invalid saved slots.");
            std::unordered_set<std::string> used;
            for (std::size_t i=0;i<slots.size();++i)
            {
                const auto id = value.at("slots")[i].get<std::string>();
                if (!id.empty() && IsOwned(id) && progression.GetSkillLevel(id)>0 && used.insert(id).second) slots[i]=id;
            }
        }
        catch (const std::exception&) { slots.fill({}); status=L"저장된 단축키 파일을 읽지 못했습니다"; }
    }
    void SkillUi::SaveSlots()
    {
        const auto path = SlotPath();
        try
        {
            if (path.empty()) throw std::runtime_error("No user data directory.");
            std::filesystem::create_directories(path.parent_path());
            const auto temporary = path.wstring()+L"."+std::to_wstring(GetCurrentProcessId())+L".tmp";
            // Each process owns its temporary file; cleanup also runs when writing/replacing fails.
            struct TemporaryFile
            {
                std::filesystem::path path;
                ~TemporaryFile() { std::error_code error; std::filesystem::remove(path,error); }
            } cleanup{temporary};
            { std::ofstream file(std::filesystem::path(temporary),std::ios::binary|std::ios::trunc); file << Json{{"format","SkillSlots"},{"schemaVersion",1},{"characterId",characterId},{"slots",slots}}.dump(2); file.flush(); if (!file.good()) throw std::runtime_error("Slot write failed."); }
            if (!MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) throw std::runtime_error("Slot replacement failed.");
        }
        catch (const std::exception&) { status=L"단축키 저장에 실패했습니다. 현재 배치는 이번 실행에 유지됩니다"; }
    }
    void SkillUi::Assign(std::size_t index,const std::string& id)
    {
        if (!id.empty() && (!IsOwned(id) || progression.GetSkillLevel(id)==0)) return;
        const auto previous=slots;
        if (!id.empty()) for (auto& slot:slots) if (slot==id) slot.clear();
        slots[index]=id;
        if (slots!=previous) SaveSlots();
    }

    SkillUiAction SkillUi::Update(float dt,const InputState& input,float width,float height,bool editing,bool barVisible)
    {
        SkillUiAction action;
        mouseX=input.mouseX; mouseY=input.mouseY;
        for (auto& [id,seconds]:cooldowns) seconds=std::max(0.0f,seconds-dt);
        if (learnPending)
        {
            learnSeconds+=dt;
            if (learnSeconds>=Number(settings.at("layout"),"learnTimeoutSeconds"))
            { learnPending=false; ready=false; status=L"습득 응답을 확인하지 못했습니다. 상태를 다시 요청합니다"; action.requestState=true; }
        }
        if (!editing || input.cancelDrag) CancelDrag();
        if (input.cancelDrag) return action;
        if (!barVisible || !ready) return action;
        const auto layout=CalculateLayout(width,height);
        const auto& node=settings.at(layout.compact ? "compactNode":"node");
        float maxX=0,maxY=0;
        for (const auto& id:OrderedSkills())
        {
            const auto& value=trees.nodes.at(id);
            maxX=std::max(maxX,value.at("column").get<float>()*(Number(node,"width")+Number(node,"columnGap"))+Number(node,"width"));
            maxY=std::max(maxY,value.at("row").get<float>()*(Number(node,"height")+Number(node,"rowGap"))+Number(node,"height"));
        }
        maxX=std::max(0.0f,maxX-(layout.tree.right-layout.tree.left));
        maxY=std::max(0.0f,maxY-(layout.tree.bottom-layout.tree.top));
        treeScrollX=std::clamp(treeScrollX,0.0f,maxX); treeScrollY=std::clamp(treeScrollY,0.0f,maxY);
        if (input.rightMousePressed) for (std::size_t i=0;i<slots.size();++i)
            if (Contains(layout.slots[i],input.rightClickX,input.rightClickY)) Assign(i,"");
        if (!editing) return action;
        if (input.leftMousePressed)
        {
            CancelDrag();
            if (layout.stacked && Contains(layout.treeTab,input.clickX,input.clickY)) detailTabSelected=false;
            if (layout.stacked && Contains(layout.detailTab,input.clickX,input.clickY)) detailTabSelected=true;
            if ((!layout.stacked || !detailTabSelected) && Contains(layout.tree,input.clickX,input.clickY))
                for (const auto& id:OrderedSkills()) if (Contains(NodeRectangle(layout,id),input.clickX,input.clickY))
                { selected=id; detailScroll=0; if (progression.GetSkillLevel(id)>0) dragCandidate=id; break; }
            for (std::size_t i=0;i<slots.size();++i) if (Contains(layout.slots[i],input.clickX,input.clickY)) dragCandidate=slots[i];
            dragStartX=input.clickX; dragStartY=input.clickY;
            if ((!layout.stacked || detailTabSelected) && !selected.empty() && Contains(layout.learnButton,input.clickX,input.clickY)
                && learningAllowed && connected && !learnPending && trees.CanLearn(catalog,progression,characterId,selected,progression.GetSkillLevel(selected))=="Succeeded")
            { action.learnSkill=selected; action.expectedSkillLevel=progression.GetSkillLevel(selected); learnPending=true; learnSeconds=0; }
        }
        if (!dragCandidate.empty() && (input.leftMouseDown || input.leftMouseReleased) && std::hypot(input.mouseX-dragStartX,input.mouseY-dragStartY)>=Number(settings.at("layout"),"dragThreshold")) dragging=dragCandidate;
        if (input.leftMouseReleased)
        {
            if (!dragging.empty()) for (std::size_t i=0;i<slots.size();++i) if (Contains(layout.slots[i],input.mouseX,input.mouseY)) { Assign(i,dragging); break; }
            CancelDrag(); // Out-of-window/out-of-slot release never changes the original mapping.
        }
        const float wheel=static_cast<float>(input.mouseWheelDelta)/WHEEL_DELTA;
        if (wheel!=0 && dragging.empty())
        {
            if ((!layout.stacked || !detailTabSelected) && Contains(layout.tree,input.mouseX,input.mouseY))
            {
                if (input.shiftHeld) treeScrollX=std::clamp(treeScrollX-wheel*Number(node,"width"),0.0f,maxX);
                else treeScrollY=std::clamp(treeScrollY-wheel*Number(node,"height"),0.0f,maxY);
            }
            else if ((!layout.stacked || detailTabSelected) && !selected.empty() && Contains(layout.detail,input.mouseX,input.mouseY))
            {
                const auto text=Details(selected); const float font=Number(settings.at("fonts"),"body");
                const float lines=static_cast<float>(std::count(text.begin(),text.end(),L'\n'))+text.size()/std::max(1.0f,(layout.detail.right-layout.detail.left)/font)+1;
                const auto& window=settings.at(layout.compact ? "compactWindow":"normalWindow");
                const float maxScroll=std::max(0.0f,Number(window,"detailIconSize")+Number(window,"contentGap")+lines*font*Number(settings.at("fonts"),"lineHeight")-(layout.learnButton.top-Number(window,"contentGap")-layout.detail.top));
                detailScroll=std::clamp(detailScroll-wheel*font*Number(settings.at("fonts"),"lineHeight"),0.0f,maxScroll);
            }
        }
        return action;
    }
    void SkillUi::DrawIcon(D2DRenderer& draw,const std::string& id,const D2D1_RECT_F& rect,bool gray) const
    {
        const auto found=icons.find(id);
        if (found!=icons.end()) draw.DrawUiIcon(found->second.Get(),rect,gray);
        else
        {
            draw.FillRectangle(rect.left,rect.top,rect.right,rect.bottom,Color("locked"));
            if (catalog.skills.contains(id)) draw.DrawUiText(Wide(catalog.skills.at(id).at("name").get<std::string>()),rect,Color("text"),Number(settings.at("fonts"),"small"),true,true);
        }
    }
    void SkillUi::Render(D2DRenderer& draw,float width,float height,bool editing) const
    {
        const auto layout=CalculateLayout(width,height);
        const auto& bar=settings.at(layout.compact ? "compactBar":"normalBar");
        const auto& window=settings.at(layout.compact ? "compactWindow":"normalWindow");
        const auto& node=settings.at(layout.compact ? "compactNode":"node");
        const float bodyFont=Number(settings.at("fonts"),"body"),smallFont=Number(settings.at("fonts"),"small");
        if (editing)
        {
            draw.FillRectangle(0,0,width,height,Color("panel"));
            draw.FillRectangle(layout.panel.left,layout.panel.top,layout.panel.right,layout.panel.bottom,Color("panel"));
            draw.DrawRectangle(layout.panel.left,layout.panel.top,layout.panel.right,layout.panel.bottom,Color("border"));
            const float padding=Number(window,"padding");
            draw.DrawUiText(L"스킬  |  캐릭터 Lv."+std::to_wstring(progression.level)+L"  SP "+std::to_wstring(progression.skillPoints)+L"  | ESC 메뉴로",Offset(layout.panel,padding,padding,layout.panel.right-layout.panel.left-2*padding,Number(window,"headerHeight")),Color("text"),Number(settings.at("fonts"),"title"));
            if (layout.stacked)
            {
                draw.DrawUiText(L"스킬 트리",layout.treeTab,Color(detailTabSelected ? "border":"selected"),bodyFont,false,true);
                draw.DrawUiText(L"선택 스킬 설명",layout.detailTab,Color(detailTabSelected ? "selected":"border"),bodyFont,false,true);
            }
            if (!layout.stacked || !detailTabSelected)
            {
                draw.PushAxisAlignedClip(layout.tree);
                if (ready)
                {
                    for (const auto& id:OrderedSkills())
                    {
                        const auto rect=NodeRectangle(layout,id);
                        for (const auto& prerequisite:trees.nodes.at(id).at("prerequisites"))
                        {
                            const auto source=NodeRectangle(layout,prerequisite.at("skillId").get<std::string>());
                            draw.DrawLine((source.left+source.right)/2,source.bottom,(rect.left+rect.right)/2,rect.top,Color("border"));
                        }
                    }
                    for (const auto& id:OrderedSkills())
                    {
                        const auto rect=NodeRectangle(layout,id); if (!Overlaps(rect,layout.tree)) continue;
                        const bool learned=progression.GetSkillLevel(id)>0;
                        draw.DrawRectangle(rect.left,rect.top,rect.right,rect.bottom,Color(id==selected ? "selected":learned ? "border":"locked"));
                        DrawIcon(draw,id,Offset(rect,Number(node,"iconLeft"),Number(node,"iconTop"),Number(node,"iconSize"),Number(node,"iconSize")),!learned || !learningAllowed);
                        draw.DrawUiText(Wide(catalog.skills.at(id).at("name").get<std::string>()),Offset(rect,Number(node,"nameLeft"),Number(node,"nameTop"),Number(node,"nameWidth"),Number(node,"nameHeight")),Color("text"),smallFont,false,true);
                        draw.DrawUiText(learned ? L"Lv."+std::to_wstring(progression.GetSkillLevel(id)):L"미습득",Offset(rect,0,0,rect.right-rect.left,smallFont*Number(settings.at("fonts"),"lineHeight")),Color(learned ? "selected":"text"),smallFont);
                    }
                }
                else draw.DrawUiText(L"서버 스킬 상태를 기다리는 중...",layout.tree,Color("text"),bodyFont,true);
                draw.PopAxisAlignedClip();
            }
            if ((!layout.stacked || detailTabSelected) && ready && !selected.empty())
            {
                const float iconSize=Number(window,"detailIconSize");
                D2D1_RECT_F textRect=layout.detail;
                textRect.top+=iconSize+Number(window,"contentGap")-detailScroll;
                const auto details=Details(selected);
                const float columns=std::max(1.0f,(textRect.right-textRect.left)/bodyFont);
                textRect.bottom=textRect.top+(details.size()/columns+std::count(details.begin(),details.end(),L'\n')+1)*bodyFont*Number(settings.at("fonts"),"lineHeight");
                auto clip=layout.detail; clip.bottom=layout.learnButton.top-Number(window,"contentGap");
                draw.PushAxisAlignedClip(clip);
                DrawIcon(draw,selected,Offset(layout.detail,0,-detailScroll,iconSize,iconSize),progression.GetSkillLevel(selected)==0 || !learningAllowed);
                draw.DrawUiText(details,textRect,Color("text"),bodyFont,true);
                draw.PopAxisAlignedClip();
                const bool canLearn=learningAllowed && connected && !learnPending && trees.CanLearn(catalog,progression,characterId,selected,progression.GetSkillLevel(selected))=="Succeeded";
                if (!learningAllowed) draw.FillRectangle(layout.learnButton.left,layout.learnButton.top,layout.learnButton.right,layout.learnButton.bottom,Color("locked"));
                draw.DrawRectangle(layout.learnButton.left,layout.learnButton.top,layout.learnButton.right,layout.learnButton.bottom,Color(canLearn ? "selected":"locked"));
                draw.DrawUiText(!learningAllowed ? LearnReason(selected):learnPending ? L"서버 응답 대기...":canLearn ? (progression.GetSkillLevel(selected) ? L"SP를 사용해 강화":L"SP를 사용해 습득"):LearnReason(selected),layout.learnButton,Color("text"),bodyFont,false,true);
            }
            draw.DrawUiText((learningAllowed ? status:L"던전: 조회·단축키 등록만 가능")+L"  | 휠: 위아래, Shift+휠: 좌우",D2D1::RectF(layout.panel.left+padding,layout.panel.bottom-padding-Number(window,"footerHeight"),layout.panel.right-padding,layout.panel.bottom-padding),Color("text"),smallFont,true);
        }
        draw.FillRectangle(layout.bar.left,layout.bar.top,layout.bar.right,layout.bar.bottom,Color("panel"));
        static constexpr wchar_t KEYS[]=L"ASDFGH";
        for (std::size_t i=0;i<slots.size();++i)
        {
            const auto rect=layout.slots[i]; const auto& id=slots[i]; const float seconds=Cooldown(id);
            draw.DrawRectangle(rect.left,rect.top,rect.right,rect.bottom,Color("border"));
            if (!id.empty()) DrawIcon(draw,id,Offset(rect,Number(bar,"iconLeft"),Number(bar,"iconTop"),Number(bar,"iconSize"),Number(bar,"iconSize")),!ready || progression.GetSkillLevel(id)==0 || seconds>0);
            if (seconds>0)
            {
                const auto cd=Offset(rect,Number(bar,"cooldownLeft"),Number(bar,"cooldownTop"),Number(bar,"cooldownWidth"),Number(bar,"cooldownHeight"));
                draw.FillRectangle(cd.left,cd.top,cd.right,cd.bottom,Color("cooldownBacking"));
                draw.DrawUiText(CooldownText(seconds),cd,Color("cooldownText"),Number(bar,"cooldownFontSize"),false,true);
            }
            draw.DrawUiText(std::wstring(1,KEYS[i]),Offset(rect,Number(bar,"keyLeft"),Number(bar,"keyTop"),Number(bar,"keyWidth"),Number(bar,"keyHeight")),Color("text"),Number(bar,"keyFontSize"),false,true);
        }
        if (editing && !dragging.empty()) DrawIcon(draw,dragging,D2D1::RectF(mouseX-Number(bar,"iconSize")/2,mouseY-Number(bar,"iconSize")/2,mouseX+Number(bar,"iconSize")/2,mouseY+Number(bar,"iconSize")/2),false);
    }
}
