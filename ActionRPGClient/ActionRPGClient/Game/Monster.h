#pragma once
#include "Game/Character.h"

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

    // Dummy uses the shared character base, with no AI actions.
    class Monster final : public Character
    {
    public:
        Monster(const MonsterSpawn& inSpawn, const AssetCatalog& inAssetCatalog, D2DRenderer& inRenderer);
        Monster(const Monster& inTemplate, const MonsterSpawn& inSpawn);
        [[nodiscard]] std::uint64_t GetInstanceId() const { return instanceId; }
        [[nodiscard]] std::uint32_t GetDataId() const { return dataId; }
    private:
        std::uint64_t instanceId{};
        std::uint32_t dataId{};
    };
}
