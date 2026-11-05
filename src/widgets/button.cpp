#include <imgui-ui/widgets/button.hpp>
#include <imgui-ui/imgui/painter.hpp>
#include <imgui-ui/surface.hpp>
#include <imgui-ui/style/theme.hpp>

#include <algorithm>
#include <utility>

using namespace ui;

ButtonWidget::ButtonWidget(std::string text, LayoutSize size) : Widget({}, "Button"), m_text(std::move(text)) {
    set_size(size);
}

void ButtonWidget::apply_theme_defaults(const Theme& theme) {
    Container::apply_theme_defaults(theme);
    set_font(surface().get_primary_font(), theme.controls.font_size);
    configure_all_styles([&theme](Style& style) { style.control(theme).cursor(ImGuiMouseCursor_Hand); });

    style(StyleType::ACTIVE).background_color(theme.controls.active_color).border_color(theme.accent_color, 0.2F);
    style(StyleType::FOCUS).border_color(theme.accent_color);
    style(StyleType::HOVER).background_color(theme.controls.hover_color).border_color(theme.accent_hover_color);
    style(StyleType::DISABLED)
        .color(theme.text_secondary_color)
        .background_color(theme.background_secondary_color)
        .border_color(theme.border_color)
        .cursor(ImGuiMouseCursor_NotAllowed);
}

ButtonWidget& ButtonWidget::set_text(std::string text) {
    if (!m_text.set(std::move(text))) {
        return *this;
    }

    invalidate_measure();
    return *this;
}

ButtonWidget& ButtonWidget::set_text_overflow(TextOverflow overflow) {
    m_text_overflow = overflow;
    return *this;
}

void ButtonWidget::click_event(UiEvent&) {
    animate()
        .to(StyleAnimationProperty::BackgroundColor, style(StyleType::ACTIVE).background_color().value)
        .then(0.04F)
        .release(StyleAnimationProperty::BackgroundColor, {0.12F, easing::out_quad});

    if (m_on_click) {
        m_on_click();
    }
}

void ButtonWidget::mouse_press_event(UiEvent& event) {
    if (event.target == this && event.button == PointerButton::Left) surface().input_router().set_focus(this);
}

void ButtonWidget::key_press_event(UiEvent& event) {
    if (event.target == this && input_state().focused && (event.key == Key::Enter || event.key == Key::Space)) {
        event.stop_propagation();
        event.block_native_input();
        click_event(event);
    }
}

void ButtonWidget::on_measure() {
    m_text.set_font(font(), font_size());
    m_text.set_line_height(computed_style().line_height());

    Container::on_measure();
}

ImVec2 ButtonWidget::measure_content(ImVec2 available) const {
    ImVec2 size = m_text.text_size();
    const ImVec2 children_size = measure_children(available);
    size = {std::max(size.x, children_size.x), std::max(size.y, children_size.y)};
    size.y = std::max(size.y, font_size() * computed_style().line_height());
    return size;
}

void ButtonWidget::paint_content(const PaintContext& context) {
    const Rect content = context.content_rect;
    const ImVec2 text_size = m_text.text_size();

    const ImVec2 position = {
        content.min.x + (std::max(0.0F, content.size().x - text_size.x) * m_text_alignment.x),
        content.min.y + ((content.size().y - text_size.y) * m_text_alignment.y),
    };
    if (m_text_overflow == TextOverflow::Ellipsis) {
        // ancestor overflow narrows the visible width without changing this node's content rect.
        context.painter.text_ellipsis(
            position, context.style.color().value, m_text,
            context.painter.visible_clip({content.min.x, content.min.y, content.max.x, content.max.y})
        );
    } else {
        context.painter.text(position, context.style.color().value, m_text);
    }
}
