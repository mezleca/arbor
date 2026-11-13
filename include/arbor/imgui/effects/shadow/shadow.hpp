#pragma once

#include "../../../layout/geometry.hpp"
#include "../../../style/values.hpp"
#include "../../../style/gradient-data.hpp"

#include <imgui.h>

namespace ui {
    class EffectRegistry;

    struct BoxShadowRegion {
        Rect shape;
        Rect bounds;
        Rect cutout;
        float rounding = 0.0F;
        float cutout_rounding = 0.0F;
        float blur = 0.0F;
        ImVec4 color;
        GradientData gradient{};
        float opacity = 1.0F;
    };

    void draw_box_shadow(
        EffectRegistry& effects, ImDrawList& draw_list, Rect rect, const BoxShadow& shadow, float rounding, float opacity = 1.0F
    );
} // namespace ui
