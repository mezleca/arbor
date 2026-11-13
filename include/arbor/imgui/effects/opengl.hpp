#pragma once

#include "../../layout/geometry.hpp"

#include <glad/gl.h>
#include <imgui.h>

namespace ui {
    class EffectRegistry;

    GLuint create_opengl_effect_program(const char* vertex_shader, const char* fragment_shader);

    // registers the clip, blur, box shadow and gradient effects available to opengl backends.
    void register_opengl_effects(EffectRegistry& effects);

    struct FramebufferTransform {
        ImVec2 display_position;
        ImVec2 scale;

        ImVec2 apply(ImVec2 point) const {
            return {(point.x - display_position.x) * scale.x, (point.y - display_position.y) * scale.y};
        }
    };

    FramebufferTransform framebuffer_transform();

    class OpenGlFullscreenEffect {
    public:
        OpenGlFullscreenEffect() = default;
        OpenGlFullscreenEffect(const OpenGlFullscreenEffect&) = delete;
        OpenGlFullscreenEffect& operator=(const OpenGlFullscreenEffect&) = delete;

        bool initialize(const char* fragment_shader);

        // shutdown must run before destroying the current opengl context.
        void shutdown();

        // intersect effect bounds with the command clip before enabling scissor.
        bool begin(const ImDrawCmd& command, Rect bounds);
        GLint uniform(const char* name) const;
        GLuint program() const {
            return m_program;
        }

        // y uses the opengl framebuffer's bottom-left origin.
        Rect framebuffer_bounds() const;

        // copies framebuffer_bounds into captured_texture for a pass that reads prior draw commands.
        bool capture();
        GLuint captured_texture() const;
        static void draw();

    private:
        GLuint m_program = 0;
        GLuint m_vertex_array = 0;
        GLuint m_texture = 0;
        ImVec2 m_texture_size;
        Rect m_bounds;
    };
} // namespace ui
