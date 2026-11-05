#include <imgui-ui/backends/raylib/backend.hpp>

#include <imgui-ui/constants.hpp>
#include <imgui-ui/imgui/context-scope.hpp>
#include <imgui-ui/imgui/effects/opengl.hpp>
#include <imgui-ui/surface.hpp>

#include <external/glad.h>
#include <imgui_impl_opengl3.h>
#include <imgui_internal.h>
#include <raylib.h>
#include <rlgl.h>

#include <algorithm>
#include <array>
#include <cfloat>
#include <string>
#include <utility>

using namespace ui;

struct RaylibKeyMapping {
    int raylib_key;
    ImGuiKey imgui_key;
};

static int raylib_mouse_cursor(ImGuiMouseCursor cursor) {
    switch (cursor) {
        case ImGuiMouseCursor_TextInput:
            return MOUSE_CURSOR_IBEAM;
        case ImGuiMouseCursor_ResizeAll:
            return MOUSE_CURSOR_RESIZE_ALL;
        case ImGuiMouseCursor_ResizeNS:
            return MOUSE_CURSOR_RESIZE_NS;
        case ImGuiMouseCursor_ResizeEW:
            return MOUSE_CURSOR_RESIZE_EW;
        case ImGuiMouseCursor_ResizeNESW:
            return MOUSE_CURSOR_RESIZE_NESW;
        case ImGuiMouseCursor_ResizeNWSE:
            return MOUSE_CURSOR_RESIZE_NWSE;
        case ImGuiMouseCursor_Hand:
            return MOUSE_CURSOR_POINTING_HAND;
        case ImGuiMouseCursor_NotAllowed:
            return MOUSE_CURSOR_NOT_ALLOWED;
        default:
            return MOUSE_CURSOR_DEFAULT;
    }
}

