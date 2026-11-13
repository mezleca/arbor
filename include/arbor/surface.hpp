#pragma once

#include "backends/backend.hpp"
#include "file-dialog/file-dialog.hpp"
#include "style/theme.hpp"
#include "imgui/effects/effects.hpp"
#include "imgui/paint-state.hpp"
#include "input/router.hpp"

#include <imgui.h>
#include <memory>
#include <string_view>

namespace ui {
    class Debugger;
    class Node;
    class Profiler;
    class Runtime;
    class Font;
    class LayerContainer;

    struct SurfaceConfig {
        /// null is rejected during surface construction.
        std::unique_ptr<Backend> backend;
        bool enable_debugger = false;
        std::unique_ptr<FileDialogBackend> file_dialog_backend;
    };

    class Surface {
    public:
        /// keeps a reference to runtime. destroy the surface before runtime and before its backend's platform window.
        explicit Surface(Runtime& runtime, SurfaceConfig config = {});
        ~Surface();

        Surface(const Surface&) = delete;
        Surface& operator=(const Surface&) = delete;

        void exit() {
            m_done = true;
        }

        void process_events();

        /// starts the backend draw cycle and an imgui frame. call after process_events and before update or draw.
        void begin_frame();

        /// renders imgui, completes the backend draw cycle, and restores the previous imgui context.
        void end_frame();

        /// calls begin_frame, update, draw, and end_frame in order.
        void frame();

        void update(float dt);

        void draw();

        /// routes one platform event through the debugger and application tree.
        /// returns true when the event was handled or blocked.
        bool dispatch(UiEvent& event);

        bool is_done() const {
            return m_done;
        }

        /// multiplies the backend's content scale. applies next frame without changing configured sizes or styles.
        void set_scale(float scale);

        float scale() const {
            return m_scale;
        }

        /// returns the registered font, or imgui's current font when it is unavailable.
        ImFont* get_font(std::string_view id) const;

        /// sets the font inherited by widgets that use the primary font.
        void set_primary_font(Font* font);

        /// returns the primary font, falling back to imgui's current font.
        ImFont* get_primary_font() const;

        InputRouter& input_router() {
            return m_input_router;
        }

        FileDialogBackend& file_dialog() {
            return *m_file_dialog;
        }

        Profiler& profiler() {
            return *m_profiler;
        }

        const Profiler& profiler() const {
            return *m_profiler;
        }

        /// returns the debugger when diagnostic support was configured, or nullptr otherwise.
        Debugger* debugger() {
            return m_debugger;
        }

        const Debugger* debugger() const {
            return m_debugger;
        }

        bool debugger_blocks_pointer_input() const;

        EffectRegistry& effects() {
            return m_effects;
        }

        const EffectRegistry& effects() const {
            return m_effects;
        }

        PaintState& paint_state() {
            return m_paint_state;
        }

        const Theme& theme() const {
            return m_theme;
        }

        /// updates imgui metrics and colors, then reapplies theme defaults across the retained tree.
        void set_theme(Theme theme);

        Runtime& runtime() {
            return m_runtime;
        }

        Node& root() {
            return *m_content_root;
        }

        Backend& backend() {
            return *m_backend;
        }

        ImGuiContext* imgui_context() {
            return m_context;
        }

    private:
        static ImFont* resolve_font(Font* font);
        void initialize(bool enable_debugger);
        void configure_style(float scale);
        void apply_theme_colors();
        static std::unique_ptr<FileDialogBackend> make_file_dialog_backend();

        Runtime& m_runtime;
        Theme m_theme;
        ImGuiContext* m_context = nullptr;
        ImGuiContext* m_previous_context = nullptr;
        std::unique_ptr<Backend> m_backend;
        std::unique_ptr<FileDialogBackend> m_file_dialog;
        std::unique_ptr<LayerContainer> m_root;
        Node* m_content_root = nullptr;
        InputRouter m_input_router;
        EffectRegistry m_effects;
        PaintState m_paint_state;
        std::unique_ptr<Profiler> m_profiler;
        Debugger* m_debugger = nullptr;
        Font* m_primary_font = nullptr;
        float m_scale = 1.0f;
        float m_content_scale = 1.0f;
        bool m_done = false;
    };
} // namespace ui
