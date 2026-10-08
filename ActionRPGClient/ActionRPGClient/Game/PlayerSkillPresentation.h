#pragma once

#include "Game/PlayerSkillCatalog.h"
#include "Game/DungeonCombat.h"
#include "Game/Camera.h"
#include "Input/InputState.h"
#include "Resources/SpriteAnimation.h"
#include <d2d1_1.h>
#include <wrl/client.h>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace ActionRPG
{
    class AssetCatalog;
    class D2DRenderer;
    class InputCommandQueue;
    // Loaded/used only on the game thread; GPU bitmaps are never touched by network callbacks.
    class PlayerSkillPresentation final
    {
    public:
        PlayerSkillPresentation(const AssetCatalog& inAssets, D2DRenderer& inRenderer);
        [[nodiscard]] const nlohmann::json& GetDefinition(const std::string& inId) const { return catalog.skills.at(inId); }
        void ValidateServer(const PlayerSkills::Catalog& inCatalog) const;
        [[nodiscard]] std::string TryCommand(InputCommandQueue& inQueue, std::uint32_t inCharacterId,
            const std::unordered_map<std::string,std::uint32_t>& inSkillLevels, bool& outMatched) const;
        [[nodiscard]] bool Render(const CombatPlayerState& inState, Vector2 inGround, float inHeight,
            float inSeconds, D2DRenderer& inRenderer, const Camera& inCamera) const;
    private:
        struct Motion
        {
            nlohmann::json definition;
            SpriteAnimation animation;
            Microsoft::WRL::ComPtr<ID2D1Bitmap1> bitmap;
        };
        struct Variant
        {
            std::shared_ptr<const Motion> motion, effect;
            float effectSeconds{}, offsetX{}, offsetY{}, offsetHeight{};
            bool loop{};
        };
        struct Visual { std::string id; Variant ground, air; };
        std::shared_ptr<const Motion> LoadMotion(const nlohmann::json& inMotion, const AssetCatalog& inAssets, D2DRenderer& inRenderer);
        static void Draw(const Motion& inMotion, float inSeconds, bool inLoop, float inX, float inY,
            bool inFacingLeft, D2DRenderer& inRenderer);
        PlayerSkills::Catalog catalog;
        std::unordered_map<std::string, Visual> visuals;
        std::unordered_map<std::string, Microsoft::WRL::ComPtr<ID2D1Bitmap1>> bitmaps;
        std::vector<std::string> commandOrder;
        std::uint64_t decodedPixels{};
    };
}
