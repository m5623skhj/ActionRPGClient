#pragma once

#include "Input/InputState.h"

#include <deque>
#include <span>

namespace ActionRPG
{
    struct CommandInputEvent
    {
        InputKey key{};
        double timeSeconds{};
    };

    // Stores ordered key-down events. Observers such as RunState never remove events;
    // only a successfully activated skill consumes its matched command sequence.
    class InputCommandQueue final
    {
    public:
        void Record(const InputState& inInput, double inCurrentTimeSeconds);
        [[nodiscard]] bool TryConsume(std::span<const InputKey> inSequence, double inMaxStepSeconds);

    private:
        void RemoveExpiredEvents(double inCurrentTimeSeconds);

    private:
        static constexpr double HISTORY_SECONDS = 1.5;

        std::deque<CommandInputEvent> events;
    };
}
