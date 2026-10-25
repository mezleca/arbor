#include <imgui-ui/layout/layer-container.hpp>
#include <imgui-ui/imgui/window.hpp>
#include <imgui-ui/imgui/context-scope.hpp>
#include <imgui-ui/constants.hpp>
#include <imgui-ui/surface.hpp>

#include <imgui_internal.h>
#include <utility>

using namespace ui;

LayerContainer::LayerContainer(std::string id, LayerMode mode) : LayerContainer(std::move(id), mode, "LayerContainer") {}

LayerContainer::LayerContainer(std::string id, LayerMode mode, std::string_view type_name)
    : Container(std::move(id), type_name), m_mode(mode) {
    set_layout({.in_flow = false});
    set_input_mode(InputMode::None);
}

void LayerContainer::open_popup() {
    m_popup_requested = true;
    m_popup_opened = false;
}

void LayerContainer::close_popup() {
    m_popup_requested = false;
}

bool LayerContainer::native_popup_open() const {
    if (!m_popup_opened) return false;
    const ImGuiContextScope scope(surface().imgui_context());
    return ImGui::IsPopupOpen(m_popup_id, ImGuiPopupFlags_AnyPopupLevel);
}

void LayerContainer::set_popup_position(ImVec2 position) {
    m_popup_position = position;
}

ImGuiWindowFlags LayerContainer::child_window_flags() const {
    const ImGuiWindowFlags flags = Container::child_window_flags();

    // imgui performs native scrollbar hit testing separately from our input router. an active blocker must accept
    // native mouse hit testing so imgui selects its window instead of hovering the scrollbar underneath.
    // other layers keep NoMouseInputs so their child window does not intercept native input intended for the parent.
    // native hover is resolved after BeginChild, so test tree eligibility without the container's visual hit test here.
    return input_mode() == InputMode::Blocker && Node::accepts_input() // NOLINT(bugprone-parent-virtual-call)
               ? flags
               : flags | ImGuiWindowFlags_NoMouseInputs;
}

ImGuiWindowFlags LayerContainer::window_flags() const {
    if (m_mode == LayerMode::Popup) {
        return (constants::WINDOW_FLAGS & ~ImGuiWindowFlags_NoBringToFrontOnFocus) | ImGuiWindowFlags_NoFocusOnAppearing;
    }

    return constants::WINDOW_FLAGS;
}

void LayerContainer::on_layout() {
    if (has_parent_size()) {
        return;
    }

    if (m_mode == LayerMode::Inline) {
        const Rect parent_content = layout().parent_content_rect();
        if (parent_content.valid()) {
            assign_size(resolve_size(parent_content.size()));
            return;
        }
    }

    assign_size(resolve_size(ImGui::GetMainViewport()->WorkSize));
}

bool LayerContainer::paint() {
    switch (m_mode) {
        case LayerMode::Inline:
            return paint_inline();
        case LayerMode::Window:
            return paint_window();
        case LayerMode::Popup:
            return paint_popup();
    }
    return false;
}

bool LayerContainer::paint_inline() {
    const ImVec2 scroll = {ImGui::GetScrollX(), ImGui::GetScrollY()};
    if (scroll.x != 0.0F || scroll.y != 0.0F) {
        const ImVec2 cursor = ImGui::GetCursorPos();
        ImGui::SetCursorPos({cursor.x + scroll.x, cursor.y + scroll.y});
    }
    return Container::paint();
}

bool LayerContainer::paint_window() {
    ImGuiWindowFlags flags = window_flags();

    // allow the first begin call to raise the window. later frames keep its existing stacking order.
    if (!m_window_initialized) {
        flags &= ~ImGuiWindowFlags_NoBringToFrontOnFocus;
    }

    if (!accepts_input()) {
        flags |= ImGuiWindowFlags_NoInputs;
    }

    ImVec2 position = layout().layout_rect().min;
    if (parent() == nullptr) {
        const ImVec2 viewport_position = ImGui::GetMainViewport()->WorkPos;
        position = {position.x + viewport_position.x, position.y + viewport_position.y};
    }

    prepare_window(position, layout().size(), flags, false);
    ImGui::Begin(id().c_str(), nullptr, flags);
    m_window_initialized = true;
    paint_window_contents();
    return true;
}

