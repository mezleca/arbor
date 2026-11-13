#include <arbor/backends/sdl/backend.hpp>

#include <arbor/constants.hpp>
#include <arbor/imgui/context-scope.hpp>
#include <arbor/imgui/effects/opengl.hpp>
#include <arbor/surface.hpp>

#include <glad/gl.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl3.h>
#include <SDL3/SDL_log.h>

#include <cfloat>
#include <optional>

using namespace ui;

// the backend exports this mapper but omits its declaration from the public header.
ImGuiKey ImGui_ImplSDL3_KeyEventToImGuiKey(SDL_Keycode keycode, SDL_Scancode scancode);

static GLADapiproc load_opengl(const char* name) {
    return reinterpret_cast<GLADapiproc>(SDL_GL_GetProcAddress(name));
}

static SDL_WindowID event_window_id(const SDL_Event& event) {
    if (event.type >= SDL_EVENT_WINDOW_FIRST && event.type <= SDL_EVENT_WINDOW_LAST) {
        return event.window.windowID;
    }

    switch (event.type) {
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP:
            return event.key.windowID;
        case SDL_EVENT_TEXT_INPUT:
            return event.text.windowID;
        case SDL_EVENT_MOUSE_MOTION:
            return event.motion.windowID;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
            return event.button.windowID;
        case SDL_EVENT_MOUSE_WHEEL:
            return event.wheel.windowID;
        default:
            return 0;
    }
}

static PointerButton pointer_button(uint8_t button) {
    switch (button) {
        case SDL_BUTTON_LEFT:
            return PointerButton::Left;
        case SDL_BUTTON_RIGHT:
            return PointerButton::Right;
        case SDL_BUTTON_MIDDLE:
            return PointerButton::Middle;
        default:
            return PointerButton::None;
    }
}

static SDL_SystemCursor system_cursor(ImGuiMouseCursor cursor) {
    switch (cursor) {
        case ImGuiMouseCursor_TextInput:
            return SDL_SYSTEM_CURSOR_TEXT;
        case ImGuiMouseCursor_ResizeAll:
            return SDL_SYSTEM_CURSOR_MOVE;
        case ImGuiMouseCursor_ResizeNS:
            return SDL_SYSTEM_CURSOR_NS_RESIZE;
        case ImGuiMouseCursor_ResizeEW:
            return SDL_SYSTEM_CURSOR_EW_RESIZE;
        case ImGuiMouseCursor_ResizeNESW:
            return SDL_SYSTEM_CURSOR_NESW_RESIZE;
        case ImGuiMouseCursor_ResizeNWSE:
            return SDL_SYSTEM_CURSOR_NWSE_RESIZE;
        case ImGuiMouseCursor_Hand:
            return SDL_SYSTEM_CURSOR_POINTER;
        case ImGuiMouseCursor_Wait:
            return SDL_SYSTEM_CURSOR_WAIT;
        case ImGuiMouseCursor_Progress:
            return SDL_SYSTEM_CURSOR_PROGRESS;
        case ImGuiMouseCursor_NotAllowed:
            return SDL_SYSTEM_CURSOR_NOT_ALLOWED;
        default:
            return SDL_SYSTEM_CURSOR_DEFAULT;
    }
}

static std::optional<UiEvent> event_from_sdl(const SDL_Event& event) {
    UiEvent result = UiEvent::make(EventType::Cancel);

    switch (event.type) {
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP:
            result.type = event.type == SDL_EVENT_KEY_DOWN ? EventType::KeyDown : EventType::KeyUp;
            result.key = ImGui_ImplSDL3_KeyEventToImGuiKey(event.key.key, event.key.scancode);
            break;
        case SDL_EVENT_TEXT_INPUT:
            result.type = EventType::TextInput;
            result.text = event.text.text != nullptr ? event.text.text : "";
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
            result.type = event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ? EventType::PointerDown : EventType::PointerUp;
            result.position = {event.button.x, event.button.y};
            result.button = pointer_button(event.button.button);
            break;
        case SDL_EVENT_MOUSE_MOTION:
            result.type = EventType::PointerMove;
            result.position = {event.motion.x, event.motion.y};
            break;
        case SDL_EVENT_MOUSE_WHEEL:
            result.type = EventType::Scroll;
            result.position = {event.wheel.mouse_x, event.wheel.mouse_y};
            result.scroll = {
                event.wheel.x * constants::SCROLL_WHEEL_SCALE,
                event.wheel.y * constants::SCROLL_WHEEL_SCALE,
            };
            break;
        default:
            return std::nullopt;
    }

    return result;
}

SdlBackend::SdlBackend(SDL_Window* window, SDL_GLContext context) : m_window(window), m_context(context) {}

SdlBackend::~SdlBackend() {
    if (m_mouse_cursor != nullptr) {
        SDL_DestroyCursor(m_mouse_cursor);
    }
}

