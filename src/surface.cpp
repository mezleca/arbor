#include <imgui-ui/surface.hpp>

#include <imgui-ui/diagnostics/profiler.hpp>
#include <imgui-ui/imgui/context-scope.hpp>
#include <imgui-ui/diagnostics/debugger.hpp>
#include <imgui-ui/layout/layer-container.hpp>
#include <imgui-ui/layout/resizable-container.hpp>
#include <imgui-ui/runtime.hpp>
#include <imgui-ui/style/theme.hpp>

#include <algorithm>
#include <imgui_internal.h>
#include <stdexcept>
#include <utility>

using namespace ui;

class SurfaceContent final : public ResizableContainer {
public:
    SurfaceContent() : ResizableContainer("content") {
        set_type_name("SurfaceContent");
        set_size({grow(), grow()});
    }

    void apply_theme_defaults(const Theme& theme) override {
        ResizableContainer::apply_theme_defaults(theme);
        configure_all_styles([&theme](Style& style) {
            style.background_color(theme.background_color)
                .border_color(theme.controls.border_color)
                .border(BorderEdge{0.0F, BorderType::None});
        });
    }
};

Surface::Surface(Runtime& runtime, SurfaceConfig config)
    : m_runtime(runtime), m_theme(runtime.theme()), m_backend(std::move(config.backend)),
      m_file_dialog(config.file_dialog_backend != nullptr ? std::move(config.file_dialog_backend) : make_file_dialog_backend()),
      m_profiler(std::make_unique<Profiler>(runtime.performance_directory())) {
    initialize(config.enable_debugger);
}

Surface::~Surface() {
    ImGuiContext* previous_context = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(m_context);

    m_root.reset();
    m_content_root = nullptr;
    m_debugger = nullptr;

    m_effects.shutdown();
    m_runtime.fonts().release_context(m_context);
    m_runtime.textures().release_context(m_context);
    m_backend->shutdown_imgui();
    ImGui::DestroyContext(m_context);
    ImGui::SetCurrentContext(previous_context == m_context ? nullptr : previous_context);
    m_context = nullptr;
}

void Surface::process_events() {
    m_backend->process_events(*this);
}

void Surface::set_theme(Theme theme) {
    m_theme = std::move(theme);

    const ImGuiContextScope scope(m_context);

    apply_theme_metrics();
    apply_theme_colors();

    m_root->apply_theme(m_theme);
}

ImFont* Surface::resolve_font(Font* font) {
    if (font != nullptr) {
        ImFont* result = font->get();
        if (result != nullptr) {
            return result;
        }
    }

    return ImGui::GetFont();
}

ImFont* Surface::get_font(std::string_view id) const {
    const ImGuiContextScope scope(m_context);
    return resolve_font(m_runtime.fonts().find(id));
}

ImFont* Surface::get_primary_font() const {
    const ImGuiContextScope scope(m_context);
    return resolve_font(m_primary_font);
}

void Surface::set_primary_font(Font* font) {
    const ImGuiContextScope scope(m_context);
    m_primary_font = font;
    ImFont* resolved = font != nullptr ? font->get() : nullptr;
    ImGui::GetIO().FontDefault = resolved;
}

void Surface::initialize(bool enable_debugger) {
    if (m_backend == nullptr) {
        throw std::runtime_error("m_backend is nullptr");
    }

    if (!m_backend->initialize()) {
        throw std::runtime_error("failed to initialize backend");
    }

    m_context = ImGui::CreateContext();
    const ImGuiContextScope scope(m_context);

    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.Fonts->AddFontDefaultVector();

    configure_style(m_backend->content_scale());
    m_backend->register_effects(m_effects);

    if (!m_backend->initialize_imgui()) {
        throw std::runtime_error("failed to initialize imgui");
    }

    if (!m_effects.initialize()) {
        throw std::runtime_error("failed to initialize effects");
    }

    // attach the surface root before adding content so descendants inherit its router and profiler.
    m_root = std::make_unique<LayerContainer>("ui-surface", LayerMode::Window);
    m_root->set_direction(StackDirection::Horizontal);
    Node& root = *m_root;
    root.set_surface(this);
    root.set_input_router(&m_input_router);
    root.set_profiler(m_profiler.get());

    auto& content = m_root->add<SurfaceContent>();
    m_content_root = &content;
    m_profiler->set_root_node(content.identity());

    if (enable_debugger) {
        m_debugger = &m_root->add<Debugger>(*this);
    }
}

void Surface::configure_style(float main_scale) {
    ImGui::StyleColorsDark();

    m_content_scale = main_scale;
    ImGuiStyle& style = ImGui::GetStyle();
    style.ScaleAllSizes(main_scale);
    style.FontScaleDpi = main_scale;

    apply_theme_metrics();
    apply_theme_colors();
}

void Surface::apply_theme_metrics() {
    ImGuiStyle& style = ImGui::GetStyle();
    const Theme& theme = m_theme;
    const float scale = m_content_scale;
    const auto scaled = [scale](float value) { return std::max(0.0F, value) * scale; };
    const auto scaled_size = [scale](ImVec2 value) {
        return ImVec2{std::max(0.0F, value.x) * scale, std::max(0.0F, value.y) * scale};
    };

    style.WindowRounding = scaled(theme.metrics.window_rounding);
    style.ChildRounding = scaled(theme.metrics.child_rounding);
    style.PopupRounding = scaled(theme.metrics.popup_rounding);
    style.TabRounding = scaled(theme.metrics.tab_rounding);
    style.WindowPadding = scaled_size(theme.metrics.window_padding);
    style.CellPadding = scaled_size(theme.metrics.cell_padding);
    style.FramePadding = scaled_size(theme.metrics.frame_padding);
    style.FrameRounding = scaled(theme.controls.rounding);
    style.FrameBorderSize = scaled(theme.metrics.frame_border_size);
    style.ItemSpacing = scaled_size(theme.metrics.item_spacing);
    style.ItemInnerSpacing = scaled_size(theme.metrics.item_inner_spacing);
    style.CircleTessellationMaxError = std::max(0.0F, theme.metrics.circle_tessellation_max_error);
#if IMGUI_VERSION_NUM < 19300
    style.AntiAliasedLinesUseTex = theme.metrics.anti_aliased_lines_use_tex;
#endif
}

