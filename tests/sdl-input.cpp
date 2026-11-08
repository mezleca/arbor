#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <imgui-ui/backends/opengl/texture-loader.hpp>
#include <imgui-ui/backends/sdl/backend.hpp>
#include <imgui-ui/diagnostics/debugger.hpp>
#include <imgui-ui/imgui/context-scope.hpp>
#include <imgui-ui/layout/container.hpp>
#include <imgui-ui/layout/layer-container.hpp>
#include <imgui-ui/layout/resizable-container.hpp>
#include <imgui-ui/layout/tree-container.hpp>
#include <imgui-ui/layout/virtual-layout.hpp>
#include <imgui-ui/surface.hpp>
#include <imgui-ui/widgets/button.hpp>
#include <imgui-ui/widgets/checkbox.hpp>
#include <imgui-ui/widgets/dropdown.hpp>
#include <imgui-ui/widgets/file-dialog.hpp>
#include <imgui-ui/widgets/image.hpp>
#include <imgui-ui/widgets/number-input.hpp>
#include <imgui-ui/widgets/text.hpp>
#include <imgui-ui/widgets/text-input.hpp>

#include <SDL3/SDL.h>
#include <glad/gl.h>
#include <imgui.h>
#include <imgui_internal.h>

#include "imgui-context.hpp"
#include <imgui_impl_opengl3.h>

#include <algorithm>
#include <array>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

class SdlVideoSession final {
public:
    explicit SdlVideoSession(ImVec2 size) {
        REQUIRE(SDL_Init(SDL_INIT_VIDEO));

        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);

        m_window = SDL_CreateWindow(
            "imgui-ui test", static_cast<int>(size.x), static_cast<int>(size.y), SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN
        );
        m_context = m_window == nullptr ? nullptr : SDL_GL_CreateContext(m_window);
        bool supports_opengl = m_context != nullptr;
        if (supports_opengl) {
            SDL_GL_MakeCurrent(m_window, m_context);
            supports_opengl = gladLoadGL(SDL_GL_GetProcAddress) != 0 && GLAD_GL_VERSION_3_3;
        }

        if (!supports_opengl) {
            SKIP("OpenGL 3.3 is unavailable on this runner");
        }
    }

    ~SdlVideoSession() {
        if (m_context != nullptr) SDL_GL_DestroyContext(m_context);
        if (m_window != nullptr) SDL_DestroyWindow(m_window);
        SDL_Quit();
    }

    SDL_Window* window() const {
        return m_window;
    }

    SDL_GLContext context() const {
        return m_context;
    }

private:
    SDL_Window* m_window = nullptr;
    SDL_GLContext m_context = nullptr;
};

static bool process_sdl_event(ui::Surface& surface, const SDL_Event& event) {
    return static_cast<ui::SdlBackend&>(surface.backend()).process_event(surface, event);
}

static bool send_sdl_pointer(ui::Surface& surface, SDL_EventType type, ImVec2 position) {
    SDL_Event event{};
    event.type = type;
    if (type == SDL_EVENT_MOUSE_MOTION) {
        event.motion.windowID = surface.backend().window_id();
        event.motion.x = position.x;
        event.motion.y = position.y;
    } else {
        event.button.windowID = surface.backend().window_id();
        event.button.x = position.x;
        event.button.y = position.y;
        event.button.button = SDL_BUTTON_LEFT;
    }
    return process_sdl_event(surface, event);
}

static std::unique_ptr<ui::SdlBackend> make_sdl_backend(SdlVideoSession& session) {
    return std::make_unique<ui::SdlBackend>(session.window(), session.context());
}

static void draw_frame(ui::Surface& surface, std::optional<float> delta_time = std::nullopt) {
    surface.begin_frame();
    surface.update(delta_time.value_or(ImGui::GetIO().DeltaTime));
    surface.draw();
    ImGui::Render();
    ImDrawData* draw_data = ImGui::GetDrawData();
    REQUIRE(draw_data != nullptr);
    ImGui_ImplOpenGL3_RenderDrawData(draw_data);
    glFinish();
    surface.end_frame();
}

TEST_CASE("disabled widgets keep their style and native cursor across clicks", "[cursor][input][regression]") {
    SdlVideoSession sdl({320.0f, 180.0f});
    ui::Runtime runtime;
    ui::Surface surface(runtime, {.backend = make_sdl_backend(sdl)});
    const auto context = ui_test::prepare_surface(surface);
    auto& button = surface.root().add<ui::ButtonWidget>("disabled");
    if (GENERATE(false, true)) {
        button.set_enabled(false);
    } else {
        button.set_visual_style(ui::StyleType::DISABLED);
    }
    int clicks = 0;
    button.on_click([&] { ++clicks; });
    ui_test::draw_surface(surface);
    const ImVec2 position = ui_test::center(button.layout().visual_rect());
    send_sdl_pointer(surface, SDL_EVENT_MOUSE_MOTION, position);
    ui_test::draw_surface(surface);
    SDL_Cursor* cursor = SDL_GetCursor();
    for (SDL_EventType type : {SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_EVENT_MOUSE_BUTTON_UP}) {
        send_sdl_pointer(surface, type, position);
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        surface.begin_frame();
        SDL_Cursor* frame_cursor = SDL_GetCursor();
        surface.update(0.1f);
        surface.draw();
        surface.end_frame();
        CHECK(frame_cursor == cursor);
        CHECK(SDL_GetCursor() == cursor);
        CHECK(button.style_type() == ui::StyleType::DISABLED);
        CHECK(ImGui::GetMouseCursor() == ImGuiMouseCursor_NotAllowed);
        CHECK_FALSE(button.input_state().hovered);
        CHECK_FALSE(button.input_state().active);
        CHECK_FALSE(button.input_state().focused);
    }
    REQUIRE(clicks == 0);
    button.set_enabled(true);
    REQUIRE(button.style_type() != ui::StyleType::DISABLED);
    REQUIRE(button.accepts_input());
}

TEST_CASE("sdl cursor changes replace the native cursor and repeated requests reuse it", "[cursor][regression]") {
    SdlVideoSession sdl({128.0F, 128.0F});
    ui::SdlBackend backend(sdl.window(), sdl.context());
    backend.set_mouse_cursor(ImGuiMouseCursor_Arrow);
    SDL_Cursor* arrow = SDL_GetCursor();
    REQUIRE(arrow != nullptr);

    backend.set_mouse_cursor(ImGuiMouseCursor_Hand);
    SDL_Cursor* hand = SDL_GetCursor();
    REQUIRE(hand != nullptr);
    REQUIRE(hand != arrow);
    backend.set_mouse_cursor(ImGuiMouseCursor_Hand);
    CHECK(SDL_GetCursor() == hand);

    backend.set_mouse_cursor(ImGuiMouseCursor_None);
    CHECK_FALSE(SDL_CursorVisible());
    backend.set_mouse_cursor(ImGuiMouseCursor_Arrow);
    CHECK(SDL_CursorVisible());
}

TEST_CASE("opengl effects retain per-surface state across alternation resize and shutdown", "[render][cleanup]") {
    SdlVideoSession sdl({128.0F, 128.0F});
    ui::Runtime runtime;
    ui::Surface first(runtime, {.backend = std::make_unique<ui::SdlBackend>(sdl.window(), sdl.context())});
    const auto configure = [](ui::Surface& surface, const ui::Color& color) {
        auto& background = surface.root().add<ui::BoxWidget>("background", ui::LayoutSize{ui::grow(), ui::grow()});
        background.configure_all_styles([color](ui::Style& style) { style.background_color(color); });
        auto& panel = surface.root().add<ui::Container>("effects");
        panel.set_layout({
            .size = {ui::px(40.0F), ui::px(40.0F)},
            .placement = {.offset = {44.0F, 44.0F}},
            .in_flow = false,
        });
        panel.configure_all_styles([](ui::Style& style) {
            style.blur(5).box_shadow({.blur = 10.0F, .spread = 8.0F, .color = ui::rgb(0.0F, 0.0F, 0.0F)});
        });
    };
    const auto check_frame = [](ui::Surface& surface, int channel) {
        const ui::ImGuiContextScope context(surface.imgui_context());
        surface.begin_frame();
        surface.update(1.0F);
        surface.draw();
        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glFinish();
        GLint viewport[4]{};
        glGetIntegerv(GL_VIEWPORT, viewport);
        unsigned char pixel[4]{};
        glReadPixels(64, viewport[3] - 64, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
        CHECK(pixel[channel] > 200);
        CHECK(pixel[1 - channel] < 32);
        CHECK(glGetError() == GL_NO_ERROR);
        surface.end_frame();
    };

    configure(first, ui::rgb(1.0F, 0.0F, 0.0F));
    {
        ui::Surface second(runtime, {.backend = std::make_unique<ui::SdlBackend>(sdl.window(), sdl.context())});
        configure(second, ui::rgb(0.0F, 1.0F, 0.0F));
        for (int frame = 0; frame < 3; ++frame) {
            check_frame(first, 0);
            check_frame(second, 1);
        }
        REQUIRE(SDL_SetWindowSize(sdl.window(), 160, 160));
        check_frame(first, 0);
        check_frame(second, 1);
    }
    check_frame(first, 0);
}

static void click_tree(ui::Surface& surface, const ui::TreeContainer& tree) {
    const ui::ImGuiContextScope context(surface.imgui_context());
    const ui::Rect rect = tree.layout().visual_rect();
    ImGui::GetIO().AddMousePosEvent(rect.min.x + 4.0F, rect.min.y + 4.0F);
    ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, true);
}

