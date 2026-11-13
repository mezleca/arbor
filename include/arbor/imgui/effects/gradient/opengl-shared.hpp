#pragma once

#include "../../../style/gradient-data.hpp"

#include <glad/gl.h>

namespace ui {
    // included after #version by both the surface and shadow fragment shaders.
    inline constexpr const char* GRADIENT_SAMPLER_GLSL = R"(
uniform vec4 solid_color;
uniform int gradient_type;
uniform vec2 gradient_start;
uniform vec2 gradient_end;
uniform int stop_count;
uniform float stop_positions[8];
uniform vec4 stop_colors[8];

vec4 sample_color(vec2 point, vec4 shape) {
    if (stop_count == 0) return solid_color;
    vec2 size = max(shape.zw - shape.xy, vec2(0.001));
    vec2 uv = (point - shape.xy) / size;
    float t;
    if (gradient_type == 0) {
        vec2 axis = gradient_end - gradient_start;
        t = dot(uv - gradient_start, axis) / max(dot(axis, axis), 0.000001);
    } else {
        vec2 radius = max(abs(gradient_end - gradient_start), vec2(0.0001));
        t = length((uv - gradient_start) / radius);
    }
    if (t <= stop_positions[0]) return stop_colors[0];
    for (int index = 1; index < stop_count; ++index) {
        if (t <= stop_positions[index]) {
            float fraction = (t - stop_positions[index - 1]) / (stop_positions[index] - stop_positions[index - 1]);
            return mix(stop_colors[index - 1], stop_colors[index], fraction);
        }
    }
    return stop_colors[stop_count - 1];
}
)";

    struct GradientUniforms {
        GLint solid = -1;
        GLint type = -1;
        GLint start = -1;
        GLint end = -1;
        GLint count = -1;
        GLint positions = -1;
        GLint colors = -1;
    };

    GradientUniforms find_gradient_uniforms(GLuint program);
    void upload_gradient(const GradientUniforms& uniforms, ImVec4 solid, const GradientData& gradient);
} // namespace ui
