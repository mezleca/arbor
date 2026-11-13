#pragma once

#include <imgui.h>

namespace constants {
    inline constexpr ImGuiWindowFlags WINDOW_FLAGS = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                                     ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings |
                                                     ImGuiWindowFlags_NoBringToFrontOnFocus;

    inline constexpr ImGuiWindowFlags WIDGET_WINDOW_FLAGS =
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollWithMouse;

    inline constexpr float SCROLL_WHEEL_SCALE = 0.5F;
} // namespace constants
