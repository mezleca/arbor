#pragma once

#include "container.hpp"

namespace ui {
    class ResizableContainer : public Container {
    public:
        explicit ResizableContainer(std::string id);

        ResizableContainer& set_resize(ResizeAxes resize);

        bool resizing() const {
            return m_resizing != ResizeAxes::None;
        }

        bool resize_handle_contains(ImVec2 position) const {
            return m_resize != ResizeAxes::None && resize_handle().contains(position);
        }

    protected:
        Rect hit_rect(Rect visual_rect) const override;
        void on_draw_end() override;
        void event(UiEvent& event) override;
        void mouse_press_event(UiEvent& event) override;
        void mouse_release_event(UiEvent& event) override;
        void mouse_move_event(UiEvent& event) override;

    private:
        void draw_resize_indicator();
        ImGuiMouseCursor resize_cursor() const;
        Rect resize_handle() const;

        ImVec2 m_drag_start = {0.0f, 0.0f};
        ImVec2 m_previous_size = {0.0f, 0.0f};
        ImVec2 m_parent_content_max;
        ResizeAxes m_resize = ResizeAxes::None;
        ResizeAxes m_resizing = ResizeAxes::None;
    };
} // namespace ui
