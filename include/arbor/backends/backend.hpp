#pragma once

#include <imgui.h>
#include "../style/color.hpp"

#include <cstdint>
#include <optional>

namespace ui {
    class EffectRegistry;
    class Surface;

    class Backend {
    public:
        Backend() = default;
        virtual ~Backend() = default;

        Backend(const Backend&) = delete;
        Backend& operator=(const Backend&) = delete;

        virtual bool initialize() = 0;
        virtual void process_events(Surface&) = 0;
        virtual void register_effects(EffectRegistry&) {}
        virtual bool initialize_imgui() = 0;
        virtual void shutdown_imgui() = 0;

        /// starts the platform draw cycle and clears the target before Surface::draw().
        virtual void begin_frame(Color clear_color) = 0;
        virtual void set_mouse_cursor(ImGuiMouseCursor cursor) = 0;
        /// returns the latest completed gpu query from rendering imgui draw data, if available.
        virtual std::optional<double> render(ImDrawData* draw_data, bool profile_gpu = false) = 0;

        virtual float content_scale() const = 0;
        virtual uint64_t window_id() const = 0;
        virtual ImVec2 display_size() const = 0;
    };
} // namespace ui
