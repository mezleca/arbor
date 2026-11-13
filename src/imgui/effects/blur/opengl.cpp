#include <arbor/imgui/effects/blur/opengl.hpp>
#include <arbor/imgui/effects/clip/opengl.hpp>

#include <arbor/imgui/effects/blur/blur.hpp>
#include <arbor/imgui/effects/effects.hpp>
#include <arbor/imgui/effects/opengl.hpp>

#include <glad/gl.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <unordered_map>

using namespace ui;

struct BlurTextures {
    GLuint source = 0;
    GLuint ping = 0;
    GLuint result = 0;
    GLuint framebuffer = 0;
    GLuint vertex_array = 0;
    GLuint program = 0;
    int width = 0;
    int height = 0;
    GLint image = -1;
    GLint original = -1;
    GLint texel = -1;
    GLint direction = -1;
    GLint bounds = -1;
    GLint region = -1;
    GLint rounding = -1;
    GLint radius = -1;
    GLint filtered = -1;
    GLint clipped = -1;
    GLint opacity = -1;
    GLint amount = -1;
};

static std::unordered_map<ImGuiContext*, BlurTextures> texture_sets;
static BlurTextures* textures = nullptr;

static bool select_textures() {
    ImGuiContext* context = ImGui::GetCurrentContext();
    if (context == nullptr) {
        textures = nullptr;
        return false;
    }

    textures = &texture_sets[context];
    return true;
}

static constexpr const char* VERTEX_SHADER = R"(#version 330 core
void main() {
const vec2 positions[3] = vec2[](vec2(-1.0, -1.0), vec2(3.0, -1.0), vec2(-1.0, 3.0));
gl_Position = vec4(positions[gl_VertexID], 0.0, 1.0);
})";

static constexpr const char* FRAGMENT_SHADER = R"(
uniform sampler2D image;
uniform sampler2D original;
uniform vec2 texel;
uniform vec2 direction;
uniform vec4 bounds;
uniform vec4 region;
uniform float rounding;
uniform int radius;
uniform bool filtered;
uniform bool clipped;
uniform float opacity;
uniform float amount;
out vec4 color;

float rounded_box(vec2 point, vec2 size, float radius) {
vec2 distance = abs(point - size * 0.5) - size * 0.5 + radius;
return length(max(distance, 0.0)) + min(max(distance.x, distance.y), 0.0) - radius;
}

vec4 box_blur(vec2 uv, vec2 step) {
vec2 minimum = (bounds.xy + vec2(0.5)) * texel;
vec2 maximum = max(minimum, (bounds.zw - vec2(0.5)) * texel);
vec4 sum = texture(image, clamp(uv, minimum, maximum));
for (int pair = 0; pair < 32; ++pair) {
    if (pair >= radius / 2) break;
    float distance = float(pair * 2) + 1.5;
    sum += (texture(image, clamp(uv - step * distance, minimum, maximum)) + texture(image, clamp(uv + step * distance, minimum, maximum))) * 2.0;
}

if ((radius & 1) != 0) {
    sum += texture(image, clamp(uv - step * radius, minimum, maximum)) + texture(image, clamp(uv + step * radius, minimum, maximum));
}

return sum / float(radius * 2 + 1);
}

void main() {
vec2 uv = gl_FragCoord.xy * texel;
vec4 blurred = filtered ? box_blur(uv, texel * direction) : texture(image, uv);
if (!clipped) {
    color = blurred;
    return;
}

blurred = mix(texture(original, uv), blurred, amount);

vec2 point = gl_FragCoord.xy - region.xy;
if (rounded_box(point, region.zw, rounding) > 0.0) discard;
color = vec4(blurred.rgb, blurred.a * opacity * clip_coverage());
})";

static bool create_program() {
    const std::string fragment = std::string{"#version 330 core\n"} + CLIP_GLSL + FRAGMENT_SHADER;
    textures->program = create_opengl_effect_program(VERTEX_SHADER, fragment.c_str());
    if (textures->program == 0) {
        return false;
    }

    textures->image = glGetUniformLocation(textures->program, "image");
    textures->original = glGetUniformLocation(textures->program, "original");
    textures->texel = glGetUniformLocation(textures->program, "texel");
    textures->direction = glGetUniformLocation(textures->program, "direction");
    textures->bounds = glGetUniformLocation(textures->program, "bounds");
    textures->region = glGetUniformLocation(textures->program, "region");
    textures->rounding = glGetUniformLocation(textures->program, "rounding");
    textures->radius = glGetUniformLocation(textures->program, "radius");
    textures->filtered = glGetUniformLocation(textures->program, "filtered");
    textures->clipped = glGetUniformLocation(textures->program, "clipped");
    textures->opacity = glGetUniformLocation(textures->program, "opacity");
    textures->amount = glGetUniformLocation(textures->program, "amount");
    return true;
}

