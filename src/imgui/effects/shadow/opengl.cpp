#include <arbor/imgui/effects/shadow/opengl.hpp>
#include <arbor/imgui/effects/clip/opengl.hpp>

#include <arbor/imgui/effects/shadow/shadow.hpp>
#include <arbor/imgui/effects/effects.hpp>
#include <arbor/imgui/effects/opengl.hpp>
#include <arbor/imgui/effects/gradient/opengl-shared.hpp>

#include <algorithm>
#include <string>
#include <unordered_map>

using namespace ui;

struct BoxShadowGlState {
    OpenGlFullscreenEffect effect;
    GLint shape = -1;
    GLint cutout = -1;
    GLint rounding = -1;
    GLint cutout_rounding = -1;
    GLint sigma = -1;
    GLint viewport_height = -1;
    GLint gradient_bounds = -1;
    GLint opacity = -1;
    GradientUniforms sampler;
};

static std::unordered_map<ImGuiContext*, BoxShadowGlState> gl_states;
static BoxShadowGlState* gl_state = nullptr;

static bool select_gl_state() {
    ImGuiContext* context = ImGui::GetCurrentContext();
    if (context == nullptr) {
        gl_state = nullptr;
        return false;
    }

    gl_state = &gl_states[context];
    return true;
}

static constexpr const char* FRAGMENT_BODY = R"(
uniform vec4 shape;
uniform vec4 cutout;
uniform float rounding;
uniform float cutout_rounding;
uniform float sigma;
uniform float viewport_height;
uniform vec4 gradient_bounds;
uniform float opacity;
out vec4 color;

// adapted from evan wallace's cc0 rounded box shadow shader:
// https://madebyevan.com/shaders/fast-rounded-rectangle-shadows/
float erf_approx(float value) {
float direction = value < 0.0 ? -1.0 : 1.0;
float absolute = abs(value);
float polynomial = 1.0 + (0.278393 + (0.230389 + 0.078108 * absolute * absolute) * absolute) * absolute;
polynomial *= polynomial;
return direction - direction / (polynomial * polynomial);
}

float gaussian(float value, float width) {
return exp(-(value * value) / (2.0 * width * width)) / (sqrt(2.0 * 3.141592653589793) * width);
}

float rounded_shadow_x(float x, float y, float width, float corner, vec2 half_size) {
float delta = min(half_size.y - corner - abs(y), 0.0);
float curved = half_size.x - corner + sqrt(max(0.0, corner * corner - delta * delta));
float scale = sqrt(0.5) / width;
vec2 integral = 0.5 + 0.5 * vec2(erf_approx((x - curved) * scale), erf_approx((x + curved) * scale));
return integral.y - integral.x;
}

float rounded_shadow(vec2 point, vec2 half_size, float width, float corner) {
float low = point.y - half_size.y;
float high = point.y + half_size.y;
float start = clamp(-3.0 * width, low, high);
float end = clamp(3.0 * width, low, high);
float step = (end - start) / 4.0;
float value = 0.0;
for (int index = 0; index < 4; ++index) {
    float sample = start + step * (float(index) + 0.5);
    value += rounded_shadow_x(point.x, point.y - sample, width, corner, half_size) * gaussian(sample, width) * step;
}
return clamp(value, 0.0, 1.0);
}

float rounded_box_sdf(vec2 point, vec2 half_size, float corner) {
vec2 distance = abs(point) - half_size + vec2(corner);
return length(max(distance, 0.0)) + min(max(distance.x, distance.y), 0.0) - corner;
}

void main() {
vec2 point = vec2(gl_FragCoord.x, viewport_height - gl_FragCoord.y);
vec2 center = (shape.xy + shape.zw) * 0.5;
vec2 half_size = (shape.zw - shape.xy) * 0.5;
vec2 relative = point - center;
float coverage;

if (sigma <= 0.001) {
    float distance = rounded_box_sdf(relative, half_size, rounding);
    float antialias = max(fwidth(distance), 0.5);
    coverage = 1.0 - smoothstep(-antialias, antialias, distance);
} else {
    coverage = rounded_shadow(relative, half_size, sigma, rounding);
}

vec2 cutout_center = (cutout.xy + cutout.zw) * 0.5;
vec2 cutout_half_size = (cutout.zw - cutout.xy) * 0.5;
float cutout_distance = rounded_box_sdf(point - cutout_center, cutout_half_size, cutout_rounding);
float cutout_antialias = max(fwidth(cutout_distance), 0.5);
// remove the owner shape so a deferred shadow cannot darken its own node.
coverage *= smoothstep(-cutout_antialias, cutout_antialias, cutout_distance);

// shadow colors span the blurred bounds, including pixels outside the owner shape.
vec4 sampled = sample_color(point, gradient_bounds);
color = vec4(sampled.rgb, sampled.a * coverage * opacity * clip_coverage());
})";