void SdlBackend::set_mouse_cursor(ImGuiMouseCursor cursor) {
    if (cursor == ImGuiMouseCursor_None) {
        m_mouse_cursor_type = cursor;
        SDL_HideCursor();
        return;
    }

    if (cursor == m_mouse_cursor_type && m_mouse_cursor != nullptr) {
        SDL_ShowCursor();
        return;
    }

    SDL_Cursor* next_cursor = SDL_CreateSystemCursor(system_cursor(cursor));
    if (next_cursor == nullptr) {
        return;
    }

    SDL_SetCursor(next_cursor);
    SDL_ShowCursor();
    SDL_DestroyCursor(m_mouse_cursor);
    m_mouse_cursor = next_cursor;
    m_mouse_cursor_type = cursor;
}

bool SdlBackend::initialize() {
    if (m_window == nullptr || m_context == nullptr) {
        return false;
    }

    SDL_GL_MakeCurrent(m_window, m_context);
    if (gladLoadGL(load_opengl) == 0 || !GLAD_GL_VERSION_3_3) {
        SDL_Log("OpenGL 3.3 or newer is required");
        return false;
    }
    return true;
}

void SdlBackend::process_events(Surface& surface) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        process_event(surface, event);
    }
}

void SdlBackend::register_effects(EffectRegistry& effects) {
    register_opengl_effects(effects);
}

bool SdlBackend::initialize_imgui() {
    if (!ImGui_ImplSDL3_InitForOpenGL(m_window, m_context)) {
        return false;
    }

    if (!ImGui_ImplOpenGL3_Init(nullptr)) {
        ImGui_ImplSDL3_Shutdown();
        return false;
    }

    m_imgui_initialized = true;
    return true;
}

void SdlBackend::shutdown_imgui() {
    if (!m_imgui_initialized) {
        return;
    }

    SDL_GL_MakeCurrent(m_window, m_context);
    m_gpu_timer.shutdown();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    m_imgui_initialized = false;
}

void SdlBackend::begin_frame(Color clear) {
    SDL_GL_MakeCurrent(m_window, m_context);
    const ImVec2 size = display_size();
    glViewport(0, 0, static_cast<int>(size.x), static_cast<int>(size.y));
    const ImVec4 clear_color = clear.rgba();
    glClearColor(clear_color.x, clear_color.y, clear_color.z, clear_color.w);
    glClear(GL_COLOR_BUFFER_BIT);

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
}

std::optional<double> SdlBackend::render(ImDrawData* draw_data, bool profile_gpu) {
    const std::optional<double> gpu_ms = m_gpu_timer.begin(profile_gpu);
    ImGui_ImplOpenGL3_RenderDrawData(draw_data);
    m_gpu_timer.end();
    SDL_GL_SwapWindow(m_window);
    return gpu_ms;
}

float SdlBackend::content_scale() const {
    const SDL_DisplayID display = SDL_GetDisplayForWindow(m_window);
    return display == 0 ? 1.0F : SDL_GetDisplayContentScale(display);
}

uint64_t SdlBackend::window_id() const {
    return m_window == nullptr ? 0 : SDL_GetWindowID(m_window);
}

ImVec2 SdlBackend::display_size() const {
    if (m_window == nullptr) {
        return {};
    }

    int width = 0;
    int height = 0;
    SDL_GetWindowSizeInPixels(m_window, &width, &height);
    return {static_cast<float>(width), static_cast<float>(height)};
}

bool SdlBackend::process_event(Surface& surface, const SDL_Event& event) {
    if (event.type == SDL_EVENT_QUIT) {
        surface.exit();
        return true;
    }

    const SDL_WindowID window_id = event_window_id(event);
    if (window_id != 0 && window_id != SDL_GetWindowID(m_window)) {
        return false;
    }

    if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
        surface.exit();
        return true;
    }

    const ImGuiContextScope scope(surface.imgui_context());
    std::optional<UiEvent> translated = event_from_sdl(event);

    bool handled = false;
    bool native_input_blocked = false;
    if (translated.has_value()) {
        handled = surface.dispatch(*translated);
        native_input_blocked = translated->native_input_blocked;
    }

    // keep the coordinate visible to imgui even when the framework consumes the button event.
    if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_MOUSE_BUTTON_UP) {
        ImGui::GetIO().AddMousePosEvent(event.button.x, event.button.y);
    }

    const bool native_drag_active = ImGui::IsAnyItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Left);
    const bool blocked_pointer_move = native_input_blocked && event.type == SDL_EVENT_MOUSE_MOTION;

    // hide blocked motion from native hover unless a native left-button drag is already active.
    if (blocked_pointer_move && !native_drag_active) {
        ImGui::GetIO().AddMousePosEvent(-FLT_MAX, -FLT_MAX);
    }

    // scale the copied wheel event without modifying the caller's event.
    SDL_Event imgui_event = event;
    if (imgui_event.type == SDL_EVENT_MOUSE_WHEEL) {
        imgui_event.wheel.x *= constants::SCROLL_WHEEL_SCALE;
        imgui_event.wheel.y *= constants::SCROLL_WHEEL_SCALE;
    }

    // forward consumed events unless native input is blocked. blocked releases still clear native controls,
    // and motion still reaches an active drag after it crosses a blocking layer.
    if (!native_input_blocked || event.type == SDL_EVENT_MOUSE_BUTTON_UP || (blocked_pointer_move && native_drag_active)) {
        ImGui_ImplSDL3_ProcessEvent(&imgui_event);
    }
    return handled;
}
