#include <arbor/imgui/effects/clip/opengl.hpp>

#include <arbor/imgui/effects/effects.hpp>
#include <arbor/imgui/effects/opengl.hpp>
#include <arbor/imgui/paint-state.hpp>

#include <limits>
#include <array>
#include <stdexcept>
#include <string>
#include <unordered_map>

using namespace ui;

struct ClipUniforms {
    GLint count = -1;
    GLint rects = -1;
    GLint radii = -1;
    GLint transform = -1;
    GLint height = -1;
    int depth = -1;
    uint64_t clip_id = std::numeric_limits<uint64_t>::max();
    ImVec4 uploaded_transform;
    int uploaded_height = -1;
};

struct ClipGlState {
    GLuint program = 0;
    GLuint native_program = 0;
    GLint native_projection = -1;
    GLint projection = -1;
    const RoundedClip* clip = nullptr;
    std::unordered_map<GLuint, ClipUniforms> uniforms;
    bool projection_ready = false;
};

static std::unordered_map<ImGuiContext*, ClipGlState> states;

static constexpr const char* VERTEX_SHADER = R"(#version 330 core
uniform mat4 ProjMtx;
in vec2 Position;
in vec2 UV;
in vec4 Color;
out vec2 Frag_UV;
out vec4 Frag_Color;
void main() {
    Frag_UV = UV;
    Frag_Color = Color;
    gl_Position = ProjMtx * vec4(Position, 0.0, 1.0);
}
)";

static constexpr const char* FRAGMENT_BODY = R"(
uniform sampler2D Texture;
in vec2 Frag_UV;
in vec4 Frag_Color;
out vec4 color;
void main() {
    color = Frag_Color * texture(Texture, Frag_UV);
    color.a *= clip_coverage();
}
)";

bool ui::apply_opengl_clip(GLuint program) {
    auto& state = states[ImGui::GetCurrentContext()];
    auto [entry, inserted] = state.uniforms.try_emplace(program);
    ClipUniforms& uniforms = entry->second;
    if (inserted) {
        uniforms.count = glGetUniformLocation(program, "clip_count");
        uniforms.rects = glGetUniformLocation(program, "clip_rects");
        uniforms.radii = glGetUniformLocation(program, "clip_radii");
        uniforms.transform = glGetUniformLocation(program, "clip_transform");
        uniforms.height = glGetUniformLocation(program, "clip_height");
    }

    const uint64_t id = state.clip != nullptr ? state.clip->id : 0;
    if (uniforms.clip_id == id) return state.clip != nullptr;
    uniforms.clip_id = id;
    const int depth = state.clip != nullptr ? state.clip->depth : 0;
    if (uniforms.depth != depth) {
        glUniform1i(uniforms.count, depth);
        uniforms.depth = depth;
    }
    if (state.clip == nullptr) return false;

    std::array<GLfloat, 64 * 4> rects;
    std::array<GLfloat, 64> radii;
    int index = 0;
    for (const RoundedClip* clip = state.clip; clip != nullptr; clip = clip->parent, ++index) {
        rects[index * 4] = clip->rect.min.x;
        rects[index * 4 + 1] = clip->rect.min.y;
        rects[index * 4 + 2] = clip->rect.max.x;
        rects[index * 4 + 3] = clip->rect.max.y;
        radii[index] = clip->rounding;
    }
    glUniform4fv(uniforms.rects, index, rects.data());
    glUniform1fv(uniforms.radii, index, radii.data());
    const FramebufferTransform transform = framebuffer_transform();
    GLint viewport[4]{};
    glGetIntegerv(GL_VIEWPORT, viewport);
    const ImVec4 coordinates{
        transform.display_position.x, transform.display_position.y, 1.0f / transform.scale.x, 1.0f / transform.scale.y
    };
    // uniforms belong to the program and survive native renderer resets and shader switches
    // keep querying the viewport so custom callbacks can still change it within a frame
    if (uniforms.uploaded_height == -1 || uniforms.uploaded_transform.x != coordinates.x ||
        uniforms.uploaded_transform.y != coordinates.y || uniforms.uploaded_transform.z != coordinates.z ||
        uniforms.uploaded_transform.w != coordinates.w) {
        glUniform4f(uniforms.transform, coordinates.x, coordinates.y, coordinates.z, coordinates.w);
        uniforms.uploaded_transform = coordinates;
    }
    if (uniforms.uploaded_height != viewport[3]) {
        glUniform1f(uniforms.height, static_cast<float>(viewport[3]));
        uniforms.uploaded_height = viewport[3];
    }
    return true;
}

static void render(void*, const ImDrawList*, const ImDrawCmd*, const void* payload) {
    auto& state = states[ImGui::GetCurrentContext()];
    const auto& command = *static_cast<const ClipCommand*>(payload);
    if (!command.restore) state.clip = command.clip;

    if (state.clip == nullptr) {
        if (!command.restore && state.native_program != 0) glUseProgram(state.native_program);
        return;
    }

    if (state.program == 0) {
        GLint native = 0;
        glGetIntegerv(GL_CURRENT_PROGRAM, &native);
        state.native_program = static_cast<GLuint>(native);
        const std::string fragment = std::string{"#version 330 core\n"} + CLIP_GLSL + FRAGMENT_BODY;
        state.program = create_opengl_effect_program(VERTEX_SHADER, fragment.c_str());
        if (state.program == 0) throw std::runtime_error("failed to create rounded clip shader");

        // keep the vertex bindings prepared by imgui's renderer, regardless of the driver's attribute locations.
        for (const char* name : {"Position", "UV", "Color"}) {
            glBindAttribLocation(state.program, static_cast<GLuint>(glGetAttribLocation(state.native_program, name)), name);
        }
        glLinkProgram(state.program);
        GLint linked = GL_FALSE;
        glGetProgramiv(state.program, GL_LINK_STATUS, &linked);
        if (linked != GL_TRUE) throw std::runtime_error("failed to link rounded clip shader");
        state.native_projection = glGetUniformLocation(state.native_program, "ProjMtx");
        state.projection = glGetUniformLocation(state.program, "ProjMtx");
        glUseProgram(state.program);
        glUniform1i(glGetUniformLocation(state.program, "Texture"), 0);
    }

    glUseProgram(state.program);
    if (!state.projection_ready) {
        // native setup owns projection and clip origin. copy it once per frame instead of duplicating its coordinate rules.
        GLfloat projection[16]{};
        glGetUniformfv(state.native_program, state.native_projection, projection);
        glUniformMatrix4fv(state.projection, 1, GL_FALSE, projection);
        state.projection_ready = true;
    }
    apply_opengl_clip(state.program);
}

static void begin_frame(void*) {
    auto& state = states[ImGui::GetCurrentContext()];
    state.clip = nullptr;
    state.projection_ready = false;
}

static void shutdown(void*) {
    auto found = states.find(ImGui::GetCurrentContext());
    if (found == states.end()) return;
    if (found->second.program != 0) glDeleteProgram(found->second.program);
    states.erase(found);
}

void ui::register_opengl_clip(EffectRegistry& effects) {
    effects.register_effect<ClipCommand>(EffectSlot::Clip, {render, nullptr, begin_frame, shutdown, nullptr});
}
