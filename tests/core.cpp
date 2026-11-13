#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "imgui-context.hpp"
#include <arbor/imgui/border.hpp>
#include <arbor/imgui/effects/blur/blur.hpp>
#include <arbor/imgui/effects/effects.hpp>
#include <arbor/imgui/effects/shadow/shadow.hpp>
#include <arbor/style/gradient-data.hpp>
#include <arbor/style/tween/animator.hpp>
#include <arbor/layout/container.hpp>
#include <arbor/layout/geometry.hpp>
#include <arbor/layout/layer-container.hpp>
#include <arbor/layout/tree-container.hpp>
#include <arbor/resources/font-registry.hpp>
#include <arbor/tree/node.hpp>
#include <arbor/surface.hpp>
#include <arbor/widgets/dropdown.hpp>
#include <arbor/widgets/widget.hpp>

#include <imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <numbers>
#include <string>
#include <utility>
#include <vector>

using namespace ui;

class LifecycleWidget final : public Widget {
public:
    explicit LifecycleWidget(int* destructions = nullptr, std::function<void(Widget&)> update = {})
        : Widget("lifecycle"), m_update(std::move(update)), m_destructions(destructions) {}

    ~LifecycleWidget() override {
        if (m_destructions != nullptr) ++*m_destructions;
    }

private:
    void on_update(float) override {
        if (m_update) m_update(*this);
    }

    std::function<void(Widget&)> m_update;
    int* m_destructions;
};

TEST_CASE("surface scale preserves configured sizes and native coordinates", "[surface][scale]") {
    Runtime runtime;
    Surface surface(runtime, {.backend = std::make_unique<ui_test::TestBackend>(1.5f)});
    const ImGuiContextScope context(surface.imgui_context());
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = {600.0f, 400.0f};
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
    auto& panel = surface.root().add<Container>("scaled");
    panel.set_size({px(100.0f), px(40.0f)});
    panel.style().padding({5.0f, 7.0f}).border_radius(9.0f);
    panel.after().set_draw_callback([](const PaintContext& context) {
        CHECK(context.rect.size().x == Catch::Approx(100.0f).margin(1.0f));
        CHECK(context.rect.size().y == Catch::Approx(40.0f).margin(1.0f));
        if (context.painter.alpha() == 0.0f) return;

        ImDrawList& list = context.painter.draw_list();
        // native glyph origins must remain pixel aligned after the logical position is scaled.
        const int first = list.VtxBuffer.Size;
        const ImVec2 position = {context.rect.min.x + 12.3f, context.rect.min.y + 12.7f};
        context.painter.text(position, rgb(255, 255, 255), "I");
        REQUIRE(list.VtxBuffer.Size == first + 4);
        const ImFontGlyph* glyph = ImGui::GetFontBaked()->FindGlyph('I');
        CHECK(list.VtxBuffer[first].pos.x - glyph->X0 == Catch::Approx(std::floor(context.painter.to_screen(position).x)));
    });

    for (float scale : {1.0f, 1.25f, 2.0f, 0.75f, 1.0f}) {
        surface.set_scale(scale);
        ui_test::draw_surface(surface);

        CHECK(ImGui::GetStyle().FontScaleDpi == Catch::Approx(1.5f * scale));
        CHECK(panel.layout().size().x == Catch::Approx(100.0f * 1.5f * scale));
        CHECK(panel.layout().size().y == Catch::Approx(40.0f * 1.5f * scale));
    }

    CHECK(surface.scale() == 1.0f);
    CHECK(io.DisplaySize.x == 600.0f);
    CHECK(io.DisplayFramebufferScale.x == 1.0f);
    CHECK(panel.layout().size_spec().width.value == 100.0f);
    CHECK(panel.computed_style().padding().left == 5.0f);
    CHECK(panel.computed_style().border_radius() == 9.0f);

    CHECK_THROWS_AS(surface.set_scale(0.0f), std::invalid_argument);
    CHECK_THROWS_AS(surface.set_scale(-1.0f), std::invalid_argument);
    CHECK_THROWS_AS(surface.set_scale(std::numeric_limits<float>::infinity()), std::invalid_argument);
    CHECK_THROWS_AS(surface.set_scale(std::numeric_limits<float>::quiet_NaN()), std::invalid_argument);
}

