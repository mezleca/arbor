#pragma once

#include "../../../layout/geometry.hpp"
#include "../../../style/gradient-data.hpp"

namespace ui {
    class EffectRegistry;

    struct GradientRegion {
        Rect rect;
        GradientData gradient;
        float rounding = 0.0F;
        float border_thickness = 0.0F;
        float opacity = 1.0F;
    };

    bool draw_gradient_rect(
        EffectRegistry& effects, ImDrawList& draw_list, Rect rect, const Color& color, float rounding, float opacity,
        float border_thickness = 0.0F
    );
} // namespace ui
