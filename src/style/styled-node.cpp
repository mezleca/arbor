#include <imgui-ui/style/styled-node.hpp>

#include <imgui-ui/style/paint-slot.hpp>
#include <imgui-ui/style/theme.hpp>
#include <imgui-ui/imgui/painter.hpp>
#include <imgui-ui/surface.hpp>

#include <imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

using namespace ui;

void StyledNode::style_changed(void* owner, bool font_changed) {
    auto* node = static_cast<StyledNode*>(owner);
    node->apply_style_size();

    // font changes invalidate inherited metrics throughout the subtree. other style changes remeasure this node.
    if (font_changed) {
        invalidate_font_cache_subtree(*node);
    } else {
        node->invalidate_measure();
    }
}

StyledNode::StyledNode(std::string id, std::string_view type_name) : Node(std::move(id)), m_type_name(type_name) {
    m_state.set_change_callback(this, &StyledNode::style_changed);
}

void StyledNode::request_size(LayoutSize size) {
    m_has_size_target = false;
    m_state.set_size(size);
}

void StyledNode::apply_style_size() {
    const ComputedStyle& style = computed_style();
    if (m_has_size_target && m_size_target == style.size()) return;

    m_size_target = style.size();
    m_has_size_target = true;
    m_size_goal = Vec2Value{resolve_outer_size(m_size_target), style.size_transition()};

    if (m_size_goal.duration <= 0.0F) {
        apply_size_spec(m_size_target);
        return;
    }

    // restart from the current outer extent so retargeting a running transition stays continuous.
    m_animated_size.set(layout().size());
    m_animated_size.tick(m_size_goal, 0.0F);
}

ImVec2 StyledNode::resolve_outer_size(LayoutSize spec) const {
    const ImVec2 measured = layout().measured_size();
    const ImVec2 available = layout().available_size();
    const BoxInsets insets = layout().box_insets();
    const BoxSizing sizing = layout().box_sizing();
    return {
        spec.width.resolve(measured.x, available.x, insets.horizontal(), sizing),
        spec.height.resolve(measured.y, available.y, insets.vertical(), sizing),
    };
}

void StyledNode::advance_size_transition(float dt) {
    if (!m_animated_size.is_transitioning()) return;

    m_animated_size.tick(m_size_goal, dt);
    if (!m_animated_size.is_transitioning()) {
        apply_size_spec(m_size_target);
        invalidate_measure();
        return;
    }

    apply_size_spec({px(m_animated_size.value.x), px(m_animated_size.value.y)});
    invalidate_measure();
}

void StyledNode::surface_changed() {
    m_font_cache_valid = false;
}

StyledNode& StyledNode::set_font(ImFont* font, float size) {
    configure_all_styles([font, size](Style& style) { style.font(font).font_size(size); });
    return *this;
}

ImFont* StyledNode::font() const {
    const ComputedStyle& current_style = computed_style();
    if (current_style.font() != nullptr) return current_style.font();
    if (m_font_cache_valid) return m_cached_font;

    // cache the nearest ancestor's explicit font until attachment or font changes invalidate the subtree.
    // the imgui fallback is not cached.
    for (const Node* ancestor = parent(); ancestor != nullptr; ancestor = ancestor->parent()) {
        const auto* styled_ancestor = dynamic_cast<const StyledNode*>(ancestor);
        if (styled_ancestor == nullptr) continue;

        const ComputedStyle& ancestor_style = styled_ancestor->computed_style();
        if (ancestor_style.font() != nullptr) {
            m_cached_font = ancestor_style.font();
            m_font_cache_valid = true;
            return m_cached_font;
        }
    }

    return ImGui::GetFont();
}

float StyledNode::font_size() const {
    if (computed_style().font_size() > 0.0F) return computed_style().font_size();

    for (const Node* ancestor = parent(); ancestor != nullptr; ancestor = ancestor->parent()) {
        const auto* styled = dynamic_cast<const StyledNode*>(ancestor);
        if (styled != nullptr && styled->computed_style().font_size() > 0.0F) return styled->computed_style().font_size();
    }

    return font()->LegacySize;
}

