#pragma once

#include "../layout/geometry.hpp"
#include "../style/computed-style.hpp"

#include <array>
#include <cstdint>

namespace ui {
    enum class BorderPathSegmentType : uint8_t {
        Line,
        Arc,
    };

    struct BorderPathSegment {
        BorderPathSegmentType type = BorderPathSegmentType::Line;
        ImVec2 start;
        ImVec2 end;
        ImVec2 center;
        float start_angle = 0.0F;
        float end_angle = 0.0F;
        float length = 0.0F; // arc length. dash and dot placement uses this parameterization.
        uint8_t sides = BORDER_NONE;
    };

    using BorderPath = std::array<BorderPathSegment, 12>;

    BorderPath rounded_rect_border_path(Rect rect, float rounding);
    void draw_border_path(
        ImDrawList& draw_list, const BorderPath& path, uint8_t border, const Color& color, float thickness, BorderType style
    );
} // namespace ui
