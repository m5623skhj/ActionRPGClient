#pragma once
#include "Input/InputState.h"
#include "Game/Vector2.h"
#include <d2d1_1.h>
#include <deque>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include <cstdint>

namespace ActionRPG
{
    class D2DRenderer;
    enum class ChatChannel { General, Party };
    struct ChatSubmission { ChatChannel channel{}; std::wstring text; };
    struct ChatUiAction { bool consumeInput{}, consumePointer{}; std::optional<ChatSubmission> submission; };
    struct ChatBubbleAnchor { std::uint64_t playerId{}; Vector2 position; };

    // One bounded history, no network access. All mutation is on the game thread.
    class ChatUi final
    {
    public:
        static constexpr std::size_t MAX_MESSAGES = 200;
        static constexpr std::size_t MAX_INPUT_CODE_POINTS = 50;
        static constexpr std::size_t MAX_INPUT_UTF8_BYTES = 200;
        void SetLimits(std::size_t inCharacters, std::size_t inBytes)
        {
            maxCharacters = inCharacters < MAX_INPUT_CODE_POINTS ? inCharacters:MAX_INPUT_CODE_POINTS;
            maxBytes = inBytes < MAX_INPUT_UTF8_BYTES ? inBytes:MAX_INPUT_UTF8_BYTES;
        }
        [[nodiscard]] ChatUiAction Update(float inDeltaSeconds, const InputState& inInput,
            D2DRenderer& inRenderer, float inWidth, float inHeight, float inHudTop,
            bool inAllowed, bool inDungeon, bool inHasParty, bool inConnected);
        void Receive(std::uint64_t inMessageId, std::uint64_t inPlayerId,
            ChatChannel inChannel, std::wstring inName, std::wstring inText, bool inSelf, bool inShowBubble);
        void Notice(std::wstring inText);
        void CancelInput();
        [[nodiscard]] bool IsDragging() const { return drag!=Drag::None; }
        void ClearBubbles() { bubbles.clear(); }
        [[nodiscard]] bool IsEditing() const { return editing; }
        [[nodiscard]] Vector2 GetInputPosition() const;
        void Render(D2DRenderer& inRenderer, float inWidth) const;
        void RenderBubbles(D2DRenderer& inRenderer, const std::vector<ChatBubbleAnchor>& inAnchors,
            float inWidth, float inHeight) const;
    private:
        enum class Filter { All, General, Party };
        enum class Drag { None, Move, Height, Scroll };
        struct Message { std::uint64_t id{}, playerId{}; ChatChannel channel{}; std::wstring name, text; };
        struct Row { std::uint64_t id{}; ChatChannel channel{}; std::wstring text; float height{}, width{}; };
        struct Bubble { std::wstring text; float remaining{}; ChatChannel channel{}; mutable D2D1_SIZE_F size{}; mutable std::wstring displayText; };
        void Constrain(float inWidth, float inHeight, float inHudTop);
        void RebuildRows(D2DRenderer& inRenderer);
        [[nodiscard]] D2D1_RECT_F Body() const;
        [[nodiscard]] D2D1_RECT_F ScrollThumb() const;
        [[nodiscard]] float MaxScroll() const;
        void Append(std::wstring_view inText);
        void LimitText(std::wstring& inText) const;
        void CycleChannel();
        std::deque<Message> history;
        std::vector<Row> rows;
        std::unordered_map<std::uint64_t, Bubble> bubbles;
        std::wstring draft, composition, notice;
        float noticeSeconds{}, left = 16, top{}, width = 400, height = 180;
        float scroll{}, contentHeight{}, dragX{}, dragY{}, dragLeft{}, dragTop{}, dragHeight{}, dragScroll{};
        float viewportWidth = 1280, viewportHeight = 720, hudTop = 612;
        float anchorOffset{};
        std::uint64_t anchorId{}, lastMessageId{};
        std::size_t maxCharacters = MAX_INPUT_CODE_POINTS, maxBytes = MAX_INPUT_UTF8_BYTES;
        Filter filter{Filter::All};
        ChatChannel selectedChannel{ChatChannel::General};
        Drag drag{Drag::None};
        bool visible{true}, editing{}, hovered{}, initialized{}, dirty = true, dungeon{}, hasParty{}, connected{};
    };
}