void Surface::apply_theme_colors() {
    const Theme& theme = m_theme;
    ImVec4* colors = ImGui::GetStyle().Colors;

    colors[ImGuiCol_WindowBg] = theme.background_color;
    colors[ImGuiCol_ChildBg] = theme.background_secondary_color;
    colors[ImGuiCol_Border] = theme.controls.border_color;
    colors[ImGuiCol_Separator] = theme.header_border_color;
    colors[ImGuiCol_Text] = theme.text_color;
    colors[ImGuiCol_TextDisabled] = theme.text_secondary_color;
    colors[ImGuiCol_Button] = theme.background_secondary_color;
    colors[ImGuiCol_ButtonHovered] = theme.background_tertiary_color;
    colors[ImGuiCol_ButtonActive] = theme.button_active_color;
    colors[ImGuiCol_Header] = theme.header_background_color;
    colors[ImGuiCol_HeaderHovered] = theme.background_tertiary_color;
    colors[ImGuiCol_HeaderActive] = theme.button_active_color;
    colors[ImGuiCol_Tab] = theme.background_tertiary_color;
    colors[ImGuiCol_TabHovered] = theme.accent_hover_color;
    colors[ImGuiCol_TabSelected] = theme.accent_color;
    colors[ImGuiCol_TabSelectedOverline] = theme.accent_color;
    colors[ImGuiCol_TabDimmed] = theme.background_tertiary_color;
    colors[ImGuiCol_TabDimmedSelected] = theme.accent_color;
    colors[ImGuiCol_TabDimmedSelectedOverline] = theme.accent_color;
    colors[ImGuiCol_FrameBg] = theme.controls.background_color;
    colors[ImGuiCol_FrameBgHovered] = theme.controls.hover_color;
    colors[ImGuiCol_FrameBgActive] = theme.controls.active_color;
    colors[ImGuiCol_PopupBg] = theme.controls.background_color;
    colors[ImGuiCol_CheckboxSelectedBg] = theme.controls.background_color;
    colors[ImGuiCol_TitleBg] = theme.background_secondary_color;
    colors[ImGuiCol_TitleBgActive] = theme.background_secondary_color;
    colors[ImGuiCol_CheckMark] = theme.controls.mark_color;
    colors[ImGuiCol_SliderGrab] = theme.controls.mark_color;
    colors[ImGuiCol_SliderGrabActive] = theme.accent_hover_color;
}

bool Surface::dispatch(UiEvent& event) {
    const ImGuiContextScope scope(m_context);
    if (m_debugger != nullptr && m_debugger->handle_input(event)) {
        return true;
    }

    const bool handled = input_router().dispatch(event);

    // route the click first so its trigger sees the popup before an outside release closes it.
    if (event.type == EventType::PointerUp && event.button == PointerButton::Left &&
        ImGui::IsPopupOpen((ImGuiID)0, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel)) {
        ImGuiWindow* hovered = nullptr;
        ImGui::FindHoveredWindowEx(event.position, true, &hovered, nullptr);
        ImGui::ClosePopupsOverWindow(hovered, true);
    }

    return handled;
}

bool Surface::debugger_blocks_pointer_input() const {
    return m_debugger != nullptr && m_debugger->blocks_pointer_input();
}

void Surface::begin_frame() {
    // activate this surface's imgui context before the backend starts the next frame.
    m_previous_context = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(m_context);

    m_input_router.begin_frame();
    m_profiler->begin_frame();

    m_effects.begin_frame();
    m_backend->begin_frame(m_theme.background_color);

    ImGui::NewFrame();

    if (m_debugger != nullptr) {
        m_debugger->handle_hotkey();
    }
}

void Surface::frame() {
    begin_frame();
    update(ImGui::GetIO().DeltaTime);
    draw();
    end_frame();
}

void Surface::update(float dt) {
    m_root->update(dt);
}

void Surface::draw() {
    m_root->draw();
}

void Surface::end_frame() {
    m_backend->set_mouse_cursor(ImGui::GetMouseCursor());

    // finalize imgui draw data before the backend renders it and closes the draw cycle.
    ImGui::Render();
    if (m_debugger != nullptr) {
        m_debugger->finish_popup_restore();
    }
    ImDrawData* draw_data = ImGui::GetDrawData();

    if (m_profiler->enabled()) {
        const InputRouterStats input_stats = m_input_router.stats();
        m_profiler->record_frame_metrics(input_stats.entry_count, input_stats.entry_checks);
    }

    {
        UI_PROFILE_SCOPE(m_profiler.get(), "Surface::render");
        m_profiler->record_gpu_render_ms(m_backend->render_profiled(draw_data, m_profiler->enabled()));
    }

    m_profiler->end_frame();
    ImGui::SetCurrentContext(m_previous_context);
    m_previous_context = nullptr;
}