static constexpr std::array RAYLIB_KEYS = {
    RaylibKeyMapping{KEY_TAB, ImGuiKey_Tab},
    RaylibKeyMapping{KEY_LEFT, ImGuiKey_LeftArrow},
    RaylibKeyMapping{KEY_RIGHT, ImGuiKey_RightArrow},
    RaylibKeyMapping{KEY_UP, ImGuiKey_UpArrow},
    RaylibKeyMapping{KEY_DOWN, ImGuiKey_DownArrow},
    RaylibKeyMapping{KEY_PAGE_UP, ImGuiKey_PageUp},
    RaylibKeyMapping{KEY_PAGE_DOWN, ImGuiKey_PageDown},
    RaylibKeyMapping{KEY_HOME, ImGuiKey_Home},
    RaylibKeyMapping{KEY_END, ImGuiKey_End},
    RaylibKeyMapping{KEY_INSERT, ImGuiKey_Insert},
    RaylibKeyMapping{KEY_DELETE, ImGuiKey_Delete},
    RaylibKeyMapping{KEY_BACKSPACE, ImGuiKey_Backspace},
    RaylibKeyMapping{KEY_SPACE, ImGuiKey_Space},
    RaylibKeyMapping{KEY_ENTER, ImGuiKey_Enter},
    RaylibKeyMapping{KEY_ESCAPE, ImGuiKey_Escape},
    RaylibKeyMapping{KEY_APOSTROPHE, ImGuiKey_Apostrophe},
    RaylibKeyMapping{KEY_COMMA, ImGuiKey_Comma},
    RaylibKeyMapping{KEY_MINUS, ImGuiKey_Minus},
    RaylibKeyMapping{KEY_PERIOD, ImGuiKey_Period},
    RaylibKeyMapping{KEY_SLASH, ImGuiKey_Slash},
    RaylibKeyMapping{KEY_SEMICOLON, ImGuiKey_Semicolon},
    RaylibKeyMapping{KEY_EQUAL, ImGuiKey_Equal},
    RaylibKeyMapping{KEY_LEFT_BRACKET, ImGuiKey_LeftBracket},
    RaylibKeyMapping{KEY_BACKSLASH, ImGuiKey_Backslash},
    RaylibKeyMapping{KEY_RIGHT_BRACKET, ImGuiKey_RightBracket},
    RaylibKeyMapping{KEY_GRAVE, ImGuiKey_GraveAccent},
    RaylibKeyMapping{KEY_CAPS_LOCK, ImGuiKey_CapsLock},
    RaylibKeyMapping{KEY_SCROLL_LOCK, ImGuiKey_ScrollLock},
    RaylibKeyMapping{KEY_NUM_LOCK, ImGuiKey_NumLock},
    RaylibKeyMapping{KEY_PRINT_SCREEN, ImGuiKey_PrintScreen},
    RaylibKeyMapping{KEY_PAUSE, ImGuiKey_Pause},
    RaylibKeyMapping{KEY_KP_0, ImGuiKey_Keypad0},
    RaylibKeyMapping{KEY_KP_1, ImGuiKey_Keypad1},
    RaylibKeyMapping{KEY_KP_2, ImGuiKey_Keypad2},
    RaylibKeyMapping{KEY_KP_3, ImGuiKey_Keypad3},
    RaylibKeyMapping{KEY_KP_4, ImGuiKey_Keypad4},
    RaylibKeyMapping{KEY_KP_5, ImGuiKey_Keypad5},
    RaylibKeyMapping{KEY_KP_6, ImGuiKey_Keypad6},
    RaylibKeyMapping{KEY_KP_7, ImGuiKey_Keypad7},
    RaylibKeyMapping{KEY_KP_8, ImGuiKey_Keypad8},
    RaylibKeyMapping{KEY_KP_9, ImGuiKey_Keypad9},
    RaylibKeyMapping{KEY_KP_DECIMAL, ImGuiKey_KeypadDecimal},
    RaylibKeyMapping{KEY_KP_DIVIDE, ImGuiKey_KeypadDivide},
    RaylibKeyMapping{KEY_KP_MULTIPLY, ImGuiKey_KeypadMultiply},
    RaylibKeyMapping{KEY_KP_SUBTRACT, ImGuiKey_KeypadSubtract},
    RaylibKeyMapping{KEY_KP_ADD, ImGuiKey_KeypadAdd},
    RaylibKeyMapping{KEY_KP_ENTER, ImGuiKey_KeypadEnter},
    RaylibKeyMapping{KEY_KP_EQUAL, ImGuiKey_KeypadEqual},
    RaylibKeyMapping{KEY_LEFT_SHIFT, ImGuiKey_LeftShift},
    RaylibKeyMapping{KEY_LEFT_CONTROL, ImGuiKey_LeftCtrl},
    RaylibKeyMapping{KEY_LEFT_ALT, ImGuiKey_LeftAlt},
    RaylibKeyMapping{KEY_LEFT_SUPER, ImGuiKey_LeftSuper},
    RaylibKeyMapping{KEY_RIGHT_SHIFT, ImGuiKey_RightShift},
    RaylibKeyMapping{KEY_RIGHT_CONTROL, ImGuiKey_RightCtrl},
    RaylibKeyMapping{KEY_RIGHT_ALT, ImGuiKey_RightAlt},
    RaylibKeyMapping{KEY_RIGHT_SUPER, ImGuiKey_RightSuper},
    RaylibKeyMapping{KEY_KB_MENU, ImGuiKey_Menu},
    RaylibKeyMapping{KEY_ZERO, ImGuiKey_0},
    RaylibKeyMapping{KEY_ONE, ImGuiKey_1},
    RaylibKeyMapping{KEY_TWO, ImGuiKey_2},
    RaylibKeyMapping{KEY_THREE, ImGuiKey_3},
    RaylibKeyMapping{KEY_FOUR, ImGuiKey_4},
    RaylibKeyMapping{KEY_FIVE, ImGuiKey_5},
    RaylibKeyMapping{KEY_SIX, ImGuiKey_6},
    RaylibKeyMapping{KEY_SEVEN, ImGuiKey_7},
    RaylibKeyMapping{KEY_EIGHT, ImGuiKey_8},
    RaylibKeyMapping{KEY_NINE, ImGuiKey_9},
    RaylibKeyMapping{KEY_A, ImGuiKey_A},
    RaylibKeyMapping{KEY_B, ImGuiKey_B},
    RaylibKeyMapping{KEY_C, ImGuiKey_C},
    RaylibKeyMapping{KEY_D, ImGuiKey_D},
    RaylibKeyMapping{KEY_E, ImGuiKey_E},
    RaylibKeyMapping{KEY_F, ImGuiKey_F},
    RaylibKeyMapping{KEY_G, ImGuiKey_G},
    RaylibKeyMapping{KEY_H, ImGuiKey_H},
    RaylibKeyMapping{KEY_I, ImGuiKey_I},
    RaylibKeyMapping{KEY_J, ImGuiKey_J},
    RaylibKeyMapping{KEY_K, ImGuiKey_K},
    RaylibKeyMapping{KEY_L, ImGuiKey_L},
    RaylibKeyMapping{KEY_M, ImGuiKey_M},
    RaylibKeyMapping{KEY_N, ImGuiKey_N},
    RaylibKeyMapping{KEY_O, ImGuiKey_O},
    RaylibKeyMapping{KEY_P, ImGuiKey_P},
    RaylibKeyMapping{KEY_Q, ImGuiKey_Q},
    RaylibKeyMapping{KEY_R, ImGuiKey_R},
    RaylibKeyMapping{KEY_S, ImGuiKey_S},
    RaylibKeyMapping{KEY_T, ImGuiKey_T},
    RaylibKeyMapping{KEY_U, ImGuiKey_U},
    RaylibKeyMapping{KEY_V, ImGuiKey_V},
    RaylibKeyMapping{KEY_W, ImGuiKey_W},
    RaylibKeyMapping{KEY_X, ImGuiKey_X},
    RaylibKeyMapping{KEY_Y, ImGuiKey_Y},
    RaylibKeyMapping{KEY_Z, ImGuiKey_Z},
    RaylibKeyMapping{KEY_F1, ImGuiKey_F1},
    RaylibKeyMapping{KEY_F2, ImGuiKey_F2},
    RaylibKeyMapping{KEY_F3, ImGuiKey_F3},
    RaylibKeyMapping{KEY_F4, ImGuiKey_F4},
    RaylibKeyMapping{KEY_F5, ImGuiKey_F5},
    RaylibKeyMapping{KEY_F6, ImGuiKey_F6},
    RaylibKeyMapping{KEY_F7, ImGuiKey_F7},
    RaylibKeyMapping{KEY_F8, ImGuiKey_F8},
    RaylibKeyMapping{KEY_F9, ImGuiKey_F9},
    RaylibKeyMapping{KEY_F10, ImGuiKey_F10},
    RaylibKeyMapping{KEY_F11, ImGuiKey_F11},
    RaylibKeyMapping{KEY_F12, ImGuiKey_F12},
};

