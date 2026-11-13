#pragma once

#include "../backend.hpp"
#include "../opengl/gpu-timer.hpp"

namespace ui {
    class Surface;

    class RaylibBackend final : public Backend {
    public:
        /// begin_frame starts raylib drawing and render ends it. do not wrap the surface frame in another drawing cycle.
        /// close the window after surface destruction.
        RaylibBackend() = default;

        bool initialize() override;
        void process_events(Surface& surface) override;
        void register_effects(EffectRegistry& effects) override;
        bool initialize_imgui() override;
        void shutdown_imgui() override;
        void begin_frame(ui::Color clear_color) override;
        void set_mouse_cursor(ImGuiMouseCursor cursor) override;
        std::optional<double> render(ImDrawData* draw_data, bool profile_gpu = false) override;
        float content_scale() const override;
        uint64_t window_id() const override;
        ImVec2 display_size() const override;

    private:
        void apply_mouse_cursor() const;

        bool m_imgui_initialized = false;
        ImGuiMouseCursor m_mouse_cursor = ImGuiMouseCursor_Arrow;
        ImVec2 m_pointer_position;
        bool m_has_pointer_position = false;
        OpenGLGpuTimer m_gpu_timer;
    };
} // namespace ui
