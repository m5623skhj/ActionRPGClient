#include "Game/ChatUi.h"
#include "Game/HudLayout.h"
#include "Graphics/D2DRenderer.h"
#include <Windows.h>
#include <algorithm>
#include <cmath>

namespace
{
    constexpr float FONT_SIZE = 14, TAB_HEIGHT = 24, INPUT_HEIGHT = 28;
    constexpr D2D1_COLOR_F GENERAL_CHAT_COLOR{1.0f,1.0f,1.0f,1.0f};
    constexpr D2D1_COLOR_F PARTY_CHAT_COLOR{0.43f,0.84f,1.0f,1.0f};
    constexpr D2D1_COLOR_F NOTICE_COLOR{1.0f,0.82f,0.40f,1.0f};
    constexpr D2D1_COLOR_F HELP_COLOR{0.67f,0.72f,0.78f,1.0f};
    D2D1_COLOR_F ChannelColor(const ActionRPG::ChatChannel inChannel)
    { return inChannel==ActionRPG::ChatChannel::Party ? PARTY_CHAT_COLOR:GENERAL_CHAT_COLOR; }
    // A thin dark outline keeps the same channel color readable on bright maps.
    void DrawChatText(ActionRPG::D2DRenderer& inRenderer,std::wstring_view inText,
        const D2D1_RECT_F& inRect,const D2D1_COLOR_F& inColor,float inSize,bool inWrap=false)
    {
        constexpr D2D1_POINT_2F OFFSETS[]={{-1,0},{1,0},{0,-1},{0,1}};
        for(const auto offset:OFFSETS)
            inRenderer.DrawUiText(inText,D2D1::RectF(inRect.left+offset.x,inRect.top+offset.y,
                inRect.right+offset.x,inRect.bottom+offset.y),D2D1::ColorF(0,0,0,0.95f),inSize,inWrap);
        inRenderer.DrawUiText(inText,inRect,inColor,inSize,inWrap);
    }
    bool Contains(const D2D1_RECT_F& inRect, float inX, float inY)
    { return inX >= inRect.left && inX <= inRect.right && inY >= inRect.top && inY <= inRect.bottom; }
    bool High(wchar_t inChar) { return inChar >= 0xD800 && inChar <= 0xDBFF; }
    bool Low(wchar_t inChar) { return inChar >= 0xDC00 && inChar <= 0xDFFF; }
    void PopCodePoint(std::wstring& inText)
    {
        if (inText.empty()) return;
        const bool pair = inText.size() > 1 && Low(inText.back()) && High(inText[inText.size()-2]);
        inText.pop_back(); if (pair) inText.pop_back();
    }
}
namespace ActionRPG
{
    D2D1_RECT_F ChatUi::Body() const
    { return D2D1::RectF(left+6, top+TAB_HEIGHT, left+width-16, top+height-INPUT_HEIGHT-2); }
    float ChatUi::MaxScroll() const
    { const auto body=Body(); return std::max(0.0f,contentHeight-(body.bottom-body.top)); }
    D2D1_RECT_F ChatUi::ScrollThumb() const
    {
        const auto body=Body(); const float track=body.bottom-body.top;
        const float length=std::min(track,std::max(16.0f,track*track/std::max(track,contentHeight)));
        const float y=body.top+(track-length)*(MaxScroll()>0 ? 1-scroll/MaxScroll():1);
        return D2D1::RectF(left+width-10,y,left+width-3,y+length);
    }
    void ChatUi::Constrain(float inWidth,float inHeight,float inHudTop)
    {
        viewportWidth=inWidth; viewportHeight=inHeight; hudTop=inHudTop;
        width=std::min(400.0f,std::max(180.0f,inWidth-16));
        const float available=std::max(60.0f,std::min(inHeight-12,inHudTop-12));
        height=std::clamp(height,std::min(120.0f,available),std::min(360.0f,available));
        if (!initialized) { top=available-height; initialized=true; }
        left=std::clamp(left,0.0f,std::max(0.0f,inWidth-width));
        top=std::clamp(top,0.0f,std::max(0.0f,available-height));
        scroll=std::clamp(scroll,0.0f,MaxScroll());
    }
    void ChatUi::RebuildRows(D2DRenderer& inRenderer)
    {
        if (!dirty) return;
        rows.clear(); contentHeight=0;
        for (const auto& message:history)
        {
            if ((filter==Filter::General && message.channel!=ChatChannel::General)
                || (filter==Filter::Party && message.channel!=ChatChannel::Party)) continue;
            auto text=std::wstring(message.channel==ChatChannel::Party ? L"[파티] ":L"[일반] ")+message.name+L": "+message.text;
            const auto textSize=inRenderer.MeasureUiText(text,width-22,FONT_SIZE);
            const float rowHeight=textSize.height+3;
            rows.push_back({message.id,message.channel,std::move(text),rowHeight,textSize.width});
            contentHeight+=rowHeight;
        }
        if (anchorId)
        {
            float preceding=0;
            for (const auto& row:rows)
            {
                if (row.id==anchorId)
                { const auto body=Body(); scroll=contentHeight-(body.bottom-body.top)-preceding-anchorOffset; break; }
                preceding+=row.height;
            }
        }
        scroll=std::clamp(scroll,0.0f,MaxScroll()); dirty=false;
    }
    void ChatUi::Receive(std::uint64_t inMessageId,std::uint64_t inPlayerId,ChatChannel inChannel,
        std::wstring inName,std::wstring inText,bool inSelf,bool inShowBubble)
    {
        if(inMessageId<=lastMessageId)return;
        lastMessageId=inMessageId;
        history.push_back({inMessageId,inPlayerId,inChannel,std::move(inName),inText});
        if (history.size()>MAX_MESSAGES) history.pop_front();
        if(inShowBubble) bubbles[inPlayerId]={std::move(inText),4.0f,inChannel};
        dirty=true;
        // A dungeon send made from General belongs to the server-confirmed Party log.
        if (inSelf && inChannel==ChatChannel::Party && filter==Filter::General)
        { filter=Filter::Party; scroll=0; anchorId=0; }
    }
    void ChatUi::CycleChannel()
    {
        selectedChannel=selectedChannel==ChatChannel::General ? ChatChannel::Party:ChatChannel::General;
        filter=selectedChannel==ChatChannel::Party ? Filter::Party:Filter::General;
        scroll=0;anchorId=0;dirty=true;
    }
    void ChatUi::Notice(std::wstring inText) { notice=std::move(inText); noticeSeconds=5; }
    void ChatUi::CancelInput() { editing=false; draft.clear(); composition.clear(); drag=Drag::None; }
    void ChatUi::Append(std::wstring_view inText)
    {
        for (const wchar_t value:inText)
            if (value>=32 && !(value>=0x7F && value<=0x9F)
                && value!=0x2028 && value!=0x2029 && value!=0xFEFF && value!=0x061C
                && value!=0x200E && value!=0x200F && !(value>=0x202A && value<=0x202E)
                && !(value>=0x2066 && value<=0x206F)) draft+=value;
        LimitText(draft);
    }
    void ChatUi::LimitText(std::wstring& inText) const
    {
        // Count scalar values and UTF-8 bytes, not UTF-16 units. Retain a trailing
        // high surrogate only until its following WM_CHAR; submission validates it.
        for (;;)
        {
            std::size_t count=0,bytes=0;
            for (std::size_t i=0;i<inText.size();++i)
            {
                const wchar_t value=inText[i]; ++count;
                if (High(value) && i+1<inText.size() && Low(inText[i+1])) { ++i;bytes+=4; }
                else bytes+=value<0x80 ? 1:value<0x800 ? 2:3;
            }
            if (count<=maxCharacters && bytes<=maxBytes) break;
            PopCodePoint(inText);
        }
    }
    Vector2 ChatUi::GetInputPosition() const { return {left+70,top+height-INPUT_HEIGHT}; }
    ChatUiAction ChatUi::Update(float inDeltaSeconds,const InputState& inInput,D2DRenderer& inRenderer,
        float inWidth,float inHeight,float inHudTop,bool inAllowed,bool inDungeon,bool inHasParty,bool inConnected)
    {
        ChatUiAction action; dungeon=inDungeon;hasParty=inHasParty;connected=inConnected;
        for (auto it=bubbles.begin();it!=bubbles.end();)
        { it->second.remaining-=inDeltaSeconds;if(it->second.remaining<=0)it=bubbles.erase(it);else ++it; }
        noticeSeconds=std::max(0.0f,noticeSeconds-inDeltaSeconds);
        const float scale=HudLayout::Scale(inWidth);
        const auto input=HudLayout::ScaleInput(inInput,1/scale);
        Constrain(inWidth/scale,inHeight/scale,inHudTop/scale); RebuildRows(inRenderer);
        if (!visible && inAllowed && input.WasPressed(InputKey::ConfirmSelection)) visible=true;
        hovered=visible && inAllowed && Contains(D2D1::RectF(left,top,left+width,top+height),input.mouseX,input.mouseY);
        const bool wasEditing=editing;
        if (!inAllowed || input.cancelDrag) { CancelInput();action.consumeInput=wasEditing;return action; }
        if (!visible) return action;
        if (hovered && input.leftMousePressed && input.clickX>=left+width-26 && input.clickY<top+24)
        { visible=false;CancelInput();action.consumeInput=wasEditing;action.consumePointer=true;return action; }
        if (editing && input.leftMousePressed && !hovered) CancelInput();
        if (!editing && input.WasPressed(InputKey::ConfirmSelection))
        { editing=true;draft.clear();Append(input.textInput); }
        composition=editing ? input.composition:L"";
        if (input.leftMousePressed && hovered)
        {
            action.consumeInput=true; const auto body=Body(); const auto thumb=ScrollThumb();
            if (input.clickY>=top+height-4) { drag=Drag::Height; }
            else if (input.clickY<=top+4) { drag=Drag::Move; }
            else if (input.clickY<top+TAB_HEIGHT && input.clickX<left+174)
            {
                const auto tab=static_cast<int>((input.clickX-left)/58);
                filter=static_cast<Filter>(std::clamp(tab,0,2));
                if (filter!=Filter::All) selectedChannel=filter==Filter::Party ? ChatChannel::Party:ChatChannel::General;
                scroll=0;anchorId=0;dirty=true;
            }
            else if (input.clickY>=top+height-INPUT_HEIGHT)
            {
                if (input.clickX<left+62) CycleChannel();
                editing=true;
            }
            else if (input.clickX>=left+width-13)
            {
                if (!Contains(thumb,input.clickX,input.clickY))
                {
                    const float fraction=std::clamp((input.clickY-body.top)/(body.bottom-body.top),0.0f,1.0f);
                    scroll=MaxScroll()*(1-fraction);
                }
                drag=Drag::Scroll;
            }
            else
            {
                bool overText=false;float y=body.bottom-contentHeight+scroll;
                for(const auto& row:rows)
                {
                    if(Contains(D2D1::RectF(body.left,y,body.left+row.width,y+row.height-3),input.clickX,input.clickY))
                    {overText=true;break;}y+=row.height;
                }
                if(!overText)drag=Drag::Move;
            }
            dragX=input.mouseX;dragY=input.mouseY;dragLeft=left;dragTop=top;dragHeight=height;dragScroll=scroll;
        }
        if (drag!=Drag::None)
        {
            action.consumeInput=true;
            if (!input.leftMouseDown || input.leftMouseReleased) drag=Drag::None;
            else if (drag==Drag::Move) {left=dragLeft+input.mouseX-dragX;top=dragTop+input.mouseY-dragY;}
            else if (drag==Drag::Height) {top=dragTop;height=dragHeight+(input.mouseY-dragY);}
            else
            {
                const auto body=Body();const auto thumb=ScrollThumb();
                const float travel=body.bottom-body.top-(thumb.bottom-thumb.top);
                if(travel>0)scroll=dragScroll-(input.mouseY-dragY)*MaxScroll()/travel;
            }
            Constrain(viewportWidth,viewportHeight,hudTop);
        }
        if (hovered && input.mouseWheelDelta)
        { scroll=std::clamp(scroll+input.mouseWheelDelta/120.0f*48.0f,0.0f,MaxScroll());action.consumeInput=true; }
        if (editing)
        {
            for (const auto& event:input.textEdits)
            {
                if (!editing) break;
                if (event.kind==TextEditEvent::Kind::Insert) {noticeSeconds=0;Append(event.text);}
                else if(event.kind==TextEditEvent::Kind::Backspace) {noticeSeconds=0;PopCodePoint(draft);}
                else if(event.kind==TextEditEvent::Kind::Cancel) CancelInput();
                else if(event.kind==TextEditEvent::Kind::CycleChannel) CycleChannel();
                else if(event.kind==TextEditEvent::Kind::Submit)
                {
                    const auto actual=dungeon ? ChatChannel::Party:selectedChannel;
                    if (!connected) {Notice(L"채팅 서버에 연결되어 있지 않습니다.");continue;}
                    if(actual==ChatChannel::Party && !hasParty) {Notice(L"파티가 없어 전송할 수 없습니다.");continue;}
                    if(draft.empty()) {CancelInput();continue;}
                    const int bytes=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,draft.data(),static_cast<int>(draft.size()),nullptr,0,nullptr,nullptr);
                    if(bytes<=0 || static_cast<std::size_t>(bytes)>maxBytes) {Notice(L"유효하지 않거나 너무 긴 채팅입니다.");continue;}
                    if(std::all_of(draft.begin(),draft.end(),[](wchar_t value){return value==L' ' || value==L'\t';})) {CancelInput();continue;}
                    action.submission=ChatSubmission{actual,draft};CancelInput();
                }
            }
        }
        action.consumePointer=hovered || drag!=Drag::None;
        action.consumeInput=action.consumeInput || wasEditing || editing || input.WasPressed(InputKey::ConfirmSelection);
        RebuildRows(inRenderer);
        anchorId=0;
        if(scroll>0)
        {
            const auto body=Body();float y=body.bottom-contentHeight+scroll;
            for(const auto& row:rows)
            {if(y+row.height>body.top){anchorId=row.id;anchorOffset=body.top-y;break;}y+=row.height;}
        }
        return action;
    }
    void ChatUi::Render(D2DRenderer& inRenderer,float inWidth) const
    {
        if (!visible) return;
        const D2DRenderer::UiScaleScope uiScale(inRenderer,HudLayout::Scale(inWidth));
        if(hovered || editing || drag!=Drag::None)
        {
            inRenderer.FillRectangle(left,top,left+width,top+height,D2D1::ColorF(0.025f,0.035f,0.05f,0.65f));
            inRenderer.DrawLine(left,top+2,left+width,top+2,D2D1::ColorF(0.7f,0.75f,0.82f,0.55f));
            inRenderer.DrawLine(left,top+height-2,left+width,top+height-2,D2D1::ColorF(0.7f,0.75f,0.82f,0.55f));
            const auto thumb=ScrollThumb();
            inRenderer.FillRectangle(left+width-10,Body().top,left+width-3,Body().bottom,D2D1::ColorF(0.13f,0.15f,0.18f,0.7f));
            inRenderer.FillRectangle(thumb.left,thumb.top,thumb.right,thumb.bottom,D2D1::ColorF(0.7f,0.75f,0.82f,0.8f));
        }
        if (hovered || editing)
            DrawChatText(inRenderer,L"X",D2D1::RectF(left+width-25,top+3,left+width-4,top+23),HELP_COLOR,FONT_SIZE);
        const wchar_t* tabs[]={L"전체",L"일반",L"파티"};
        for(int i=0;i<3;++i)
        {
            const bool selected=static_cast<int>(filter)==i;
            auto color=i==2 ? PARTY_CHAT_COLOR:i==1 ? GENERAL_CHAT_COLOR:HELP_COLOR;
            if(!selected)color.a=0.75f;
            DrawChatText(inRenderer,tabs[i],D2D1::RectF(left+i*58+6,top+5,left+(i+1)*58,top+TAB_HEIGHT),color,FONT_SIZE);
            if(selected)inRenderer.FillRectangle(left+i*58+6,top+TAB_HEIGHT-3,left+(i+1)*58-6,top+TAB_HEIGHT-1,color);
        }
        const auto body=Body();inRenderer.PushAxisAlignedClip(body);
        float y=body.bottom-contentHeight+scroll;
        for(const auto& row:rows)
        {
            if(y+row.height>body.top && y<body.bottom)
            {
                const auto color=ChannelColor(row.channel);
                const auto rect=D2D1::RectF(body.left,y,body.right,y+row.height);
                DrawChatText(inRenderer,row.text,rect,color,FONT_SIZE,true);
            }
            y+=row.height;
        }
        inRenderer.PopAxisAlignedClip();
        const auto actual=dungeon ? ChatChannel::Party:selectedChannel;
        const auto channel=actual==ChatChannel::Party ? L"[파티]":L"[일반]";
        DrawChatText(inRenderer,channel,D2D1::RectF(left+6,top+height-INPUT_HEIGHT,left+62,top+height-2),ChannelColor(actual),FONT_SIZE);
        std::wstring text;
        if(noticeSeconds>0)text=notice;
        else if(editing) {text=draft+composition;LimitText(text);text+=L"|";}
        else text=dungeon ? L"Enter: 파티 채팅 · Tab: 기록 탭 전환":L"Enter: 입력 · 입력 중 Tab: 일반 ↔ 파티";
        if(editing && noticeSeconds<=0)
        {
            // Keep the edit tail visible without allowing it to cross panel bounds.
            while(text.size()>1 && inRenderer.MeasureUiText(text,width-78,FONT_SIZE).height>INPUT_HEIGHT-2)
            { text.erase(0,High(text.front()) && text.size()>1 && Low(text[1]) ? 2:1); }
        }
        const auto textColor=noticeSeconds>0 ? NOTICE_COLOR:editing ? ChannelColor(actual):HELP_COLOR;
        DrawChatText(inRenderer,text,D2D1::RectF(left+66,top+height-INPUT_HEIGHT,left+width-6,top+height-2),textColor,FONT_SIZE);
    }
    void ChatUi::RenderBubbles(D2DRenderer& inRenderer,const std::vector<ChatBubbleAnchor>& inAnchors,float inWidth,float inHeight) const
    {
        const float scale=HudLayout::Scale(inWidth);
        const D2DRenderer::UiScaleScope uiScale(inRenderer,scale);
        for(const auto& anchor:inAnchors)
        {
            const auto found=bubbles.find(anchor.playerId);if(found==bubbles.end())continue;
            const float x=anchor.position.x/scale,y=anchor.position.y/scale;
            if(x<0 || x>inWidth/scale || y<0 || y>inHeight/scale)continue;
            auto& size=found->second.size;
            auto& displayText=found->second.displayText;
            if(size.width==0)
            {
                // Keep full text in history. Cap the bubble at three measured
                // lines, including older peers' longer server-accepted messages.
                std::size_t end=0,characters=0;
                while(end<found->second.text.size() && characters<MAX_INPUT_CODE_POINTS)
                {
                    if(High(found->second.text[end]) && end+1<found->second.text.size() && Low(found->second.text[end+1]))++end;
                    ++end;++characters;
                }
                auto prefix=found->second.text.substr(0,end);
                bool shortened=end<found->second.text.size();
                const float maximumHeight=inRenderer.MeasureUiText(L"가",220,12.0f).height*3;
                for(;;)
                {
                    displayText=prefix+(shortened ? L"…":L"");
                    const float innerWidth=std::clamp(std::ceil(inRenderer.MeasureUiText(displayText,10000,12.0f).width),24.0f,220.0f);
                    size=D2D1::SizeF(innerWidth,inRenderer.MeasureUiText(displayText,innerWidth,12.0f).height);
                    if(size.height<=maximumHeight || prefix.empty())break;
                    PopCodePoint(prefix);shortened=true;
                }
            }
            const float w=size.width+12,h=size.height+10;
            // At the top edge keep the head clear; history still contains the text.
            if(y<h+4)continue;
            const float l=std::clamp(x-w/2,2.0f,std::max(2.0f,inWidth/scale-w-2));
            const float t=std::max(2.0f,y-h-10);
            inRenderer.FillRectangle(l,t,l+w,t+h,D2D1::ColorF(0.02f,0.025f,0.035f,0.88f));
            const auto color=ChannelColor(found->second.channel);
            inRenderer.DrawRectangle(l,t,l+w,t+h,color);
            inRenderer.DrawLine(std::clamp(x,l+4,l+w-4),t+h,std::clamp(x,l+4,l+w-4),t+h+4,color);
            DrawChatText(inRenderer,displayText,D2D1::RectF(l+6,t+5,l+w-6,t+h-3),color,12.0f,true);
        }
    }
}