BoxInsets StyledNode::box_insets() const {
    const ComputedStyle& style = computed_style();
    const BoxInsets padding = style.padding();
    const BoxInsets border = style.border_widths();
    return {
        padding.left + border.left,
        padding.top + border.top,
        padding.right + border.right,
        padding.bottom + border.bottom,
    };
}

void StyledNode::set_measured_content_size(ImVec2 size) {
    ImGui::PushFont(font(), font_size());
    const float line_height = ImGui::GetTextLineHeight();
    ImGui::PopFont();

    size.y = std::max(size.y, line_height * computed_style().line_height());
    set_measured_size(outer_size(size));
}

void StyledNode::invalidate_font_cache_subtree(Node& node) {
    auto* styled = dynamic_cast<StyledNode*>(&node);
    if (styled != nullptr) styled->m_font_cache_valid = false;

    node.invalidate_measure();
    for (const auto& child : node.children()) {
        if (child->removal_pending()) continue;

        invalidate_font_cache_subtree(*child);
    }
}

Painter StyledNode::painter() const {
    return Painter(*ImGui::GetWindowDrawList(), &paint_state(), effect_registry());
}

void StyledNode::draw_surface(Rect rect, const std::optional<Color>& background) const {
    painter().frame(rect, computed_style(), background);
}

PaintSlot& StyledNode::before() {
    return ensure_slot(m_before, this);
}

PaintSlot& StyledNode::after() {
    return ensure_slot(m_after, this);
}

PaintSlot& StyledNode::ensure_slot(std::unique_ptr<PaintSlot>& slot, StyledNode* owner) {
    if (slot == nullptr) {
        slot = std::make_unique<PaintSlot>(owner, &StyledNode::style_changed);
    }

    return *slot;
}

void StyledNode::remove_before() {
    m_before.reset();
}

void StyledNode::remove_after() {
    m_after.reset();
}

void StyledNode::draw() {
    if (!visible() || removal_pending() || !m_state.is_visible()) {
        return;
    }

    const bool is_disabled = !enabled();
    if (!is_disabled) update_cursor();
    const ComputedStyle& current_style = computed_style();
    const PushState push_state = current_style.push(opacity(), font(), font_size());

    const ImVec2 scale = current_style.scale();
    const float rotation_deg = current_style.rotation();

    if (rotation_deg == 0.0F && scale.x == 1.0F && scale.y == 1.0F) {
        Node::draw();
        if (is_disabled) update_cursor();
        ComputedStyle::pop(push_state);
        return;
    }

    struct DrawListSnapshot {
        ImDrawList* draw_list = nullptr;
        int vertex_count = 0;
        ImGuiWindow* window = nullptr;
    };

    ImGuiContext& context = *ImGui::GetCurrentContext();
    const int initial_window_count = context.Windows.Size;
    ImDrawList* background_draw_list = ImGui::GetBackgroundDrawList();
    ImDrawList* foreground_draw_list = ImGui::GetForegroundDrawList();
    std::vector<DrawListSnapshot> draw_list_snapshots;
    draw_list_snapshots.reserve(context.Windows.Size + 2);

    // active windows already contain this frame's vertices. opening an inactive child resets its buffer, so start at zero.
    for (ImGuiWindow* window : context.Windows) {
        const int first_vertex = window->LastFrameActive == context.FrameCount ? window->DrawList->VtxBuffer.Size : 0;
        draw_list_snapshots.push_back({window->DrawList, first_vertex, window});
    }
    draw_list_snapshots.push_back({background_draw_list, background_draw_list->VtxBuffer.Size});
    draw_list_snapshots.push_back({foreground_draw_list, foreground_draw_list->VtxBuffer.Size});

    Node::draw();
    if (is_disabled) update_cursor();

    const Rect rect = layout().visual_rect();
    const ImVec2 center = {(rect.min.x + rect.max.x) * 0.5F, (rect.min.y + rect.max.y) * 0.5F};
    const float rotation = rotation_deg * std::numbers::pi_v<float> / 180.0F;
    const float sine = std::sin(rotation);
    const float cosine = std::cos(rotation);

    // apply the transform after node drawing because children may append vertices to other draw lists.
    const auto transform_draw_list = [&](ImDrawList& draw_list, int start) {
        for (int index = start; index < draw_list.VtxBuffer.Size; ++index) {
            ImDrawVert& vertex = draw_list.VtxBuffer[index];
            const ImVec2 offset = {(vertex.pos.x - center.x) * scale.x, (vertex.pos.y - center.y) * scale.y};
            vertex.pos = {
                center.x + (offset.x * cosine) - (offset.y * sine),
                center.y + (offset.x * sine) + (offset.y * cosine),
            };
        }
    };

    for (const DrawListSnapshot& snapshot : draw_list_snapshots) {
        if (snapshot.window != nullptr && snapshot.window->LastFrameActive != context.FrameCount) continue;
        transform_draw_list(*snapshot.draw_list, snapshot.vertex_count);
    }

    for (int index = initial_window_count; index < context.Windows.Size; ++index) {
        transform_draw_list(*context.Windows[index]->DrawList, 0);
    }

    ComputedStyle::pop(push_state);
}