TEST_CASE("fonts load once per context and tolerate an empty origin", "[resources][cleanup]") {
    Font empty({}, ImFontConfig{});
    Font font(std::string(ARBOR_ASSETS_DIR) + "/fonts/Inter.ttf", ImFontConfig{});
    ::ImGuiContext* previous = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(nullptr);
    CHECK(empty.get() == nullptr);
    CHECK(font.get() == nullptr);
    ImGui::SetCurrentContext(previous);

    ui_test::ImGuiContext first({320.0F, 180.0F});
    CHECK(empty.get() == nullptr);
    ImFont* first_font = font.get();
    REQUIRE(first_font != nullptr);
    CHECK(font.get() == first_font);
    {
        ui_test::ImGuiContext second({320.0F, 180.0F});
        ImFont* second_font = font.get();
        REQUIRE(second_font != nullptr);
        CHECK(second_font != first_font);
        font.release_context(ImGui::GetCurrentContext());
    }
    CHECK(font.get() == first_font);
    font.release_context(ImGui::GetCurrentContext());
    CHECK(font.get() != first_font);
}

TEST_CASE("surface primary font replaces the embedded fallback for text", "[resources][font][regression]") {
    Runtime runtime;
    Font* primary = runtime.fonts().add("primary", std::string(ARBOR_ASSETS_DIR) + "/fonts/Inter.ttf");
    Surface surface = ui_test::make_surface(runtime);
    surface.set_primary_font(primary);
    auto& text = surface.root().add<Widget>("primary-font-text");
    text.set_size({px(100.0F), px(30.0F)});
    ImFont* drawn_font = nullptr;
    text.before().set_draw_callback([&](const PaintContext&) { drawn_font = ImGui::GetFont(); });

    const ImGuiContextScope scope(surface.imgui_context());
    ImGui::GetIO().DisplaySize = {320.0F, 180.0F};
    ImGui::GetIO().BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
    ui_test::draw_surface(surface);

    REQUIRE(drawn_font == primary->get());
}

TEST_CASE("node animator callbacks keep registration order and defer callbacks scheduled during dispatch", "[Animator]") {
    StyledNode node("animated");
    Animator& animator = node.animator();
    std::vector<int> calls;
    float value = 0.0F;
    animator.animate().to(value, 1.0F, {3.0F, easing::linear});

    animator.animate().delay(1.0F).end([&] {
        calls.push_back(1);
        animator.animate().end([&] { calls.push_back(4); });
    });
    animator.animate().delay(3.0F).end([&] { calls.push_back(3); });
    animator.animate().delay(2.0F).end([&] { calls.push_back(2); });
    animator.animate().delay(1.0F).end([&] { calls.push_back(5); });

    node.update(2.0F);
    REQUIRE(calls == (std::vector<int>{1, 2, 5}));
    REQUIRE(value == Catch::Approx(2.0F / 3.0F));
    REQUIRE(animator.transitioning());

    node.update(0.0F);
    REQUIRE(calls == (std::vector<int>{1, 2, 5, 4}));

    node.update(1.0F);
    REQUIRE(calls == (std::vector<int>{1, 2, 5, 4, 3}));
    REQUIRE(value == Catch::Approx(1.0F));
    REQUIRE_FALSE(animator.transitioning());
}

TEST_CASE("colors retain directional gradients and solid fallbacks") {
    const Color vertical = gradient(
        GradientType::Linear, {{0.0F, rgb(1.0F, 0.0F, 0.0F)}, {1.0F, rgb(0.0F, 0.0F, 1.0F)}}, {0.0F, 0.0F}, {0.0F, 1.0F}
    );
    REQUIRE(vertical.rgba().x == Catch::Approx(0.5F));
    REQUIRE(vertical.rgba().z == Catch::Approx(0.5F));

    const Color radial =
        gradient(GradientType::Radial, {{0.0F, rgba(1.0F, 1.0F, 1.0F, 0.5F)}, {1.0F, rgba(0.0F, 0.0F, 0.0F, 0.0F)}});
    REQUIRE(radial.max_alpha() == 0.5F);
}

static const BoxShadowRegion* collected_shadow_region = nullptr;
static const ImDrawList* collected_effect_draw_list = nullptr;

static void collect_shadow_callback(void*, const ImDrawList* draw_list, const ImDrawCmd*, const void* command) {
    collected_shadow_region = static_cast<const BoxShadowRegion*>(command);
    collected_effect_draw_list = draw_list;
}

static void render_effect_commands(const ImDrawData& draw_data) {
    for (int list_index = 0; list_index < draw_data.CmdListsCount; ++list_index) {
        const ImDrawList* draw_list = draw_data.CmdLists[list_index];
        for (const ImDrawCmd& command : draw_list->CmdBuffer) {
            if (command.UserCallback != nullptr && command.UserCallback != ImDrawCallback_ResetRenderState) {
                command.UserCallback(draw_list, &command);
            }
        }
    }
}

template <typename Command>
static void initialize_effect(EffectRegistry& effects, EffectSlot slot, EffectRender render) {
    effects.register_effect<Command>(slot, {render});
    effects.initialize();
    effects.begin_frame();
}

