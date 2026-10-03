#pragma once
#include "Game/DungeonCombat.h"
#include <chrono>
#include <deque>
#include <optional>

namespace ActionRPG
{
    // Game-thread-owned display history; no damage, collision or local-input decisions.
    class DungeonCombatBuffer final
    {
    public:
        void Clear();
        void Push(const DungeonCombatSnapshot& inSnapshot);
        [[nodiscard]] bool HasFreshRealtime() const;
        [[nodiscard]] std::optional<DungeonCombatSnapshot> Sample();
    private:
        std::deque<DungeonCombatSnapshot> frames;
        std::optional<DungeonCombatSnapshot> lastPresentation;
        std::chrono::steady_clock::time_point lastArrival{};
        double clockOffsetMs{}, lastDisplayTimeMs{};
    };
}
