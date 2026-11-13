#include <arbor/layout/geometry.hpp>

#include <algorithm>
#include <cmath>

using namespace ui;

Placement NodeLayout::active_placement() const {
    if (m_has_arranged_position) return m_arranged_placement;

    Placement placement = m_config.placement;
    placement.offset.x *= dpi_scale();
    placement.offset.y *= dpi_scale();
    return placement;
}

float ui::dpi_scale() {
    return ImGui::GetCurrentContext() != nullptr ? ImGui::GetStyle().FontScaleDpi : 1.0f;
}

static float constrain_extent(float size, float minimum, float maximum, float insets, BoxSizing box_sizing) {
    const float padding = box_sizing == BoxSizing::ContentBox ? insets : 0.0F;
    const float lower = std::max(insets, minimum + padding);
    return std::clamp(size, lower, std::max(lower, maximum + padding));
}

Rect ui::viewport_work_area() {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    if (viewport == nullptr) return {};

    const ImVec2 size =
        viewport->WorkSize.x > 0.0F && viewport->WorkSize.y > 0.0F ? viewport->WorkSize : ImGui::GetIO().DisplaySize;
    return Rect::from_position_size(viewport->WorkPos, size);
}

float LayoutLimit::resolve(float available) const {
    return percentage ? std::max(0.0f, available) * value / 100.0f : value * dpi_scale();
}

float LayoutAxis::intrinsic(float measured, float insets, BoxSizing box_sizing) const {
    float size = mode == LayoutSizeMode::Percent ? 0.0F : measured;
    if (mode == LayoutSizeMode::Fixed) {
        size = (value * dpi_scale()) + (box_sizing == BoxSizing::ContentBox ? insets : 0.0f);
    }

    // percentage limits cannot participate in measurement of an unresolved parent. pixels still limit its fit size.
    return constrain_extent(
        size, minimum.percentage ? 0.0f : minimum.value * dpi_scale(),
        maximum.percentage ? std::numeric_limits<float>::infinity() : maximum.value * dpi_scale(), insets, box_sizing
    );
}

float LayoutAxis::resolve(float measured, float available, float insets, BoxSizing box_sizing) const {
    if (!std::isfinite(available)) return intrinsic(measured, insets, box_sizing);

    float size = std::max(0.0F, available);
    if (mode == LayoutSizeMode::Fixed)
        size = value * dpi_scale();
    else if (mode == LayoutSizeMode::Fit)
        size = measured;
    else if (mode == LayoutSizeMode::Percent)
        size *= value / 100.0F;

    if ((mode == LayoutSizeMode::Fixed || mode == LayoutSizeMode::Percent) && box_sizing == BoxSizing::ContentBox) size += insets;
    return constrain(size, available, insets, box_sizing);
}

float LayoutAxis::constrain(float size, float available, float insets, BoxSizing box_sizing) const {
    return constrain_extent(size, minimum.resolve(available), maximum.resolve(available), insets, box_sizing);
}

bool Placement::operator==(const Placement& other) const {
    return anchor.x == other.anchor.x && anchor.y == other.anchor.y && origin.x == other.origin.x && origin.y == other.origin.y &&
           offset.x == other.offset.x && offset.y == other.offset.y;
}

ImVec2 ui::clamp_position(Rect bounds, ImVec2 size, ImVec2 position) {
    return {
        std::clamp(position.x, bounds.min.x, std::max(bounds.min.x, bounds.max.x - size.x)),
        std::clamp(position.y, bounds.min.y, std::max(bounds.min.y, bounds.max.y - size.y)),
    };
}

ImVec2 NodeLayout::intrinsic_size() const {
    return intrinsic_size(m_measured_size);
}

ImVec2 NodeLayout::intrinsic_size(ImVec2 measured) const {
    return {
        m_config.size.width.intrinsic(measured.x, m_box_insets.horizontal(), m_box_sizing),
        m_config.size.height.intrinsic(measured.y, m_box_insets.vertical(), m_box_sizing),
    };
}

ImVec2 NodeLayout::resolve_size(ImVec2 available_size) const {
    return resolve_size(available_size, m_measured_size);
}

ImVec2 NodeLayout::resolve_size(ImVec2 available_size, ImVec2 measured) const {
    return {
        m_config.size.width.resolve(measured.x, available_size.x, m_box_insets.horizontal(), m_box_sizing),
        m_config.size.height.resolve(measured.y, available_size.y, m_box_insets.vertical(), m_box_sizing),
    };
}

ImVec2 NodeLayout::constrain_size(ImVec2 size, ImVec2 available_size) const {
    return {
        m_config.size.width.constrain(size.x, available_size.x, m_box_insets.horizontal(), m_box_sizing),
        m_config.size.height.constrain(size.y, available_size.y, m_box_insets.vertical(), m_box_sizing),
    };
}

void NodeLayout::set_size(LayoutSize size) {
    m_config.size = size;
    invalidate_resolved_size();
}

void NodeLayout::set_config(LayoutConfig config) {
    const bool size_changed = m_config.size != config.size;
    m_config = config;
    m_has_arranged_position = false;

    // placement changes discard arrangement without discarding an unchanged size allocation.
    if (size_changed) {
        invalidate_resolved_size();
    }
}

void NodeLayout::set_measured_size(ImVec2 size) {
    m_measured_size = size;
    invalidate_resolved_size();
}

void NodeLayout::set_box_insets(BoxInsets insets) {
    if (m_box_insets == insets) {
        return;
    }

    m_box_insets = insets;
    invalidate_resolved_size();
}

void NodeLayout::set_box_sizing(BoxSizing sizing) {
    if (m_box_sizing == sizing) {
        return;
    }

    m_box_sizing = sizing;
    invalidate_resolved_size();
}

void NodeLayout::assign_size(ImVec2 size) {
    m_size = size;
}

void NodeLayout::invalidate_resolved_size() {
    // expose the current intrinsic size until the next layout pass resolves or assigns an allocation.
    m_size = intrinsic_size();
    m_has_parent_size = false;
}

void NodeLayout::set_arranged_rects(Rect local_rect, Rect layout_rect) {
    m_local_rect = local_rect;
    m_layout_rect = layout_rect;
    m_visual_rect = layout_rect;
}
