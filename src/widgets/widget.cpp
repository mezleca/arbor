#include <imgui-ui/widgets/widget.hpp>

using namespace ui;

Widget::Widget(std::string id, std::string_view type_name, InputMode input_mode)
    : Widget(std::move(id), StackDirection::Vertical, type_name, input_mode) {}

Widget::Widget(std::string id, StackDirection direction, std::string_view type_name, InputMode input_mode)
    : Container(std::move(id), direction, type_name) {
    set_input_mode(input_mode);
}

void Widget::set_label_layout(LabelPlacement placement, ImVec2 spacing) {
    const bool inline_label = placement == LabelPlacement::Inline;
    set_direction(inline_label ? StackDirection::Horizontal : StackDirection::Vertical);
    set_spacing(inline_label ? spacing.x : spacing.y);
}

Widget& Widget::on_event(InputCallback callback) {
    m_on_event = std::move(callback);

    if (m_on_event && !has_input_mode()) {
        set_input_mode(InputMode::Target);
    }

    return *this;
}

Widget& Widget::on_event(EventType type, InputCallback callback) {
    const auto index = static_cast<std::size_t>(type);
    if (type > EventType::Cancel) return *this;

    if (!m_event_callbacks) {
        if (!callback) return *this;

        m_event_callbacks = std::make_unique<EventCallbacks>();
    }

    if (callback && ui::contains(EventMask::Pointer, event_mask(type)) && !has_input_mode()) {
        set_input_mode(InputMode::Target);
    }

    (*m_event_callbacks)[index] = std::move(callback);
    return *this;
}

Widget& Widget::on_change(std::function<void()> callback) {
    m_on_change = std::move(callback);
    return *this;
}

void Widget::notify_change() {
    if (m_on_change) m_on_change();
}

void Widget::dispatch_event(UiEvent& event) {
    Node::dispatch_event(event); // NOLINT(bugprone-parent-virtual-call)
    if (removal_pending() || event.propagation_stopped) return;

    // internal overrides run first, followed by the general callback, the specific callback, and the click hook.
    // removal or stopped propagation ends this sequence before the next handler.
    if (m_on_event) {
        m_on_event(event);
        if (removal_pending() || event.propagation_stopped) return;
    }

    const auto index = static_cast<std::size_t>(event.type);
    if (m_event_callbacks && index < m_event_callbacks->size() && (*m_event_callbacks)[index]) {
        (*m_event_callbacks)[index](event);
        if (removal_pending() || event.propagation_stopped) return;
    }

    if (event.type == EventType::Click) click_event(event);
    scroll_event(event);
}
