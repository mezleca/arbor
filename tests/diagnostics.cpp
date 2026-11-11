#include <catch2/catch_test_macros.hpp>

#include <imgui-ui/diagnostics/debugger.hpp>
#include <imgui-ui/diagnostics/profiler.hpp>
#include <imgui-ui/imgui/effects/effects.hpp>
#include <imgui-ui/layout/resizable-container.hpp>
#include <imgui-ui/style/styled-node.hpp>
#include <imgui-ui/surface.hpp>
#include <imgui-ui/widgets/checkbox.hpp>
#include <imgui-ui/widgets/color-picker.hpp>

#include <imgui.h>
#include <imgui_internal.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string_view>
#include <utility>

#include "imgui-context.hpp"

using namespace ui;

struct EffectProbe {
    int initialized = 0;
    int frames = 0;
    int shutdown = 0;
    int command = 0;
};

static bool initialize_test_effect(void* data) {
    ++static_cast<EffectProbe*>(data)->initialized;
    return true;
}

static void begin_test_effect_frame(void* data) {
    ++static_cast<EffectProbe*>(data)->frames;
}

static void shutdown_test_effect(void* data) {
    ++static_cast<EffectProbe*>(data)->shutdown;
}

static void render_test_effect(void* data, const ImDrawList*, const ImDrawCmd*, const void* command) {
    static_cast<EffectProbe*>(data)->command = *static_cast<const int*>(command);
}

static int draw_list_index(const ImDrawData& draw_data, std::string_view owner) {
    for (int index = 0; index < draw_data.CmdListsCount; ++index) {
        const ImDrawList* draw_list = draw_data.CmdLists[index];
        if (draw_list != nullptr && draw_list->_OwnerName != nullptr && std::string_view{draw_list->_OwnerName}.contains(owner)) {
            return index;
        }
    }

    return -1;
}

TEST_CASE("debugger renders in the target surface and intercepts its overlay") {
    ui::Runtime runtime;
    ui::Surface surface = ui_test::make_surface(runtime, true);
    REQUIRE(surface.debugger() != nullptr);
    REQUIRE_FALSE(surface.debugger()->is_open());

    const auto surface_context = ui_test::prepare_surface(surface, {320.0F, 240.0F});

    surface.begin_frame();
    surface.end_frame();
    REQUIRE_FALSE(surface.debugger()->is_open());

    surface.debugger()->set_open(true);
    const auto draw_modal_frame = [&surface] {
        surface.begin_frame();
        const bool visible = ImGui::Begin("modal-layer");
        ImGui::TextUnformatted("modal");
        const int command_count = ImGui::GetWindowDrawList()->CmdBuffer.Size;
        ImGui::End();
        surface.update(ImGui::GetIO().DeltaTime);
        surface.draw();
        surface.end_frame();
        return std::pair{visible, command_count};
    };

    const auto [modal_visible, command_count] = draw_modal_frame();
    REQUIRE(modal_visible);
    REQUIRE(command_count > 1);
    draw_modal_frame();

    ImGui::SetCurrentContext(surface.imgui_context());
    const ImDrawData* draw_data = ImGui::GetDrawData();
    REQUIRE(draw_data != nullptr);
    const int modal_index = draw_list_index(*draw_data, "modal-layer");
    const int debugger_index = draw_list_index(*draw_data, "##debugger-sections");
    REQUIRE(modal_index >= 0);
    REQUIRE(debugger_index >= 0);
    REQUIRE(modal_index < debugger_index);

    surface.begin_frame();

    const ImVec2 debugger_position = ui_test::center(surface.debugger()->layout().visual_rect());
    ui::UiEvent down = ui_test::pointer_event(ui::EventType::PointerDown, debugger_position);
    REQUIRE(surface.dispatch(down));

    ui::UiEvent up = ui_test::pointer_event(ui::EventType::PointerUp, down.position);
    REQUIRE(surface.dispatch(up));
    surface.end_frame();
}