static Key core_key(int key) {
    switch (key) {
        case KEY_ESCAPE:
            return Key::Escape;
        case KEY_ENTER:
            return Key::Enter;
        case KEY_TAB:
            return Key::Tab;
        case KEY_LEFT:
            return Key::Left;
        case KEY_RIGHT:
            return Key::Right;
        case KEY_UP:
            return Key::Up;
        case KEY_DOWN:
            return Key::Down;
        case KEY_SPACE:
            return Key::Space;
        default:
            return Key::Unknown;
    }
}

static ::Color raylib_color(ImVec4 color) {
    return {
        static_cast<unsigned char>(std::clamp(color.x, 0.0F, 1.0F) * 255.0F),
        static_cast<unsigned char>(std::clamp(color.y, 0.0F, 1.0F) * 255.0F),
        static_cast<unsigned char>(std::clamp(color.z, 0.0F, 1.0F) * 255.0F),
        static_cast<unsigned char>(std::clamp(color.w, 0.0F, 1.0F) * 255.0F),
    };
}

static void
dispatch_pointer(Surface& surface, EventType type, ImVec2 position, PointerButton button, bool& native_input_blocked) {
    UiEvent event = UiEvent::make(type);
    event.position = position;
    event.button = button;
    surface.dispatch(event);
    native_input_blocked |= event.native_input_blocked;
}

