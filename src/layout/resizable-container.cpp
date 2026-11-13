#include <arbor/layout/resizable-container.hpp>

#include <arbor/imgui/painter.hpp>

#include <algorithm>

static constexpr float MIN_CHILD_SIZE = 32.0F;
static constexpr float CHILD_RESIZE_HANDLE_SIZE = 10.0F;
static constexpr float CHILD_RESIZE_HANDLE_INSET = 1.0F;

using namespace ui;

ResizableContainer::ResizableContainer(std::string id) : Container(std::move(id), "ResizableContainer") {}

ResizableContainer& ResizableContainer::set_resize(ResizeAxes resize) {
    if (m_resize == resize) {
        return *this;
    }

    m_resize = resize;
    set_input_mode(resize == ResizeAxes::None ? InputMode::None : InputMode::Target);
    return *this;
}

ImGuiMouseCursor ResizableContainer::resize_cursor() const {
    if (m_resize == ResizeAxes::X) return ImGuiMouseCursor_ResizeEW;
    if (m_resize == ResizeAxes::Y) return ImGuiMouseCursor_ResizeNS;
    if (m_resize == ResizeAxes::Both) return ImGuiMouseCursor_ResizeNWSE;
    return ImGuiMouseCursor_Arrow;
}

void ResizableContainer::on_draw_end() {
    draw_resize_indicator();
    Container::on_draw_end();

    if (resizing()) {
        ImGui::SetMouseCursor(resize_cursor());
    }

    const ImVec2 cursor = ImGui::GetCursorScreenPos();
    const ImVec2 available = ImGui::GetContentRegionAvail();
    m_parent_content_max = {cursor.x + available.x, cursor.y + available.y};
}

Rect ResizableContainer::hit_rect(Rect visual_rect) const {
    if (m_resize == ResizeAxes::None) {
        return visual_rect;
    }

    return resize_handle();
}

Rect ResizableContainer::resize_handle() const {
    const ImVec2 max = layout().visual_rect().max;
    const float scale = dpi_scale();
    return Rect::from_position_size(
        {max.x - ((CHILD_RESIZE_HANDLE_SIZE + CHILD_RESIZE_HANDLE_INSET) * scale),
         max.y - ((CHILD_RESIZE_HANDLE_SIZE + CHILD_RESIZE_HANDLE_INSET) * scale)},
        {CHILD_RESIZE_HANDLE_SIZE * scale, CHILD_RESIZE_HANDLE_SIZE * scale}
    );
}

void ResizableContainer::event(UiEvent& event) {
    if (event.type == EventType::Cancel) m_resizing = ResizeAxes::None;
}

void ResizableContainer::mouse_release_event(UiEvent& event) {
    if (event.button != PointerButton::Left || !resizing()) return;

    m_resizing = ResizeAxes::None;
    release_pointer();
    event.block_native_input();
    event.stop_propagation();
}

void ResizableContainer::mouse_press_event(UiEvent& event) {
    if (m_resize == ResizeAxes::None || event.button != PointerButton::Left || !resize_handle().contains(event.position)) return;

    // capture the drag before recording its origin. later motion uses this pointer position and size.
    if (!capture_pointer()) return;

    m_drag_start = event.position;
    m_previous_size = layout().size();
    m_resizing = m_resize;
    event.prevent_default();
    event.block_native_input();
    event.stop_propagation();
}

void ResizableContainer::mouse_move_event(UiEvent& event) {
    if (!resizing()) return;

    const ImVec2 child_min = layout().visual_rect().min;
    const float minimum = MIN_CHILD_SIZE * dpi_scale();
    const ImVec2 max_size = {
        std::max(minimum, m_parent_content_max.x - child_min.x),
        std::max(minimum, m_parent_content_max.y - child_min.y),
    };

    LayoutSize updated = layout().size_spec();
    const ImVec2 insets = layout().box_sizing() == BoxSizing::ContentBox
                              ? ImVec2{layout().box_insets().horizontal(), layout().box_insets().vertical()}
                              : ImVec2{};
    const auto resize_axis = [&](bool horizontal, LayoutAxis& axis, float previous) {
        const float position = horizontal ? event.position.x : event.position.y;
        const float start = horizontal ? m_drag_start.x : m_drag_start.y;
        const float maximum = horizontal ? max_size.x : max_size.y;
        const float size = std::clamp(previous + position - start, minimum, maximum);
        axis.mode = LayoutSizeMode::Fixed;
        axis.value = std::max(0.0f, size - (horizontal ? insets.x : insets.y)) / dpi_scale();
    };

    if ((m_resizing & ResizeAxes::X) != ResizeAxes::None) resize_axis(true, updated.width, m_previous_size.x);
    if ((m_resizing & ResizeAxes::Y) != ResizeAxes::None) resize_axis(false, updated.height, m_previous_size.y);

    set_size(updated);
    event.block_native_input();
    event.stop_propagation();
}

void ResizableContainer::draw_resize_indicator() {
    if (m_resize == ResizeAxes::None) {
        return;
    }

    const ComputedStyle& current_style = computed_style();
    const float border_thickness = current_style.border_thickness();
    const Painter paint = painter();
    const ImVec2 max = paint.to_logical(resize_handle().max);

    for (int i = 0; i < 3; ++i) {
        const float distance = 3.0f + (static_cast<float>(i) * 4.0f);
        paint.line(
            {max.x - distance - 1.0f, max.y}, {max.x, max.y - distance}, current_style.border_color().value, border_thickness
        );
        paint.line(
            {max.x - distance + border_thickness + 0.5f, max.y}, {max.x, max.y - distance + border_thickness + 0.5f},
            current_style.background_color().value, border_thickness
        );
    }
}
