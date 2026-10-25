#pragma once

#include "container.hpp"

#include <string>

namespace ui {
    class TreeContainer final : public Container {
    public:
        explicit TreeContainer(std::string label, std::string id = {});

    private:
        ImVec2 measure_content(ImVec2 available) const override;
        bool paint() override;
        void on_draw_end() override;
        ImVec2 child_window_size() const override;
        Rect shadow_rect(Rect child_rect) const override;

        std::string m_label;
        bool m_open = false;
        Rect m_outer_rect{};
        ImVec2 m_body_size;
    };
} // namespace ui
