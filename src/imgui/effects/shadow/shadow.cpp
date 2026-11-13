#include <arbor/imgui/effects/shadow/shadow.hpp>

#include <arbor/imgui/effects/effects.hpp>

#include <algorithm>
#include <cmath>

void ui::draw_box_shadow(
    EffectRegistry& effects, ImDrawList& draw_list, Rect rect, const BoxShadow& shadow, float rounding, float opacity
) {
    if (opacity <= 0.0F || shadow.color.max_alpha() <= 0.0F || !rect.valid()) {
        return;
    }

    const float spread = shadow.spread;
    const Rect shape = {
        {rect.min.x + shadow.offset.x - spread, rect.min.y + shadow.offset.y - spread},
        {rect.max.x + shadow.offset.x + spread, rect.max.y + shadow.offset.y + spread},
    };
    if (!shape.valid()) {
        return;
    }

    const float blur = std::max(0.0F, shadow.blur);
    const float extent = std::max(1.0F, blur * 1.5F);
    const Rect bounds = {{shape.min.x - extent, shape.min.y - extent}, {shape.max.x + extent, shape.max.y + extent}};
    const GradientData* gradient = shadow.color.gradient();

    effects.effect(EffectSlot::BoxShadow)
        .submit(
            draw_list, BoxShadowRegion{
                           shape,
                           bounds,
                           rect,
                           std::max(0.0F, rounding + spread),
                           std::max(0.0F, rounding),
                           blur,
                           shadow.color.rgba(),
                           gradient != nullptr ? *gradient : GradientData(),
                           std::clamp(opacity, 0.0F, 1.0F),
                       }
        );
}
