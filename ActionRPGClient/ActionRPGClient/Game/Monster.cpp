#include "Game/Monster.h"
#include <stdexcept>

namespace ActionRPG
{
    namespace
    {
        constexpr CharacterAnimationSet DUMMY_ANIMATIONS{
            "DummyIdle", "DummyIdle", "DummyIdle", "DummyIdle", "DummyIdle", "DummyIdle",
            "DummyIdle", "DummyIdle", "DummyIdle", "DummyIdle", "DummyIdle", "DummyIdle",
            "DummyIdle", "DummyIdle", "DummyIdle", "DummyIdle", "DummyIdle"
        };
    }
    Monster::Monster(const Monster& inTemplate, const MonsterSpawn& inSpawn)
        : Character(inTemplate), instanceId(inSpawn.instanceId), dataId(inSpawn.dataId)
    {
        if (dataId != 1 || instanceId == 0) throw std::runtime_error("Unsupported monster.");
        SetGroundPosition(inSpawn.position);
        SetFacingLeft(inSpawn.facingLeft);
        ResetActionState();
    }
    Monster::Monster(const MonsterSpawn& inSpawn, const AssetCatalog& inAssetCatalog, D2DRenderer& inRenderer)
        : Character(inSpawn.position, inAssetCatalog, inRenderer, DUMMY_ANIMATIONS),
          instanceId(inSpawn.instanceId), dataId(inSpawn.dataId)
    {
        if (dataId != 1 || instanceId == 0) throw std::runtime_error("Unsupported monster.");
        SetFacingLeft(inSpawn.facingLeft);
        SetRunningEnabled(false);
    }
}