bool RaylibBackend::initialize() {
    return IsWindowReady();
}

void RaylibBackend::register_effects(EffectRegistry& effects) {
    register_opengl_effects(effects);
}

bool RaylibBackend::initialize_imgui() {
    if (!GLAD_GL_VERSION_3_3) {
        TraceLog(LOG_ERROR, "OpenGL 3.3 or newer is required");
        return false;
    }

    m_imgui_initialized = ImGui_ImplOpenGL3_Init(nullptr);
    return m_imgui_initialized;
}

void RaylibBackend::shutdown_imgui() {
    if (!m_imgui_initialized) return;
    m_gpu_timer.shutdown();
    ImGui_ImplOpenGL3_Shutdown();
    m_imgui_initialized = false;
}

void RaylibBackend::begin_frame(ui::Color clear_color) {
    BeginDrawing();
    ClearBackground(raylib_color(clear_color.rgba()));

    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = display_size();
    const Vector2 scale = GetWindowScaleDPI();
    io.DisplayFramebufferScale = {scale.x, scale.y};
    io.DeltaTime = std::max(GetFrameTime(), 1.0F / 1000.0F);
    ImGui_ImplOpenGL3_NewFrame();
}

void RaylibBackend::set_mouse_cursor(ImGuiMouseCursor cursor) {
    m_mouse_cursor = cursor;
}

void RaylibBackend::apply_mouse_cursor() const {
    if (m_mouse_cursor == ImGuiMouseCursor_None) {
        HideCursor();
        return;
    }

    SetMouseCursor(raylib_mouse_cursor(m_mouse_cursor));
    ShowCursor();
}

void RaylibBackend::render(ImDrawData* draw_data) {
    render_profiled(draw_data, false);
}

std::optional<double> RaylibBackend::render_profiled(ImDrawData* draw_data, bool profile_gpu) {
    rlDrawRenderBatchActive();
    const std::optional<double> gpu_ms = m_gpu_timer.begin(profile_gpu);
    ImGui_ImplOpenGL3_RenderDrawData(draw_data);
    m_gpu_timer.end();
    apply_mouse_cursor();
    // enddrawing swaps buffers and polls input for the next frame. call it once per application frame.
    EndDrawing();
    return gpu_ms;
}

float RaylibBackend::content_scale() const {
    return IsWindowReady() ? GetWindowScaleDPI().x : 1.0F;
}

uint64_t RaylibBackend::window_id() const {
    return IsWindowReady() ? 1 : 0;
}

ImVec2 RaylibBackend::display_size() const {
    return IsWindowReady() ? ImVec2{static_cast<float>(GetScreenWidth()), static_cast<float>(GetScreenHeight())} : ImVec2{};
}

