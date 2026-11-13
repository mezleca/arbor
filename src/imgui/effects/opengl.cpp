#include <arbor/imgui/effects/opengl.hpp>
#include <arbor/imgui/effects/clip/opengl.hpp>
#include <arbor/imgui/effects/blur/opengl.hpp>
#include <arbor/imgui/effects/gradient/opengl.hpp>
#include <arbor/imgui/effects/shadow/opengl.hpp>
#include <algorithm>
#include <cmath>

using namespace ui;

void ui::register_opengl_effects(EffectRegistry& effects) {
    register_opengl_clip(effects);
    register_opengl_blur(effects);
    register_opengl_box_shadow(effects);
    register_opengl_gradient(effects);
}

FramebufferTransform ui::framebuffer_transform() {
    const ImDrawData* draw_data = ImGui::GetDrawData();
    if (draw_data == nullptr) return {{}, {1.0F, 1.0F}};

    return {draw_data->DisplayPos, draw_data->FramebufferScale};
}

static constexpr const char* VERTEX_SHADER = R"(#version 330 core
void main() {
const vec2 positions[3] = vec2[](vec2(-1.0, -1.0), vec2(3.0, -1.0), vec2(-1.0, 3.0));
gl_Position = vec4(positions[gl_VertexID], 0.0, 1.0);
})";

static GLuint compile_shader(GLenum type, const char* source) {
    const GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    GLint compiled = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (compiled == GL_TRUE) {
        return shader;
    }

    glDeleteShader(shader);
    return 0;
}

GLuint ui::create_opengl_effect_program(const char* vertex_shader, const char* fragment_shader) {
    const GLuint vertex = compile_shader(GL_VERTEX_SHADER, vertex_shader);
    const GLuint fragment = compile_shader(GL_FRAGMENT_SHADER, fragment_shader);
    if (vertex == 0 || fragment == 0) {
        if (vertex != 0) glDeleteShader(vertex);
        if (fragment != 0) glDeleteShader(fragment);
        return 0;
    }

    const GLuint program = glCreateProgram();
    glAttachShader(program, vertex);
    glAttachShader(program, fragment);
    glLinkProgram(program);
    glDeleteShader(vertex);
    glDeleteShader(fragment);

    GLint linked = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (linked != GL_TRUE) {
        glDeleteProgram(program);
        return 0;
    }

    return program;
}

bool OpenGlFullscreenEffect::initialize(const char* fragment_shader) {
    m_program = create_opengl_effect_program(VERTEX_SHADER, fragment_shader);
    if (m_program == 0) {
        return false;
    }

    glGenVertexArrays(1, &m_vertex_array);
    return true;
}

void OpenGlFullscreenEffect::shutdown() {
    if (m_program != 0) glDeleteProgram(m_program);
    if (m_vertex_array != 0) glDeleteVertexArrays(1, &m_vertex_array);
    if (m_texture != 0) glDeleteTextures(1, &m_texture);
    m_program = 0;
    m_vertex_array = 0;
    m_texture = 0;
    m_texture_size = {};
}

bool OpenGlFullscreenEffect::begin(const ImDrawCmd& command, Rect bounds) {
    if (m_program == 0 || m_vertex_array == 0 || !bounds.valid()) {
        return false;
    }

    GLint viewport[4]{};
    glGetIntegerv(GL_VIEWPORT, viewport);
    const int width = viewport[2];
    const int height = viewport[3];
    if (width <= 0 || height <= 0) {
        return false;
    }

    const FramebufferTransform transform = framebuffer_transform();
    const ImVec2 bounds_min = transform.apply(bounds.min);
    const ImVec2 bounds_max = transform.apply(bounds.max);
    const ImVec2 clip_min = transform.apply({command.ClipRect.x, command.ClipRect.y});
    const ImVec2 clip_max = transform.apply({command.ClipRect.z, command.ClipRect.w});
    // intersect effect bounds with imgui's clip rectangle in framebuffer coordinates.
    const int left = std::clamp(static_cast<int>(std::floor(std::max(bounds_min.x, clip_min.x))), 0, width);
    const int right = std::clamp(static_cast<int>(std::ceil(std::min(bounds_max.x, clip_max.x))), 0, width);
    const int top = std::clamp(static_cast<int>(std::floor(std::max(bounds_min.y, clip_min.y))), 0, height);
    const int bottom = std::clamp(static_cast<int>(std::ceil(std::min(bounds_max.y, clip_max.y))), 0, height);
    if (right <= left || bottom <= top) {
        return false;
    }

    const float framebuffer_height = static_cast<float>(height);
    m_bounds = {{bounds_min.x, framebuffer_height - bounds_max.y}, {bounds_max.x, framebuffer_height - bounds_min.y}};
    glEnable(GL_SCISSOR_TEST);
    glScissor(left, height - bottom, right - left, bottom - top);
    glDisable(GL_BLEND);
    glUseProgram(m_program);
    glBindVertexArray(m_vertex_array);
    return true;
}

GLint OpenGlFullscreenEffect::uniform(const char* name) const {
    return glGetUniformLocation(m_program, name);
}

Rect OpenGlFullscreenEffect::framebuffer_bounds() const {
    return m_bounds;
}

bool OpenGlFullscreenEffect::capture() {
    const int left = static_cast<int>(std::floor(m_bounds.min.x));
    const int bottom = static_cast<int>(std::floor(m_bounds.min.y));
    const int right = static_cast<int>(std::ceil(m_bounds.max.x));
    const int top = static_cast<int>(std::ceil(m_bounds.max.y));
    const int width = right - left;
    const int height = top - bottom;
    if (width <= 0 || height <= 0) {
        return false;
    }

    if (m_texture == 0) {
        glGenTextures(1, &m_texture);
        glBindTexture(GL_TEXTURE_2D, m_texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    } else {
        glBindTexture(GL_TEXTURE_2D, m_texture);
    }

    const ImVec2 texture_size = {static_cast<float>(width), static_cast<float>(height)};
    if (m_texture_size.x != texture_size.x || m_texture_size.y != texture_size.y) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        m_texture_size = texture_size;
    }

    m_bounds = {{static_cast<float>(left), static_cast<float>(bottom)}, {static_cast<float>(right), static_cast<float>(top)}};
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, left, bottom, width, height);
    return true;
}

GLuint OpenGlFullscreenEffect::captured_texture() const {
    return m_texture;
}

void OpenGlFullscreenEffect::draw() {
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glDisable(GL_SCISSOR_TEST);
}
