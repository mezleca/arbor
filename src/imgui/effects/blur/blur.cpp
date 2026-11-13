#include <arbor/imgui/effects/blur/blur.hpp>

#include <arbor/imgui/effects/effects.hpp>

#include <algorithm>

void ui::draw_blur(EffectRegistry& effects, ImDrawList& draw_list, Rect rect, int strength, float rounding, float opacity) {
    if (strength <= 0 || opacity <= 0.0F || !rect.valid()) {
        return;
    }

    effects.effect(EffectSlot::Blur).submit(draw_list, BlurRegion{rect, strength, rounding, std::clamp(opacity, 0.0F, 1.0F)});
}
