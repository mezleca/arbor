#pragma once

#include "../backend.hpp"
#include "../opengl/gpu-timer.hpp"
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_video.h>

namespace ui {
    class Surface;

    class SdlBackend final : public Backend {
    public:
        /// the application-owned window and context must outlive this backend and surface destruction.
        SdlBackend(SDL_Window* window, SDL_GLContext context);
        ~SdlBackend() override;

        bool initialize() override;
        void process_events(Surface& surface) override;
        void register_effects(EffectRegistry& effects) override;
        bool initialize_imgui() override;
        void shutdown_imgui() override;
        void begin_frame(Color clear_color) override;
        void set_mouse_cursor(ImGuiMouseCursor cursor) override;
        std::optional<double> render(ImDrawData* draw_data, bool profile_gpu = false) override;
        float content_scale() const override;
        uint64_t window_id() const override;
        ImVec2 display_size() const override;

        /// dispatches translated input before native forwarding. blocked releases still reach imgui.
        bool process_event(Surface& surface, const SDL_Event& event);

    private:
        SDL_Window* m_window = nullptr;
        SDL_GLContext m_context = nullptr;
        SDL_Cursor* m_mouse_cursor = nullptr;
        ImGuiMouseCursor m_mouse_cursor_type = ImGuiMouseCursor_Arrow;
        bool m_imgui_initialized = false;
        OpenGLGpuTimer m_gpu_timer;
    };

} // namespace ui