TEST_CASE("debugger clicks preserve an open popup") {
    ui::Runtime runtime;
    ui::Surface surface = ui_test::make_surface(runtime, true);
    ui::Color color = ui::rgb(0.26F, 0.59F, 0.98F);
    auto& picker = surface.root().add<ui::ColorPickerWidget>(color);

    const auto surface_context = ui_test::prepare_surface(surface, {900.0F, 600.0F});
    ui_test::draw_surface(surface);

    const ImVec2 preview = ui_test::center(picker.preview().layout().visual_rect());
    ui::UiEvent down = ui_test::pointer_event(ui::EventType::PointerDown, preview);
    ui::UiEvent up = ui_test::pointer_event(ui::EventType::PointerUp, preview);
    surface.dispatch(down);
    surface.dispatch(up);
    ui_test::draw_surface(surface);
    REQUIRE(picker.is_open());
    REQUIRE_FALSE(ImGui::GetCurrentContext()->OpenPopupStack.empty());
    const ImGuiID popup_id = ImGui::GetCurrentContext()->OpenPopupStack.back().PopupId;

    surface.debugger()->set_open(true);
    ui_test::draw_surface(surface);
    REQUIRE(picker.is_open());
    REQUIRE(ImGui::IsPopupOpen(popup_id, ImGuiPopupFlags_AnyPopupLevel));

    const ui::Rect debugger_rect = surface.debugger()->layout().visual_rect();
    const ImVec2 inspect = {debugger_rect.min.x + 20.0F, debugger_rect.min.y + 20.0F};
    down = ui_test::pointer_event(ui::EventType::PointerDown, inspect);
    REQUIRE(surface.dispatch(down));
    REQUIRE_FALSE(down.native_input_blocked);
    ImGui::GetIO().AddMousePosEvent(inspect.x, inspect.y);
    ImGui::GetIO().AddMouseButtonEvent(0, true);
    ui_test::draw_surface(surface);
    REQUIRE(picker.is_open());

    up = ui_test::pointer_event(ui::EventType::PointerUp, inspect);
    REQUIRE(surface.dispatch(up));
    ImGui::GetIO().AddMouseButtonEvent(0, false);
    ui_test::draw_surface(surface);
    REQUIRE(picker.is_open());
    REQUIRE(ImGui::IsPopupOpen(popup_id, ImGuiPopupFlags_AnyPopupLevel));
}

TEST_CASE("debugger exposes the content resize handle", "[Debugger][ResizableContainer][regression]") {
    ui::Runtime runtime;
    ui::Surface surface = ui_test::make_surface(runtime, true);
    surface.debugger()->set_open(true);

    const auto surface_context = ui_test::prepare_surface(surface, {900.0F, 600.0F});
    ui_test::draw_surface(surface);

    auto* content = dynamic_cast<ui::ResizableContainer*>(&surface.root());
    REQUIRE(content != nullptr);
    const ui::Rect initial = content->layout().visual_rect();
    REQUIRE(initial.valid());

    const ImVec2 handle = {initial.max.x - 5.0F, initial.max.y - 5.0F};
    ui::UiEvent down = ui_test::pointer_event(ui::EventType::PointerDown, handle);
    REQUIRE(surface.dispatch(down));
    REQUIRE(content->resizing());

    ui::UiEvent move = ui_test::pointer_event(ui::EventType::PointerMove, {handle.x - 100.0F, handle.y});
    REQUIRE(surface.dispatch(move));

    ui::UiEvent up = ui_test::pointer_event(ui::EventType::PointerUp, move.position);
    REQUIRE(surface.dispatch(up));
    REQUIRE_FALSE(content->resizing());

    ui_test::draw_surface(surface);
    REQUIRE(content->layout().size().x < initial.size().x);
}

TEST_CASE("effect registry invokes lifecycle callbacks and queues draw callbacks") {
    EffectProbe probe;
    EffectRegistry effects;
    const EffectPass pass = effects.register_effect<int>(
        {render_test_effect, initialize_test_effect, begin_test_effect_frame, shutdown_test_effect, &probe}
    );

    REQUIRE(effects.initialize());
    REQUIRE(probe.initialized == 1);

    effects.begin_frame();
    REQUIRE(probe.frames == 1);

    ui_test::ImGuiContext context({100.0F, 100.0F});
    ImDrawList* draw_list = nullptr;
    ui_test::draw_window("effect-registry-test", {100.0F, 100.0F}, [&] {
        draw_list = ImGui::GetWindowDrawList();
        int command_value = 42;
        REQUIRE(pass.submit(*draw_list, command_value));
        command_value = 0;
        REQUIRE(draw_list->CmdBuffer.Size >= 2);
    });
    ImGui::Render();

    bool found_callback = false;
    for (const ImDrawCmd& command : draw_list->CmdBuffer) {
        if (command.UserCallback == nullptr || command.UserCallback == ImDrawCallback_ResetRenderState) {
            continue;
        }

        command.UserCallback(draw_list, &command);
        found_callback = true;
    }
    REQUIRE(found_callback);
    REQUIRE(probe.command == 42);

    REQUIRE(effects.unregister_effect(pass));
    REQUIRE(probe.shutdown == 1);
    REQUIRE_FALSE(pass.submit(*draw_list, 42));

    effects.shutdown();
    effects.register_effect<int>({render_test_effect, initialize_test_effect, nullptr, shutdown_test_effect, &probe});
    effects.register_effect<int>({render_test_effect, [](void*) { return false; }});
    REQUIRE_FALSE(effects.initialize());
    REQUIRE(probe.shutdown == 2);

    effects.shutdown();
    REQUIRE(probe.shutdown == 2);
}

