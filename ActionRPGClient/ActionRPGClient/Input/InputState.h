#pragma once

#include <vector>
#include <string>

namespace ActionRPG
{
    enum class InputKey
    {
        MoveLeft,
        MoveRight,
        MoveUp,
        MoveDown,
        ActionZ,
        ActionC,
        ActionX,
        ActionV,
        SkillA, SkillS, SkillD, SkillF, SkillG, SkillH,
        SkillQ, SkillW, SkillE, SkillR, SkillT, SkillY,
        Item1, Item2, Item3, Item4, Item5, Item6,
        ConfirmSelection,
        ToggleSystemMenu
    };

    struct TextEditEvent
    {
        enum class Kind { Insert, Backspace, Submit, Cancel, CycleChannel };
        Kind kind{Kind::Insert};
        std::wstring text;
    };

    struct InputState
    {
        bool moveLeft{};
        bool moveRight{};
        bool moveUp{};
        bool moveDown{};
        float mouseX{};
        float mouseY{};
        float clickX{};
        float clickY{};
        int mouseWheelDelta{};
        bool leftMousePressed{};
        bool leftMouseDown{}, leftMouseReleased{}, rightMousePressed{}, cancelDrag{}, shiftHeld{};
        float rightClickX{}, rightClickY{};
        std::wstring textInput;
        std::vector<TextEditEvent> textEdits;
        std::wstring composition;
        bool imeComposing{};
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