TEST_CASE("ui does not write to imgui's fallback window") {
    Runtime runtime;
    Surface surface = ui_test::make_surface(runtime);
    surface.root().add<Node>("content");

    const auto surface_context = ui_test::prepare_surface(surface, {320.0F, 240.0F});
    surface.begin_frame();

    ImGuiWindow* fallback = ImGui::FindWindowByName("Debug##Default");
    REQUIRE(fallback != nullptr);
    REQUIRE_FALSE(fallback->WriteAccessed);

    surface.update(ImGui::GetIO().DeltaTime);
    REQUIRE_FALSE(fallback->WriteAccessed);

    surface.draw();
    REQUIRE_FALSE(fallback->WriteAccessed);

    surface.end_frame();
    REQUIRE_FALSE(fallback->Active);
}

TEST_CASE("paint callbacks apply owner and decoration alpha once", "[Painter][paint]") {
    ui_test::ImGuiContext imgui_context({160.0F, 120.0F});
    ui_test::draw_window("painter-alpha-test", [&] {
        StyledNode node;
        node.set_size({px(60.0F), px(30.0F)});
        node.style().alpha(0.5F);
        node.before().style().alpha(0.2f);

        int first_vertex = -1;
        node.before().set_draw_callback([&](const PaintContext& context) {
            first_vertex = context.painter.draw_list().VtxBuffer.Size;
            context.painter.rect_filled(context.rect, rgb(255, 0, 0));
        });
        node.update(1.0F);
        node.draw();

        REQUIRE(first_vertex >= 0);
        const ImU32 expected = ImGui::ColorConvertFloat4ToU32({1.0F, 0.0F, 0.0F, 0.1F});
        REQUIRE(ImGui::GetWindowDrawList()->VtxBuffer[first_vertex].col == expected);
    });
}

TEST_CASE("surface effect clips remain isolated while another surface paints", "[Painter][paint]") {
    Runtime runtime;
    Surface first = ui_test::make_surface(runtime);
    Surface second = ui_test::make_surface(runtime);
    first.paint_state().push_effect_clip({10.0F, 20.0F, 30.0F, 40.0F});

    const auto second_context = ui_test::prepare_surface(second, {160.0F, 120.0F});
    ui_test::draw_window("painter-surface-test", [&] {
        EffectRegistry effects;
        initialize_effect<BoxShadowRegion>(effects, EffectSlot::BoxShadow, collect_shadow_callback);
        Style style;
        style.box_shadow({.blur = 4.0F, .color = rgb(0, 0, 0)});
        ImDrawList& list = *ImGui::GetWindowDrawList();
        const ImVec2 minimum = list.GetClipRectMin();
        const ImVec2 maximum = list.GetClipRectMax();

        const Painter paint(list, &second.paint_state(), &effects);
        paint.frame({{50.0F, 50.0F}, {80.0F, 80.0F}}, style);
        bool found_effect = false;
        for (const ImDrawCmd& command : list.CmdBuffer) {
            if (command.UserCallback == nullptr || command.UserCallback == ImDrawCallback_ResetRenderState) continue;

            found_effect = true;
            REQUIRE(command.ClipRect.x == Catch::Approx(minimum.x));
            REQUIRE(command.ClipRect.y == Catch::Approx(minimum.y));
            REQUIRE(command.ClipRect.z == Catch::Approx(maximum.x));
            REQUIRE(command.ClipRect.w == Catch::Approx(maximum.y));
        }
        REQUIRE(found_effect);
    });

    first.paint_state().pop_effect_clip();
}

TEST_CASE("rounded border paths split corners between adjacent sides") {
    const BorderPath path = rounded_rect_border_path({{10.0F, 20.0F}, {110.0F, 80.0F}}, 12.0F);

    REQUIRE(path[0].type == BorderPathSegmentType::Line);
    REQUIRE(path[0].length == Catch::Approx(76.0F));
    REQUIRE(path[1].type == BorderPathSegmentType::Arc);
    REQUIRE(path[1].sides == BORDER_TOP);
    REQUIRE(path[2].sides == BORDER_RIGHT);
    REQUIRE(path[5].sides == BORDER_BOTTOM);
    REQUIRE(path[8].sides == BORDER_LEFT);
    REQUIRE(path[1].end.x == Catch::Approx(106.49F).margin(0.01F));
    REQUIRE(path[8].start.y == Catch::Approx(76.49F).margin(0.01F));

    const BorderPath clamped = rounded_rect_border_path({{0.0F, 0.0F}, {40.0F, 20.0F}}, 30.0F);
    REQUIRE(clamped[0].length == Catch::Approx(20.0F));
    REQUIRE(clamped[3].length == Catch::Approx(0.0F));
    REQUIRE(clamped[1].length == Catch::Approx(std::numbers::pi_v<float> * 2.5F));
}

