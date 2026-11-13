#pragma once

#include "computed-style.hpp"
#include "theme.hpp"

#include <cstdint>
#include <utility>

namespace ui {
    inline constexpr int MAX_BLUR_STRENGTH = 32;  // three box-blur passes sample at most 32 texels from either side.

    class VisualState;

    enum class StyleType : uint8_t {
        DEFAULT = 0,
        HOVER,
        ACTIVE,
        FOCUS,
        DISABLED,
        COUNT,
    };

    class Style : public ComputedStyle {
    public:
        using ChangeCallback = void (*)(void*, bool font_changed);

        using ComputedStyle::alpha;
        using ComputedStyle::background_color;
        using ComputedStyle::blur;
        using ComputedStyle::border;
        using ComputedStyle::border_bottom;
        using ComputedStyle::border_color;
        using ComputedStyle::border_left;
        using ComputedStyle::border_radius;
        using ComputedStyle::border_right;
        using ComputedStyle::border_style;
        using ComputedStyle::border_thickness;
        using ComputedStyle::border_top;
        using ComputedStyle::box_shadow;
        using ComputedStyle::box_sizing;
        using ComputedStyle::color;
        using ComputedStyle::cursor;
        using ComputedStyle::font;
        using ComputedStyle::font_size;
        using ComputedStyle::font_slant;
        using ComputedStyle::font_weight;
        using ComputedStyle::line_height;
        using ComputedStyle::margin;
        using ComputedStyle::overflow;
        using ComputedStyle::padding;
        using ComputedStyle::rotation;
        using ComputedStyle::scale;
        using ComputedStyle::scrollbar_background_color;
        using ComputedStyle::scrollbar_grab_active_color;
        using ComputedStyle::scrollbar_grab_color;
        using ComputedStyle::scrollbar_grab_hovered_color;
        using ComputedStyle::scrollbar_grab_rounding;
        using ComputedStyle::scrollbar_minimum_grab_size;
        using ComputedStyle::scrollbar_rounding;
        using ComputedStyle::scrollbar_size;
        using ComputedStyle::size;
        using ComputedStyle::variables;

        Style() = default;

        Style& font(FontReference value);
        Style& font_size(float value);
        /// nullopt restores inheritance for each attribute
        Style& font_weight(std::optional<FontWeight> value);
        Style& font_slant(std::optional<FontSlant> value);

        StyleVariableStore& variables() {
            return m_vars;
        }

        Style& padding(BoxInsets value, TransitionSpec transition = {});
        Style& padding_left(float value, TransitionSpec transition = {});
        Style& padding_top(float value, TransitionSpec transition = {});
        Style& padding_right(float value, TransitionSpec transition = {});
        Style& padding_bottom(float value, TransitionSpec transition = {});
        Style& margin(BoxInsets value, TransitionSpec transition = {});
        Style& margin_left(float value, TransitionSpec transition = {});
        Style& margin_top(float value, TransitionSpec transition = {});
        Style& margin_right(float value, TransitionSpec transition = {});
        Style& margin_bottom(float value, TransitionSpec transition = {});
        Style& line_height(float value, TransitionSpec transition = {});
        Style& box_sizing(BoxSizing value);
        Style& overflow(Overflow value);

        /// sets the layout request for this style state. a transition animates the resolved outer extent.
        Style& size(LayoutSize value, TransitionSpec transition = {});
        Style& width(LayoutAxis value, TransitionSpec transition = {});
        Style& height(LayoutAxis value, TransitionSpec transition = {});

        Style& rotation(float value, TransitionSpec transition = {});
        Style& scale(ImVec2 value, TransitionSpec transition = {});
        Style& scale(float value, TransitionSpec transition = {});
        Style& alpha(float value);
        Style& cursor(ImGuiMouseCursor value);

        Style& control(const Theme& theme, TransitionSpec transition = {0.15F});
        Style& control(const Theme& theme, ImVec2 padding, TransitionSpec transition = {0.15F});
        Style& color(Color value, TransitionSpec transition = {});
        Style& background_color(Color value, TransitionSpec transition = {});
        Style& border_color(Color value, TransitionSpec transition = {});
        Style& border_radius(float value);
        Style& border_thickness(float value, TransitionSpec transition = {});
        Style& border(BorderEdge all, TransitionSpec transition = {});
        Style& border(BorderEdges edges, TransitionSpec transition = {});
        Style& border_left(float size, BorderType type = BorderType::Solid, TransitionSpec transition = {});
        Style& border_top(float size, BorderType type = BorderType::Solid, TransitionSpec transition = {});
        Style& border_right(float size, BorderType type = BorderType::Solid, TransitionSpec transition = {});
        Style& border_bottom(float size, BorderType type = BorderType::Solid, TransitionSpec transition = {});
        Style& border_style(BorderType value);
        Style& box_shadow(BoxShadow value, TransitionSpec transition = {});
        Style& blur(int value);

        Style& scrollbar(const Theme::Scrollbar& value);
        Style& scrollbar_size(float value);
        Style& scrollbar_rounding(float value);
        Style& scrollbar_minimum_grab_size(float value);
        Style& scrollbar_grab_rounding(float value);
        Style& scrollbar_background_color(Color value, TransitionSpec transition = {});
        Style& scrollbar_grab_color(Color value, TransitionSpec transition = {});
        Style& scrollbar_grab_hovered_color(Color value, TransitionSpec transition = {});
        Style& scrollbar_grab_active_color(Color value, TransitionSpec transition = {});

        static bool lerp(Style& style, const Style& target, float dt);

    private:
        friend class VisualState;
        friend class PaintSlot;

        Style&
        set_inset(Value<BoxInsets> ComputedStyle::* member, float BoxInsets::* field, float value, TransitionSpec transition);
        Style& set_border_edge(
            FloatValue ComputedStyle::* size, BorderType BorderTypes::* type, float value, BorderType border_type,
            TransitionSpec transition
        );

        template <typename Field>
        Style& set_property(Field ComputedStyle::* member, Field value, bool affects_measure = true) {
            Field& current = this->*member;
            if (transition_values_equal(current, value)) return *this;

            current = std::move(value);
            if (affects_measure) notify_change();
            return *this;
        }

        template <typename ValueType, typename Field>
        Style& set_animated_transition(
            ValueType ComputedStyle::* member, Field value, TransitionSpec transition, bool affects_measure = true
        ) {
            ValueType& current = this->*member;
            const bool changed = !transition_values_equal(current.value, value);
            if (changed) current.set(std::move(value));

            current.set_transition(transition);
            if (changed && affects_measure) notify_change();

            return *this;
        }

        void set_change_callback(void* owner, ChangeCallback callback) {
            m_change_owner = owner;
            m_change_callback = callback;
        }

        void notify_change(bool font_changed = false) const {
            if (m_change_callback != nullptr) {
                m_change_callback(m_change_owner, font_changed);
            }
        }

        void* m_change_owner = nullptr;
        ChangeCallback m_change_callback = nullptr;
    };

} // namespace ui