void StyledNode::advance_frame_state(float dt) {
    m_state.update(dt);
    advance_size_transition(dt);
}

void StyledNode::input_state_changed() {
    const InputState input = subtree_input_state();
    set_interaction_style(input.hovered, input.active, input.focused);
    if (enabled()) update_cursor();
}

void StyledNode::disabled_changed() {
    if (!enabled()) {
        m_state.set_style(StyleType::DISABLED);
        return;
    }

    const InputState input = subtree_input_state();
    m_state.set_item_state(input.hovered, input.active, input.focused);
}

void StyledNode::set_open_style(bool open) {
    if (open) {
        set_visual_style(StyleType::ACTIVE);
        return;
    }

    set_interaction_style(input_state().hovered, input_state().active, input_state().focused);
}

void StyledNode::update_cursor() {
    if (ImGui::GetCurrentContext() == nullptr) return;

    if (!enabled()) {
        const Rect rect = layout().visual_rect();
        if (!ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) || !ImGui::IsMouseHoveringRect(rect.min, rect.max)) {
            return;
        }
    } else if (!input_state().hovered) {
        return;
    }

    for (const Node* node = this; node != nullptr; node = node->parent()) {
        const auto* styled = dynamic_cast<const StyledNode*>(node);
        if (styled == nullptr) continue;

        const ImGuiMouseCursor cursor = styled->style(styled->style_type()).cursor();
        if (cursor != ImGuiMouseCursor_None) {
            ImGui::SetMouseCursor(cursor);
            return;
        }
    }
    ImGui::SetMouseCursor(ImGuiMouseCursor_Arrow);
}

StyledNode& StyledNode::set_tooltip(std::string text) {
    m_tooltip_text = std::move(text);
    m_tooltip_callback = {};

    if (!has_input_mode()) {
        set_input_mode(InputMode::Target);
    }

    return *this;
}

StyledNode& StyledNode::set_tooltip(std::function<void()> callback) {
    m_tooltip_text.clear();
    m_tooltip_callback = std::move(callback);

    if (!has_input_mode()) {
        set_input_mode(InputMode::Target);
    }

    return *this;
}

void StyledNode::draw_before() {
    if (m_before != nullptr) draw_slot(*m_before, *ImGui::GetWindowDrawList());
}

void StyledNode::draw_after() {
    if (m_after != nullptr) draw_slot(*m_after, *ImGui::GetForegroundDrawList());
    if (!input_state().hovered) return;
    if (!m_tooltip_callback && m_tooltip_text.empty()) return;

    const Theme& theme = surface().theme();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, theme.metrics.window_padding);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, theme.metrics.popup_rounding);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, theme.controls.border_thickness);
    ImGui::PushStyleColor(ImGuiCol_PopupBg, theme.controls.background_color.rgba());
    ImGui::PushStyleColor(ImGuiCol_Border, theme.controls.border_color.rgba());

    if (m_tooltip_callback) {
        if (ImGui::BeginTooltip()) {
            m_tooltip_callback();
            ImGui::EndTooltip();
        }
    } else {
        ImGui::SetTooltip("%s", m_tooltip_text.c_str());
    }

    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(3);
}

void StyledNode::draw_slot(PaintSlot& slot, ImDrawList& draw_list) {
    const Rect rect = layout().visual_rect();
    const Painter painter(draw_list, &paint_state(), effect_registry(), slot.m_opacity * slot.m_style.alpha());
    slot.paint(painter, rect, content_rect(rect));
}
