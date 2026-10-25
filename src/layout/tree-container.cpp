#include <imgui-ui/layout/tree-container.hpp>

#include <algorithm>
#include <imgui.h>

using namespace ui;

TreeContainer::TreeContainer(std::string label, std::string id)
    : Container(id.empty() ? label : std::move(id), "TreeContainer"), m_label(std::move(label)) {
    set_size({grow(), fit()});
}

ImVec2 TreeContainer::measure_content(ImVec2 available) const {
    const float indent = ImGui::GetStyle().IndentSpacing;
    const float header = ImGui::GetFrameHeightWithSpacing();
    ImVec2 size = measure_children({std::max(0.0F, available.x - indent), std::max(0.0F, available.y - header)});
    if (!m_open) size.y = 0.0F;

    if (size.x > 0.0F) {
        size.x += indent;
    }

    size.x = std::max(size.x, ImGui::CalcTextSize(m_label.c_str()).x);
    size.y += header;
    return size;
}

bool TreeContainer::paint() {
    const bool open = ImGui::TreeNodeEx(this, ImGuiTreeNodeFlags_None, "%s", m_label.c_str());
    if (m_open != open) {
        m_open = open;
        invalidate_measure();
    }

    // treenodeex has already consumed the header.
    // the body child receives only the remaining outer rect.
    if (!open) {
        return false;
    }

    const ImVec2 body_position = ImGui::GetCursorScreenPos();
    m_outer_rect = layout().visual_rect();
    m_body_size = {
        std::max(0.0F, m_outer_rect.max.x - body_position.x),
        std::max(0.0F, m_outer_rect.max.y - body_position.y),
    };
    return Container::paint();
}

void TreeContainer::on_draw_end() {
    Container::on_draw_end();
    const Rect body_rect = layout().visual_rect();
    m_outer_rect.max.x = std::max(m_outer_rect.max.x, body_rect.max.x);
    m_outer_rect.max.y = std::max(m_outer_rect.max.y, body_rect.max.y);
    set_layout_rect(m_outer_rect);
    ImGui::TreePop();
}

ImVec2 TreeContainer::child_window_size() const {
    return m_body_size;
}

Rect TreeContainer::shadow_rect(Rect) const {
    return m_outer_rect;
}
