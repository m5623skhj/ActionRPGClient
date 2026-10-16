#pragma once

#include "Input/InputState.h"
#include <d2d1_1helper.h>
#include <cstddef>

namespace ActionRPG
{
    // Reference HUD: items | HP | two-row skills | MP | menu. Rendering and input share these bounds.
    struct HudLayout
    {
        static constexpr float SLOT_SIZE = 40.0f;
        static constexpr float ICON_PADDING = 3.0f;
        static constexpr float SLOT_GAP = 4.0f;
        static constexpr float ORB_SIZE = 92.0f;
        static constexpr float ORB_GAP = 6.0f;
        static constexpr float SIDE_GAP = 10.0f;
        static constexpr std::size_t ITEM_SLOT_COUNT = 6;
        static constexpr std::size_t SKILL_SLOT_COUNT = 12;
        static constexpr float ITEM_WIDTH = ITEM_SLOT_COUNT * SLOT_SIZE + (ITEM_SLOT_COUNT - 1) * SLOT_GAP;
        static constexpr float MENU_WIDTH = 96.0f;
        static constexpr std::size_t SKILL_COLUMNS = 6;

        D2D1_RECT_F items{}, hp{}, mp{}, menu{}, level{}, experience{};

        [[nodiscard]] static float Scale(float inViewportWidth) { return inViewportWidth / 1280.0f; }
        [[nodiscard]] static D2D1_RECT_F ScaleRectangle(const D2D1_RECT_F& inRect, float inScale)
        { return D2D1::RectF(inRect.left*inScale,inRect.top*inScale,inRect.right*inScale,inRect.bottom*inScale); }
        [[nodiscard]] static InputState ScaleInput(InputState inInput, float inScale)
        {
            inInput.mouseX*=inScale; inInput.mouseY*=inScale;
            inInput.clickX*=inScale; inInput.clickY*=inScale;
            inInput.rightClickX*=inScale; inInput.rightClickY*=inScale;
            return inInput;
        }

        [[nodiscard]] static HudLayout Calculate(const D2D1_RECT_F& inSkillBar, float inScale = 1.0f)
        {
            HudLayout result;
            const float centerY = (inSkillBar.top + inSkillBar.bottom) * 0.5f;
            const float orbSize=ORB_SIZE*inScale, orbGap=ORB_GAP*inScale, sideGap=SIDE_GAP*inScale;
            result.hp = D2D1::RectF(inSkillBar.left - orbGap - orbSize, centerY - orbSize / 2,
                inSkillBar.left - orbGap, centerY + orbSize / 2);
            result.mp = D2D1::RectF(inSkillBar.right + orbGap, result.hp.top,
                inSkillBar.right + orbGap + orbSize, result.hp.bottom);
            const float itemRight = result.hp.left - sideGap;
            result.items = D2D1::RectF(itemRight - ITEM_WIDTH*inScale, inSkillBar.bottom - SLOT_SIZE*inScale, itemRight, inSkillBar.bottom);
            result.menu = D2D1::RectF(result.mp.right + sideGap, result.items.top,
                result.mp.right + sideGap + MENU_WIDTH*inScale, result.items.bottom);
            const float experienceTop=inSkillBar.bottom+8.0f*inScale;
            result.level=D2D1::RectF(result.items.left,experienceTop,result.items.left+88.0f*inScale,experienceTop+22.0f*inScale);
            result.experience=D2D1::RectF(result.level.right+8.0f*inScale,experienceTop,result.menu.right,experienceTop+22.0f*inScale);
            return result;
        }
    };
}