TEST_CASE("partial borders keep every draw style inside its selected side") {
    const BorderPath path = rounded_rect_border_path({{10.0F, 20.0F}, {110.0F, 80.0F}}, 12.0F);
    ui_test::ImGuiContext context({160.0F, 120.0F});
    ui_test::draw_window("border-path-test", [&] {
        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        const int vertices_before = draw_list->VtxBuffer.Size;

        const auto require_left_bounds = [&](BorderType style) {
            const int first_vertex = draw_list->VtxBuffer.Size;
            draw_border_path(*ImGui::GetWindowDrawList(), path, BORDER_LEFT, rgb(255, 255, 255), 2.0F, style);
            REQUIRE(draw_list->VtxBuffer.Size > first_vertex);

            float min_y = std::numeric_limits<float>::max();
            float max_y = std::numeric_limits<float>::lowest();
            for (int index = first_vertex; index < draw_list->VtxBuffer.Size; ++index) {
                const float y = draw_list->VtxBuffer[index].pos.y;
                min_y = std::min(min_y, y);
                max_y = std::max(max_y, y);
            }

            REQUIRE(min_y > 21.0F);
            REQUIRE(max_y < 79.0F);
        };

        draw_border_path(*ImGui::GetWindowDrawList(), path, BORDER_NONE, rgb(255, 255, 255), 2.0F, BorderType::Solid);
        REQUIRE(draw_list->VtxBuffer.Size == vertices_before);

        require_left_bounds(BorderType::Solid);
        require_left_bounds(BorderType::Dashed);
        require_left_bounds(BorderType::Dotted);
    });
}

TEST_CASE("patterned borders keep every side visible") {
    const Rect rect = {{20.0F, 20.0F}, {180.0F, 100.0F}};
    const BorderPath path = rounded_rect_border_path(rect, 12.0F);
    ui_test::ImGuiContext context({220.0F, 140.0F});
    ui_test::draw_window("patterned-border-test", [&] {
        const auto require_sides = [&](BorderType style) {
            const int first_vertex = ImGui::GetWindowDrawList()->VtxBuffer.Size;
            draw_border_path(*ImGui::GetWindowDrawList(), path, BORDER_ALL, rgb(255, 255, 255), 2.0F, style);
            const auto& vertices = ImGui::GetWindowDrawList()->VtxBuffer;
            const auto has_side = [&](auto&& predicate) {
                for (int index = first_vertex; index < vertices.Size; ++index) {
                    if (predicate(vertices[index].pos)) {
                        return true;
                    }
                }
                return false;
            };

            REQUIRE(has_side([&](ImVec2 point) {
                return point.x > 35.0F && point.x < 165.0F && std::abs(point.y - rect.min.y) < 3.0F;
            }));
            REQUIRE(has_side([&](ImVec2 point) {
                return point.y > 35.0F && point.y < 85.0F && std::abs(point.x - rect.max.x) < 3.0F;
            }));
            REQUIRE(has_side([&](ImVec2 point) {
                return point.x > 35.0F && point.x < 165.0F && std::abs(point.y - rect.max.y) < 3.0F;
            }));
            REQUIRE(has_side([&](ImVec2 point) {
                return point.y > 35.0F && point.y < 85.0F && std::abs(point.x - rect.min.x) < 3.0F;
            }));
        };

        require_sides(BorderType::Dashed);
        require_sides(BorderType::Dotted);
    });
}