static void release_click(ui::Surface& surface) {
    const ui::ImGuiContextScope context(surface.imgui_context());
    ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, false);
}

TEST_CASE("opengl box shadows cover the spread outside a panel", "[render][regression]") {
    SdlVideoSession sdl({128.0F, 128.0F});
    ui::Runtime runtime;
    ui::Surface surface(runtime, {.backend = make_sdl_backend(sdl)});
    auto& panel = surface.root().add<ui::Container>("shadow-panel");
    panel.set_layout({
        .size = {ui::px(40.0F), ui::px(40.0F)},
        .placement = {.offset = {44.0F, 44.0F}},
        .in_flow = false,
    });
    panel.configure_all_styles([](ui::Style& style) {
        style.background_color(ui::rgb(1.0F, 1.0F, 1.0F))
            .box_shadow({
                .blur = 20.0F,
                .spread = 20.0F,
                .color = ui::rgb(0.0F, 0.0F, 0.0F),
            });
    });

    surface.begin_frame();
    surface.update(ImGui::GetIO().DeltaTime);
    surface.draw();
    ImGui::Render();
    ImDrawData* draw_data = ImGui::GetDrawData();
    REQUIRE(draw_data != nullptr);
    ImGui_ImplOpenGL3_RenderDrawData(draw_data);
    glFinish();

    GLint viewport[4]{};
    glGetIntegerv(GL_VIEWPORT, viewport);
    const ImVec2 panel_center = ui_test::center(panel.layout().visual_rect());
    const ImVec2 sample = {panel_center.x, panel.layout().visual_rect().min.y - 8.0F};
    const ImVec2 framebuffer_sample = {
        (sample.x - draw_data->DisplayPos.x) * draw_data->FramebufferScale.x,
        static_cast<float>(viewport[3]) - ((sample.y - draw_data->DisplayPos.y) * draw_data->FramebufferScale.y),
    };
    unsigned char pixel[4]{};
    const int pixel_x = std::clamp(static_cast<int>(framebuffer_sample.x), 0, viewport[2] - 1);
    const int pixel_y = std::clamp(static_cast<int>(framebuffer_sample.y), 0, viewport[3] - 1);
    glReadPixels(pixel_x, pixel_y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    CHECK(pixel[0] < 80);
    CHECK(pixel[1] < 80);
    CHECK(pixel[2] < 80);
    surface.end_frame();
}

TEST_CASE("opengl blur excludes content outside its rect", "[render][regression]") {
    SdlVideoSession sdl({160.0F, 160.0F});
    ui::Runtime runtime;
    ui::Surface surface(runtime, {.backend = make_sdl_backend(sdl)});
    auto& parent = surface.root().add<ui::Container>("scroll-parent");
    parent.set_layout({
        .size = {ui::px(100.0F), ui::px(100.0F)},
        .placement = {.offset = {20.0F, 20.0F}},
        .in_flow = false,
    });
    parent.configure_all_styles([](ui::Style& style) { style.background_color(ui::rgb(0.0F, 0.0F, 0.0F)); });
    auto& outside = parent.add<ui::Container>("outside");
    outside.set_layout({
        .size = {ui::px(20.0F), ui::px(60.0F)},
        .placement = {.offset = {80.0F, 0.0F}},
        .in_flow = false,
    });
    outside.configure_all_styles([](ui::Style& style) { style.background_color(ui::rgb(0.0F, 0.0F, 1.0F)); });
    auto& blurred = parent.add<ui::Container>("blurred");
    blurred.set_size({ui::px(80.0F), ui::px(60.0F)});
    blurred.configure_all_styles([](ui::Style& style) { style.background_color(ui::rgba(0.0F, 0.0F, 0.0F, 0.0F)).blur(12); });

    draw_frame(surface);
    draw_frame(surface);

    GLint viewport[4]{};
    glGetIntegerv(GL_VIEWPORT, viewport);
    const auto read_pixel = [&](ImVec2 position) {
        unsigned char pixel[4]{};
        const int pixel_x = std::clamp(static_cast<int>(position.x), 0, viewport[2] - 1);
        const int pixel_y = std::clamp(viewport[3] - static_cast<int>(position.y), 0, viewport[3] - 1);
        glReadPixels(pixel_x, pixel_y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
        return std::array<unsigned char, 4>{pixel[0], pixel[1], pixel[2], pixel[3]};
    };

    const ui::Rect blur_rect = blurred.layout().visual_rect();
    const auto pixel = read_pixel({blur_rect.max.x - 2.0F, blur_rect.min.y + 20.0F});
    CHECK(pixel[2] < 32);
}

TEST_CASE("container border stays above a child widget surface", "[render][regression]") {
    SdlVideoSession sdl({160.0F, 140.0F});
    ui::Runtime runtime;
    ui::Surface surface(runtime, {.backend = make_sdl_backend(sdl)});
    auto& parent = surface.root().add<ui::Container>("parent");
    parent.set_layout({
        .size = {ui::px(120.0F), ui::px(80.0F)},
        .placement = {.offset = {20.0F, 20.0F}},
        .in_flow = false,
    });
    parent.set_spacing(6.0F);
    parent.configure_all_styles([](ui::Style& style) {
        style.background_color(ui::rgb(1.0F, 0.0F, 0.0F))
            .border(ui::BorderEdge{1.0F, ui::BorderType::Solid})
            .border_color(ui::rgb(1.0F, 0.0F, 0.0F))
            .padding({});
    });
    auto& spacer = parent.add<ui::Container>("spacer");
    spacer.set_size({ui::grow(), ui::px(12.0F)});
    auto& dialog = parent.add<ui::FileDialogWidget>("file");
    dialog.set_size({ui::grow(), ui::px(60.0F)});
    dialog.configure_all_styles([](ui::Style& style) {
        style.background_color(ui::rgb(0.0F, 0.0F, 1.0F)).border(ui::BorderEdge{0.0F, ui::BorderType::None}).padding({});
    });

    draw_frame(surface);
    draw_frame(surface);

    const ui::Rect parent_rect = parent.layout().visual_rect();
    const ui::Rect dialog_rect = dialog.layout().visual_rect();
    REQUIRE(parent_rect.valid());
    REQUIRE(dialog_rect.valid());
    REQUIRE(dialog_rect.max.y == Catch::Approx(parent_rect.max.y - 1.0F));

    GLint viewport[4]{};
    glGetIntegerv(GL_VIEWPORT, viewport);
    unsigned char pixel[4]{};
    glReadPixels(
        static_cast<int>(dialog_rect.min.x + 20.0F), viewport[3] - static_cast<int>(parent_rect.max.y - 1.0F), 1, 1, GL_RGBA,
        GL_UNSIGNED_BYTE, pixel
    );
    CHECK(pixel[0] > 32);
    CHECK(pixel[2] < 224);
}

TEST_CASE("scrolling keeps inline overlay panels above earlier content", "[LayerContainer][render][scroll][regression]") {
    class ScrollContainer final : public ui::Container {
    public:
        ScrollContainer() : Container("scroll-overlay-parent") {
            set_size({ui::px(140.0F), ui::px(100.0F)});
            set_scrollable(true);
        }

        bool scroll_to_end = false;

    protected:
        bool paint() override {
            ImGui::SetNextWindowContentSize({140.0F, 400.0F});
            return Container::paint();
        }

        void on_draw_end() override {
            if (scroll_to_end) {
                ImGui::SetScrollY(20.0F);
            }
            Container::on_draw_end();
        }
    };

    SdlVideoSession sdl({240.0F, 180.0F});
    ui::Runtime runtime;
    ui::Surface surface(runtime, {.backend = make_sdl_backend(sdl)});
    auto& scroll = surface.root().add<ScrollContainer>();

    auto& input = scroll.add<ui::Container>("input");
    input.set_layout({
        .size = {ui::px(100.0F), ui::px(50.0F)},
        .placement = {.offset = {20.0F, 60.0F}},
        .in_flow = false,
    });
    input.configure_all_styles([](ui::Style& style) {
        style.background_color(ui::rgb(1.0F, 0.0F, 0.0F))
            .border(ui::BorderEdge{1.0F, ui::BorderType::Solid})
            .border_color(ui::rgb(1.0F, 0.0F, 0.0F))
            .padding({});
    });

    scroll.add<ui::Container>("filler").set_size({ui::px(140.0F), ui::px(400.0F)});

    auto& overlay = scroll.add<ui::LayerContainer>("overlay");
    auto& panel = overlay.add<ui::Container>("panel");
    panel.set_layout({
        .size = {ui::px(100.0F), ui::px(50.0F)},
        .placement = {.offset = {20.0F, 60.0F}},
        .in_flow = false,
    });
    panel.configure_all_styles([](ui::Style& style) {
        style.background_color(ui::rgb(0.0F, 1.0F, 0.0F))
            .border(ui::BorderEdge{1.0F, ui::BorderType::Solid})
            .border_color(ui::rgb(0.5F, 0.5F, 0.5F))
            .padding({});
    });
    auto& resize = panel.add<ui::ResizableContainer>("resize");
    resize.set_size({ui::grow(), ui::grow()});
    resize.set_resize(ui::ResizeAxes::Both).set_scrollable(true);
    resize.add<ui::Container>("row").set_size({ui::grow(), ui::px(200.0F)});

    draw_frame(surface);
    const ImVec2 initial_size = resize.layout().size();
    scroll.scroll_to_end = true;
    draw_frame(surface);
    draw_frame(surface);

    REQUIRE(panel.layout().visual_rect().valid());
    REQUIRE(resize.layout().visual_rect().valid());
    REQUIRE(resize.layout().size().x == Catch::Approx(initial_size.x));
    REQUIRE(resize.layout().size().y == Catch::Approx(initial_size.y));

    const ui::Rect panel_rect = panel.layout().visual_rect();
    const ImVec2 sample = ui_test::center(panel_rect);
    GLint viewport[4]{};
    glGetIntegerv(GL_VIEWPORT, viewport);
    unsigned char pixel[4]{};
    glReadPixels(static_cast<int>(sample.x), viewport[3] - static_cast<int>(sample.y), 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    CHECK(pixel[1] > 160);
    CHECK(pixel[0] < 96);
}

TEST_CASE("scrolled inline layers keep virtual list input separate", "[LayerContainer][VirtualLayout][input][regression]") {
    class VirtualListProbe final : public ui::VirtualLayout {
    public:
        VirtualListProbe() : VirtualLayout("virtual-list", 20.0F) {}

        ImGuiWindow* window = nullptr;
        ImRect scrollbar{};

    protected:
        void draw_children() override {
            window = ImGui::GetCurrentWindow();
            scrollbar = window->ScrollbarY ? ImGui::GetWindowScrollbarRect(window, ImGuiAxis_Y) : ImRect{};
            VirtualLayout::draw_children();
        }
    };

    SdlVideoSession sdl({900.0F, 600.0F});
    ui::Runtime runtime;
    ui::Surface surface(runtime, {.backend = make_sdl_backend(sdl)});
    auto& demo = surface.root().add<ui::Container>("demo");
    demo.set_size({ui::px(900.0F), ui::px(600.0F)});
    demo.set_scrollable(true).style().padding({12.0F, 12.0F});
    // wheel routing must not depend on wall-clock time between backend frames.
    demo.scroll().set_behaviour(ui::ScrollBehavior::Instant);
    demo.add<ui::Container>("before-list").set_size({ui::grow(), ui::px(300.0F)});

    auto& list = demo.add<VirtualListProbe>();
    list.set_size({ui::grow(), ui::px(100.0F)});
    list.scroll().set_behaviour(ui::ScrollBehavior::Instant);
    std::vector<ui::Node*> rows(1000);
    list.set_items(1000, [&list, &rows](size_t index) -> ui::Node& {
        if (rows[index] == nullptr) {
            rows[index] = &list.add<ui::Node>(std::to_string(index));
        }
        return *rows[index];
    });
    demo.add<ui::Container>("after-list").set_size({ui::grow(), ui::px(300.0F)});
    auto& overlay = demo.add<ui::LayerContainer>("demo-overlay");
    auto& panel = overlay.add<ui::Container>("overlay-panel");
    panel.set_layout({
        .size = {ui::px(180.0F), ui::px(80.0F)},
        .placement = {.anchor = ui::Anchor::TopRight, .origin = ui::Anchor::TopRight, .offset = {-20.0F, 20.0F}},
        .in_flow = false,
    });

    const auto scroll = [&surface](ImVec2 position) {
        SDL_Event event{};
        event.type = SDL_EVENT_MOUSE_WHEEL;
        event.wheel.windowID = surface.backend().window_id();
        event.wheel.mouse_x = position.x;
        event.wheel.mouse_y = position.y;
        event.wheel.y = -1.0F;
        return process_sdl_event(surface, event);
    };

    draw_frame(surface);
    REQUIRE_FALSE(send_sdl_pointer(surface, SDL_EVENT_MOUSE_MOTION, {100.0F, 240.0F}));
    draw_frame(surface);
    for (int frame = 0; frame < 6; ++frame) {
        scroll({100.0F, 240.0F});
        draw_frame(surface);
    }
    REQUIRE(list.scrollbar.GetHeight() > 0.0F);
    REQUIRE(list.layout().visual_rect().contains({100.0F, 240.0F}));
    scroll({100.0F, 240.0F});
    draw_frame(surface);
    draw_frame(surface);
    REQUIRE(list.window->Scroll.y > 0.0F);

    const ImVec2 thumb = {(list.scrollbar.Min.x + list.scrollbar.Max.x) * 0.5F, list.scrollbar.Min.y + 4.0F};

    send_sdl_pointer(surface, SDL_EVENT_MOUSE_MOTION, thumb);
    draw_frame(surface);
    REQUIRE_FALSE(send_sdl_pointer(surface, SDL_EVENT_MOUSE_BUTTON_DOWN, thumb));
    draw_frame(surface);
    REQUIRE(GImGui->ActiveId == ImGui::GetWindowScrollbarID(list.window, ImGuiAxis_Y));
    send_sdl_pointer(surface, SDL_EVENT_MOUSE_MOTION, {thumb.x, thumb.y + 40.0F});
    draw_frame(surface);
    send_sdl_pointer(surface, SDL_EVENT_MOUSE_BUTTON_UP, {thumb.x, thumb.y + 40.0F});
    draw_frame(surface);

    const float dragged_scroll = list.window->Scroll.y;

    const ImVec2 list_position = {list.layout().visual_rect().min.x + 20.0F, list.layout().visual_rect().min.y + 20.0F};
    REQUIRE_FALSE(send_sdl_pointer(surface, SDL_EVENT_MOUSE_MOTION, list_position));
    draw_frame(surface);

    scroll(list_position);
    draw_frame(surface);
    draw_frame(surface);

    REQUIRE(list.window->Scroll.y > dragged_scroll);
}

TEST_CASE("container borders stay below popup surfaces", "[render][regression]") {
    SdlVideoSession sdl({160.0F, 120.0F});
    ui::Runtime runtime;
    ui::Surface surface(runtime, {.backend = make_sdl_backend(sdl)});
    auto& file = surface.root().add<ui::FileDialogWidget>("file");
    file.set_layout({
        .size = {ui::px(120.0F), ui::px(50.0F)},
        .placement = {.offset = {20.0F, 50.0F}},
        .in_flow = false,
    });
    std::string value = "first";
    auto& dropdown = surface.root().add<ui::DropdownWidget>(
        value, std::vector<ui::DropdownOption>{{"first", "first"}, {"second", "second"}}, "", ui::LabelPlacement::Above,
        "dropdown"
    );
    dropdown.set_layout({
        .size = {ui::px(80.0F), ui::px(30.0F)},
        .placement = {.offset = {40.0F, 10.0F}},
        .in_flow = false,
    });
    draw_frame(surface, 0.1F);
    file.configure_all_styles([](ui::Style& style) {
        style.background_color(ui::rgb(0.0F, 0.0F, 0.0F))
            .border(ui::BorderEdge{20.0F, ui::BorderType::Solid})
            .border_color(ui::rgb(1.0F, 0.0F, 0.0F))
            .padding({});
    });
    dropdown.body()
        .style()
        .background_color(ui::rgb(0.0F, 1.0F, 0.0F))
        .border(ui::BorderEdge{0.0F, ui::BorderType::None})
        .padding({});
    draw_frame(surface, 0.1F);
    CHECK(file.computed_style().border_color().value.rgba().x > 0.9F);
    CHECK(dropdown.body().computed_style().background_color().value.rgba().y > 0.9F);
    dropdown.open();
    for (int frame = 0; frame < 5; ++frame) {
        draw_frame(surface, 0.1F);
    }
    CHECK(dropdown.body().opacity() > 0.99F);
    const ui::Rect file_rect = file.layout().visual_rect();
    const ImVec2 overlap = {file_rect.min.x + (file_rect.size().x * 0.5F), file_rect.min.y + 10.0F};
    REQUIRE(dropdown.body().layout().visual_rect().contains(overlap));

    GLint viewport[4]{};
    glGetIntegerv(GL_VIEWPORT, viewport);
    unsigned char pixel[4]{};
    glReadPixels(
        static_cast<int>(std::round(overlap.x)), viewport[3] - static_cast<int>(std::round(overlap.y)), 1, 1, GL_RGBA,
        GL_UNSIGNED_BYTE, pixel
    );
    CHECK(pixel[0] < 96);
    CHECK(pixel[1] > 160);
}

TEST_CASE("container borders stay below window-layer panels", "[render][regression]") {
    SdlVideoSession sdl({160.0F, 120.0F});
    ui::Runtime runtime;
    ui::Surface surface(runtime, {.backend = make_sdl_backend(sdl)});
    auto& input = surface.root().add<ui::Container>("input");
    input.set_layout({
        .size = {ui::px(120.0F), ui::px(50.0F)},
        .placement = {.offset = {20.0F, 20.0F}},
        .in_flow = false,
    });
    input.configure_all_styles([](ui::Style& style) {
        style.background_color(ui::rgb(0.0F, 0.0F, 0.0F))
            .border(ui::BorderEdge{2.0F, ui::BorderType::Solid})
            .border_color(ui::rgb(1.0F, 0.0F, 0.0F))
            .padding({});
    });

    auto& layer = surface.root().add<ui::LayerContainer>("modal-layer");
    auto& panel = layer.add<ui::Container>("modal-panel");
    panel.set_layout({
        .size = {ui::px(120.0F), ui::px(60.0F)},
        .placement = {.offset = {20.0F, 10.0F}},
        .in_flow = false,
    });
    panel.configure_all_styles([](ui::Style& style) {
        style.background_color(ui::rgb(0.0F, 1.0F, 0.0F)).border(ui::BorderEdge{0.0F, ui::BorderType::None}).padding({});
    });

    draw_frame(surface);
    draw_frame(surface);

    GLint viewport[4]{};
    glGetIntegerv(GL_VIEWPORT, viewport);
    unsigned char pixel[4]{};
    glReadPixels(70, viewport[3] - 20, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    CHECK(pixel[0] < 96);
    CHECK(pixel[1] > 160);
}

TEST_CASE("container overflow controls child surfaces", "[render][regression]") {
    SdlVideoSession sdl({160.0F, 120.0F});
    const auto sample_overflow = [&sdl](ui::Overflow overflow) {
        ui::Runtime runtime;
        ui::Surface surface(runtime, {.backend = make_sdl_backend(sdl)});
        auto& parent = surface.root().add<ui::Container>("parent");
        parent.set_layout({
            .size = {ui::px(80.0F), ui::px(60.0F)},
            .placement = {.offset = {20.0F, 20.0F}},
            .in_flow = false,
        });
        auto& child = parent.add<ui::Container>("child");
        child.set_layout({
            .size = {ui::px(40.0F), ui::px(30.0F)},
            .placement = {.offset = {70.0F, 10.0F}},
            .in_flow = false,
        });
        draw_frame(surface);
        parent.style().overflow(overflow).border(ui::BorderEdge{0.0F, ui::BorderType::None}).padding({});
        child.style().background_color(ui::rgb(0.0F, 0.0F, 1.0F)).padding({});
        for (int frame = 0; frame < 2; ++frame) {
            draw_frame(surface);
        }

        GLint viewport[4]{};
        glGetIntegerv(GL_VIEWPORT, viewport);
        unsigned char pixel[4]{};
        glReadPixels(108, viewport[3] - 40, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
        return std::array<unsigned char, 4>{pixel[0], pixel[1], pixel[2], pixel[3]};
    };

    const auto visible = sample_overflow(ui::Overflow::Visible);
    CHECK(visible[2] > 200);

    for (const ui::Overflow overflow : {ui::Overflow::Hidden, ui::Overflow::Clip}) {
        const auto clipped = sample_overflow(overflow);
        CHECK(clipped[2] < 80);
    }
}

TEST_CASE("node text follows the container overflow style", "[render][regression]") {
    SdlVideoSession sdl({200.0F, 140.0F});
    const auto count_text_outside = [&sdl](ui::Overflow overflow, bool button_node) {
        ui::Runtime runtime;
        ui::Surface surface(runtime, {.backend = make_sdl_backend(sdl)});
        const ImVec2 bounds_min = {20.0F, 20.0F};
        const ImVec2 bounds_size = {80.0F, 32.0F};

        ui::StyledNode* node = nullptr;
        if (button_node) {
            auto& button = surface.root().add<ui::ButtonWidget>(
                "MMMMMMMMMMMMMMMMMMMMMMMMMMMM", ui::LayoutSize{ui::px(bounds_size.x), ui::px(bounds_size.y)}
            );
            button.set_text_alignment({0.0F, 0.5F});
            node = &button;
        } else {
            auto& text = surface.root().add<ui::TextWidget>("MMMMMMMMMMMMMMMMMMMMMMMMMMMM");
            text.set_size({ui::px(bounds_size.x), ui::px(bounds_size.y)});
            node = &text;
        }

        node->set_layout({
            .size = {ui::px(bounds_size.x), ui::px(bounds_size.y)},
            .placement = {.offset = {bounds_min.x, bounds_min.y}},
            .in_flow = false,
        });
        node->configure_all_styles([overflow](ui::Style& style) {
            style.background_color(ui::rgb(0.0F, 0.0F, 0.0F)).padding({}).overflow(overflow);
        });

        for (int frame = 0; frame < 2; ++frame) {
            draw_frame(surface);
        }

        GLint viewport[4]{};
        glGetIntegerv(GL_VIEWPORT, viewport);
        const int x = static_cast<int>(bounds_min.x + bounds_size.x) + 4;
        const int y = static_cast<int>(bounds_min.y) + 8;
        const int width = static_cast<int>(bounds_min.x + bounds_size.x) + 60 - x;
        const int height = static_cast<int>(bounds_size.y) - 16;
        std::vector<unsigned char> pixels(static_cast<std::size_t>(width * height * 4));
        glReadPixels(x, viewport[3] - (y + height), width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());

        int bright = 0;
        for (std::size_t index = 0; index < pixels.size(); index += 4) {
            if (pixels[index] > 180 && pixels[index + 1] > 180 && pixels[index + 2] > 180) {
                ++bright;
            }
        }
        return bright;
    };

    for (const bool button_node : {true, false}) {
        CAPTURE(button_node);

        // evaluate outside CHECK so an unavailable OpenGL context skips the test instead of failing the assertion.
        const int visible = count_text_outside(ui::Overflow::Visible, button_node);
        CHECK(visible > 0);

        const int hidden = count_text_outside(ui::Overflow::Hidden, button_node);
        CHECK(hidden == 0);

        const int clipped = count_text_outside(ui::Overflow::Clip, button_node);
        CHECK(clipped == 0);
    }
}

TEST_CASE("scrolled tree bodies clip oversized child surfaces", "[TreeContainer][render][regression]") {
    class ScrollContainer final : public ui::Container {
    public:
        ScrollContainer() : Container("tree-scroll-parent") {
            set_size({ui::px(160.0F), ui::px(100.0F)});
            set_scrollable(true);
        }

        bool scroll_to_end = false;

    protected:
        bool paint() override {
            ImGui::SetNextWindowContentSize({160.0F, 360.0F});
            return Container::paint();
        }

        void on_draw_end() override {
            if (scroll_to_end) {
                ImGui::SetScrollY(20.0F);
            }
            Container::on_draw_end();
        }
    };

    SdlVideoSession sdl({240.0F, 180.0F});
    ui::Runtime runtime;
    ui::Surface surface(runtime, {.backend = make_sdl_backend(sdl)});
    auto& backdrop = surface.root().add<ui::Container>("backdrop");
    backdrop.set_layout({.size = {ui::px(240.0F), ui::px(180.0F)}, .in_flow = false});
    backdrop.configure_all_styles([](ui::Style& style) { style.background_color(ui::rgb(1.0F, 1.0F, 1.0F)); });
    auto& parent = surface.root().add<ScrollContainer>();
    parent.configure_all_styles([](ui::Style& style) { style.background_color(ui::rgb(0.0F, 1.0F, 0.0F)).padding({}); });

    auto& tree = parent.add<ui::TreeContainer>("widgets");
    auto& panel = tree.add<ui::Container>("panel");
    panel.set_size({ui::px(80.0F), ui::px(64.0F)});
    panel.configure_all_styles([](ui::Style& style) {
        style.background_color(ui::rgb(0.0F, 0.0F, 0.0F))
            .border(ui::BorderEdge{1.0F, ui::BorderType::Solid})
            .border_color(ui::rgb(1.0F, 0.0F, 0.0F))
            .padding({});
    });
    auto& child = panel.add<ui::Container>("oversized");
    child.set_size({ui::px(160.0F), ui::px(40.0F)});
    child.configure_all_styles([](ui::Style& style) {
        style.background_color(ui::rgb(0.0F, 0.0F, 1.0F)).border(ui::BorderEdge{0.0F, ui::BorderType::None}).padding({});
    });
    parent.add<ui::Container>("filler").set_size({ui::px(160.0F), ui::px(280.0F)});

    draw_frame(surface);
    click_tree(surface, tree);
    draw_frame(surface);
    release_click(surface);
    draw_frame(surface);
    parent.scroll_to_end = true;
    draw_frame(surface);
    draw_frame(surface);

    const ui::Rect panel_rect = panel.layout().visual_rect();
    const ui::Rect child_rect = child.layout().visual_rect();
    REQUIRE(panel_rect.valid());
    REQUIRE(child_rect.valid());
    REQUIRE(child_rect.max.x > panel_rect.max.x);

    GLint viewport[4]{};
    glGetIntegerv(GL_VIEWPORT, viewport);
    unsigned char pixel[4]{};
    glReadPixels(
        static_cast<int>(panel_rect.max.x + 5.0F), viewport[3] - static_cast<int>((child_rect.min.y + child_rect.max.y) * 0.5F),
        1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel
    );
    CHECK(pixel[1] > 160);
    CHECK(pixel[2] < 80);
}

TEST_CASE("nested tree viewports clip oversized child surfaces", "[TreeContainer][render][regression]") {
    SdlVideoSession sdl({240.0F, 180.0F});
    ui::Runtime runtime;
    ui::Surface surface(runtime, {.backend = make_sdl_backend(sdl)});
    auto& backdrop = surface.root().add<ui::Container>("backdrop");
    backdrop.set_layout({.size = {ui::px(240.0F), ui::px(180.0F)}, .in_flow = false});
    backdrop.configure_all_styles([](ui::Style& style) { style.background_color(ui::rgb(0.0F, 1.0F, 0.0F)); });

    auto& visual = surface.root().add<ui::Container>("visual-tests");
    visual.set_layout({
        .size = {ui::px(180.0F), ui::px(140.0F)},
        .placement = {.offset = {20.0F, 20.0F}},
        .in_flow = false,
    });
    visual.set_scrollable(true);
    visual.configure_all_styles([](ui::Style& style) { style.background_color(ui::rgb(0.0F, 1.0F, 0.0F)); });
    auto& outer = visual.add<ui::TreeContainer>("ui visual tests");
    auto& tree = outer.add<ui::TreeContainer>("widgets");
    auto& widgets = tree.add<ui::Container>("widget-view");
    widgets.set_size({ui::px(100.0F), ui::px(80.0F)});
    widgets.set_scrollable(true);
    widgets.configure_all_styles([](ui::Style& style) {
        style.background_color(ui::rgb(0.0F, 0.0F, 0.0F))
            .border(ui::BorderEdge{1.0F, ui::BorderType::Solid})
            .border_color(ui::rgb(1.0F, 0.0F, 0.0F))
            .padding({12.0F, 12.0F});
    });
    auto& child = widgets.add<ui::Container>("oversized");
    child.set_size({ui::px(240.0F), ui::px(100.0F)});
    child.configure_all_styles([](ui::Style& style) {
        style.background_color(ui::rgb(0.0F, 0.0F, 1.0F)).border(ui::BorderEdge{0.0F, ui::BorderType::None}).padding({});
    });
    widgets.add<ui::Container>("filler").set_size({ui::px(100.0F), ui::px(200.0F)});

    draw_frame(surface);
    click_tree(surface, outer);
    draw_frame(surface);
    release_click(surface);
    draw_frame(surface);
    click_tree(surface, tree);
    draw_frame(surface);
    release_click(surface);
    draw_frame(surface);

    const ui::Rect viewport_rect = widgets.layout().visual_rect();
    const ui::Rect child_rect = child.layout().visual_rect();
    REQUIRE(viewport_rect.valid());
    REQUIRE(child_rect.valid());
    REQUIRE(child_rect.max.x > viewport_rect.max.x);

    GLint viewport[4]{};
    glGetIntegerv(GL_VIEWPORT, viewport);
    unsigned char pixel[4]{};
    glReadPixels(
        static_cast<int>(viewport_rect.max.x + 5.0F),
        viewport[3] - static_cast<int>((child_rect.min.y + child_rect.max.y) * 0.5F), 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel
    );
    CAPTURE(viewport_rect.min.x, viewport_rect.max.x, viewport_rect.min.y, viewport_rect.max.y);
    CAPTURE(child_rect.min.x, child_rect.max.x, child_rect.min.y, child_rect.max.y);
    CAPTURE(pixel[0], pixel[1], pixel[2], pixel[3]);
    CHECK(pixel[2] < 80);
}

TEST_CASE("nested tree shadows escape their parent body clip", "[render][regression]") {
    SdlVideoSession sdl({200.0F, 180.0F});
    ui::Runtime runtime;
    ui::Surface surface(runtime, {.backend = make_sdl_backend(sdl)});
    auto& backdrop = surface.root().add<ui::Container>("backdrop");
    backdrop.set_layout({
        .size = {ui::px(200.0F), ui::px(180.0F)},
        .in_flow = false,
    });
    backdrop.configure_all_styles([](ui::Style& style) { style.background_color(ui::rgb(1.0F, 1.0F, 1.0F)); });
    auto& visual_tests = surface.root().add<ui::Container>("visual-tests");
    visual_tests.set_layout({
        .size = {ui::px(140.0F), ui::px(140.0F)},
        .placement = {.offset = {40.0F, 20.0F}},
        .in_flow = false,
    });
    visual_tests.set_scrollable(true);
    auto& outer = visual_tests.add<ui::TreeContainer>("outer");
    auto& inner = outer.add<ui::TreeContainer>("inner");
    inner.set_size({ui::grow(), ui::px(80.0F)});
    inner.configure_all_styles([](ui::Style& style) {
        style.background_color(ui::rgb(1.0F, 1.0F, 1.0F)).box_shadow({.spread = 16.0F, .color = ui::rgb(0.0F, 0.0F, 0.0F)});
    });
    auto& child = inner.add<ui::Container>("child");
    child.set_size({ui::grow(), ui::px(40.0F)});
    child.configure_all_styles([](ui::Style& style) {
        style.background_color(ui::rgb(1.0F, 1.0F, 1.0F)).box_shadow({.spread = 16.0F, .color = ui::rgb(0.0F, 0.0F, 0.0F)});
    });
    auto& filler = visual_tests.add<ui::Container>("filler");
    filler.set_size({ui::grow(), ui::px(200.0F)});

    draw_frame(surface);
    click_tree(surface, outer);
    draw_frame(surface);
    release_click(surface);
    draw_frame(surface);
    click_tree(surface, inner);
    draw_frame(surface);
    release_click(surface);
    draw_frame(surface);
    draw_frame(surface);

    GLint viewport[4]{};
    glGetIntegerv(GL_VIEWPORT, viewport);
    const ui::Rect inner_rect = inner.layout().visual_rect();
    const ui::Rect child_rect = child.layout().visual_rect();
    REQUIRE(inner_rect.valid());
    REQUIRE(child_rect.valid());
    unsigned char pixel[4]{};
    glReadPixels(
        static_cast<int>(inner_rect.min.x - 8.0F), viewport[3] - static_cast<int>(inner_rect.min.y + 10.0F), 1, 1, GL_RGBA,
        GL_UNSIGNED_BYTE, pixel
    );
    CHECK(pixel[0] < 80);
    CHECK(pixel[1] < 80);
    CHECK(pixel[2] < 80);
    glReadPixels(
        static_cast<int>(child_rect.min.x - 8.0F), viewport[3] - static_cast<int>(child_rect.min.y + 20.0F), 1, 1, GL_RGBA,
        GL_UNSIGNED_BYTE, pixel
    );
    CHECK(pixel[0] < 80);
    CHECK(pixel[1] < 80);
    CHECK(pixel[2] < 80);
}

TEST_CASE("raster textures decode from memory without a graphics context", "[texture][raster]") {
    const auto path = std::filesystem::path{IMGUI_UI_ASSETS_DIR} / "images/tiny.jpg";
    std::ifstream file(path, std::ios::binary);
    REQUIRE(file.is_open());
    const std::string bytes{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    ui::OpenGLTextureLoader loader;
    auto memory = loader.load(std::string_view(bytes), "memory");
    auto disk = loader.load(path, "disk");
    REQUIRE(memory->size().x == disk->size().x);
    REQUIRE(memory->size().y == disk->size().y);
    REQUIRE(memory->memory_usage().cpu_bytes > 0);
    REQUIRE(memory->memory_usage().gpu_bytes == 0);
}

TEST_CASE("svg textures cache uploads by size and release their context", "[texture][svg]") {
    SdlVideoSession sdl({64.0F, 64.0F});
    ui_test::ImGuiContext context({64.0F, 64.0F});
    ui::OpenGLTextureLoader loader;
    auto texture = loader.load(
        std::string_view{R"(<svg xmlns="http://www.w3.org/2000/svg" width="4" height="4">
            <rect width="4" height="4" fill="#ff00ff"/>
        </svg>)"},
        "svg"
    );
    REQUIRE(texture != nullptr);

    const ImTextureID small = texture->get({4.0F, 4.0F});
    REQUIRE(small != 0);
    REQUIRE(texture->get({4.0F, 4.0F}) == small);
    const ImTextureID large = texture->get({8.0F, 8.0F});
    REQUIRE(large != small);

    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(small));
    std::array<uint8_t, 64> pixels{};
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    REQUIRE(pixels[0] == 255);
    REQUIRE(pixels[1] == 0);
    REQUIRE(pixels[2] == 255);
    REQUIRE(pixels[3] == 255);

    texture->release_context(ImGui::GetCurrentContext());
    REQUIRE(glIsTexture(static_cast<GLuint>(small)) == GL_FALSE);
    REQUIRE(glIsTexture(static_cast<GLuint>(large)) == GL_FALSE);
}

TEST_CASE("unsupported texture data returns an empty handle", "[texture][regression]") {
    ui::TextureRegistry textures(std::make_unique<ui::OpenGLTextureLoader>());
    const auto entries = textures.stats().entry_count;
    REQUIRE(textures.acquire("unsupported", std::string_view{"not an image"}) == nullptr);
    REQUIRE(textures.acquire("broken-gif", std::string_view{"GIF89a"}) == nullptr);
    REQUIRE(textures.add("unsupported-file", std::filesystem::path{IMGUI_UI_ASSETS_DIR} / "../CMakeLists.txt") == nullptr);
    REQUIRE(textures.stats().entry_count == entries);
}

TEST_CASE("gif texture data decodes into an opengl texture", "[texture][gif]") {
    static constexpr char gif_data[] = "GIF89a\x01\x00\x01\x00\x80\x00\x00\x00\x00\x00\xff\xff\xff!\xf9\x04\x01\x00\x00\x00\x00,"
                                       "\x00\x00\x00\x00\x01\x00\x01\x00\x00\x02\x02D\x01\x00;";
    constexpr std::string_view gif{gif_data, sizeof(gif_data) - 1U};

    SdlVideoSession sdl({128.0F, 128.0F});
    ui::Runtime runtime({.texture_loader = std::make_unique<ui::OpenGLTextureLoader>()});
    ui::Surface surface(runtime, {.backend = make_sdl_backend(sdl)});
    ui::Texture* texture = runtime.textures().add("gif", gif);
    REQUIRE(texture != nullptr);
    REQUIRE(texture->size().x == 1.0F);
    REQUIRE(texture->size().y == 1.0F);

    const ui::ImGuiContextScope context(surface.imgui_context());
    const ImTextureID id = texture->get(texture->size());
    REQUIRE(id != ImTextureID{});
    REQUIRE(glIsTexture(static_cast<GLuint>(id)) == GL_TRUE);
}

TEST_CASE("texture cache collects real gpu uploads after rendering and recreates them", "[texture-cache][render]") {
    SdlVideoSession sdl({128.0F, 128.0F});
    ui::Runtime runtime({
        .texture_loader = std::make_unique<ui::OpenGLTextureLoader>(),
        .texture_cache = {.cpu_budget_bytes = 0, .gpu_budget_bytes = 0},
    });
    ui::Surface surface(runtime, {.backend = make_sdl_backend(sdl)});
    const auto surface_context = ui_test::prepare_surface(surface);
    ui::TextureHandle texture;
    SECTION("raster") {
        texture = runtime.textures().acquire("cover", std::filesystem::path{IMGUI_UI_ASSETS_DIR} / "images/tiny.jpg");
    }
    SECTION("svg") {
        texture =
            runtime.textures().acquire("cover", std::string_view{R"(<svg xmlns="http://www.w3.org/2000/svg" width="4" height="4">
                <rect width="4" height="4" fill="#ff00ff"/></svg>)"});
    }
    SECTION("gif") {
        texture = runtime.textures().acquire("cover", std::filesystem::path{IMGUI_UI_ASSETS_DIR} / "images/t3.gif");
    }
    REQUIRE(texture != nullptr);
    auto& image = surface.root().add<ui::ImageWidget>(texture);
    image.set_size({ui::px(16.0F), ui::px(16.0F)});
    surface.frame();
    const ImTextureID first = texture->get({8.0F, 8.0F});
    REQUIRE(glIsTexture(static_cast<GLuint>(first)) == GL_TRUE);
    REQUIRE(texture->memory_usage().cpu_bytes > 0);
    REQUIRE(texture->memory_usage().gpu_bytes > 0);

    image.set_visible(false);
    surface.frame();
    REQUIRE(glIsTexture(static_cast<GLuint>(first)) == GL_FALSE);
    REQUIRE(texture->memory_usage().gpu_bytes == 0);
    image.set_visible(true);
    surface.frame();
    REQUIRE(texture->memory_usage().gpu_bytes > 0);
    const ImTextureID restored = texture->get({8.0F, 8.0F});
    REQUIRE(glIsTexture(static_cast<GLuint>(restored)) == GL_TRUE);
    image.set_texture(nullptr);
    texture.reset();
    surface.frame();
    REQUIRE(glIsTexture(static_cast<GLuint>(restored)) == GL_FALSE);
    REQUIRE(runtime.textures().find_handle("cover") == nullptr);
    REQUIRE(glGetError() == GL_NO_ERROR);
}

TEST_CASE("handled button clicks still release ImGui mouse state", "[input][regression]") {
    SdlVideoSession sdl({320.0F, 240.0F});
    ui::Runtime runtime;
    ui::Surface surface(runtime, {.backend = make_sdl_backend(sdl)});
    const auto surface_context = ui_test::prepare_surface(surface);

    int click_count = 0;
    auto& button = surface.root().add<ui::ButtonWidget>("test button", ui::LayoutSize{ui::px(160.0F), ui::px(36.0F)});
    button.on_event([&click_count](ui::UiEvent& event) {
        if (event.type == ui::EventType::Click) {
            ++click_count;
            event.mark_handled();
        }
    });

    ui_test::draw_surface(surface);
    const ui::Rect button_rect = button.layout().visual_rect();
    const ImVec2 click_position = ui_test::center(button_rect);
    const SDL_WindowID window_id = surface.backend().window_id();

    SDL_Event down{};
    down.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    down.button.windowID = window_id;
    down.button.x = click_position.x;
    down.button.y = click_position.y;
    down.button.button = SDL_BUTTON_LEFT;
    REQUIRE_FALSE(process_sdl_event(surface, down));

    SDL_Event up = down;
    up.type = SDL_EVENT_MOUSE_BUTTON_UP;
    REQUIRE(process_sdl_event(surface, up));
    REQUIRE(click_count == 1);

    ui_test::draw_surface(surface);
    {
        const ui::ImGuiContextScope context(surface.imgui_context());
        CHECK(ImGui::IsMouseDown(ImGuiMouseButton_Left));
    }

    ui_test::draw_surface(surface);
    {
        const ui::ImGuiContextScope context(surface.imgui_context());
        REQUIRE_FALSE(ImGui::IsMouseDown(ImGuiMouseButton_Left));
    }
}

TEST_CASE("blocked modal number sliders keep receiving sdl drag motion", "[input][regression]") {
    SdlVideoSession sdl({900.0F, 600.0F});
    ui::Runtime runtime;
    ui::Surface surface(runtime, {.backend = make_sdl_backend(sdl)});
    const auto surface_context = ui_test::prepare_surface(surface, {900.0F, 600.0F});

    auto& modal_layer = surface.root().add<ui::LayerContainer>("modal-layer");
    modal_layer.set_input_mode(ui::InputMode::Blocker);
    modal_layer.configure_all_styles([](ui::Style& style) {
        style.background_color(ui::rgba(0.0F, 0.0F, 0.0F, 0.0F)).blur(5.0F);
    });

    auto& modal = modal_layer.add<ui::Container>("modal");
    modal.set_size({ui::px(480.0F), ui::px(220.0F)});
    modal.set_layout({
        .size = {ui::px(480.0F), ui::px(220.0F)},
        .placement = {.anchor = ui::Anchor::Center, .origin = ui::Anchor::Center},
        .in_flow = false,
    });
    modal.set_spacing(10.0F);

    int value = 5;
    auto& input = modal.add<ui::NumberInputWidget<int>>(value, "modal-blur");
    input.set_range(0, 32).set_size({ui::px(180.0F), ui::px(48.0F)});
    input.on_change([&modal_layer, &value] {
        modal_layer.configure_all_styles([&value](ui::Style& style) { style.blur(value); });
    });
    surface.input_router().set_focus(&modal_layer);
    ui_test::draw_surface(surface);
    const ui::Rect rect = input.layout().visual_rect();
    const ImVec2 press = {rect.min.x + (rect.size().x * 0.70F), ui_test::center(rect).y};

    send_sdl_pointer(surface, SDL_EVENT_MOUSE_MOTION, press);
    ui_test::draw_surface(surface);
    send_sdl_pointer(surface, SDL_EVENT_MOUSE_BUTTON_DOWN, press);
    ui_test::draw_surface(surface);
    const ImGuiID slider_id = GImGui->ActiveId;
    REQUIRE(slider_id != 0);
    int previous_value = value;

    for (float ratio : {0.75F, 0.80F, 0.85F, 0.90F, 0.95F}) {
        const ImVec2 position = {rect.min.x + (rect.size().x * ratio), press.y};
        send_sdl_pointer(surface, SDL_EVENT_MOUSE_MOTION, position);
        ui_test::draw_surface(surface);
        REQUIRE(GImGui->ActiveId == slider_id);
        REQUIRE(value > previous_value);
        previous_value = value;
    }

    // blur reaching zero removes the visual effect while the active slider keeps the same imgui parent.
    for (float ratio : {0.20F, 0.05F}) {
        const ImVec2 position = {rect.min.x + (rect.size().x * ratio), press.y};
        send_sdl_pointer(surface, SDL_EVENT_MOUSE_MOTION, position);
        ui_test::draw_surface(surface);
        REQUIRE(GImGui->ActiveId == slider_id);
        REQUIRE(value <= previous_value);
        previous_value = value;
    }

    REQUIRE(value == 0);
    const ImVec2 restore = {rect.min.x + (rect.size().x * 0.80F), press.y};
    send_sdl_pointer(surface, SDL_EVENT_MOUSE_MOTION, restore);
    ui_test::draw_surface(surface);
    REQUIRE(GImGui->ActiveId == slider_id);
    REQUIRE(value > 0);

    const int value_before_leaving_modal = value;
    send_sdl_pointer(surface, SDL_EVENT_MOUSE_MOTION, {850.0F, press.y});
    ui_test::draw_surface(surface);
    const int outside_value = value;
    REQUIRE(outside_value != value_before_leaving_modal);
    send_sdl_pointer(surface, SDL_EVENT_MOUSE_BUTTON_UP, {850.0F, press.y});
    ui_test::draw_surface(surface);
    REQUIRE(GImGui->ActiveId == 0);
    send_sdl_pointer(surface, SDL_EVENT_MOUSE_MOTION, {rect.min.x + (rect.size().x * 0.80F), press.y});
    ui_test::draw_surface(surface);
    REQUIRE(value == outside_value);
}

TEST_CASE("debugger hotkey is received through the sdl backend", "[input][regression]") {
    SdlVideoSession sdl({320.0F, 240.0F});
    ui::Runtime runtime;
    ui::Surface surface(runtime, {.backend = make_sdl_backend(sdl), .enable_debugger = true});
    REQUIRE(surface.debugger() != nullptr);
    surface.debugger()->set_open(false);
    const auto surface_context = ui_test::prepare_surface(surface);

    const SDL_WindowID window_id = surface.backend().window_id();
    SDL_Event shift_down{};
    shift_down.type = SDL_EVENT_KEY_DOWN;
    shift_down.key.windowID = window_id;
    shift_down.key.key = SDLK_LSHIFT;
    shift_down.key.scancode = SDL_SCANCODE_LSHIFT;
    shift_down.key.mod = SDL_KMOD_SHIFT;
    REQUIRE_FALSE(process_sdl_event(surface, shift_down));

    SDL_Event d_down = shift_down;
    d_down.key.key = SDLK_D;
    d_down.key.scancode = SDL_SCANCODE_D;
    REQUIRE_FALSE(process_sdl_event(surface, d_down));

    surface.begin_frame();
    REQUIRE(surface.debugger()->is_open());
    surface.end_frame();
}

TEST_CASE("pointer blocker prevents native content mutation but keeps descendants interactive", "[input][regression]") {
    SdlVideoSession sdl({320.0F, 240.0F});
    ui::Runtime runtime;
    ui::Surface surface(runtime, {.backend = make_sdl_backend(sdl)});
    const auto surface_context = ui_test::prepare_surface(surface);

    bool content_value = false;
    bool overlay_value = false;
    std::string dropdown_value = "one";
    auto& content_checkbox = surface.root().add<ui::CheckboxWidget>(content_value, "content");
    content_checkbox.set_layout({
        .size = {ui::fit(), ui::fit()},
        .placement = {.offset = {20.0F, 20.0F}},
        .in_flow = false,
    });
    auto& content_dropdown = surface.root().add<ui::DropdownWidget>(
        dropdown_value, std::vector<ui::DropdownOption>{{"one", "one"}, {"two", "two"}}, "", ui::LabelPlacement::Above, "dropdown"
    );
    content_dropdown.set_size({ui::px(180.0F), ui::px(52.0F)});
    content_dropdown.set_layout({
        .size = {ui::px(180.0F), ui::px(52.0F)},
        .placement = {.offset = {20.0F, 60.0F}},
        .in_flow = false,
    });

    auto& blocker = surface.root().add<ui::LayerContainer>("blocker", ui::LayerMode::Inline);
    blocker.set_input_mode(ui::InputMode::Blocker);
    auto& overlay_checkbox = blocker.add<ui::CheckboxWidget>(overlay_value, "overlay");
    overlay_checkbox.set_layout({
        .size = {ui::fit(), ui::fit()},
        .placement = {.offset = {20.0F, 130.0F}},
        .in_flow = false,
    });

    ui_test::draw_surface(surface);
    const auto checkbox_center = [](const ui::CheckboxWidget& checkbox) {
        const ui::Rect widget_rect = checkbox.layout().visual_rect();
        const ui::BoxInsets padding = checkbox.style().padding();
        const ui::Rect box_rect = ui::Rect::from_position_size(
            {widget_rect.min.x + padding.left, widget_rect.min.y + padding.top}, checkbox.frame().layout().size()
        );
        return ui_test::center(box_rect);
    };
    const ImVec2 content_position = checkbox_center(content_checkbox);
    const ImVec2 dropdown_position = ui_test::center(content_dropdown.layout().visual_rect());
    const ImVec2 overlay_position = checkbox_center(overlay_checkbox);
    const SDL_WindowID window_id = surface.backend().window_id();

    // the blocker consumes each event. native forwarding is verified by the control state after drawing.
    const auto click = [&](ImVec2 position) {
        SDL_Event motion{};
        motion.type = SDL_EVENT_MOUSE_MOTION;
        motion.motion.windowID = window_id;
        motion.motion.x = position.x;
        motion.motion.y = position.y;
        CHECK(process_sdl_event(surface, motion));

        SDL_Event down{};
        down.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
        down.button.windowID = window_id;
        down.button.x = position.x;
        down.button.y = position.y;
        down.button.button = SDL_BUTTON_LEFT;
        CHECK(process_sdl_event(surface, down));

        SDL_Event up = down;
        up.type = SDL_EVENT_MOUSE_BUTTON_UP;
        CHECK(process_sdl_event(surface, up));
        ui_test::draw_surface(surface);
        ui_test::draw_surface(surface);
    };

    click(content_position);
    CHECK_FALSE(content_value);

    click(dropdown_position);
    CHECK_FALSE(content_dropdown.is_open());
    CHECK(dropdown_value == "one");

    click(overlay_position);
    CHECK(overlay_value);
}

TEST_CASE("searchable dropdown stays open after releasing its trigger", "[dropdown][input][regression]") {
    SdlVideoSession sdl({400.0f, 300.0f});
    ui::Runtime runtime;
    ui::Surface surface(runtime, {.backend = make_sdl_backend(sdl)});
    const auto context = ui_test::prepare_surface(surface);
    auto& dropdown = surface.root().add<ui::DropdownWidget>();
    dropdown.set_size({ui::px(300.0f), ui::fit()});
    dropdown.set_options({{"Online", "online"}, {"Away", "away"}});
    auto& search = dropdown.trigger().add<ui::TextInputWidget>();
    search.set_flags(ImGuiInputTextFlags_AutoSelectAll);
    search.set_value("Online");
    search.on_event(ui::EventType::Click, [&](ui::UiEvent&) { dropdown.open(); });
    ui_test::draw_surface(surface);
    const ImVec2 position = ui_test::center(search.frame().layout().visual_rect());
    send_sdl_pointer(surface, SDL_EVENT_MOUSE_MOTION, position);
    send_sdl_pointer(surface, SDL_EVENT_MOUSE_BUTTON_DOWN, position);
    if (GENERATE(false, true)) ui_test::draw_surface(surface);
    REQUIRE_FALSE(dropdown.is_open());
    send_sdl_pointer(surface, SDL_EVENT_MOUSE_BUTTON_UP, position);
    ui_test::draw_surface(surface);
    ui_test::draw_surface(surface);
    REQUIRE(dropdown.is_open());
}

TEST_CASE("dropdown selection and cursor use the sdl input path", "[dropdown][input][regression]") {
    SdlVideoSession sdl({320.0F, 240.0F});
    ui::Runtime runtime;
    ui::Surface surface(runtime, {.backend = make_sdl_backend(sdl)});
    const auto surface_context = ui_test::prepare_surface(surface);

    std::string value = "one";
    int changes = 0;
    auto& dropdown = surface.root().add<ui::DropdownWidget>(
        value, std::vector<ui::DropdownOption>{{"one", "one"}, {"two", "two"}}, "", ui::LabelPlacement::Above, "dropdown"
    );
    dropdown.set_layout({
        .size = {ui::px(180.0F), ui::px(36.0F)},
        .placement = {.offset = {20.0F, 20.0F}},
        .in_flow = false,
    });
    dropdown.on_change([&changes] { ++changes; });
    auto& outside = surface.root().add<ui::TreeContainer>("outside tree");
    outside.set_layout({
        .size = {ui::px(80.0F), ui::fit()},
        .placement = {.offset = {220.0F, 20.0F}},
        .in_flow = false,
    });

    ui_test::draw_surface(surface);
    const ui::Rect trigger_rect = dropdown.trigger().layout().visual_rect();
    const ImVec2 trigger_position = ui_test::center(trigger_rect);
    send_sdl_pointer(surface, SDL_EVENT_MOUSE_MOTION, trigger_position);
    send_sdl_pointer(surface, SDL_EVENT_MOUSE_BUTTON_DOWN, trigger_position);
    send_sdl_pointer(surface, SDL_EVENT_MOUSE_BUTTON_UP, trigger_position);
    ui_test::draw_surface(surface);
    REQUIRE(dropdown.is_open());
    REQUIRE(ImGui::GetMouseCursor() == ImGuiMouseCursor_Hand);

    const ImVec2 outside_position = ui_test::center(outside.layout().visual_rect());
    send_sdl_pointer(surface, SDL_EVENT_MOUSE_BUTTON_DOWN, outside_position);
    send_sdl_pointer(surface, SDL_EVENT_MOUSE_BUTTON_UP, outside_position);
    ui_test::draw_surface(surface);
    REQUIRE_FALSE(dropdown.is_open());

    send_sdl_pointer(surface, SDL_EVENT_MOUSE_BUTTON_DOWN, trigger_position);
    send_sdl_pointer(surface, SDL_EVENT_MOUSE_BUTTON_UP, trigger_position);
    ui_test::draw_surface(surface);
    REQUIRE(dropdown.is_open());

    const ui::Rect body_rect = dropdown.body().layout().visual_rect();
    const float item_height = body_rect.size().y * 0.5F;
    const float body_center_x = ui_test::center(body_rect).x;
    const auto require_option = [&surface](ImVec2 position) {
        const ui::Node* option = surface.input_router().node_at(position);
        REQUIRE(option != nullptr);
        REQUIRE(option->type_name() == "DropdownOption");
    };
    for (int index = 0; index < 2; ++index) {
        const ImVec2 option_position = {
            body_center_x,
            body_rect.min.y + (item_height * (static_cast<float>(index) + 0.5F)),
        };

        send_sdl_pointer(surface, SDL_EVENT_MOUSE_MOTION, option_position);
        require_option(option_position);
        REQUIRE(ImGui::GetMouseCursor() == ImGuiMouseCursor_Hand);

        ui_test::draw_surface(surface);
        require_option(option_position);
        REQUIRE(ImGui::GetMouseCursor() == ImGuiMouseCursor_Hand);
    }

    const ImVec2 option_position = {body_center_x, body_rect.min.y + (item_height * 1.5F)};
    send_sdl_pointer(surface, SDL_EVENT_MOUSE_MOTION, option_position);

    const float visible_opacity = dropdown.body().opacity();
    send_sdl_pointer(surface, SDL_EVENT_MOUSE_BUTTON_DOWN, option_position);
    send_sdl_pointer(surface, SDL_EVENT_MOUSE_BUTTON_UP, option_position);
    REQUIRE(value == "two");
    REQUIRE_FALSE(dropdown.is_open());

    ui_test::draw_surface(surface, 1.0F / 60.0F);
    REQUIRE(dropdown.body().opacity() < visible_opacity);
    for (int frame = 0; frame < 8; ++frame) {
        ui_test::draw_surface(surface, 1.0F / 60.0F);
    }
    REQUIRE_FALSE(dropdown.body().visually_visible());
    REQUIRE(changes == 1);
}
