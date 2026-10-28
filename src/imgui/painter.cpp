#include <imgui-ui/imgui/painter.hpp>
#include <imgui-ui/imgui/paint-state.hpp>

#include <algorithm>
#include <array>

using namespace ui;

Painter::Painter(ImDrawList& draw_list, PaintState* state, EffectRegistry* effects, float opacity)
    : m_draw_list(draw_list), m_state(state), m_effects(effects),
      m_alpha(std::clamp(ImGui::GetStyle().Alpha, 0.0F, 1.0F) * std::clamp(opacity, 0.0F, 1.0F)) {}

ImColor Painter::color(const Color& source) const {
    ImVec4 rgba = source.rgba();
    rgba.w *= m_alpha;
    return ImColor{rgba};
}

void Painter::line(ImVec2 start, ImVec2 end, const Color& source, float thickness) const {
    m_draw_list.AddLine(start, end, color(source), thickness);
}

void Painter::circle(ImVec2 center, float radius, const Color& source) const {
    m_draw_list.AddCircleFilled(center, radius, color(source));
}

void Painter::circle_outline(ImVec2 center, float radius, const Color& source, float thickness) const {
    m_draw_list.AddCircle(center, radius, color(source), 0, thickness);
}

void Painter::rect_filled(Rect rect, const Color& source, float rounding, ImDrawFlags flags) const {
    m_draw_list.AddRectFilled(rect.min, rect.max, color(source), rounding, flags);
}

void Painter::rect_outline(Rect rect, const Color& source, float thickness, float rounding) const {
    if (thickness <= 0.0F) return;

    m_draw_list.AddRect(rect.min, rect.max, color(source), rounding, std::max(1.0F, thickness));
}

void Painter::triangle(ImVec2 center, ImVec2 size, const Color& source, TriangleDirection direction) const {
    static constexpr std::array<std::array<ImVec2, 3>, 4> DIRECTION_OFFSETS = {
        {
            {{{-1.0F, 1.0F}, {0.0F, -1.0F}, {1.0F, 1.0F}}},
            {{{-1.0F, -1.0F}, {1.0F, -1.0F}, {0.0F, 1.0F}}},
            {{{1.0F, -1.0F}, {1.0F, 1.0F}, {-1.0F, 0.0F}}},
            {{{-1.0F, -1.0F}, {-1.0F, 1.0F}, {1.0F, 0.0F}}},
        },
    };

    const ImVec2 half_size = {size.x * 0.5F, size.y * 0.5F};
    const auto& offsets = DIRECTION_OFFSETS[static_cast<std::size_t>(direction)];
    const auto vertex = [&](std::size_t index) {
        return ImVec2{center.x + (offsets[index].x * half_size.x), center.y + (offsets[index].y * half_size.y)};
    };

    m_draw_list.AddTriangleFilled(vertex(0), vertex(1), vertex(2), color(source));
}

void Painter::image(ImTextureID texture, Rect rect, ImVec2 uv_min, ImVec2 uv_max, const Color& tint, float rounding) const {
    m_draw_list.AddImageRounded(texture, rect.min, rect.max, uv_min, uv_max, color(tint), rounding);
}

void Painter::border(Rect rect, const ComputedStyle& style, const Color& source) const {
    const ImColor draw_color = color(source);
    if (style.uniform_border() && style.border_style() == BorderType::Solid) {
        const float inset = style.border_thickness() * BORDER_STROKE_INSET_SCALE;
        m_draw_list.AddRect(
            {rect.min.x + inset, rect.min.y + inset}, {rect.max.x - inset, rect.max.y - inset}, draw_color,
            std::max(0.0F, style.border_radius() - inset), style.border_thickness()
        );
        return;
    }

    const BoxInsets widths = style.border_widths();
    const Rect stroke_rect{
        {rect.min.x + (widths.left * 0.5F), rect.min.y + (widths.top * 0.5F)},
        {rect.max.x - (widths.right * 0.5F), rect.max.y - (widths.bottom * 0.5F)},
    };
    const float stroke_radius = std::max(0.0F, style.border_radius() - (style.border_thickness() * 0.5F));

    const auto draw_edges = [&](const BorderPath& path) {
        const auto draw_edge = [&](uint8_t side, BorderEdge edge) {
            if (edge.type != BorderType::None) {
                draw_border_path(m_draw_list, path, side, draw_color, edge.size, edge.type);
            }
        };
        draw_edge(BORDER_LEFT, style.border_left());
        draw_edge(BORDER_TOP, style.border_top());
        draw_edge(BORDER_RIGHT, style.border_right());
        draw_edge(BORDER_BOTTOM, style.border_bottom());
    };

    m_draw_list.PushClipRect(rect.min, rect.max, true);
    if (m_state != nullptr) {
        draw_edges(m_state->border_path(stroke_rect, stroke_radius));
    } else {
        draw_edges(rounded_rect_border_path(stroke_rect, stroke_radius));
    }
    m_draw_list.PopClipRect();
}
