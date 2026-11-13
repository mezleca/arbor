#pragma once

#include "color.hpp"

#include <array>

namespace ui {
    inline constexpr std::size_t MAX_GRADIENT_STOPS = 8;

    // fixed size snapshot for draw commands.
    // color owns immutable shared instances.
    struct GradientData {
        GradientType type = GradientType::Linear;
        ImVec2 start;
        ImVec2 end{1.0F, 0.0F};
        std::array<GradientStop, MAX_GRADIENT_STOPS> stops{};
        uint8_t count = 0;
    };
} // namespace ui