TEST_CASE("ui profiler records completed zones and frame metrics") {
    ui::Profiler profiler;
    profiler.set_enabled(true);
    profiler.begin_frame();

    {
        ui::ScopedProfileZone outer(&profiler, "outer", 10);
        ui::ScopedProfileZone inner(&profiler, "Node::draw", 20);
    }
    profiler.record_frame_metrics(2, 7);
    profiler.record_gpu_render_ms(1.25);

    profiler.end_frame();
    const std::span<const ui::ProfileEvent> events = profiler.latest_events();

    REQUIRE(events.size() == 2);
    REQUIRE(events[0].name == "outer");
    REQUIRE(events[1].name == "Node::draw");
    REQUIRE(events[0].end >= events[0].start);
    REQUIRE(events[1].end >= events[1].start);
    REQUIRE(profiler.node_duration_ms(20) > 0.0);
    REQUIRE(profiler.dropped_events() == 0);
    REQUIRE(profiler.latest_metrics().input_entries == 2);
    REQUIRE(profiler.latest_metrics().input_entry_checks == 7);
    REQUIRE(profiler.latest_metrics().gpu_render_ms.has_value());
    REQUIRE(*profiler.latest_metrics().gpu_render_ms == 1.25);
    REQUIRE(profiler.gpu_render_summary().average_ms == 1.25);
    REQUIRE(profiler.has_report());
    REQUIRE(profiler.save_report());

    {
        std::ifstream report(profiler.output_path());
        const std::string contents{std::istreambuf_iterator<char>(report), {}};
        REQUIRE(contents.contains("latest.input_entry_checks = 7"));
        REQUIRE(contents.contains("latest.gpu_render_ms = 1.25"));
        REQUIRE(contents.contains("gpu_render.average_ms = 1.25"));
    }

    std::filesystem::remove(profiler.output_path());

    profiler.clear_report();
    REQUIRE_FALSE(profiler.has_report());
    REQUIRE(profiler.gpu_render_summary().samples == 0);
}

TEST_CASE("gpu profiling summarizes recent completed queries") {
    ui::Profiler profiler;
    profiler.set_enabled(true);
    for (int sample = 0; sample < 61; ++sample) {
        profiler.begin_frame();
        profiler.record_gpu_render_ms(static_cast<double>(sample));
        profiler.end_frame();
    }

    profiler.begin_frame();
    profiler.record_gpu_render_ms(std::nullopt);
    profiler.end_frame();

    const ui::ProfileGpuSummary summary = profiler.gpu_render_summary();
    REQUIRE(summary.samples == 60);
    REQUIRE(summary.minimum_ms == 1.0);
    REQUIRE(summary.maximum_ms == 60.0);
    REQUIRE(summary.average_ms == 30.5);

    profiler.set_enabled(false);
    REQUIRE(profiler.gpu_render_summary().samples == 0);
}

TEST_CASE("ui closes render profiling before publishing the frame") {
    ui::Runtime runtime;
    ui::Surface surface = ui_test::make_surface(runtime);
    const auto surface_context = ui_test::prepare_surface(surface, {320.0F, 240.0F});
    surface.profiler().set_enabled(true);

    surface.begin_frame();
    surface.draw();
    surface.end_frame();

    REQUIRE(surface.profiler().latest_metrics().render_ms > 0.0);
}

TEST_CASE("ui profiler separates root update and draw time") {
    ui::Profiler profiler;
    profiler.set_root_node(10);
    profiler.set_enabled(true);
    profiler.begin_frame();

    {
        ui::ScopedProfileZone update(&profiler, "Node::update", 10);
    }
    {
        ui::ScopedProfileZone measure(&profiler, "Node::measure", 10);
    }
    {
        ui::ScopedProfileZone draw(&profiler, "Node::draw", 10);
    }
    {
        ui::ScopedProfileZone input(&profiler, "Node::input", 10);
    }
    {
        ui::ScopedProfileZone layout(&profiler, "Node::layout", 20);
    }
    {
        ui::ScopedProfileZone render(&profiler, "Surface::render");
    }

    profiler.end_frame();
    const ui::ProfileFrameMetrics& metrics = profiler.latest_metrics();
    REQUIRE(metrics.update_ms > 0.0);
    REQUIRE(metrics.measure_ms > 0.0);
    REQUIRE(metrics.layout_ms > 0.0);
    REQUIRE(metrics.draw_ms > 0.0);
    REQUIRE(metrics.input_ms > 0.0);
    REQUIRE(metrics.render_ms > 0.0);
}

TEST_CASE("ui profiler reports event overflow without corrupting the frame") {
    ui::Profiler profiler;
    profiler.set_enabled(true);
    profiler.begin_frame();

    for (std::size_t index = 0; index < ui::Profiler::EVENT_CAPACITY + 1; ++index) {
        ui::ScopedProfileZone zone(&profiler, "overflow");
    }

    profiler.end_frame();

    REQUIRE(profiler.latest_events().size() == ui::Profiler::EVENT_CAPACITY);
    REQUIRE(profiler.dropped_events() == 1);
}

TEST_CASE("ui profiler counts drawn nodes") {
    ui_test::ImGuiContext context({100.0F, 100.0F});
    ImGui::NewFrame();

    ui::Profiler profiler;
    profiler.set_enabled(true);
    ui::StyledNode node("styled");
    node.set_profiler(&profiler);
    node.fade_in();

    profiler.begin_frame();
    node.update(0.01F);
    node.draw();
    profiler.end_frame();

    const ui::ProfileFrameMetrics& metrics = profiler.latest_metrics();
    REQUIRE(metrics.nodes_drawn == 1);

    ImGui::EndFrame();
}
