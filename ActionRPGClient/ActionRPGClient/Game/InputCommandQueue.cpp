#include "Game/InputCommandQueue.h"

#include <cstddef>

namespace ActionRPG
{
    void InputCommandQueue::Record(const InputState& inInput, const double inCurrentTimeSeconds)
    {
        RemoveExpiredEvents(inCurrentTimeSeconds);

        for (const InputKey key : inInput.pressedKeys)
        {
            events.push_back(CommandInputEvent{ key, inCurrentTimeSeconds });
        }
    }

    bool InputCommandQueue::TryConsume(const std::span<const InputKey> inSequence,
        const double inMaxStepSeconds)
    {
        if (inSequence.empty() || events.size() < inSequence.size())
        {
            return false;
        }

        const std::size_t firstEventIndex = events.size() - inSequence.size();
        for (std::size_t sequenceIndex = 0; sequenceIndex < inSequence.size(); ++sequenceIndex)
        {
            const std::size_t eventIndex = firstEventIndex + sequenceIndex;
            if (events[eventIndex].key != inSequence[sequenceIndex])
            {
                return false;
            }

            if (sequenceIndex > 0)
            {
                const double stepSeconds = events[eventIndex].timeSeconds - events[eventIndex - 1].timeSeconds;
                if (stepSeconds > inMaxStepSeconds)
                {
                    return false;
                }
            }
        }

        events.erase(events.begin() + static_cast<std::ptrdiff_t>(firstEventIndex), events.end());
        return true;
    }

    void InputCommandQueue::RemoveExpiredEvents(const double inCurrentTimeSeconds)
    {
        while (!events.empty() && inCurrentTimeSeconds - events.front().timeSeconds > HISTORY_SECONDS)
        {
            events.pop_front();
        }
    }
}