static void create_texture(GLuint& texture, int width, int height) {
    if (texture == 0) glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
}

static bool ensure_textures(int width, int height) {
    if (textures->program == 0 && !create_program()) {
        return false;
    }

    if (textures->width == width && textures->height == height) {
        return true;
    }

    textures->width = width;
    textures->height = height;

    create_texture(textures->source, width, height);
    create_texture(textures->ping, width, height);

    if (textures->framebuffer == 0) glGenFramebuffers(1, &textures->framebuffer);
    if (textures->vertex_array == 0) glGenVertexArrays(1, &textures->vertex_array);

    return true;
}

static void
blur_pass(GLuint input, GLuint output, int width, int height, int radius, float direction_x, float direction_y, ImVec4 bounds) {
    glBindFramebuffer(GL_FRAMEBUFFER, textures->framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, output, 0);
    glViewport(0, 0, width, height);
    glDisable(GL_BLEND);
    glUseProgram(textures->program);
    glUniform1i(textures->image, 0);
    glUniform2f(textures->texel, 1.0F / static_cast<float>(width), 1.0F / static_cast<float>(height));
    glUniform2f(textures->direction, direction_x, direction_y);
    glUniform4f(textures->bounds, bounds.x, bounds.y, bounds.z, bounds.w);
    glUniform1i(textures->radius, radius);
    glUniform1i(textures->filtered, GL_TRUE);
    glUniform1i(textures->clipped, GL_FALSE);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, input);
    glBindVertexArray(textures->vertex_array);
    glDrawArrays(GL_TRIANGLES, 0, 3);
}

static std::array<int, 3> box_widths(int sigma) {
    constexpr int passes = 3;
    // three integer box widths approximate the requested gaussian standard deviation.
    int lower = static_cast<int>(std::floor(std::sqrt((12.0 * sigma * sigma / passes) + 1.0)));
    if (lower % 2 == 0) --lower;

    const int upper = lower + 2;
    const int lower_count = static_cast<int>(std::round(
        ((12.0 * sigma * sigma) - (passes * lower * lower) - (4.0 * passes * lower) - (3.0 * passes)) / ((-4.0 * lower) - 4.0)
    ));

    return {
        lower_count > 0 ? lower : upper,
        lower_count > 1 ? lower : upper,
        lower_count > 2 ? lower : upper,
    };
}

static void blur(int width, int height, int sigma, ImVec4 bounds) {
    const std::array<int, 3> widths = box_widths(sigma);
    const int left = std::clamp(static_cast<int>(std::floor(bounds.x)) - 1, 0, width);
    const int right = std::clamp(static_cast<int>(std::ceil(bounds.z)) + 1, 0, width);
    const int bottom = std::clamp(static_cast<int>(std::floor(bounds.y)) - 1, 0, height);
    const int top = std::clamp(static_cast<int>(std::ceil(bounds.w)) + 1, 0, height);
    glEnable(GL_SCISSOR_TEST);
    glScissor(left, bottom, right - left, top - bottom);
    GLuint input = textures->source;
    GLuint output = textures->ping;

    for (const int window : widths) {
        blur_pass(input, output, width, height, window / 2, 1.0F, 0.0F, bounds);
        std::swap(input, output);
    }

    for (const int window : widths) {
        blur_pass(input, output, width, height, window / 2, 0.0F, 1.0F, bounds);
        std::swap(input, output);
    }

    textures->result = input;
}

