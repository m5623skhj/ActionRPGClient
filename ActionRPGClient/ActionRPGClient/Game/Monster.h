#pragma once
#include "Game/Character.h"

#include <map>
#include <string>
#include <unordered_map>
#include <vector>

namespace ActionRPG
{
    struct MonsterSpawn
    {
        std::uint64_t instanceId{};
        std::uint32_t dataId{};
        Vector2 position{};
        bool facingLeft{};
        std::uint32_t hp{};
        std::uint32_t maxHp{};
    };

    enum class MonsterMotion
    {
        Idle, Move, Hit, AirborneLaunch, AirborneHold, AirborneFall,
        Knockdown, GetUp, Attack, Death
    };

    struct MonsterClipDefinition
    {
        std::string image;
        std::vector<SpriteFrame> frames;
        float frameSeconds{ 1.0f };
        float scale{ 1.0f };
        bool loop{};
        bool holdLastFrame{};
    };

    struct MonsterVisualDefinition
    {
        std::string monsterId;
        std::string idleAnimationSection;
        std::map<MonsterMotion, MonsterClipDefinition> clips;
    };

    // Loaded once; runtime IDs are checked against this immutable client registry.
    class MonsterCatalog final
    {
    public:
        explicit MonsterCatalog(const AssetCatalog& inAssetCatalog);
        [[nodiscard]] bool Contains(std::uint32_t inDataId) const { return definitions.contains(inDataId); }
        [[nodiscard]] const MonsterVisualDefinition& Get(std::uint32_t inDataId) const;
    private:
        std::unordered_map<std::uint32_t, MonsterVisualDefinition> definitions;
    };

    // All pose and playback mutations happen on the game thread. No local AI or damage inference.
    class Monster final : public Character
    {
    public:
        Monster(const MonsterSpawn& inSpawn, const MonsterCatalog& inCatalog,
            const AssetCatalog& inAssetCatalog, D2DRenderer& inRenderer);
        Monster(const Monster& inTemplate, const MonsterSpawn& inSpawn);
        void Update(float inDeltaSeconds);
        void Render(D2DRenderer& inRenderer, const Camera& inCamera) const override;
        void ApplyHit(CharacterHitType inType) override;
        void ResetActionState() override;
        [[nodiscard]] bool IsHitReacting() const override;
        [[nodiscard]] MonsterMotion GetMotion() const { return motion; }
        void PlayMotion(MonsterMotion inMotion);
        // Flight height and phase come from authoritative state, not a timed atlas preview.
        void ApplyPresentationState(MonsterMotion inMotion, Vector2 inPosition,
            bool inFacingLeft, float inHeight);
        void ApplyCombatState(const CombatMonsterState& inState, const CombatRules& inRules);
        [[nodiscard]] std::uint32_t GetHp() const { return serverState ? serverState->hp : 0; }
        [[nodiscard]] std::uint32_t GetMaxHp() const { return serverState ? serverState->maxHp : 0; }
        [[nodiscard]] std::uint64_t GetInstanceId() const { return instanceId; }
        [[nodiscard]] std::uint32_t GetDataId() const { return dataId; }
    private:
        struct Clip
        {
            SpriteAnimation animation;
            bool loop{};
            bool holdLastFrame{};
        };
        void ConfigureSpawn(const MonsterSpawn& inSpawn);
        std::uint64_t instanceId{};
        std::uint32_t dataId{};
        MonsterMotion motion{ MonsterMotion::Idle };
        std::map<MonsterMotion, Clip> clips;
        std::optional<CombatMonsterState> serverState;
        CombatRules serverRules;
        float serverPresentationSeconds{};
    };
}
