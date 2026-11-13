#include <arbor/surface.hpp>

#include <arbor/diagnostics/profiler.hpp>
#include <arbor/imgui/context-scope.hpp>
#include <arbor/diagnostics/debugger.hpp>
#include <arbor/layout/layer-container.hpp>
#include <arbor/layout/resizable-container.hpp>
#include <arbor/runtime.hpp>
#include <arbor/style/theme.hpp>

#include <algorithm>
#include <cmath>
#include <imgui_internal.h>
#include <stdexcept>
#include <utility>

using namespace ui;

static void invalidate_measurements(Node& node) {
    node.invalidate_measure();
    for (const auto& child : node.children()) {
        invalidate_measurements(*child);
    }
}

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

void Surface::set_scale(float scale) {
    if (!std::isfinite(scale) || scale <= 0.0f) {
        throw std::invalid_argument("surface scale must be finite and positive");
    }

    m_scale = scale;
}

void Surface::set_theme(Theme theme) {
    m_theme = std::move(theme);

    const ImGuiContextScope scope(m_context);

    configure_style(m_content_scale);
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
    m_root->configure_all_styles([font](Style& style) { style.font(font); });
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
    // the surface applies the final cursor after drawing, platform updates would compete with it
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.Fonts->Flags |= ImFontAtlasFlags_NoBakedRoundCorners;
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

void Surface::configure_style(float scale) {
    // imgui rounds scaled style dimensions. rebuild unscaled defaults to avoid accumulating that rounding across zoom changes.
    ImGuiStyle& style = ImGui::GetStyle();
    style = ImGuiStyle{};
    m_content_scale = scale;

    const Theme& theme = m_theme;
    const auto nonnegative = [](float value) { return std::max(0.0f, value); };
    const auto nonnegative_size = [](ImVec2 value) { return ImVec2{std::max(0.0f, value.x), std::max(0.0f, value.y)}; };

    style.WindowRounding = nonnegative(theme.metrics.window_rounding);
    style.ChildRounding = nonnegative(theme.metrics.child_rounding);
    style.PopupRounding = nonnegative(theme.metrics.popup_rounding);
    style.TabRounding = nonnegative(theme.metrics.tab_rounding);
    style.WindowPadding = nonnegative_size(theme.metrics.window_padding);
    style.CellPadding = nonnegative_size(theme.metrics.cell_padding);
    style.FramePadding = nonnegative_size(theme.metrics.frame_padding);
    style.FrameRounding = nonnegative(theme.controls.rounding);
    style.FrameBorderSize = nonnegative(theme.metrics.frame_border_size);
    style.ItemSpacing = nonnegative_size(theme.metrics.item_spacing);
    style.ItemInnerSpacing = nonnegative_size(theme.metrics.item_inner_spacing);
    style.CircleTessellationMaxError = std::max(0.0F, theme.metrics.circle_tessellation_max_error);

    style.ScaleAllSizes(scale);
    style.FontScaleDpi = scale;
    apply_theme_colors();
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

    m_paint_state.begin_frame();
    m_effects.begin_frame();
    const ImVec2 previous_density = ImGui::GetIO().DisplayFramebufferScale;
    m_backend->begin_frame(m_theme.background_color);

    const float scale = m_scale * m_backend->content_scale();
    const ImVec2 density = ImGui::GetIO().DisplayFramebufferScale;
    if (scale != m_content_scale || density.x != previous_density.x || density.y != previous_density.y) {
        configure_style(scale);
        invalidate_measurements(*m_root);
    }

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
    m_paint_state.finish_frame(m_effects);
}

void Surface::end_frame() {
    m_backend->set_mouse_cursor(ImGui::GetMouseCursor());

    // external drawing can add clipped lists after draw() has finalized the root's output.
    m_paint_state.finish_frame(m_effects);

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
        m_profiler->record_gpu_render_ms(m_backend->render(draw_data, m_profiler->enabled()));
    }

    // draw data has been consumed. texture collection must run before leaving this surface's graphics context.
    m_runtime.textures().collect(m_context);

    m_profiler->end_frame();
    ImGui::SetCurrentContext(m_previous_context);
    m_previous_context = nullptr;
}
