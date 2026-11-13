#pragma once

#include "style.hpp"
#include "../layout/geometry.hpp"
#include "../imgui/painter.hpp"

#include <functional>

namespace ui {
    // valid during the paint callback. vertex transforms run after the callback returns.
    struct PaintContext {
        /// converts native layout bounds to the logical coordinates accepted by painter.
        PaintContext(Rect rect, Rect content_rect, Painter painter, const ComputedStyle& style);

        // outer bounds in logical coordinates before the node's vertex transform.
        Rect rect;

        // rect inset by the owning node's padding and borders, including for before/after slots.
        Rect content_rect;
        Painter painter;

        // widget style in widget hooks, decoration style in before/after callbacks.
        const ComputedStyle& style;
    };

    class PaintSlot final {
    public:
        using DrawCallback = std::function<void(const PaintContext&)>;

        PaintSlot(void* change_owner, Style::ChangeCallback change_callback);

        Style& style() {
            return m_style;
        }

        const Style& style() const {
            return m_style;
        }

        PaintSlot& set_draw_callback(DrawCallback callback);

    private:
        friend class StyledNode;

        void paint(Painter painter, Rect rect, Rect content_rect);

        Style m_style;
        DrawCallback m_draw_callback;
    };
} // namespace ui