static void render_blur(void*, const ImDrawList*, const ImDrawCmd* command, const void* payload) {
    const auto* region = static_cast<const BlurRegion*>(payload);

    if (region == nullptr || !select_textures()) {
        return;
    }

    GLint viewport[4]{};
    GLint framebuffer = 0;

    glGetIntegerv(GL_VIEWPORT, viewport);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &framebuffer);

    const int width = viewport[2];
    const int height = viewport[3];

    if (width <= 0 || height <= 0 || !ensure_textures(width, height)) {
        return;
    }

    const FramebufferTransform transform = framebuffer_transform();
    const ImVec2 minimum = transform.apply(region->output.min);
    const ImVec2 maximum = transform.apply(region->output.max);

    const float region_left = minimum.x;
    const float region_bottom = static_cast<float>(height) - maximum.y;
    const float region_width = maximum.x - minimum.x;
    const float region_height = maximum.y - minimum.y;
    const int left = std::clamp(static_cast<int>(std::floor(region_left)), 0, width);
    const int right = std::clamp(static_cast<int>(std::ceil(region_left + region_width)), 0, width);
    const int bottom = std::clamp(static_cast<int>(std::floor(region_bottom)), 0, height);
    const int top = std::clamp(static_cast<int>(std::ceil(region_bottom + region_height)), 0, height);
    if (right <= left || top <= bottom) {
        return;
    }

    const ImVec2 clip_min = transform.apply({command->ClipRect.x, command->ClipRect.y});
    const ImVec2 clip_max = transform.apply({command->ClipRect.z, command->ClipRect.w});
    const int clip_left = std::clamp(static_cast<int>(std::floor(clip_min.x)), 0, width);
    const int clip_right = std::clamp(static_cast<int>(std::ceil(clip_max.x)), 0, width);
    const int clip_bottom = std::clamp(static_cast<int>(std::floor(static_cast<float>(height) - clip_max.y)), 0, height);
    const int clip_top = std::clamp(static_cast<int>(std::ceil(static_cast<float>(height) - clip_min.y)), 0, height);
    const int clipped_left = std::max(left, clip_left);
    const int clipped_right = std::min(right, clip_right);
    const int clipped_bottom = std::max(bottom, clip_bottom);
    const int clipped_top = std::min(top, clip_top);
    if (clipped_right <= clipped_left || clipped_top <= clipped_bottom) {
        return;
    }

    const ImVec4 sample_bounds = {
        minimum.x, static_cast<float>(height) - maximum.y, maximum.x, static_cast<float>(height) - minimum.y
    };
    const int sample_left = std::clamp(static_cast<int>(std::floor(sample_bounds.x)) - 1, 0, width);
    const int sample_right = std::clamp(static_cast<int>(std::ceil(sample_bounds.z)) + 1, 0, width);
    const int sample_bottom = std::clamp(static_cast<int>(std::floor(sample_bounds.y)) - 1, 0, height);
    const int sample_top = std::clamp(static_cast<int>(std::ceil(sample_bounds.w)) + 1, 0, height);
    if (sample_right <= sample_left || sample_top <= sample_bottom) {
        return;
    }

    // each backdrop reads the current framebuffer before its own node is drawn.
    glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(framebuffer));
    glBindTexture(GL_TEXTURE_2D, textures->source);
    glCopyTexSubImage2D(
        GL_TEXTURE_2D, 0, sample_left, sample_bottom, sample_left, sample_bottom, sample_right - sample_left,
        sample_top - sample_bottom
    );
    blur(width, height, region->strength, sample_bounds);

    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(framebuffer));
    glViewport(0, 0, width, height);
    glEnable(GL_SCISSOR_TEST);
    glScissor(clipped_left, clipped_bottom, clipped_right - clipped_left, clipped_top - clipped_bottom);
    glUseProgram(textures->program);
    glUniform1i(textures->image, 0);
    glUniform1i(textures->original, 1);
    glUniform2f(textures->texel, 1.0F / static_cast<float>(width), 1.0F / static_cast<float>(height));
    glUniform4f(textures->region, region_left, region_bottom, region_width, region_height);
    glUniform1f(
        textures->rounding,
        std::min(region->rounding * std::min(transform.scale.x, transform.scale.y), std::min(region_width, region_height) * 0.5F)
    );
    glUniform1f(textures->opacity, region->opacity);
    glUniform1f(textures->amount, static_cast<float>(region->strength) / (static_cast<float>(region->strength) + 4.0F));
    glUniform1i(textures->clipped, GL_TRUE);
    glUniform1i(textures->filtered, GL_FALSE);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, textures->result);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, textures->source);
    glBindVertexArray(textures->vertex_array);

    const bool rounded_clip = apply_opengl_clip(textures->program);
    if (region->opacity < 1.0f || rounded_clip) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    } else {
        glDisable(GL_BLEND);
    }

    glDrawArrays(GL_TRIANGLES, 0, 3);
    glDisable(GL_SCISSOR_TEST);
    glActiveTexture(GL_TEXTURE0);
}

static bool initialize_blur_effect(void*) {
    return GLAD_GL_VERSION_3_3 && select_textures();
}

static void shutdown_blur_effect(void*) {
    if (!select_textures()) {
        return;
    }

    if (textures->program != 0) glDeleteProgram(textures->program);
    if (textures->vertex_array != 0) glDeleteVertexArrays(1, &textures->vertex_array);
    if (textures->framebuffer != 0) glDeleteFramebuffers(1, &textures->framebuffer);
    if (textures->source != 0) glDeleteTextures(1, &textures->source);
    if (textures->ping != 0) glDeleteTextures(1, &textures->ping);

    texture_sets.erase(ImGui::GetCurrentContext());
    textures = nullptr;
}

void ui::register_opengl_blur(EffectRegistry& effects) {
    effects.register_effect<BlurRegion>(
        EffectSlot::Blur, {render_blur, initialize_blur_effect, nullptr, shutdown_blur_effect, nullptr}
    );
}