bool LayerContainer::paint_popup() {
    ImGui::PushID(this);
    m_popup_id = ImGui::GetID(id().c_str());

    if (m_popup_requested && !m_popup_opened) {
        ImGui::OpenPopup(id().c_str());
        m_popup_opened = true;
    }
    if (!m_popup_opened) {
        ImGui::PopID();
        return false;
    }

    const Rect work_area = viewport_work_area();
    ImVec2 size = layout().size();
    size = {std::min(size.x, work_area.size().x), std::min(size.y, work_area.size().y)};

    const ImVec2 position = clamp_position(work_area, size, m_popup_position);
    const ImGuiWindowFlags flags = window_flags();

    prepare_window(position, size, flags, true);
    const bool draw_content = ImGui::BeginPopup(id().c_str(), flags);

    if (!draw_content || !m_popup_requested) {
        if (draw_content) ImGui::CloseCurrentPopup();
        finish_popup(draw_content);
        m_popup_opened = false;
        m_popup_requested = false;
        return false;
    }

    paint_window_contents();
    return true;
}

void LayerContainer::prepare_window(ImVec2 position, ImVec2 size, ImGuiWindowFlags flags, bool popup) {
    // set movable windows' position and resizable windows' size only on first use.
    ImGui::SetNextWindowPos(position, (flags & ImGuiWindowFlags_NoMove) ? ImGuiCond_Always : ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(size, (flags & ImGuiWindowFlags_NoResize) ? ImGuiCond_Always : ImGuiCond_FirstUseEver);

    // native window resizing bypasses container allocation. give imgui the same resolved limits before Begin
    // so user dragging and saved window sizes cannot escape the node's configured range.
    const ImVec2 content =
        layout().parent_content_rect().valid() ? layout().parent_content_rect().size() : viewport_work_area().size();
    const BoxInsets margin = layout_margin();
    const ImVec2 available = {std::max(0.0F, content.x - margin.horizontal()), std::max(0.0F, content.y - margin.vertical())};

    ImGui::SetNextWindowSizeConstraints(
        layout().constrain_size({}, available),
        layout().constrain_size({std::numeric_limits<float>::infinity(), std::numeric_limits<float>::infinity()}, available)
    );
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, box_insets().window_padding());
    ImGui::PushStyleVar(
        popup ? ImGuiStyleVar_PopupRounding : ImGuiStyleVar_WindowRounding, popup ? computed_style().border_radius() : 0.0F
    );
    ImGui::PushStyleVar(popup ? ImGuiStyleVar_PopupBorderSize : ImGuiStyleVar_WindowBorderSize, 0.0F);

    // imgui applies its style minimum after size constraints. use the node's configured minimum instead.
    ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2{});
    ImGui::PushStyleColor(popup ? ImGuiCol_PopupBg : ImGuiCol_WindowBg, ImVec4{});
}

void LayerContainer::paint_window_contents() {
    apply_window_insets(box_insets());

    const Rect window_rect = Rect::from_position_size(ImGui::GetWindowPos(), ImGui::GetWindowSize());
    assign_size(window_rect.size());
    set_layout_rect(window_rect);

    draw_surface(window_rect);
}

void LayerContainer::on_draw_end() {
    if (m_mode == LayerMode::Inline) {
        Container::on_draw_end();
        return;
    }

    if (m_mode == LayerMode::Popup) {
        finish_popup(true);
        return;
    }

    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(4);
}

void LayerContainer::finish_popup(bool open) {
    if (open) ImGui::EndPopup();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(4);
    ImGui::PopID();
}