TEST_CASE("style normalizes discrete fields and interpolates effect values") {
    Style style;
    style.border(
        BorderEdges{
            {1.0F, BorderType::Dashed},
            {0.0F, BorderType::None},
            {0.0F, BorderType::None},
            {1.0F, BorderType::Dashed},
        }
    );

    REQUIRE(style.border() == (BORDER_LEFT | BORDER_BOTTOM));
    REQUIRE(style.border_style() == BorderType::Dashed);

    Style target = style;
    target.border_style(BorderType::Dotted);
    REQUIRE(style.border_style() != target.border_style());

    Style::lerp(style, target, 0.0F);
    REQUIRE(style.border_style() == BorderType::Dotted);

    Style edge = style;
    edge.border(BorderEdge{.size = 2.0F, .type = BorderType::Solid});
    Style edge_target = edge;
    edge_target.border_left(6.0F, BorderType::Dashed, {0.2F});
    edge_target.border_top(10.0F, BorderType::Dotted, {0.4F});
    Style::lerp(edge, edge_target, 0.1F);
    REQUIRE(edge.border_left().size == Catch::Approx(4.0F));
    REQUIRE(edge.border_top().size == Catch::Approx(4.0F));
    REQUIRE(edge.border_right().size == Catch::Approx(2.0F));
    REQUIRE(edge.border_left().type == BorderType::Dashed);
    REQUIRE(edge.border_top().type == BorderType::Dotted);

    Style shadow_target;
    shadow_target.box_shadow(
        {
            .offset = {8.0F, 4.0F},
            .blur = 12.0F,
            .spread = 2.0F,
            .color = ImColor{0.1F, 0.2F, 0.3F, 1.4F},
        },
        0.2F
    );
    Style shadow_current;
    Style::lerp(shadow_current, shadow_target, 0.1F);
    REQUIRE(shadow_current.box_shadow().offset.x == Catch::Approx(4.0F));
    REQUIRE(shadow_current.box_shadow().offset.y == Catch::Approx(2.0F));
    REQUIRE(shadow_current.box_shadow().blur == Catch::Approx(6.0F));
    REQUIRE(shadow_current.box_shadow().spread == Catch::Approx(1.0F));
    REQUIRE(shadow_current.box_shadow().color.rgba().w == Catch::Approx(0.5F));

    Style normalized;
    normalized.box_shadow({.blur = -4.0F, .color = ImColor{0.0F, 0.0F, 0.0F, -1.0F}});
    REQUIRE(normalized.box_shadow().blur == 0.0F);
    REQUIRE(normalized.box_shadow().color.rgba().w == 0.0F);
}

TEST_CASE("container shadows stay in their owner draw list") {
    ui_test::ImGuiContext context({320.0F, 240.0F});
    ImGui::NewFrame();
    ImGui::SetNextWindowPos({0.0F, 0.0F});
    ImGui::SetNextWindowSize({320.0F, 240.0F});
    ImGui::Begin("container-shadow-test");

    EffectRegistry effects;
    initialize_effect<BoxShadowRegion>(effects, EffectSlot::BoxShadow, collect_shadow_callback);
    ImDrawList* foreground_draw_list = ImGui::GetForegroundDrawList();

    Container node("container");
    node.set_size({px(100.0F), px(60.0F)});
    node.configure_all_styles([](Style& style) {
        style.background_color(rgb(0.2F, 0.2F, 0.2F))
            .box_shadow({
                .offset = {4.0F, 6.0F},
                .blur = 12.0F,
                .spread = 40.0F,
                .color = rgb(0.0F, 0.0F, 0.0F),
            });
    });
    node.update(1.0F);
    node.draw();
    draw_box_shadow(effects, *ImGui::GetWindowDrawList(), node.layout().visual_rect(), node.computed_style().box_shadow(), 0.0F);

    ImGui::End();
    ImGui::EndFrame();
    ImGui::Render();

    const ImDrawData* draw_data = ImGui::GetDrawData();
    REQUIRE(draw_data != nullptr);
    collected_shadow_region = nullptr;
    collected_effect_draw_list = nullptr;
    render_effect_commands(*draw_data);

    REQUIRE(collected_shadow_region != nullptr);
    REQUIRE(collected_shadow_region->shape.size().x == Catch::Approx(180.0F));
    REQUIRE(collected_shadow_region->shape.size().y == Catch::Approx(140.0F));
    REQUIRE(collected_shadow_region->cutout.size().x == Catch::Approx(100.0F));
    REQUIRE(collected_shadow_region->cutout.size().y == Catch::Approx(60.0F));
    REQUIRE(collected_effect_draw_list != foreground_draw_list);
}

TEST_CASE("styled paint slots render before the node and above completed subtrees") {
    ui_test::ImGuiContext context({160.0F, 120.0F});
    ui_test::draw_window("decoration-test", [&] {
        StyledNode node("node");
        node.set_size({px(80.0F), px(40.0F)});
        node.before().style().background_color(rgb(255, 0, 0));
        node.after().style().border(BorderEdge{1.0F, BorderType::Solid}).border_color(rgb(255, 255, 255));
        node.update(1.0F);

        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        const int vertices_before = draw_list->VtxBuffer.Size;
        ImDrawList* foreground_draw_list = ImGui::GetForegroundDrawList();
        const int foreground_vertices_before = foreground_draw_list->VtxBuffer.Size;
        node.draw();
        REQUIRE(draw_list->VtxBuffer.Size > vertices_before);
        REQUIRE(foreground_draw_list->VtxBuffer.Size > foreground_vertices_before);

        const ImU32 before_color = ImGui::GetColorU32(ImVec4{1.0F, 0.0F, 0.0F, 1.0F});
        const ImU32 after_color = ImGui::GetColorU32(ImVec4{1.0F, 1.0F, 1.0F, 1.0F});
        int first_before = -1;
        int first_after = -1;
        for (int index = vertices_before; index < draw_list->VtxBuffer.Size; ++index) {
            if (draw_list->VtxBuffer[index].col == before_color && first_before < 0) {
                first_before = index;
            }
        }

        for (int index = foreground_vertices_before; index < foreground_draw_list->VtxBuffer.Size; ++index) {
            if (foreground_draw_list->VtxBuffer[index].col == after_color && first_after < 0) first_after = index;
        }

        REQUIRE(first_before >= 0);
        REQUIRE(first_after >= foreground_vertices_before);
    });
}

