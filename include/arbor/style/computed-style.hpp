#pragma once

#include "box-sizing.hpp"
#include "variables.hpp"
#include "../layout/geometry.hpp"
#include "../transition.hpp"
#include "../resources/font-registry.hpp"

#include <algorithm>
#include <cstdint>

namespace ui {
    class StyledNode;

    enum Border : uint8_t {
        BORDER_NONE = 0,
        BORDER_LEFT = 1U << 0U,
        BORDER_TOP = 1U << 1U,
        BORDER_RIGHT = 1U << 2U,
        BORDER_BOTTOM = 1U << 3U,

        BORDER_ALL = BORDER_LEFT | BORDER_TOP | BORDER_RIGHT | BORDER_BOTTOM,
    };

    enum class BorderType : uint8_t {
        Solid,
        Dashed,
        Dotted,
        None,
    };

    struct BorderEdge {
        float size = 0.0F;
        BorderType type = BorderType::Solid;
    };

    struct BorderEdges {
        BorderEdge left;
        BorderEdge top;
        BorderEdge right;
        BorderEdge bottom;
    };

    struct BorderTypes {
        BorderType left = BorderType::None;
        BorderType top = BorderType::None;
        BorderType right = BorderType::None;
        BorderType bottom = BorderType::None;

        constexpr bool operator==(const BorderTypes&) const = default;
    };

    enum class Overflow : uint8_t {
        /// lets the node's own content and descendant effects escape. borderless containers also let surfaces escape.
        Visible,

        /// clips descendants and effects to the container while retaining enabled scrolling.
        Hidden,

        /// clips descendants and effects to the container and disables scrolling.
        Clip,
    };

    struct PushState {
        bool font_pushed = false;
        int variables = 0;
        int colors = 0;
    };

    class ComputedStyle {
    public:
        ComputedStyle();

        FontReference font() const {
            return m_font;
        }

        std::optional<FontWeight> font_weight() const {
            return m_font_weight;
        }

        std::optional<FontSlant> font_slant() const {
            return m_font_slant;
        }

        float font_size() const {
            return m_font_size;
        }

        const LayoutSize& size() const {
            return m_size;
        }

        const TransitionSpec& size_transition() const {
            return m_size_transition;
        }

        const BoxInsets& padding() const {
            return m_padding.value;
        }

        BoxSizing box_sizing() const {
            return m_box_sizing;
        }

        Overflow overflow() const {
            return m_overflow;
        }

        const BoxInsets& margin() const {
            return m_margin.value;
        }

        float line_height() const {
            return m_line_height.value;
        }

        float rotation() const {
            return m_rotation.value;
        }

        const ImVec2& scale() const {
            return m_scale.value;
        }

        float alpha() const {
            return m_alpha;
        }

        ImGuiMouseCursor cursor() const {
            return m_cursor;
        }

        float scrollbar_size() const {
            return m_scrollbar_size;
        }

        float scrollbar_rounding() const {
            return m_scrollbar_rounding;
        }

        float scrollbar_minimum_grab_size() const {
            return m_scrollbar_minimum_grab_size;
        }

        float scrollbar_grab_rounding() const {
            return m_scrollbar_grab_rounding;
        }

        const ColorValue& scrollbar_background_color() const {
            return m_scrollbar_background_color;
        }

        const ColorValue& scrollbar_grab_color() const {
            return m_scrollbar_grab_color;
        }

        const ColorValue& scrollbar_grab_hovered_color() const {
            return m_scrollbar_grab_hovered_color;
        }

        const ColorValue& scrollbar_grab_active_color() const {
            return m_scrollbar_grab_active_color;
        }

        const ColorValue& color() const {
            return m_color;
        }

        const ColorValue& border_color() const {
            return m_border_color;
        }

        const ColorValue& background_color() const {
            return m_background_color;
        }

        const BoxShadow& box_shadow() const {
            return m_box_shadow.value;
        }

        int blur() const {
            return m_blur;
        }

        float border_radius() const {
            return m_border_radius;
        }

        float border_thickness() const {
            return std::max({
                m_border_left_size.value,
                m_border_top_size.value,
                m_border_right_size.value,
                m_border_bottom_size.value,
            });
        }

        uint8_t border() const;
        bool uniform_border() const;
        BoxInsets border_widths() const;

        BorderEdge border_left() const {
            return {m_border_left_size.value, m_border_types.left};
        }

        BorderEdge border_top() const {
            return {m_border_top_size.value, m_border_types.top};
        }

        BorderEdge border_right() const {
            return {m_border_right_size.value, m_border_types.right};
        }

        BorderEdge border_bottom() const {
            return {m_border_bottom_size.value, m_border_types.bottom};
        }

        BorderType border_style() const {
            return m_border_types.left;
        }

        const StyleVariableStore& variables() const {
            return m_vars;
        }

    protected:
        PushState push(float opacity, ImFont* effective_font, float effective_font_size) const;

        static void pop(PushState state);

        friend class StyledNode;

        FontReference m_font;
        std::optional<FontWeight> m_font_weight;
        std::optional<FontSlant> m_font_slant;
        float m_font_size = 0.0F;
        Value<BoxInsets> m_margin;
        Value<BoxInsets> m_padding;
        LayoutSize m_size;
        TransitionSpec m_size_transition;
        FloatValue m_line_height{1.0F};
        FloatValue m_rotation{0.0F};
        Vec2Value m_scale{ImVec2{1.0F, 1.0F}};
        float m_alpha = 1.0F;
        ImGuiMouseCursor m_cursor = ImGuiMouseCursor_None;
        float m_scrollbar_size = 14.0F;
        float m_scrollbar_rounding = 9.0F;
        float m_scrollbar_minimum_grab_size = 12.0F;
        float m_scrollbar_grab_rounding = 9.0F;
        int m_blur = 0;
        FloatValue m_border_left_size{1.0F};
        FloatValue m_border_top_size{1.0F};
        FloatValue m_border_right_size{1.0F};
        FloatValue m_border_bottom_size{1.0F};
        float m_border_radius = 4.0F;
        BoxSizing m_box_sizing = BoxSizing::ContentBox;
        Overflow m_overflow = Overflow::Visible;
        BorderTypes m_border_types;
        BoxShadowValue m_box_shadow;
        ColorValue m_color;
        ColorValue m_border_color;
        ColorValue m_background_color;
        ColorValue m_scrollbar_background_color;
        ColorValue m_scrollbar_grab_color;
        ColorValue m_scrollbar_grab_hovered_color;
        ColorValue m_scrollbar_grab_active_color;
        StyleVariableStore m_vars;
    };
} // namespace ui
