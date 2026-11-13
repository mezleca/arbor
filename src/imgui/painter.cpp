#include <arbor/imgui/painter.hpp>
#include <arbor/imgui/paint-state.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <imgui_internal.h>
#include <stdexcept>

using namespace ui;

Painter::Painter(ImDrawList& draw_list, PaintState* state, EffectRegistry* effects, float opacity)
    : m_draw_list(draw_list), m_state(state), m_effects(effects),
      m_alpha(std::clamp(ImGui::GetStyle().Alpha, 0.0f, 1.0f) * std::clamp(opacity, 0.0f, 1.0f)), m_scale(dpi_scale()) {
    if (m_state != nullptr) m_state->apply_clip(m_draw_list, m_effects);
}

void Painter::push_clip(Rect rect, float rounding, bool intersect) const {
    if (m_state == nullptr) throw std::logic_error("painter clipping requires a paint state");

    rect = to_screen(rect);
    m_state->push_clip(rect, rounding * m_scale, intersect);

    // the shader owns the exact edge. a conservative pixel scissor must not cut its antialiasing coverage.
    if (rounding > 0.0f && rect.valid()) {
        const ImVec2 density = ImGui::GetIO().DisplayFramebufferScale;
        const ImVec2 origin = ImGui::GetMainViewport()->Pos;
        rect.min.x = origin.x + (std::floor((rect.min.x - origin.x) * density.x) / density.x);
        rect.min.y = origin.y + (std::floor((rect.min.y - origin.y) * density.y) / density.y);
        rect.max.x = origin.x + (std::ceil((rect.max.x - origin.x) * density.x) / density.x);
        rect.max.y = origin.y + (std::ceil((rect.max.y - origin.y) * density.y) / density.y);
    }

    if (&m_draw_list == ImGui::GetCurrentWindowRead()->DrawList) {
        ImGui::PushClipRect(rect.min, rect.max, intersect);
    } else {
        m_draw_list.PushClipRect(rect.min, rect.max, intersect);
    }
    m_state->apply_clip(m_draw_list, m_effects);
}

void Painter::pop_clip() const {
    if (m_state == nullptr) throw std::logic_error("painter clipping requires a paint state");

    m_state->pop_clip();
    if (&m_draw_list == ImGui::GetCurrentWindowRead()->DrawList) {
        ImGui::PopClipRect();
    } else {
        m_draw_list.PopClipRect();
    }
    m_state->apply_clip(m_draw_list, m_effects);
}

ImVec2 Painter::to_screen(ImVec2 point) const {
    return {point.x * m_scale, point.y * m_scale};
}

Rect Painter::to_screen(Rect rect) const {
    return {to_screen(rect.min), to_screen(rect.max)};
}

ImVec2 Painter::to_logical(ImVec2 point) const {
    return {point.x / m_scale, point.y / m_scale};
}

Rect Painter::to_logical(Rect rect) const {
    return {to_logical(rect.min), to_logical(rect.max)};
}

ImColor Painter::color(const Color& source) const {
    ImVec4 rgba = source.rgba();
    rgba.w *= m_alpha;
    return ImColor{rgba};
}

void Painter::line(ImVec2 start, ImVec2 end, const Color& source, float thickness) const {
    m_draw_list.AddLine(to_screen(start), to_screen(end), color(source), thickness * m_scale);
}

void Painter::circle(ImVec2 center, float radius, const Color& source) const {
    m_draw_list.AddCircleFilled(to_screen(center), radius * m_scale, color(source));
}

void Painter::circle_outline(ImVec2 center, float radius, const Color& source, float thickness) const {
    m_draw_list.AddCircle(to_screen(center), radius * m_scale, color(source), 0, thickness * m_scale);
}

void Painter::rect_filled(Rect rect, const Color& source, float rounding, ImDrawFlags flags) const {
    rect = to_screen(rect);
    m_draw_list.AddRectFilled(rect.min, rect.max, color(source), rounding * m_scale, flags);
}

void Painter::rect_outline(Rect rect, const Color& source, float thickness, float rounding) const {
    if (thickness <= 0.0F) return;

    rect = to_screen(rect);
    m_draw_list.AddRect(rect.min, rect.max, color(source), rounding * m_scale, std::max(1.0f, thickness * m_scale));
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
        return to_screen(ImVec2{center.x + (offsets[index].x * half_size.x), center.y + (offsets[index].y * half_size.y)});
    };

    m_draw_list.AddTriangleFilled(vertex(0), vertex(1), vertex(2), color(source));
}

void Painter::image(ImTextureID texture, Rect rect, ImVec2 uv_min, ImVec2 uv_max, const Color& tint, float rounding) const {
    rect = to_screen(rect);
    m_draw_list.AddImageRounded(texture, rect.min, rect.max, uv_min, uv_max, color(tint), rounding * m_scale);
}

void Painter::border(Rect rect, const ComputedStyle& style, const Color& source) const {
    rect = to_screen(rect);
    const ImColor draw_color = color(source);
    if (style.uniform_border() && style.border_style() == BorderType::Solid) {
        m_draw_list.AddRect(rect.min, rect.max, draw_color, style.border_radius() * m_scale, style.border_thickness() * m_scale);
        return;
    }

    const BoxInsets widths = style.border_widths() * m_scale;
    const Rect stroke_rect{
        {rect.min.x + (widths.left * 0.5F), rect.min.y + (widths.top * 0.5F)},
        {rect.max.x - (widths.right * 0.5F), rect.max.y - (widths.bottom * 0.5F)},
    };
    const float stroke_radius = std::max(0.0f, style.border_radius() - (style.border_thickness() * 0.5f)) * m_scale;

    // the inset path already positions the stroke. a local scissor would cut its aa fringe and stay fixed after rotation.
    const auto draw_edges = [&](const BorderPath& path) {
        const auto draw_edge = [&](uint8_t side, BorderEdge edge) {
            if (edge.type != BorderType::None) {
                draw_border_path(m_draw_list, path, side, draw_color, edge.size * m_scale, edge.type);
            }
        };
        draw_edge(BORDER_LEFT, style.border_left());
        draw_edge(BORDER_TOP, style.border_top());
        draw_edge(BORDER_RIGHT, style.border_right());
        draw_edge(BORDER_BOTTOM, style.border_bottom());
    };

    if (m_state != nullptr) {
        draw_edges(m_state->border_path(stroke_rect, stroke_radius));
    } else {
        draw_edges(rounded_rect_border_path(stroke_rect, stroke_radius));
    }
}
