#include <arbor/layout/container.hpp>

#include <arbor/constants.hpp>
#include <arbor/imgui/painter.hpp>
#include <arbor/imgui/paint-state.hpp>
#include <arbor/imgui/window.hpp>

#include <algorithm>
#include <cmath>
#include <imgui_internal.h>

using namespace ui;

static ImVec4 current_clip(const ImDrawList& draw_list) {
    const ImVec2 min = draw_list.GetClipRectMin();
    const ImVec2 max = draw_list.GetClipRectMax();
    return {min.x, min.y, max.x, max.y};
}

static ImVec4 content_clip(const ComputedStyle& style, Rect rect) {
    const BoxInsets widths = style.border_widths() * dpi_scale();
    rect.min.x += widths.left;
    rect.min.y += widths.top;
    if (widths.right > 0.0F) rect.max.x -= widths.right + 1.0F;
    if (widths.bottom > 0.0F) rect.max.y -= widths.bottom + 1.0F;
    return {rect.min.x, rect.min.y, rect.max.x, rect.max.y};
}

static ImVec4 intersect_clip(ImVec4 clip, Rect rect) {
    clip.x = std::max(clip.x, rect.min.x);
    clip.y = std::max(clip.y, rect.min.y);
    clip.z = std::min(clip.z, rect.max.x);
    clip.w = std::min(clip.w, rect.max.y);
    return clip;
}

bool Container::paint() {
    const ComputedStyle& current_style = computed_style();

    ImGuiChildFlags child_flags = ImGuiChildFlags_AlwaysUseWindowPadding;
    ImGuiWindowFlags window_flags = child_window_flags() | ImGuiWindowFlags_NoBackground;
    const bool scrollable = scroll_active();
    // keep native scrollbar dragging enabled. routed wheel events enqueue the container's configured scroll behavior.
    if (scrollable) {
        window_flags &= ~ImGuiWindowFlags_NoScrollbar;
    }
    if (scrollable && m_scroll.horizontal()) {
        window_flags |= ImGuiWindowFlags_HorizontalScrollbar;
    }

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, box_insets().window_padding());

    // preserve the allocated max edge after rounding the child origin.
    const ImVec2 position = ImGui::GetCursorScreenPos();
    const ImVec2 child_position = {std::round(position.x), std::round(position.y)};
    const ImVec2 requested_size = child_window_size();
    ImVec2 child_size = {
        std::max(0.0F, requested_size.x + position.x - child_position.x),
        std::max(0.0F, requested_size.y + position.y - child_position.y),
    };
    const LayoutSize& size_spec = layout().size_spec();
    // clamp growing children to the parent's visible width.
    if (size_spec.width.mode == LayoutSizeMode::Grow && layout().in_flow()) {
        child_size.x = std::min(child_size.x, ImGui::GetCurrentWindow()->InnerRect.GetWidth());
    }
    ImGui::SetCursorScreenPos(child_position);

    const ImGuiID child_id = ImGui::GetID(this);
    // beginchild replaces the incoming clip before border and overflow drawing.
    const ImVec4 parent_clip = current_clip(*ImGui::GetWindowDrawList());
    PaintState& state = paint_state();
    const ImVec4 parent_effect_clip = state.effect_clip(parent_clip);
    ImGui::BeginChild(child_id, child_size, child_flags, window_flags);
    apply_window_insets(box_insets());
    m_child_window_open = true;
    // register the visible child area for wheel hit testing, then advance its pending scroll offset.
    // nested children register later and take precedence over this area.
    if (scrollable) {
        const ImDrawList* draw_list = ImGui::GetWindowDrawList();
        register_scroll_target({draw_list->GetClipRectMin(), draw_list->GetClipRectMax()});
        m_scroll.advance(*ImGui::GetCurrentWindow());
    }

    const Rect resolved_child_rect = Rect::from_position_size(ImGui::GetWindowPos(), ImGui::GetWindowSize());
    set_layout_rect(resolved_child_rect);

    const Painter paint = painter();
    EffectRegistry* effects = effect_registry();

    // blur and shadow read the ancestor effect clip before the child content clip is narrowed.
    if (effects != nullptr && (current_style.blur() > 0 || current_style.box_shadow().color.max_alpha() > 0.0F)) {
        const ImRect blur_rect = ImGui::GetCurrentWindow()->InnerRect;
        ImGui::PushClipRect({parent_effect_clip.x, parent_effect_clip.y}, {parent_effect_clip.z, parent_effect_clip.w}, false);
        paint.blur(paint.to_logical(Rect{blur_rect.Min, blur_rect.Max}), current_style);
        paint.shadow(paint.to_logical(shadow_rect(resolved_child_rect)), current_style);
        ImGui::PopClipRect();
    }

    // the child's inner clip can cut off its own border. replace it with the ancestor clip while painting the frame.
    ImGui::PushClipRect({parent_clip.x, parent_clip.y}, {parent_clip.z, parent_clip.w}, false);
    const PaintContext paint_context{resolved_child_rect, content_rect(resolved_child_rect), paint, current_style};
    paint_surface(paint_context);
    ImGui::PopClipRect();

    // visible overflow escapes this box but stays inside ancestor effect clips.
    state.push_effect_clip(
        current_style.overflow() == Overflow::Visible ? parent_effect_clip
                                                      : intersect_clip(parent_effect_clip, resolved_child_rect)
    );

    m_content_clip_pushed = !scrollable || current_style.border() != BORDER_NONE;
    // clip bordered descendants to the frame unless this frame is transformed.
    if (m_content_clip_pushed) {
        const ImVec2 scale = current_style.scale();
        const bool transformed = current_style.rotation() != 0.0F || scale.x != 1.0F || scale.y != 1.0F;
        if (current_style.overflow() == Overflow::Visible && (current_style.border() == BORDER_NONE || transformed)) {
            ImGui::PushClipRect({parent_clip.x, parent_clip.y}, {parent_clip.z, parent_clip.w}, false);
        } else {
            const ImVec4 clip = content_clip(current_style, resolved_child_rect);
            ImGui::PushClipRect({clip.x, clip.y}, {clip.z, clip.w}, true);
        }
    }

    ImGui::PopStyleVar();
    // own content follows the node's overflow even when bordered descendants stay clipped to the frame.
    if (current_style.overflow() == Overflow::Visible && !scrollable) {
        ImGui::PushClipRect({parent_clip.x, parent_clip.y}, {parent_clip.z, parent_clip.w}, false);
        paint_content(paint_context);
        ImGui::PopClipRect();
    } else {
        paint_content(paint_context);
    }
    return true;
}

void Container::paint_surface(const PaintContext& context) const {
    context.painter.frame_surface(context.rect, context.style);
}

void Container::on_draw_end() {
    if (!m_child_window_open) {
        return;
    }

    if (m_content_clip_pushed) {
        ImGui::PopClipRect();
        m_content_clip_pushed = false;
    }
    ImGui::EndChild();
    m_child_window_open = false;
    paint_state().pop_effect_clip();
}
