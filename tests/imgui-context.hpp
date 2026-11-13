#pragma once

#include <arbor/backends/backend.hpp>
#include <arbor/imgui/context-scope.hpp>
#include <arbor/layout/geometry.hpp>
#include <arbor/runtime.hpp>
#include <arbor/tree/node.hpp>
#include <arbor/surface.hpp>

#include <imgui.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <utility>

namespace ui_test {
    class TestBackend final : public ui::Backend {
    public:
        explicit TestBackend(float content_scale = 1.0f) : m_content_scale(content_scale) {}

        bool initialize() override {
            return true;
        }

        void process_events(ui::Surface&) override {}

        bool initialize_imgui() override {
            return true;
        }

        void shutdown_imgui() override {}
        void begin_frame(ui::Color) override {}
        void set_mouse_cursor(ImGuiMouseCursor) override {}
        std::optional<double> render(ImDrawData*, bool) override {
            return std::nullopt;
        }

        float content_scale() const override {
            return m_content_scale;
        }

        uint64_t window_id() const override {
            return 1;
        }

        ImVec2 display_size() const override {
            return {};
        }

    private:
        float m_content_scale;
    };

    inline std::unique_ptr<ui::Backend> make_backend() {
        return std::make_unique<TestBackend>();
    }

    inline ui::Surface make_surface(ui::Runtime& runtime, bool enable_debugger = false) {
        return ui::Surface(runtime, {.backend = make_backend(), .enable_debugger = enable_debugger});
    }

    class ImGuiContext {
    public:
        explicit ImGuiContext(ImVec2 display_size) : m_previous(ImGui::GetCurrentContext()) {
            m_context = ImGui::CreateContext();
            ImGui::SetCurrentContext(m_context);
            ImGui::GetIO().DisplaySize = display_size;
            ImGui::GetIO().DeltaTime = 1.0F / 60.0F;
            build_fonts();
        }

        ImGuiContext(const ImGuiContext&) = delete;
        ImGuiContext& operator=(const ImGuiContext&) = delete;

        ~ImGuiContext() {
            ImGui::DestroyContext(m_context);
            ImGui::SetCurrentContext(m_previous == m_context ? nullptr : m_previous);
        }

        static void build_fonts() {
            unsigned char* pixels = nullptr;
            int width = 0;
            int height = 0;
            int bytes_per_pixel = 0;
            ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&pixels, &width, &height, &bytes_per_pixel);
        }

    private:
        ::ImGuiContext* m_previous = nullptr;
        ::ImGuiContext* m_context = nullptr;
    };

    class SurfaceContext {
    public:
        explicit SurfaceContext(ui::Surface& surface, ImVec2 display_size) : m_scope(surface.imgui_context()) {
            ImGui::GetIO().DisplaySize = display_size;
            ImGuiContext::build_fonts();
        }

        SurfaceContext(const SurfaceContext&) = delete;
        SurfaceContext& operator=(const SurfaceContext&) = delete;

    private:
        ui::ImGuiContextScope m_scope;
    };

    [[nodiscard]] inline SurfaceContext prepare_surface(ui::Surface& surface) {
        return SurfaceContext(surface, surface.backend().display_size());
    }

    [[nodiscard]] inline SurfaceContext prepare_surface(ui::Surface& surface, ImVec2 display_size) {
        return SurfaceContext(surface, display_size);
    }

    inline void draw_surface(ui::Surface& surface, std::optional<float> delta_time = std::nullopt) {
        if (!delta_time.has_value()) {
            surface.frame();
            return;
        }

        surface.begin_frame();
        surface.update(*delta_time);
        surface.draw();
        surface.end_frame();
    }

    template <typename Draw>
    inline void draw_window(const char* name, Draw&& draw) {
        ImGui::NewFrame();
        ImGui::Begin(name);
        std::forward<Draw>(draw)();
        ImGui::End();
        ImGui::EndFrame();
    }

    template <typename Draw>
    inline void draw_window(const char* name, ImVec2 size, Draw&& draw) {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({0.0F, 0.0F});
        ImGui::SetNextWindowSize(size);
        ImGui::Begin(name);
        std::forward<Draw>(draw)();
        ImGui::End();
        ImGui::EndFrame();
    }

    inline void draw_node(ui::Node& node, const char* name) {
        draw_window(name, [&node] { node.draw(); });
    }

    inline ImVec2 center(const ui::Rect& rect) {
        return {(rect.min.x + rect.max.x) * 0.5F, (rect.min.y + rect.max.y) * 0.5F};
    }

    inline ui::UiEvent pointer_event(ui::EventType type, ImVec2 position, ui::PointerButton button = ui::PointerButton::Left) {
        ui::UiEvent event = ui::UiEvent::make(type);
        event.position = position;
        event.button = button;
        return event;
    }
} // namespace ui_test
