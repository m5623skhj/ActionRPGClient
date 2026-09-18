#include "Game/RunState.h"

#include <stdexcept>

namespace ActionRPG
{
    void RunState::Update(const InputState& inInput, const double inCurrentTimeSeconds)
    {
        for (const InputKey key : inInput.pressedKeys)
        {
            switch (key)
            {
            case InputKey::MoveLeft:
                OnDirectionPressed(RunDirection::Left, inCurrentTimeSeconds);
                break;
            case InputKey::MoveRight:
                OnDirectionPressed(RunDirection::Right, inCurrentTimeSeconds);
                break;
            case InputKey::MoveUp:
                OnDirectionPressed(RunDirection::Up, inCurrentTimeSeconds);
                break;
            case InputKey::MoveDown:
                OnDirectionPressed(RunDirection::Down, inCurrentTimeSeconds);
                break;
            default:
                break;
            }
        }

        if (!IsRunningDirectionHeld(inInput))
        {
            runningDirection = RunDirection::None;
        }
    }

    void RunState::OnDirectionPressed(const RunDirection inDirection, const double inCurrentTimeSeconds)
    {
        const std::size_t directionIndex = ToIndex(inDirection);
        const double elapsedSinceLastPress = inCurrentTimeSeconds - lastPressedTimes[directionIndex];
        if (elapsedSinceLastPress <= DOUBLE_TAP_SECONDS)
        {
            runningDirection = inDirection;
        }

        lastPressedTimes[directionIndex] = inCurrentTimeSeconds;
    }

    bool RunState::IsRunningDirectionHeld(const InputState& inInput) const
    {
        switch (runningDirection)
        {
        case RunDirection::Left:
            return inInput.moveLeft;
        case RunDirection::Right:
            return inInput.moveRight;
        case RunDirection::Up:
            return inInput.moveUp;
        case RunDirection::Down:
            return inInput.moveDown;
        case RunDirection::None:
        default:
            return false;
        }
    }

    std::size_t RunState::ToIndex(const RunDirection inDirection)
    {
        switch (inDirection)
        {
        case RunDirection::Left:
            return 0;
        case RunDirection::Right:
            return 1;
        case RunDirection::Up:
            return 2;
        case RunDirection::Down:
            return 3;
        case RunDirection::None:
        default:
            throw std::logic_error("RunDirection::None does not have a double-tap timestamp.");
        }
    }
}
