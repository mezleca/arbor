#pragma once

#include "../layout/geometry.hpp"
#include "../style/computed-style.hpp"

#include <optional>
#include <string_view>

namespace ui {
    class EffectRegistry;
    class TextValue;
    class PaintState;
    enum class TriangleDirection : uint8_t {
        Up,
        Down,
        Left,
        Right,
    };

    class Painter {
    public:
        /// borrows the draw list, state and effects for this paint scope. recreate inside child or popup windows.
        /// drawing methods accept logical coordinates and apply dpi scaling and alpha once.
        explicit Painter(
            ImDrawList& draw_list, PaintState* state = nullptr, EffectRegistry* effects = nullptr, float opacity = 1.0F
        );

        /// native imgui calls need screen coordinates and colors multiplied by alpha().
        ImDrawList& draw_list() const {
            return m_draw_list;
        }

        float alpha() const {
            return m_alpha;
        }

        ImVec2 to_screen(ImVec2 point) const;
        Rect to_screen(Rect rect) const;
        ImVec2 to_logical(ImVec2 point) const;
        Rect to_logical(Rect rect) const;

        /// measures text in logical units with the current font and size.
        ImVec2 text_size(std::string_view text) const;

        /// clips drawing in logical units, including arbor child windows. false replaces the current clip.
        /// requires a paint state and, for rounded clips, a surface renderer. supports 64 nested rounded regions.
        void push_clip(Rect rect, float rounding = 0.0f, bool intersect = true) const;
        void pop_clip() const;

        void line(ImVec2 start, ImVec2 end, const Color& source, float thickness) const;
        void circle(ImVec2 center, float radius, const Color& source) const;
        void circle_outline(ImVec2 center, float radius, const Color& source, float thickness) const;
        void rect_filled(Rect rect, const Color& source, float rounding = 0.0F, ImDrawFlags flags = ImDrawFlags_None) const;
        void rect_outline(Rect rect, const Color& source, float thickness = 1.0F, float rounding = 0.0F) const;
        void triangle(ImVec2 center, ImVec2 size, const Color& source, TriangleDirection direction) const;
        void image(ImTextureID texture, Rect rect, ImVec2 uv_min, ImVec2 uv_max, const Color& tint, float rounding = 0.0F) const;

        void text(ImVec2 position, const Color& source, std::string_view text) const;
        void text(ImVec2 position, const Color& source, const TextValue& text, const ImVec4* clip_rect = nullptr) const;
        /// truncates against both the requested bounds and the draw list's ancestor clip.
        void text_ellipsis(ImVec2 position, const Color& source, const TextValue& text, ImVec4 clip_rect) const;

        void frame(Rect rect, const ComputedStyle& style, const std::optional<Color>& background = {}) const;

        /// paints background and border after a container has emitted effects with its ancestor clip.
        void frame_surface(Rect rect, const ComputedStyle& style, const std::optional<Color>& background = {}) const;
        void blur(Rect rect, const ComputedStyle& style) const;
        void shadow(Rect rect, const ComputedStyle& style) const;
        void border(Rect rect, const ComputedStyle& style, const Color& source) const;

    private:
        ImColor color(const Color& source) const;
        void frame_effects(Rect rect, const ComputedStyle& style) const;
        void draw_full_frame(Rect rect, const ComputedStyle& style, ImColor background, ImColor border) const;

        ImDrawList& m_draw_list;
        PaintState* m_state;
        EffectRegistry* m_effects;
        float m_alpha;
        float m_scale;
    };
} // namespace ui