TEST_CASE("styled paint slots pass owner bounds and their target draw list to callbacks") {
    ui_test::ImGuiContext context({160.0F, 120.0F});
    ui_test::draw_window("decoration-callback-test", [&] {
        StyledNode node("node");
        node.set_size({px(80.0F), px(40.0F)});
        node.configure_all_styles([](Style& style) {
            style.padding({8.0F, 6.0F});
            style.border(BorderEdge{2.0F, BorderType::Solid});
        });
        Rect before_rect{};
        Rect after_rect{};
        Rect before_content_rect{};
        ImDrawList* before_draw_list = nullptr;
        ImDrawList* after_draw_list = nullptr;
        node.before().set_draw_callback([&](const PaintContext& context) {
            before_rect = context.rect;
            before_content_rect = context.content_rect;
            before_draw_list = &context.painter.draw_list();
        });
        node.after().set_draw_callback([&](const PaintContext& context) {
            after_rect = context.rect;
            after_draw_list = &context.painter.draw_list();
        });

        node.update(1.0F);
        node.draw();

        REQUIRE(before_rect.valid());
        REQUIRE(after_rect.valid());
        REQUIRE(before_rect.min.x == Catch::Approx(after_rect.min.x));
        REQUIRE(before_rect.min.y == Catch::Approx(after_rect.min.y));
        REQUIRE(before_rect.max.x == Catch::Approx(after_rect.max.x));
        REQUIRE(before_rect.max.y == Catch::Approx(after_rect.max.y));
        REQUIRE(before_content_rect.min.x == Catch::Approx(before_rect.min.x + 10.0F));
        REQUIRE(before_content_rect.min.y == Catch::Approx(before_rect.min.y + 8.0F));
        REQUIRE(before_content_rect.max.x == Catch::Approx(before_rect.max.x - 10.0F));
        REQUIRE(before_content_rect.max.y == Catch::Approx(before_rect.max.y - 8.0F));
        REQUIRE(before_draw_list == ImGui::GetWindowDrawList());
        REQUIRE(after_draw_list == ImGui::GetForegroundDrawList());
    });
}

TEST_CASE("ui nodes close child scopes before drawing after hooks") {
    ui_test::ImGuiContext context({100.0F, 100.0F});

    class DrawNode final : public Node {
    public:
        DrawNode(std::string id, std::vector<std::string>& events, bool skip = false)
            : Node(std::move(id)), m_events(events), m_skip(skip) {}

    private:
        void on_layout() override {
            m_events.push_back(id() + ":layout");
        }

        bool paint() override {
            m_events.push_back(id() + ":begin");
            return !m_skip;
        }

        void on_draw_end() override {
            m_events.push_back(id() + ":end");
        }

        void draw_after() override {
            m_events.push_back(id() + ":after");
        }

        std::vector<std::string>& m_events;
        bool m_skip = false;
    };

    std::vector<std::string> events;
    DrawNode root("root", events);
    root.add<DrawNode>("child", events);

    root.draw();
    REQUIRE(
        events ==
        std::vector<std::string>{
            "root:layout", "root:begin", "child:layout", "child:begin", "child:end", "child:after", "root:end", "root:after"
        }
    );

    events.clear();
    DrawNode hidden_root("hidden", events, true);
    hidden_root.add<DrawNode>("child", events);

    hidden_root.draw();
    REQUIRE(events == std::vector<std::string>{"hidden:layout", "hidden:begin"});
}

TEST_CASE("external subtrees transfer ownership and reconnect surface input", "[node][ownership]") {
    class AttachedWidget final : public Widget {
    public:
        using Node::surface;
        using Widget::Widget;
    };

    auto page = std::make_unique<Container>("page");
    auto& field = page->add<AttachedWidget>("field");
    Container* original = page.get();

    Runtime runtime;
    Surface surface = ui_test::make_surface(runtime);
    auto& first = surface.root().add<Container>("first");
    auto& second = surface.root().add<Container>("second");

    Container& attached = first.add(std::move(page));
    REQUIRE(page == nullptr);
    REQUIRE(&attached == original);
    REQUIRE(attached.parent() == &first);
    REQUIRE(field.parent() == &attached);
    REQUIRE(&field.surface() == &surface);
    REQUIRE(surface.input_router().set_focus(&field));

    auto detached = first.detach(attached);
    REQUIRE(surface.input_router().focused_node() == nullptr);

    Node& moved = second.add(std::move(detached));
    REQUIRE(first.children().empty());
    REQUIRE(&moved == original);
    REQUIRE(moved.parent() == &second);
    REQUIRE(&field.surface() == &surface);
    REQUIRE(surface.input_router().set_focus(&field));
    REQUIRE_THROWS_AS(second.add(std::unique_ptr<Node>{}), std::invalid_argument);
}

