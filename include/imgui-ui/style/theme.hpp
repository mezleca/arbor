#pragma once

#include "color.hpp"

namespace ui {
    struct Theme {
        struct Scrollbar {
            float size = 14.0F;
            float rounding = 9.0F;
            float minimum_grab_size = 12.0F;
            float grab_rounding = 9.0F;

            Color background_color = rgba(0.02F, 0.025F, 0.04F, 0.53F);
            Color grab_color = rgb(0.35F, 0.42F, 0.58F);
            Color grab_hovered_color = rgb(0.42F, 0.70F, 1.0F);
            Color grab_active_color = rgb(0.26F, 0.59F, 0.98F);
        } scrollbar;

        struct Controls {
            float font_size = 18.0F;
            float rounding = 4.0F;
            float border_thickness = 1.0F;
            float thumb_size = 12.0F;

            Color background_color = rgb(0.13F, 0.20F, 0.32F);
            Color hover_color = rgb(0.22F, 0.39F, 0.62F);
            Color active_color = rgba(0.26F, 0.59F, 0.98F, 0.67F);
            Color border_color = rgb(0.35F, 0.42F, 0.58F);
            Color mark_color = rgb(0.26F, 0.59F, 0.98F);
        } controls;

        struct Metrics {
            float window_rounding = 0.0F;
            float child_rounding = 0.0F;
            float popup_rounding = 6.0F;
            float tab_rounding = 4.0F;
            float frame_border_size = 0.0F;

            ImVec2 window_padding = {12.0F, 12.0F};
            ImVec2 cell_padding;
            ImVec2 frame_padding = {12.0F, 8.0F};
            ImVec2 item_spacing = {10.0F, 10.0F};
            ImVec2 item_inner_spacing = {8.0F, 6.0F};

            float circle_tessellation_max_error = 0.10F;
            // imgui 1.92 only, 1.93 always uses textures for anti-aliased lines.
            bool anti_aliased_lines_use_tex = false;
        } metrics;

        Color accent_color = rgb(0.26F, 0.59F, 0.98F);
        Color accent_hover_color = rgb(0.42F, 0.70F, 1.0F);
        Color background_color = rgba(0.06F, 0.065F, 0.085F, 0.94F);
        Color background_secondary_color = rgb(0.10F, 0.11F, 0.14F);
        Color background_tertiary_color = rgb(0.045F, 0.05F, 0.07F);
        Color header_background_color = rgb(0.13F, 0.17F, 0.24F);
        Color text_color = rgb(1.0F, 1.0F, 1.0F);
        Color text_secondary_color = rgb(0.62F, 0.67F, 0.78F);
        Color border_color = rgba(0.34F, 0.38F, 0.48F, 0.65F);
        Color header_border_color = rgba(0.35F, 0.42F, 0.58F, 0.35F);
        Color button_active_color = rgba(0.26F, 0.59F, 0.98F, 0.35F);
        Color transparent = rgba(0.0F, 0.0F, 0.0F, 0.0F);
    };

} // namespace ui
