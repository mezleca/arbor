#include <arbor/imgui/effects/gradient/gradient.hpp>

#include <arbor/imgui/effects/effects.hpp>

#include <algorithm>

bool ui::draw_gradient_rect(
    EffectRegistry& effects, ImDrawList& draw_list, Rect rect, const Color& color, float rounding, float opacity,
    float border_thickness
) {
    if (!rect.valid() || color.gradient() == nullptr || color.max_alpha() <= 0.0F || opacity <= 0.0F) return false;

    return effects.effect(EffectSlot::Gradient)
        .submit(
            draw_list, GradientRegion{
                           rect,
                           *color.gradient(),
                           std::max(0.0F, rounding),
                           std::max(0.0F, border_thickness),
                           std::clamp(opacity, 0.0F, 1.0F),
                       }
        );
}
