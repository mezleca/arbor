#pragma once

#include <glad/gl.h>

namespace ui {
    class EffectRegistry;

    void register_opengl_clip(EffectRegistry& effects);
    bool apply_opengl_clip(GLuint program);

    inline constexpr const char* CLIP_GLSL = R"(
uniform int clip_count;
uniform vec4 clip_rects[64];
uniform float clip_radii[64];
uniform vec4 clip_transform;
uniform float clip_height;

float clip_coverage() {
    if (clip_count == 0) return 1.0;
    vec2 point = vec2(gl_FragCoord.x, clip_height - gl_FragCoord.y) * clip_transform.zw + clip_transform.xy;
    float distance = -1e20;
    for (int i = 0; i < clip_count; ++i) {
        vec2 center = (clip_rects[i].xy + clip_rects[i].zw) * 0.5;
        vec2 half_size = (clip_rects[i].zw - clip_rects[i].xy) * 0.5;
        vec2 q = abs(point - center) - half_size + clip_radii[i];
        float d = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - clip_radii[i];
        distance = max(distance, d);
    }
    return clamp(0.5 - distance / max(fwidth(distance), 0.001), 0.0, 1.0);
}
)";
} // namespace ui
