#pragma once

#include "Input/InputState.h"

#include <array>
#include <cstddef>

namespace ActionRPG
{
    enum class RunDirection
    {
        None,
        Left,
        Right,
        Up,
        Down
    };

    // Detects a direction double-tap independently from the skill command queue.
    class RunState final
    {
    public:
        void Update(const InputState& inInput, double inCurrentTimeSeconds);

        [[nodiscard]] bool IsRunning() const { return runningDirection != RunDirection::None; }

    private:
        void OnDirectionPressed(RunDirection inDirection, double inCurrentTimeSeconds);
        [[nodiscard]] bool IsRunningDirectionHeld(const InputState& inInput) const;
        [[nodiscard]] static std::size_t ToIndex(RunDirection inDirection);

    private:
        static constexpr double DOUBLE_TAP_SECONDS = 0.25;

        RunDirection runningDirection{ RunDirection::None };
        std::array<double, 4> lastPressedTimes{ -1000.0, -1000.0, -1000.0, -1000.0 };
    };
}
