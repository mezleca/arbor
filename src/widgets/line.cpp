#include <imgui-ui/widgets/line.hpp>

#include <imgui-ui/imgui/painter.hpp>

#include <algorithm>

using namespace ui;

LineWidget::LineWidget(ImVec2 start, ImVec2 end, const Color& color, float thickness)
    : StyledNode({}, "Line"), m_start(start), m_end(end) {
    configure_all_styles([&color, thickness](Style& style) { style.color(color).border_thickness(thickness); });
}

bool LineWidget::paint() {
    const ComputedStyle& current_style = computed_style();
    const float half_thickness = current_style.border_thickness() * 0.5F;
    set_visual_rect({
        {std::min(m_start.x, m_end.x) - half_thickness, std::min(m_start.y, m_end.y) - half_thickness},
        {std::max(m_start.x, m_end.x) + half_thickness, std::max(m_start.y, m_end.y) + half_thickness},
    });
    painter().line(m_start, m_end, current_style.color().value, current_style.border_thickness());
    return true;
}

LineWidget& LineWidget::set_points(ImVec2 start, ImVec2 end) {
    m_start = start;
    m_end = end;
    return *this;
}
