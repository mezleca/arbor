#pragma once

#include "../../../layout/geometry.hpp"

namespace ui {
    class EffectRegistry;

    struct BlurRegion {
        Rect output;
        int strength = 0;
        float rounding = 0.0F;
        float opacity = 1.0F;
    };

    void draw_blur(EffectRegistry& effects, ImDrawList& draw_list, Rect rect, int strength, float rounding, float opacity = 1.0F);
} // namespace ui
