#include <arbor/style/style.hpp>

#include <algorithm>
#include <cmath>
#include <type_traits>
#include <variant>

using namespace ui;

static BoxInsets nonnegative_insets(BoxInsets value) {
    return {std::max(0.0F, value.left), std::max(0.0F, value.top), std::max(0.0F, value.right), std::max(0.0F, value.bottom)};
}

Style& Style::font(FontReference value) {
    if (m_font == value) return *this;

    m_font = value;
    notify_change(true);
    return *this;
}

Style& Style::font_weight(std::optional<FontWeight> value) {
    if (m_font_weight == value) return *this;

    m_font_weight = value;
    notify_change(true);
    return *this;
}

Style& Style::font_slant(std::optional<FontSlant> value) {
    if (m_font_slant == value) return *this;

    m_font_slant = value;
    notify_change(true);
    return *this;
}

Style& Style::font_size(float value) {
    value = std::max(0.0F, value);
    if (m_font_size == value) return *this;

    m_font_size = value;
    notify_change(true);
    return *this;
}

Style& Style::padding(BoxInsets value, TransitionSpec transition) {
    return set_animated_transition(&Style::m_padding, nonnegative_insets(value), transition);
}

Style& Style::margin(BoxInsets value, TransitionSpec transition) {
    return set_animated_transition(&Style::m_margin, nonnegative_insets(value), transition);
}

Style&
Style::set_inset(Value<BoxInsets> ComputedStyle::* member, float BoxInsets::* field, float value, TransitionSpec transition) {
    BoxInsets insets = (this->*member).value;
    insets.*field = value;
    return set_animated_transition(member, nonnegative_insets(insets), transition);
}

Style& Style::padding_left(float value, TransitionSpec transition) {
    return set_inset(&Style::m_padding, &BoxInsets::left, value, transition);
}

Style& Style::padding_top(float value, TransitionSpec transition) {
    return set_inset(&Style::m_padding, &BoxInsets::top, value, transition);
}

Style& Style::padding_right(float value, TransitionSpec transition) {
    return set_inset(&Style::m_padding, &BoxInsets::right, value, transition);
}

Style& Style::padding_bottom(float value, TransitionSpec transition) {
    return set_inset(&Style::m_padding, &BoxInsets::bottom, value, transition);
}

Style& Style::margin_left(float value, TransitionSpec transition) {
    return set_inset(&Style::m_margin, &BoxInsets::left, value, transition);
}

Style& Style::margin_top(float value, TransitionSpec transition) {
    return set_inset(&Style::m_margin, &BoxInsets::top, value, transition);
}

Style& Style::margin_right(float value, TransitionSpec transition) {
    return set_inset(&Style::m_margin, &BoxInsets::right, value, transition);
}

Style& Style::margin_bottom(float value, TransitionSpec transition) {
    return set_inset(&Style::m_margin, &BoxInsets::bottom, value, transition);
}

Style& Style::line_height(float value, TransitionSpec transition) {
    return set_animated_transition(&Style::m_line_height, std::max(0.0F, value), transition);
}

Style& Style::box_sizing(BoxSizing value) {
    return set_property(&Style::m_box_sizing, value);
}

Style& Style::overflow(Overflow value) {
    return set_property(&Style::m_overflow, value);
}

Style& Style::size(LayoutSize value, TransitionSpec transition) {
    m_size_transition = transition;
    return set_property(&Style::m_size, value);
}

Style& Style::width(LayoutAxis value, TransitionSpec transition) {
    return size({value, m_size.height}, transition);
}

Style& Style::height(LayoutAxis value, TransitionSpec transition) {
    return size({m_size.width, value}, transition);
}

Style& Style::rotation(float value, TransitionSpec transition) {
    return set_animated_transition(&Style::m_rotation, std::isfinite(value) ? value : 0.0F, transition, false);
}

Style& Style::scale(ImVec2 value, TransitionSpec transition) {
    value = {std::isfinite(value.x) ? value.x : 1.0F, std::isfinite(value.y) ? value.y : 1.0F};
    return set_animated_transition(&Style::m_scale, value, transition, false);
}

Style& Style::scale(float value, TransitionSpec transition) {
    return scale({value, value}, transition);
}

Style& Style::alpha(float value) {
    return set_property(&Style::m_alpha, std::clamp(value, 0.0F, 1.0F), false);
}

Style& Style::cursor(ImGuiMouseCursor value) {
    return set_property(&Style::m_cursor, value, false);
}

Style& Style::control(const Theme& theme, TransitionSpec transition) {
    return control(theme, theme.metrics.frame_padding, transition);
}

