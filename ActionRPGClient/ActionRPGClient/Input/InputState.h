#pragma once

#include <vector>

namespace ActionRPG
{
    enum class InputKey
    {
        MoveLeft,
        MoveRight,
        MoveUp,
        MoveDown,
        ActionZ,
        ActionC
    };

    struct InputState
    {
        bool moveLeft{};
        bool moveRight{};
        bool moveUp{};
        bool moveDown{};
        std::vector<InputKey> pressedKeys;

        [[nodiscard]] bool WasPressed(const InputKey inKey) const
        {
            for (const InputKey key : pressedKeys)
            {
                if (key == inKey)
                {
                    return true;
                }
            }

            return false;
        }
    };
}