void RaylibBackend::process_events(Surface& surface) {
    if (WindowShouldClose()) {
        surface.exit();
        return;
    }

    const ImGuiContextScope scope(surface.imgui_context());
    const Vector2 mouse = GetMousePosition();
    const ImVec2 mouse_position = {mouse.x, mouse.y};
    const ImVec2 viewport_size = display_size();
    const bool pointer_inside = mouse.x >= 0.0F && mouse.y >= 0.0F && mouse.x < viewport_size.x && mouse.y < viewport_size.y;
    const ImVec2 input_position = pointer_inside ? mouse_position : ImVec2{-FLT_MAX, -FLT_MAX};

    ImGuiIO& io = ImGui::GetIO();

    bool native_input_blocked = false;
    bool pointer_move_blocked = false;
    // redispatch pointer motion when coordinates change or debugger inspection needs a new target.
    if (surface.debugger_blocks_pointer_input() || !m_has_pointer_position || mouse_position.x != m_pointer_position.x ||
        mouse_position.y != m_pointer_position.y) {
        dispatch_pointer(surface, EventType::PointerMove, input_position, PointerButton::None, pointer_move_blocked);
        native_input_blocked |= pointer_move_blocked;
        m_pointer_position = mouse_position;
        m_has_pointer_position = true;
    }

    const bool native_drag_active = ImGui::IsAnyItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Left);
    if (pointer_move_blocked && !native_drag_active) {
        io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
    } else {
        io.AddMousePosEvent(input_position.x, input_position.y);
    }

    constexpr std::array mouse_buttons = {
        std::pair{MOUSE_BUTTON_LEFT, PointerButton::Left},
        std::pair{MOUSE_BUTTON_RIGHT, PointerButton::Right},
        std::pair{MOUSE_BUTTON_MIDDLE, PointerButton::Middle},
    };

    for (std::size_t index = 0; index < mouse_buttons.size(); ++index) {
        const auto [raylib_button, core_button] = mouse_buttons[index];

        if (IsMouseButtonPressed(raylib_button)) {
            dispatch_pointer(surface, EventType::PointerDown, mouse_position, core_button, native_input_blocked);
        }

        const bool released = IsMouseButtonReleased(raylib_button);
        if (released) {
            dispatch_pointer(surface, EventType::PointerUp, mouse_position, core_button, native_input_blocked);
        }

        // a release clears an active native imgui item even when the retained input layer consumes the pointer event.
        if (!native_input_blocked || released) io.AddMouseButtonEvent(static_cast<int>(index), IsMouseButtonDown(raylib_button));
    }

    const Vector2 wheel = GetMouseWheelMoveV();
    if (wheel.x != 0.0F || wheel.y != 0.0F) {
        const ImVec2 scroll = {wheel.x * constants::SCROLL_WHEEL_SCALE, wheel.y * constants::SCROLL_WHEEL_SCALE};
        UiEvent event = UiEvent::make(EventType::Scroll);
        event.position = mouse_position;
        event.scroll = scroll;
        surface.dispatch(event);
        native_input_blocked |= event.native_input_blocked;
        if (!native_input_blocked) io.AddMouseWheelEvent(scroll.x, scroll.y);
    }

    io.AddKeyEvent(ImGuiMod_Ctrl, IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL));
    io.AddKeyEvent(ImGuiMod_Shift, IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT));
    io.AddKeyEvent(ImGuiMod_Alt, IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_RIGHT_ALT));
    io.AddKeyEvent(ImGuiMod_Super, IsKeyDown(KEY_LEFT_SUPER) || IsKeyDown(KEY_RIGHT_SUPER));

    for (const RaylibKeyMapping& mapping : RAYLIB_KEYS) {
        io.AddKeyEvent(mapping.imgui_key, IsKeyDown(mapping.raylib_key));
        const bool pressed = IsKeyPressed(mapping.raylib_key);
        const bool released = IsKeyReleased(mapping.raylib_key);
        if (!pressed && !released) continue;

        UiEvent event = UiEvent::make(pressed ? EventType::KeyDown : EventType::KeyUp);
        event.key = core_key(mapping.raylib_key);
        surface.dispatch(event);
    }

    for (int codepoint = GetCharPressed(); codepoint > 0; codepoint = GetCharPressed()) {
        io.AddInputCharacter(static_cast<unsigned int>(codepoint));
        UiEvent event = UiEvent::make(EventType::TextInput);
        char utf8[5];
        ImTextCharToUtf8(utf8, static_cast<unsigned int>(codepoint));
        event.text = utf8;
        surface.dispatch(event);
    }
}
