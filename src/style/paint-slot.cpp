#include <arbor/style/paint-slot.hpp>

#include <utility>

using namespace ui;

PaintContext::PaintContext(Rect rect, Rect content_rect, Painter painter, const ComputedStyle& style)
    : rect(painter.to_logical(rect)), content_rect(painter.to_logical(content_rect)), painter(painter), style(style) {}

PaintSlot::PaintSlot(void* change_owner, Style::ChangeCallback change_callback) {
    m_style.set_change_callback(change_owner, change_callback);
}

PaintSlot& PaintSlot::set_draw_callback(DrawCallback callback) {
    m_draw_callback = std::move(callback);
    return *this;
}

void PaintSlot::paint(Painter painter, Rect rect, Rect content_rect) {
    if (!rect.valid()) {
        return;
    }

    const PaintContext context{rect, content_rect, painter, m_style};

    // callbacks can observe layout during transparent frames. the painter applies opacity to their drawing operations.
    if (m_draw_callback) {
        m_draw_callback(context);
        return;
    }

    painter.frame(context.rect, m_style);
}