Style& Style::control(const Theme& theme, ImVec2 padding, TransitionSpec transition) {
    return color(theme.text_color)
        .background_color(theme.controls.background_color, transition)
        .border_color(theme.controls.border_color, transition)
        .padding(BoxInsets{padding.x, padding.y})
        .border(BorderEdge{theme.controls.border_thickness, BorderType::Solid})
        .border_radius(theme.controls.rounding);
}

Style& Style::color(Color value, TransitionSpec transition) {
    return set_animated_transition(&Style::m_color, std::move(value), transition, false);
}

Style& Style::background_color(Color value, TransitionSpec transition) {
    return set_animated_transition(&Style::m_background_color, std::move(value), transition, false);
}

Style& Style::border_color(Color value, TransitionSpec transition) {
    return set_animated_transition(&Style::m_border_color, std::move(value), transition, false);
}

Style& Style::border_radius(float value) {
    return set_property(&Style::m_border_radius, std::max(0.0f, value), false);
}

Style& Style::border_thickness(float value, TransitionSpec transition) {
    value = std::max(0.0F, value);
    FloatValue ComputedStyle::* const sizes[] = {
        &Style::m_border_left_size,
        &Style::m_border_top_size,
        &Style::m_border_right_size,
        &Style::m_border_bottom_size,
    };
    for (auto size : sizes) {
        set_animated_transition(size, value, transition);
    }
    return *this;
}

Style& Style::border_style(BorderType value) {
    BorderTypes types = m_border_types;
    BorderType BorderTypes::* const members[] = {
        &BorderTypes::left,
        &BorderTypes::top,
        &BorderTypes::right,
        &BorderTypes::bottom,
    };
    for (auto member : members) {
        if (types.*member != BorderType::None) types.*member = value;
    }
    return set_property(&Style::m_border_types, types);
}

Style& Style::border(BorderEdge all, TransitionSpec transition) {
    return border(BorderEdges{all, all, all, all}, transition);
}

Style& Style::border(BorderEdges edges, TransitionSpec transition) {
    border_left(edges.left.size, edges.left.type, transition);
    border_top(edges.top.size, edges.top.type, transition);
    border_right(edges.right.size, edges.right.type, transition);
    return border_bottom(edges.bottom.size, edges.bottom.type, transition);
}

Style& Style::set_border_edge(
    FloatValue ComputedStyle::* size, BorderType BorderTypes::* type, float value, BorderType border_type,
    TransitionSpec transition
) {
    BorderTypes types = m_border_types;
    types.*type = border_type;
    set_property(&Style::m_border_types, types);
    return set_animated_transition(size, std::max(0.0F, value), transition);
}

Style& Style::border_left(float size, BorderType type, TransitionSpec transition) {
    return set_border_edge(&Style::m_border_left_size, &BorderTypes::left, size, type, transition);
}

Style& Style::border_top(float size, BorderType type, TransitionSpec transition) {
    return set_border_edge(&Style::m_border_top_size, &BorderTypes::top, size, type, transition);
}

Style& Style::border_right(float size, BorderType type, TransitionSpec transition) {
    return set_border_edge(&Style::m_border_right_size, &BorderTypes::right, size, type, transition);
}

Style& Style::border_bottom(float size, BorderType type, TransitionSpec transition) {
    return set_border_edge(&Style::m_border_bottom_size, &BorderTypes::bottom, size, type, transition);
}

Style& Style::box_shadow(BoxShadow value, TransitionSpec transition) {
    value.blur = std::max(0.0F, value.blur);

    if (value.color.gradient() == nullptr) {
        ImVec4 solid = value.color.rgba();
        solid.w = std::clamp(solid.w, 0.0F, 1.0F);
        value.color = solid;
    }

    return set_animated_transition(&Style::m_box_shadow, std::move(value), transition, false);
}

Style& Style::blur(int value) {
    return set_property(&Style::m_blur, std::clamp(value, 0, MAX_BLUR_STRENGTH), false);
}

Style& Style::scrollbar(const Theme::Scrollbar& value) {
    return scrollbar_size(value.size)
        .scrollbar_rounding(value.rounding)
        .scrollbar_minimum_grab_size(value.minimum_grab_size)
        .scrollbar_grab_rounding(value.grab_rounding)
        .scrollbar_background_color(value.background_color)
        .scrollbar_grab_color(value.grab_color)
        .scrollbar_grab_hovered_color(value.grab_hovered_color)
        .scrollbar_grab_active_color(value.grab_active_color);
}

Style& Style::scrollbar_size(float value) {
    return set_property(&Style::m_scrollbar_size, std::max(0.0F, value));
}

