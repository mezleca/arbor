#include <arbor/widgets/button.hpp>
#include <arbor/imgui/painter.hpp>
#include <arbor/surface.hpp>
#include <arbor/style/theme.hpp>

#include <algorithm>
#include <utility>

using namespace ui;

ButtonWidget::ButtonWidget(std::string text, LayoutSize size) : Widget({}, "Button"), m_text(std::move(text)) {
    set_size(size);
}

void ButtonWidget::apply_theme_defaults(const Theme& theme) {
    Container::apply_theme_defaults(theme);
    configure_all_styles([&theme](Style& style) {
        style.control(theme).font_size(theme.controls.font_size).cursor(ImGuiMouseCursor_Hand);
    });

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
    if (event.target == this && input_state().focused && (event.key == ImGuiKey_Enter || event.key == ImGuiKey_Space)) {
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
    const ImVec2 size = m_text.text_size();
    const ImVec2 children_size = measure_children(available);
    return {std::max(size.x, children_size.x), std::max(size.y, children_size.y)};
}

void ButtonWidget::paint_content(const PaintContext& context) {
    const Rect content = context.content_rect;
    const ImVec2 text_size = context.painter.to_logical(m_text.text_size());

    const ImVec2 position = {
        content.min.x + (std::max(0.0F, content.size().x - text_size.x) * m_text_alignment.x),
        content.min.y + ((content.size().y - text_size.y) * m_text_alignment.y),
    };
    if (m_text_overflow == TextOverflow::Ellipsis) {
        context.painter.text_ellipsis(
            position, context.style.color().value, m_text, {content.min.x, content.min.y, content.max.x, content.max.y}
        );
    } else {
        context.painter.text(position, context.style.color().value, m_text);
    }
}