static bool create_program() {
    const std::string fragment_source = std::string{"#version 330 core\n"} + CLIP_GLSL + GRADIENT_SAMPLER_GLSL + FRAGMENT_BODY;
    if (!gl_state->effect.initialize(fragment_source.c_str())) {
        return false;
    }

    const GLuint program = gl_state->effect.program();
    gl_state->shape = glGetUniformLocation(program, "shape");
    gl_state->cutout = glGetUniformLocation(program, "cutout");
    gl_state->rounding = glGetUniformLocation(program, "rounding");
    gl_state->cutout_rounding = glGetUniformLocation(program, "cutout_rounding");
    gl_state->sigma = glGetUniformLocation(program, "sigma");
    gl_state->viewport_height = glGetUniformLocation(program, "viewport_height");
    gl_state->gradient_bounds = glGetUniformLocation(program, "gradient_bounds");
    gl_state->opacity = glGetUniformLocation(program, "opacity");
    gl_state->sampler = find_gradient_uniforms(program);
    return true;
}

static bool ensure_gl_state() {
    return gl_state->effect.program() != 0 || create_program();
}

static void render_box_shadow(void*, const ImDrawList*, const ImDrawCmd* command, const void* payload) {
    const auto* region = static_cast<const BoxShadowRegion*>(payload);
    if (region == nullptr || !select_gl_state() || !ensure_gl_state()) {
        return;
    }

    if (!gl_state->effect.begin(*command, region->bounds)) {
        return;
    }

    GLint viewport[4]{};
    glGetIntegerv(GL_VIEWPORT, viewport);
    const int height = viewport[3];
    const FramebufferTransform transform = framebuffer_transform();
    const float scale_factor = std::min(transform.scale.x, transform.scale.y);

    const ImVec2 shape_min = transform.apply(region->shape.min);
    const ImVec2 shape_max = transform.apply(region->shape.max);
    const ImVec2 cutout_min = transform.apply(region->cutout.min);
    const ImVec2 cutout_max = transform.apply(region->cutout.max);
    const Rect bounds = gl_state->effect.framebuffer_bounds();

    const float shape_width = shape_max.x - shape_min.x;
    const float shape_height = shape_max.y - shape_min.y;
    const float radius = std::min(region->rounding * scale_factor, std::min(shape_width, shape_height) * 0.5F);
    const float blur = region->blur * 0.5F * scale_factor;

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendEquation(GL_FUNC_ADD);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUniform4f(gl_state->shape, shape_min.x, shape_min.y, shape_max.x, shape_max.y);
    glUniform4f(gl_state->cutout, cutout_min.x, cutout_min.y, cutout_max.x, cutout_max.y);
    glUniform1f(gl_state->rounding, radius);
    glUniform1f(gl_state->cutout_rounding, region->cutout_rounding * scale_factor);
    glUniform1f(gl_state->sigma, blur);
    glUniform1f(gl_state->viewport_height, static_cast<float>(height));
    const float framebuffer_height = static_cast<float>(height);
    glUniform4f(
        gl_state->gradient_bounds, bounds.min.x, framebuffer_height - bounds.max.y, bounds.max.x,
        framebuffer_height - bounds.min.y
    );
    glUniform1f(gl_state->opacity, region->opacity);
    upload_gradient(gl_state->sampler, region->color, region->gradient);
    apply_opengl_clip(gl_state->effect.program());
    OpenGlFullscreenEffect::draw();
}

static bool initialize_box_shadow_effect(void*) {
    return GLAD_GL_VERSION_3_3 && select_gl_state() && ensure_gl_state();
}

static void shutdown_box_shadow_effect(void*) {
    if (!select_gl_state()) {
        return;
    }

    gl_state->effect.shutdown();
    gl_states.erase(ImGui::GetCurrentContext());
    gl_state = nullptr;
}

void ui::register_opengl_box_shadow(EffectRegistry& effects) {
    effects.register_effect<BoxShadowRegion>(
        EffectSlot::BoxShadow, {render_box_shadow, initialize_box_shadow_effect, nullptr, shutdown_box_shadow_effect, nullptr}
    );
}