Style& Style::scrollbar_rounding(float value) {
    return set_property(&Style::m_scrollbar_rounding, std::max(0.0f, value), false);
}

Style& Style::scrollbar_minimum_grab_size(float value) {
    return set_property(&Style::m_scrollbar_minimum_grab_size, std::max(1.0f, value), false);
}

Style& Style::scrollbar_grab_rounding(float value) {
    return set_property(&Style::m_scrollbar_grab_rounding, std::max(0.0f, value), false);
}

Style& Style::scrollbar_background_color(Color value, TransitionSpec transition) {
    return set_animated_transition(&Style::m_scrollbar_background_color, std::move(value), transition, false);
}

Style& Style::scrollbar_grab_color(Color value, TransitionSpec transition) {
    return set_animated_transition(&Style::m_scrollbar_grab_color, std::move(value), transition, false);
}

Style& Style::scrollbar_grab_hovered_color(Color value, TransitionSpec transition) {
    return set_animated_transition(&Style::m_scrollbar_grab_hovered_color, std::move(value), transition, false);
}

Style& Style::scrollbar_grab_active_color(Color value, TransitionSpec transition) {
    return set_animated_transition(&Style::m_scrollbar_grab_active_color, std::move(value), transition, false);
}

bool Style::lerp(Style& style, const Style& target, float dt) {
    const bool font_changed = style.m_font != target.m_font || style.m_font_size != target.m_font_size ||
                              style.m_font_weight != target.m_font_weight || style.m_font_slant != target.m_font_slant;
    bool measure_changed = style.m_size != target.m_size || style.m_box_sizing != target.m_box_sizing ||
                           style.m_border_types != target.m_border_types;

    style.m_font = target.m_font;
    style.m_font_weight = target.m_font_weight;
    style.m_font_slant = target.m_font_slant;
    style.m_font_size = target.m_font_size;
    style.m_box_sizing = target.m_box_sizing;
    style.m_overflow = target.m_overflow;
    style.m_size = target.m_size;
    style.m_size_transition = target.m_size_transition;
    style.m_alpha = target.m_alpha;
    style.m_cursor = target.m_cursor;
    style.m_scrollbar_size = target.m_scrollbar_size;
    style.m_scrollbar_rounding = target.m_scrollbar_rounding;
    style.m_scrollbar_minimum_grab_size = target.m_scrollbar_minimum_grab_size;
    style.m_scrollbar_grab_rounding = target.m_scrollbar_grab_rounding;
    style.m_blur = target.m_blur;
    style.m_border_radius = target.m_border_radius;
    style.m_border_types = target.m_border_types;

    bool transitioning = false;
    const auto tick = [&](auto& current, const auto& destination, bool affects_measure = false) {
        const bool changed = current.tick(destination, dt);
        measure_changed |= changed && affects_measure;
        transitioning |= current.is_transitioning();
    };

    // record changes to margin, padding and line height. notify the owner after all properties have advanced.
    tick(style.m_margin, target.m_margin, true);
    tick(style.m_padding, target.m_padding, true);
    tick(style.m_border_left_size, target.m_border_left_size, true);
    tick(style.m_border_top_size, target.m_border_top_size, true);
    tick(style.m_border_right_size, target.m_border_right_size, true);
    tick(style.m_border_bottom_size, target.m_border_bottom_size, true);
    tick(style.m_line_height, target.m_line_height, true);

    tick(style.m_rotation, target.m_rotation);
    tick(style.m_scale, target.m_scale);

    tick(style.m_box_shadow, target.m_box_shadow);
    tick(style.m_color, target.m_color);
    tick(style.m_border_color, target.m_border_color);
    tick(style.m_background_color, target.m_background_color);

    tick(style.m_scrollbar_background_color, target.m_scrollbar_background_color);
    tick(style.m_scrollbar_grab_color, target.m_scrollbar_grab_color);
    tick(style.m_scrollbar_grab_hovered_color, target.m_scrollbar_grab_hovered_color);
    tick(style.m_scrollbar_grab_active_color, target.m_scrollbar_grab_active_color);

    for (auto& [key, value] : style.m_vars) {
        const StyleValue* target_value = target.m_vars.find(key);

        std::visit(
            [&](auto& current_value) {
                using T = std::decay_t<decltype(current_value)>;
                const T* typed_target = std::get_if<T>(target_value);
                if (typed_target != nullptr) {
                    current_value.tick(*typed_target, dt);
                }
                transitioning |= current_value.is_transitioning();
            },
            value
        );
    }

    if (font_changed || measure_changed) {
        style.notify_change(font_changed);
    }

    return transitioning;
}