TEST_CASE("clearing children defers subtree destruction and clears input targets") {
    int destructions = 0;
    Node parent("parent");
    auto& child = parent.add<LifecycleWidget>(&destructions);
    child.add<LifecycleWidget>(&destructions);

    InputRouter router;
    parent.set_input_router(&router);
    REQUIRE(router.set_focus(&child));
    REQUIRE(router.capture_pointer(child));

    // clear removes input eligibility immediately but keeps the subtree alive until its parent's next update.
    parent.clear();
    REQUIRE(child.removal_pending());
    REQUIRE(destructions == 0);

    parent.update(0.0f);

    REQUIRE(parent.children().empty());
    REQUIRE(destructions == 2);
    REQUIRE(router.focused_node() == nullptr);
}

TEST_CASE("nodes can remove active widgets and containers during update") {
    int self_updates = 0;
    int self_destructions = 0;
    int container_sibling_updates = 0;
    Container container("self-removal-container");
    auto& self_remover = container.add<LifecycleWidget>(&self_destructions, [&](Widget& self) {
        ++self_updates;
        container.remove(self);
    });
    container.add<LifecycleWidget>(nullptr, [&](Widget&) { ++container_sibling_updates; });

    // self-removal marks the current child without destroying the object while its update callback is running.
    container.update(1.0f);

    REQUIRE(self_updates == 1);
    REQUIRE(self_remover.removal_pending());
    REQUIRE(self_destructions == 0);
    REQUIRE(container_sibling_updates == 1);

    container.update(1.0f);
    REQUIRE(self_destructions == 1);
    REQUIRE(container.children().size() == 1);
    REQUIRE(container_sibling_updates == 2);

    int layer_destructions = 0;
    int remover_updates = 0;
    int root_sibling_updates = 0;
    Node root("update-root");
    auto& layer = root.add<LayerContainer>("tracked-layer");
    layer.add<LifecycleWidget>(&layer_destructions, [&](Widget&) {
        ++remover_updates;
        root.remove(layer);
    });
    root.add<LifecycleWidget>(nullptr, [&](Widget&) { ++root_sibling_updates; });

    root.update(1.0f);

    REQUIRE(remover_updates == 1);
    REQUIRE(layer.removal_pending());
    REQUIRE(layer_destructions == 0);
    REQUIRE(root_sibling_updates == 1);

    root.update(1.0f);
    REQUIRE(layer_destructions == 1);
    REQUIRE(root.children().size() == 1);
    REQUIRE(root_sibling_updates == 2);
}

TEST_CASE("containers can clear descendants during update") {
    int destructions = 0;
    Container container("clear-during-update");
    auto& clearer = container.add<LifecycleWidget>(nullptr, [&](Widget&) { container.clear(); });
    container.add<LifecycleWidget>(&destructions);

    // the later sibling is removed before its turn, while the currently updating child survives until the next pass.
    container.update(1.0f);

    REQUIRE(clearer.removal_pending());
    REQUIRE(destructions == 1);

    container.update(1.0f);
    REQUIRE(container.children().empty());
}

TEST_CASE("visual bounds stay on layout unless paint overrides them") {
    class NoItemNode final : public Node {
    public:
        using Node::Node;
    };

    class ItemNode final : public Node {
    public:
        ItemNode(std::string id, bool input) : Node(std::move(id)) {
            set_size({px(10.0F), px(10.0F)});
            if (input) {
                set_input_mode(InputMode::Target);
            }
        }

    private:
        bool paint() override {
            ImGui::Dummy({40.0F, 30.0F});
            return true;
        }
    };

    class ManualRectNode final : public Node {
    public:
        ManualRectNode() : Node("manual") {}

    private:
        bool paint() override {
            set_visual_rect({{40.0F, 50.0F}, {70.0F, 80.0F}});
            return true;
        }
    };

    ui_test::ImGuiContext context({200.0F, 120.0F});

    ImGui::NewFrame();
    ImGui::Begin("leaf-rect-test");
    ImGui::Dummy({30.0F, 20.0F});

    NoItemNode no_item("no-item");
    no_item.draw();

    ItemNode passive_item("passive-item", false);
    passive_item.draw();

    ItemNode input_item("input-item", true);
    input_item.draw();

    ManualRectNode manual;
    manual.draw();

    ImGui::End();
    ImGui::EndFrame();

    const Rect no_item_rect = no_item.layout().visual_rect();
    const Rect passive_item_rect = passive_item.layout().visual_rect();
    const Rect input_item_rect = input_item.layout().visual_rect();
    const Rect manual_rect = manual.layout().visual_rect();

    REQUIRE_FALSE(no_item_rect.valid());
    REQUIRE(passive_item_rect.size().x == 10.0F);
    REQUIRE(passive_item_rect.size().y == 10.0F);
    REQUIRE(input_item.layout().layout_rect().size().x == 10.0F);
    REQUIRE(input_item.layout().layout_rect().size().y == 10.0F);
    REQUIRE(input_item_rect.size().x == 10.0F);
    REQUIRE(input_item_rect.size().y == 10.0F);
    REQUIRE(manual_rect.min.x == 40.0F);
    REQUIRE(manual_rect.min.y == 50.0F);
    REQUIRE(manual_rect.max.x == 70.0F);
    REQUIRE(manual_rect.max.y == 80.0F);
}

