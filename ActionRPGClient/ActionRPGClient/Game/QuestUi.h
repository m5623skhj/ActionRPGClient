#pragma once

#include "Game/QuestState.h"
#include "Game/Vector2.h"
#include "Input/InputState.h"
#include <d2d1_1.h>
#include <wrl/client.h>
#include <array>

namespace ActionRPG
{
    class AssetCatalog;
    class Camera;
    class D2DRenderer;
    struct QuestUiAction
    {
        std::string stateRequestId, operationJson;
        bool openWindow{}, consumePointer{}, refreshProgression{};
    };

    // One game-thread model/view, using the same scaled bounds for rendering and hit testing.
    class QuestUi final
    {
    public:
        QuestUi(const AssetCatalog& inAssets, D2DRenderer& inRenderer);
        void SetCharacterContext(std::uint64_t inCharacterId);
        void SetConnected(bool inConnected);
        void SetMapContext(std::string inMapId);
        void OpenAll();
        void Close();
        void RequestRefresh();
        void ObserveRevision(std::uint64_t inRevision);
        [[nodiscard]] std::uint64_t GetObservedRevision() const { return observedRevision; }
        void ApplyPage(std::string_view inPayload);
        void ApplyResult(std::string_view inPayload);
        void InvalidResponse();
        [[nodiscard]] QuestUiAction Update(float inDeltaSeconds, const InputState& inInput,
            float inWidth, float inHeight, float inHudTop, bool inVisible, bool inWorldInteractive,
            bool inTrackerVisible, Vector2 inPlayer, const Camera& inCamera);
        void Render(D2DRenderer& inRenderer, float inWidth, float inHeight, float inHudTop) const;
        void RenderTracker(D2DRenderer& inRenderer, float inWidth, float inHeight, float inHudTop) const;
        void RenderNpcs(D2DRenderer& inRenderer, const Camera& inCamera, float inPlayerDepth, bool inForeground) const;
    private:
        struct Layout
        {
            D2D1_RECT_F panel, list, detail, primary, abandon, track, retry, npcTalk;
            std::array<D2D1_RECT_F, 3> tabs;
        };
        [[nodiscard]] Layout CalculateLayout(float inWidth, float inHeight, float inHudTop) const;
        [[nodiscard]] std::vector<const QuestEntryView*> Filtered() const;
        [[nodiscard]] const QuestEntryView* Selected() const;
        [[nodiscard]] bool IsListed(const QuestEntryView& inQuest) const;
        [[nodiscard]] bool NpcRelevant(const QuestEntryView& inQuest) const;
        [[nodiscard]] bool NpcInRange(const std::string& inId) const;
        [[nodiscard]] bool CanAct() const;
        [[nodiscard]] std::wstring ItemName(const std::string& inId) const;
        [[nodiscard]] std::wstring ObjectiveText(const QuestEntryView& inQuest, const QuestObjectiveView& inObjective) const;
        [[nodiscard]] std::wstring RewardText(const nlohmann::json& inRewards) const;
        [[nodiscard]] std::wstring Details(const QuestEntryView& inQuest) const;
        [[nodiscard]] float DetailsHeight(const QuestEntryView& inQuest, float inWidth) const;
        [[nodiscard]] D2D1_RECT_F TrackerBounds(float inWidth, float inHeight, float inHudTop) const;
        [[nodiscard]] std::vector<const QuestEntryView*> Tracked() const;
        void BeginAction(std::string inAction, const QuestEntryView* inQuest = nullptr,
            std::vector<std::string> inTracked = {});
        void LoadIcons();
        struct NpcVisual
        {
            Microsoft::WRL::ComPtr<ID2D1Bitmap1> bitmap;
            D2D1_RECT_F source{};
            D2D1_RECT_U opaquePixels{};
            std::vector<std::uint8_t> alpha;
            std::uint32_t pixelWidth{};
            float height{};
            bool quests{};
        };
        void LoadNpcVisuals();
        [[nodiscard]] D2D1_RECT_F NpcBounds(const QuestNpcView& inNpc, const NpcVisual& inVisual, const Camera& inCamera) const;
        [[nodiscard]] const QuestNpcView* PickNpc(const Camera& inCamera, float inX, float inY) const;
        std::unordered_map<std::string, NpcVisual> npcVisuals;
        std::string hoveredNpcId;
        const AssetCatalog& assets;
        D2DRenderer& renderer;
        QuestState state;
        std::unordered_map<std::string, Microsoft::WRL::ComPtr<ID2D1Bitmap1>> icons;
        std::string mapId, npcId, selectedId, selectedCycle, choiceId;
        std::string queryId, pendingAction, pendingJson, outgoingJson;
        std::wstring notice;
        std::uint64_t observedRevision{};
        Vector2 playerPosition{};
        float querySeconds{}, refreshCooldown{}, pendingSeconds{}, actionCooldown{}, detailScroll{}, listScroll{};
        int category{};
        bool connected{}, wantRefresh{true}, refreshProgression{}, confirmAbandon{};
    };
}
