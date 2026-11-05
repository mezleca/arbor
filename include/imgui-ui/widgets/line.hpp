#pragma once

#include "../style/styled-node.hpp"

#include <imgui.h>

namespace ui {
    class LineWidget : public StyledNode {
    public:
        LineWidget(ImVec2 start, ImVec2 end, const Color& color, float thickness = 1.0f);
        /// endpoints use screen coordinates, use a sized BoxWidget for a layout separator
        LineWidget& set_points(ImVec2 start, ImVec2 end);

    private:
        bool paint() override;
        ImVec2 m_start;
        ImVec2 m_end;
    };

} // namespace ui