TEST_CASE("nodes register only explicitly configured local input entries") {
    class RectNode final : public Node {
    public:
        RectNode(std::string id, Rect rect, std::function<void(UiEvent&)> callback = {})
            : Node(std::move(id)), m_rect(rect), m_callback(std::move(callback)) {}

    private:
        void event(UiEvent& event) override {
            if (m_callback) {
                m_callback(event);
            }
        }

        bool paint() override {
            set_visual_rect(m_rect);
            return true;
        }

        Rect m_rect;
        std::function<void(UiEvent&)> m_callback;
    };

    InputRouter router;
    Node root("root");
    int callbacks = 0;
    auto& passive = root.add<RectNode>("passive", Rect{{100.0F, 20.0F}, {200.0F, 120.0F}});
    auto& target = root.add<RectNode>("target", Rect{{100.0F, 20.0F}, {200.0F, 120.0F}}, [&callbacks](UiEvent&) { ++callbacks; });
    target.set_input_mode(InputMode::Target, {{10.0F, 20.0F}, {50.0F, 60.0F}});
    root.set_input_router(&router);

    root.draw();

    REQUIRE(router.node_at({120.0F, 50.0F}) == &target);
    REQUIRE(router.node_at({180.0F, 100.0F}) == nullptr);
    REQUIRE(router.node_at({120.0F, 50.0F}) != &passive);

    UiEvent event = UiEvent::make(EventType::PointerDown);
    event.position = {120.0F, 50.0F};
    event.button = PointerButton::Left;
    REQUIRE_FALSE(router.dispatch(event));
    REQUIRE(callbacks == 1);
}

TEST_CASE("positioned nodes preserve their cursor placement when drawing is skipped") {
    class SkippedNode final : public Node {
    private:
        bool paint() override {
            return false;
        }
    };

    class EmptyNode final : public Node {
    public:
        using Node::Node;
    };

    ui_test::ImGuiContext context({200.0F, 120.0F});
    Container container("container");
    container.set_size({px(100.0F), px(80.0F)});
    auto& skipped = container.add<SkippedNode>();
    skipped.set_layout({.placement = {.offset = {120.0F, 0.0F}}, .in_flow = false});
    auto& empty = container.add<EmptyNode>();
    empty.set_layout({.placement = {.offset = {140.0F, 20.0F}}, .in_flow = false});

    ui_test::draw_node(container, "skipped-placement-test");

    REQUIRE(skipped.layout().local_rect().min.x == container.style().padding().left + 120.0F);
}

TEST_CASE("inline layers draw children in a native child window") {
    class WindowNode final : public Node {
    public:
        ImGuiWindow* window = nullptr;

    private:
        bool paint() override {
            window = ImGui::GetCurrentWindowRead();
            return true;
        }
    };

    ui_test::ImGuiContext context({200.0F, 120.0F});
    LayerContainer overlay("overlay");
    auto& child = overlay.add<WindowNode>();

    REQUIRE_FALSE(overlay.layout().in_flow());

    ui_test::draw_node(overlay, "surface");

    REQUIRE(child.window != nullptr);
    REQUIRE((child.window->Flags & ImGuiWindowFlags_ChildWindow) != 0);
    REQUIRE(child.window->ParentWindow == ImGui::FindWindowByName("surface"));
    REQUIRE(overlay.layout().layout_rect().min.x == Catch::Approx(child.window->Pos.x));
    REQUIRE(overlay.layout().layout_rect().min.y == Catch::Approx(child.window->Pos.y));
    REQUIRE(overlay.layout().layout_rect().size().x == Catch::Approx(200.0F));
    REQUIRE(overlay.layout().layout_rect().size().y == Catch::Approx(120.0F));
}
