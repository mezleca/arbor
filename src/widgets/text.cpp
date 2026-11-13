#include <arbor/widgets/text.hpp>
#include <arbor/imgui/painter.hpp>

#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <utility>

using namespace ui;

TextWidget::TextWidget(std::string text) : StyledNode({}, "Text"), m_text(std::move(text)) {
    set_size({fit(), fit()});
}

void TextWidget::apply_theme_defaults(const Theme& theme) {
    configure_all_styles([&theme](Style& style) { style.color(theme.text_color); });
}

TextWidget& TextWidget::set_wrap(float width) {
    m_wrap_available = false;
    if (m_text.wrap_width() != width) {
        m_text.set_wrap(width);
        invalidate_measure();
    }

    return *this;
}

TextWidget& TextWidget::set_wrap_available(bool wrap) {
    m_wrap_available = wrap;
    m_text.set_wrap(-1.0f);
    invalidate_measure();
    return *this;
}

TextWidget& TextWidget::set_alignment(ImVec2 alignment) {
    m_alignment = {std::clamp(alignment.x, 0.0f, 1.0f), std::clamp(alignment.y, 0.0f, 1.0f)};
    return *this;
}

ImVec2 TextWidget::measure_size(ImVec2 allocation) const {
    if (!m_wrap_available) return Node::measure_size(allocation);
    const float width = content_size(allocation).x / dpi_scale();
    m_text.set_wrap(std::isfinite(width) ? std::max(1.0f, width) : -1.0f);
    return outer_size(m_text.text_size());
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
    if (m_wrap_available) m_text.set_wrap(-1.0f);
    m_text.set_font(font(), font_size());
    m_text.set_line_height(computed_style().line_height());
    const ImVec2 text_size = m_text.text_size();
    set_measured_content_size(text_size);
}

bool TextWidget::paint() {
    const ComputedStyle& current_style = computed_style();
    const Painter paint = painter();
    const ImVec2 minimum = ImGui::GetCursorScreenPos();
    const Rect native_outer = Rect::from_position_size(minimum, layout().size());
    const Rect outer = paint.to_logical(native_outer);
    const Rect content = paint.to_logical(content_rect(native_outer));
    const ImVec2 text_size = paint.to_logical(m_text.text_size());
    const ImVec2 position = {
        content.min.x + (std::max(0.0f, content.size().x - text_size.x) * m_alignment.x),
        content.min.y + (std::max(0.0f, content.size().y - text_size.y) * m_alignment.y),
    };

    paint.frame(outer, current_style);

    ImGui::Dummy(layout().size());
    const ImVec4 clip_rect = {content.min.x, content.min.y, content.max.x, content.max.y};
    const bool clipped = current_style.overflow() != Overflow::Visible;

    if (m_overflow == TextOverflow::Ellipsis) {
        paint.text_ellipsis(position, current_style.color().value, m_text, clip_rect);
    } else {
        paint.text(position, current_style.color().value, m_text, clipped ? &clip_rect : nullptr);
    }

    return true;
}
