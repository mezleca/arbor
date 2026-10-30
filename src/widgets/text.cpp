#include <imgui-ui/widgets/text.hpp>
#include <imgui-ui/imgui/painter.hpp>

#include <imgui.h>
#include <algorithm>
#include <utility>

using namespace ui;

TextWidget::TextWidget(std::string text) : StyledNode({}, "Text"), m_text(std::move(text)) {
    set_size({fit(), fit()});
}

void TextWidget::apply_theme_defaults(const Theme& theme) {
    configure_all_styles([&theme](Style& style) { style.color(theme.text_color); });
}

TextWidget& TextWidget::set_wrap(float width) {
    if (m_text.wrap_width() != width) {
        m_text.set_wrap(width);
        invalidate_measure();
    }

    return *this;
}

TextWidget& TextWidget::set_overflow(TextOverflow overflow) {
    m_overflow = overflow;
    return *this;
}

bool TextWidget::empty() const {
    return m_text.str().empty();
}

TextWidget& TextWidget::set_text(std::string text) {
    if (m_text.set(std::move(text))) {
        invalidate_measure();
    }

    return *this;
}

void TextWidget::on_measure() {
    m_text.set_font(font(), font_size());
    m_text.set_line_height(computed_style().line_height());
    const ImVec2 text_size = m_text.text_size();
    set_measured_content_size(text_size);
}

bool TextWidget::paint() {
    const ComputedStyle& current_style = computed_style();
    const ImVec2 minimum = ImGui::GetCursorScreenPos();
    const Rect outer = Rect::from_position_size(minimum, layout().size());
    const Rect content = content_rect(outer);

    const Painter paint = painter();
    paint.frame(outer, current_style);

    ImGui::Dummy(layout().size());
    const ImVec4 clip_rect = {content.min.x, content.min.y, content.max.x, content.max.y};
    const bool clipped = current_style.overflow() != Overflow::Visible;

    if (m_overflow == TextOverflow::Ellipsis) {
        // ancestor overflow narrows the visible width without changing this node's content rect.
        paint.text_ellipsis(content.min, current_style.color().value, m_text, paint.visible_clip(clip_rect));
    } else {
        paint.text(content.min, current_style.color().value, m_text, clipped ? &clip_rect : nullptr);
    }

    return true;
}
