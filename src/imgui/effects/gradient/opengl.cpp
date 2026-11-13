#include <arbor/imgui/effects/gradient/opengl.hpp>
#include <arbor/imgui/effects/clip/opengl.hpp>

#include <arbor/imgui/effects/gradient/gradient.hpp>
#include <arbor/imgui/effects/gradient/opengl-shared.hpp>
#include <arbor/imgui/effects/effects.hpp>
#include <arbor/imgui/effects/opengl.hpp>

#include <algorithm>
#include <string>
#include <unordered_map>

using namespace ui;

struct GradientGlState {
    OpenGlFullscreenEffect effect;
    GradientUniforms sampler;
    GLint shape = -1;
    GLint rounding = -1;
    GLint border = -1;
    GLint height = -1;
    GLint opacity = -1;
};

static std::unordered_map<ImGuiContext*, GradientGlState> states;

static constexpr const char* FRAGMENT_BODY = R"(
uniform vec4 shape;
uniform float rounding;
uniform float border_thickness;
uniform float viewport_height;
uniform float opacity;
out vec4 color;

float rounded_box_sdf(vec2 point, vec2 half_size, float radius) {
    vec2 distance = abs(point) - half_size + vec2(radius);
    return length(max(distance, 0.0)) + min(max(distance.x, distance.y), 0.0) - radius;
}

void main() {
    vec2 point = vec2(gl_FragCoord.x, viewport_height - gl_FragCoord.y);
    vec2 center = (shape.xy + shape.zw) * 0.5;
    vec2 half_size = (shape.zw - shape.xy) * 0.5;
    float distance = rounded_box_sdf(point - center, half_size, rounding);
    float antialias = max(fwidth(distance), 0.5);
    float coverage = 1.0 - smoothstep(-antialias, antialias, distance);
    if (border_thickness > 0.0) {
        vec2 inner_size = max(half_size - vec2(border_thickness), vec2(0.0));
        float inner = rounded_box_sdf(point - center, inner_size, max(0.0, rounding - border_thickness));
        coverage *= smoothstep(-antialias, antialias, inner);
    }
    vec4 sampled = sample_color(point, shape);
    color = vec4(sampled.rgb, sampled.a * coverage * opacity * clip_coverage());
}
)";

static bool initialize(void*) {
    auto& state = states[ImGui::GetCurrentContext()];
    const std::string shader = std::string{"#version 330 core\n"} + CLIP_GLSL + GRADIENT_SAMPLER_GLSL + FRAGMENT_BODY;
    if (!state.effect.initialize(shader.c_str())) return false;
    const GLuint program = state.effect.program();
    state.sampler = find_gradient_uniforms(program);
    state.shape = state.effect.uniform("shape");
    state.rounding = state.effect.uniform("rounding");
    state.border = state.effect.uniform("border_thickness");
    state.height = state.effect.uniform("viewport_height");
    state.opacity = state.effect.uniform("opacity");
    return true;
}

static void render(void*, const ImDrawList*, const ImDrawCmd* command, const void* payload) {
    const auto& region = *static_cast<const GradientRegion*>(payload);
    auto it = states.find(ImGui::GetCurrentContext());
    if (it == states.end() || !it->second.effect.begin(*command, region.rect)) return;

    auto& state = it->second;
    GLint viewport[4]{};
    glGetIntegerv(GL_VIEWPORT, viewport);
    const Rect bounds = state.effect.framebuffer_bounds();
    const float height = static_cast<float>(viewport[3]);
    const float scale = std::min(bounds.size().x / region.rect.size().x, bounds.size().y / region.rect.size().y);
    const float radius = std::clamp(region.rounding * scale, 0.0F, std::min(bounds.size().x, bounds.size().y) * 0.5F);
    glEnable(GL_BLEND);
    glBlendEquation(GL_FUNC_ADD);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUniform4f(state.shape, bounds.min.x, height - bounds.max.y, bounds.max.x, height - bounds.min.y);
    glUniform1f(state.rounding, radius);
    glUniform1f(state.border, region.border_thickness * scale);
    glUniform1f(state.height, height);
    glUniform1f(state.opacity, region.opacity);
    upload_gradient(state.sampler, {}, region.gradient);
    apply_opengl_clip(state.effect.program());
    OpenGlFullscreenEffect::draw();
}

static void shutdown(void*) {
    auto it = states.find(ImGui::GetCurrentContext());
    if (it == states.end()) return;
    it->second.effect.shutdown();
    states.erase(it);
}

void ui::register_opengl_gradient(EffectRegistry& effects) {
    effects.register_effect<GradientRegion>(EffectSlot::Gradient, {render, initialize, nullptr, shutdown, nullptr});
}
