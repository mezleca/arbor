#include <catch2/catch_approx.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/catch_test_macros.hpp>
#include <imgui_internal.h>

#include <imgui-ui/layout/container.hpp>
#include <imgui-ui/layout/geometry.hpp>
#include <imgui-ui/layout/layer-container.hpp>
#include <imgui-ui/layout/resizable-container.hpp>
#include <imgui-ui/layout/tree-container.hpp>
#include <imgui-ui/layout/virtual-layout.hpp>
#include <imgui-ui/input/router.hpp>
#include <imgui-ui/widgets/text.hpp>
#include <imgui-ui/widgets/button.hpp>
#include "imgui-context.hpp"

#include <cfloat>
#include <algorithm>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace ui;

class LayoutProbeNode : public Node {
public:
    explicit LayoutProbeNode(std::string id = {}) : Node(std::move(id)) {}

    LayoutProbeNode(std::string id, ImVec2 size) : LayoutProbeNode(std::move(id)) {
        set_size({px(size.x), px(size.y)});
    }

    explicit LayoutProbeNode(ImVec2 size) : LayoutProbeNode({}, size) {}

private:
    bool paint() override {
        ImGui::Dummy(layout().size());
        return true;
    }
};

class MeasuredLayoutNode final : public LayoutProbeNode {
public:
    explicit MeasuredLayoutNode(ImVec2 size) : m_intrinsic_size(size) {
        set_size({fit(), fit()});
    }

    void set_intrinsic_size(ImVec2 size) {
        m_intrinsic_size = size;
        invalidate_measure();
    }

private:
    void on_measure() override {
        set_measured_size(m_intrinsic_size);
    }

    ImVec2 m_intrinsic_size;
};

static void draw_open_tree(Node& node, const char* name) {
    ui_test::draw_window(name, [&] {
        ImGui::SetNextItemOpen(true, ImGuiCond_Always);
        node.draw();
    });
}

TEST_CASE("wrap measures and allocates each line in either direction", "[layout][wrap]") {
    ui_test::ImGuiContext context({500, 400});
    const auto direction = GENERATE(StackDirection::Horizontal, StackDirection::Vertical);
    const int main = direction == StackDirection::Horizontal ? 0 : 1;
    const int cross = 1 - main;
    const auto size = [main](LayoutAxis along, LayoutAxis across) {
        return main == 0 ? LayoutSize{along, across} : LayoutSize{across, along};
    };
    const auto extent = [main](float along, float across) { return main == 0 ? ImVec2{along, across} : ImVec2{across, along}; };
    Container row("wrapping-row", direction);
    row.set_size(size(px(210), fit()));
    row.set_spacing(10);
    auto& first = row.add<MeasuredLayoutNode>(extent(100, 20));
    auto& second = row.add<MeasuredLayoutNode>(extent(100, 40));
    auto& third = row.add<MeasuredLayoutNode>(extent(100, 30));
    ui_test::draw_node(row, "wrap-lines");
    CHECK(third.layout().local_rect().min[main] == Catch::Approx(220));
    CHECK(row.layout().size()[cross] == Catch::Approx(40));
    row.set_wrap(StackWrap::Wrap);
    ui_test::draw_node(row, "wrap-lines");
    CHECK(first.layout().local_rect().min[cross] == Catch::Approx(second.layout().local_rect().min[cross]));
    CHECK(third.layout().local_rect().min[main] == Catch::Approx(0));
    CHECK(third.layout().local_rect().min[cross] == Catch::Approx(50));
    CHECK(row.layout().size()[cross] == Catch::Approx(80));

    SECTION("grow limits are shared within each line") {
        first.set_size(size({grow(1), px(80), px(90)}, px(20)));
        second.set_size(size({grow(2), px(80), px(300)}, px(40)));
        third.set_size(size({grow(), px(80), px(300)}, px(30)));
        ui_test::draw_node(row, "wrap-lines");
        CHECK(first.layout().size()[main] == Catch::Approx(90));
        CHECK(second.layout().size()[main] == Catch::Approx(110));
        CHECK(third.layout().size()[main] == Catch::Approx(210));
        CHECK(third.layout().local_rect().min[cross] == Catch::Approx(50));
    }
    SECTION("reverse swaps cross edges without reversing items") {
        row.set_wrap(StackWrap::WrapReverse);
        ui_test::draw_node(row, "wrap-lines");
        CHECK(first.layout().local_rect().min[cross] == Catch::Approx(60));
        CHECK(second.layout().local_rect().min[main] == Catch::Approx(110));
        CHECK(third.layout().local_rect().min[cross] == Catch::Approx(0));
    }
    SECTION("lines stretch and align independently") {
        row.set_size(size(px(200), px(140)));
        row.set_spacing(0);
        row.set_content_alignment(Anchor::Center);
        first.set_size(size(percent(50), grow()));
        third.set_intrinsic_size(extent(100, 20));
        third.set_size(size(percent(50), grow()));
        ui_test::draw_node(row, "wrap-lines");
        CHECK(first.layout().size()[cross] == Catch::Approx(80));
        CHECK(second.layout().local_rect().min[cross] == Catch::Approx(20));
        CHECK(third.layout().size()[cross] == Catch::Approx(60));
        CHECK(third.layout().local_rect().min[main] == Catch::Approx(50));
        CHECK(third.layout().local_rect().min[cross] == Catch::Approx(80));
    }
    SECTION("zero sized items stay on a full line and empty rows reserve no gap") {
        row.set_size(size(px(100), fit()));
        row.set_spacing(0);
        second.set_size(size(px(0), px(5)));
        third.set_size(size(px(1), px(10)));
        ui_test::draw_node(row, "wrap-lines");
        CHECK(second.layout().local_rect().min[cross] == Catch::Approx(0));
        CHECK(third.layout().local_rect().min[cross] == Catch::Approx(20));
        CHECK(row.layout().size()[cross] == Catch::Approx(30));
        row.clear();
        row.update(0);
        row.set_spacing(20);
        ui_test::draw_node(row, "wrap-lines");
        CHECK(row.layout().size()[cross] == Catch::Approx(0));
    }
    SECTION("fit main limits and fractional insets") {
        row.set_size(size({fit(), px(0), px(160)}, fit()));
        row.set_spacing(0);
        row.configure_all_styles([main](Style& style) {
            style.padding(main == 0 ? BoxInsets{0.1F, 0.2F} : BoxInsets{0.2F, 0.1F});
        });
        first.set_size(size(px(80), px(20)));
        second.set_size(size(px(80), px(30)));
        third.set_visible(false);
        ui_test::draw_node(row, "wrap-lines");
        CHECK(row.layout().size()[main] == Catch::Approx(160));
        CHECK(row.layout().size()[cross] == Catch::Approx(50.4F));
        row.set_size({fit(), fit()});
        ui_test::draw_node(row, "wrap-lines");
        CHECK(row.layout().size()[main] == Catch::Approx(160.2F));
        CHECK(row.layout().size()[cross] == Catch::Approx(30.4F));
    }
}

TEST_CASE("wrap respects box insets margins visibility and oversized items", "[layout][wrap]") {
    ui_test::ImGuiContext context({500, 400});
    Container row("wrap-boxes", StackDirection::Horizontal);
    row.set_wrap(StackWrap::Wrap);
    auto& first = row.add<Container>("first");
    first.set_size({px(90), px(20)});
    first.configure_all_styles([](Style& style) { style.margin({5, 3, 5, 7}); });
    SECTION("outer sizes include box insets and skip inactive children") {
        row.set_size({px(230), fit()});
        row.configure_all_styles([](Style& style) { style.padding({10, 5}).border(BorderEdge{2, BorderType::Solid}); });
        auto& second = row.add<LayoutProbeNode>(ImVec2{100, 40});
        auto& oversized = row.add<LayoutProbeNode>(ImVec2{250, 25});
        row.add<LayoutProbeNode>(ImVec2{300, 300}).set_visible(false);
        row.add<LayoutProbeNode>(ImVec2{300, 300}).set_anchor(Anchor::TopRight);
        auto& removed = row.add<LayoutProbeNode>(ImVec2{300, 300});
        row.remove(removed);
        ui_test::draw_node(row, "wrap-boxes");
        CHECK(first.layout().local_rect().min.x == Catch::Approx(17));
        CHECK(second.layout().local_rect().min.x == Catch::Approx(112));
        CHECK(oversized.layout().local_rect().min.x == Catch::Approx(12));
        CHECK(oversized.layout().local_rect().min.y == Catch::Approx(47));
        CHECK(oversized.layout().size().x == Catch::Approx(250));
        CHECK(row.layout().size().y == Catch::Approx(79));
    }
    SECTION("reverse keeps physical margins") {
        row.set_size({px(100), px(100)});
        row.set_wrap(StackWrap::WrapReverse);
        ui_test::draw_node(row, "wrap-boxes");
        CHECK(first.layout().local_rect().min.y == Catch::Approx(73));
    }
}

TEST_CASE("nested wrap measurements use the current allocated width", "[layout][wrap][regression]") {
    ui_test::ImGuiContext context({600, 400});
    Container column("outer-column");
    auto& row = column.add<Container>("inner-row", StackDirection::Horizontal);
    row.set_size({grow(), fit()});
    row.set_wrap(StackWrap::Wrap);
    row.set_spacing(10);
    for (int index = 0; index < 3; ++index)
        row.add<LayoutProbeNode>(ImVec2{100, 30});
    auto& footer = column.add<LayoutProbeNode>(ImVec2{100, 20});
    SECTION("resize positions siblings on the first frame") {
        for (const auto& [width, height] : {std::pair{220.0F, 70.0F}, std::pair{320.0F, 30.0F}, std::pair{90.0F, 110.0F}}) {
            column.set_size({px(width), fit()});
            ui_test::draw_node(column, "nested-wrap");
            CHECK(row.layout().size().y == Catch::Approx(height));
            CHECK(footer.layout().local_rect().min.y == Catch::Approx(height));
            CHECK(column.layout().size().y == Catch::Approx(height + 20));
        }
    }
    SECTION("grow sharing resolves fit and grow cross extents") {
        column.set_direction(StackDirection::Horizontal);
        column.set_size({px(200), fit()});
        row.set_spacing(0);
        row.set_size({grow(), GENERATE(fit(), grow())});
        footer.set_size({grow(), px(30)});
        ui_test::draw_node(column, "nested-wrap");
        CHECK(row.layout().size().x == Catch::Approx(100));
        CHECK(row.layout().size().y == Catch::Approx(90));
        CHECK(column.layout().size().y == Catch::Approx(90));
    }
    SECTION("bounded fit width constrains growing wrapped children") {
        column.set_size({{fit(), px(0), px(100)}, fit()});
        ui_test::draw_node(column, "nested-wrap");
        CHECK(row.layout().size().x == Catch::Approx(100));
        CHECK(row.layout().size().y == Catch::Approx(110));
        CHECK(footer.layout().local_rect().min.y == Catch::Approx(110));
        CHECK(column.layout().size().y == Catch::Approx(130));
    }
}

TEST_CASE("wrapped rows contribute all lines to the scroll extent", "[layout][wrap][scroll]") {
    ui_test::ImGuiContext context({500, 400});
    Container viewport("wrap-scroll");
    viewport.set_size({px(230), px(60)});
    viewport.set_scrollable(true);
    auto& row = viewport.add<Container>("scroll-row", StackDirection::Horizontal);
    row.set_size({grow(), fit()});
    row.set_wrap(StackWrap::Wrap);
    for (int index = 0; index < 10; ++index)
        row.add<LayoutProbeNode>(ImVec2{100, 20});
    for (int frame = 0; frame < 3; ++frame)
        ui_test::draw_node(viewport, "wrap-scroll-window");
    CHECK(row.layout().size().y == Catch::Approx(100));
    CHECK(viewport.scroll().max().y >= 40);
    viewport.scroll().seek_to({0, 40}, ScrollBehavior::Instant);
    ui_test::draw_node(viewport, "wrap-scroll-window");
    ui_test::draw_node(viewport, "wrap-scroll-window");
    CHECK(viewport.scroll().position().y == Catch::Approx(40));
}

TEST_CASE("tree measurements reserve the wrapped body below the header", "[layout][wrap][TreeContainer]") {
    ui_test::ImGuiContext context({500, 400});
    TreeContainer tree("wrapping tree");
    tree.set_size({px(230), fit()});
    auto& row = tree.add<Container>("tree-row", StackDirection::Horizontal);
    row.set_size({grow(), fit()});
    row.set_wrap(StackWrap::Wrap);
    for (int index = 0; index < 3; ++index)
        row.add<LayoutProbeNode>(ImVec2{100, 20});
    draw_open_tree(tree, "wrap-tree-window");
    draw_open_tree(tree, "wrap-tree-window");
    CHECK(row.layout().size().y == Catch::Approx(40));
    const float initial_height = tree.layout().size().y;

    tree.set_size({px(120), fit()});
    draw_open_tree(tree, "wrap-tree-window");
    CHECK(row.layout().size().y == Catch::Approx(60));
    CHECK(tree.layout().size().y == Catch::Approx(initial_height + 20));
}

TEST_CASE("button children stay inside their card when the parent moves and resizes", "[layout][Widget][regression]") {
    ui_test::ImGuiContext context({800.0F, 600.0F});
    Container root("card-root");
    root.set_size({px(600.0F), px(400.0F)});
    auto& button = root.add<ButtonWidget>("", LayoutSize{percent(50), percent(50)});
    button.set_anchor(Anchor::Center);
    button.configure_all_styles([](Style& style) { style.padding({12.0F, 12.0F}).border(BorderEdge{0.0F, BorderType::None}); });
    auto& content = button.add<Container>("card-content");
    content.set_size({grow(), grow()});
    content.set_anchor(Anchor::Center);
    content.set_input_mode(InputMode::None);
    content.set_content_alignment(Anchor::Center);
    content.set_spacing(10.0F);
    auto& title = content.add<LayoutProbeNode>(ImVec2{80.0F, 30.0F});
    auto& description = content.add<LayoutProbeNode>(ImVec2{120.0F, 20.0F});

    const auto check_content = [&] {
        const Rect card_rect = button.layout().visual_rect();
        const Rect content_rect = content.layout().visual_rect();
        CHECK(content_rect.min.x == Catch::Approx(card_rect.min.x + 12.0F).margin(1.0F));
        CHECK(content_rect.min.y == Catch::Approx(card_rect.min.y + 12.0F).margin(1.0F));
        CHECK(content_rect.max.x == Catch::Approx(card_rect.max.x - 12.0F).margin(1.0F));
        CHECK(content_rect.max.y == Catch::Approx(card_rect.max.y - 12.0F).margin(1.0F));
        CHECK(ui_test::center(title.layout().visual_rect()).x == Catch::Approx(ui_test::center(card_rect).x).margin(1.0F));
        CHECK(ui_test::center(description.layout().visual_rect()).x == Catch::Approx(ui_test::center(card_rect).x).margin(1.0F));
        CHECK(description.layout().visual_rect().min.y - title.layout().visual_rect().max.y == Catch::Approx(10.0F));
        CHECK(
            (title.layout().visual_rect().min.y + description.layout().visual_rect().max.y) * 0.5F ==
            Catch::Approx(ui_test::center(card_rect).y).margin(1.0F)
        );
    };

    ui_test::draw_window("card-layout", {800.0F, 600.0F}, [&] { root.draw(); });
    check_content();

    button.set_anchor(Anchor::BottomRight);
    root.set_size({px(500.0F), px(300.0F)});
    ui_test::draw_window("card-layout", {800.0F, 600.0F}, [&] { root.draw(); });
    check_content();
}

TEST_CASE("buttons arrange flowing children and fit their measured sizes", "[layout][Widget][regression]") {
    ui_test::ImGuiContext context({500.0F, 300.0F});
    ButtonWidget button("");
    button.configure_all_styles([](Style& style) { style.padding({5.0F, 7.0F}).border(BorderEdge{0.0F, BorderType::None}); });
    button.set_direction(StackDirection::Horizontal);
    button.set_spacing(10.0F);
    auto& first = button.add<LayoutProbeNode>(ImVec2{40.0F, 30.0F});
    auto& second = button.add<LayoutProbeNode>(ImVec2{60.0F, 20.0F});

    ui_test::draw_window("button-fit", {500.0F, 300.0F}, [&] { button.draw(); });
    CHECK(button.layout().size().x == Catch::Approx(120.0F));
    CHECK(button.layout().size().y == Catch::Approx(44.0F));
    CHECK(second.layout().visual_rect().min.x - first.layout().visual_rect().max.x == Catch::Approx(10.0F));

    first.set_size({px(70.0F), px(40.0F)});
    ui_test::draw_window("button-fit", {500.0F, 300.0F}, [&] { button.draw(); });
    CHECK(button.layout().size().x == Catch::Approx(150.0F));
    CHECK(button.layout().size().y == Catch::Approx(54.0F));

    auto& custom_first = button.add<Widget>("same-id");
    custom_first.set_size({px(20.0F), px(20.0F)});
    auto& custom_second = button.add<Widget>("same-id");
    custom_second.set_size({px(20.0F), px(20.0F)});
    ui_test::draw_window("button-fit", {500.0F, 300.0F}, [&] { button.draw(); });
    CHECK(custom_second.layout().visual_rect().min.x - custom_first.layout().visual_rect().max.x == Catch::Approx(10.0F));
}

TEST_CASE("style size resolves independently for each visual state", "[layout][Style][size]") {
    ui_test::ImGuiContext context({500.0F, 300.0F});
    ButtonWidget button("MMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMM");
    button.configure_all_styles([](Style& style) { style.padding({}).border(BorderEdge{0.0F, BorderType::None}); });
    button.set_size({px(240.0F), px(44.0F)});
    button.style(StyleType::HOVER).width(fit()).height(fit());
    button.style(StyleType::ACTIVE).size({px(120.0F), px(30.0F)});

    ui_test::draw_node(button, "state-size");
    CHECK(button.layout().size().x == Catch::Approx(240.0F));
    CHECK(button.layout().size().y == Catch::Approx(44.0F));

    button.set_visual_style(StyleType::HOVER);
    button.update(0.1F);
    ui_test::draw_node(button, "state-size");
    CHECK(button.layout().size().x > 240.0F);
    CHECK(button.layout().size_spec().width.mode == LayoutSizeMode::Fit);

    button.set_visual_style(StyleType::ACTIVE);
    button.update(0.1F);
    ui_test::draw_node(button, "state-size");
    CHECK(button.layout().size().x == Catch::Approx(120.0F));
    CHECK(button.layout().size().y == Catch::Approx(30.0F));

    button.set_visual_style(StyleType::DEFAULT);
    button.update(0.1F);
    ui_test::draw_node(button, "state-size");
    CHECK(button.layout().size().x == Catch::Approx(240.0F));
    CHECK(button.layout().size().y == Catch::Approx(44.0F));
}

TEST_CASE("style size transition interpolates the resolved layout extent", "[layout][Style][size]") {
    ui_test::ImGuiContext context({500.0F, 300.0F});
    ButtonWidget button("MMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMM");
    button.configure_all_styles([](Style& style) { style.padding({}).border(BorderEdge{0.0F, BorderType::None}); });
    button.set_size({px(160.0F), px(44.0F)});
    button.style(StyleType::HOVER).size({fit(), px(44.0F)}, {0.1F, easing::linear});

    ui_test::draw_node(button, "state-size-transition");
    CHECK(button.layout().size().x == Catch::Approx(160.0F));

    button.set_visual_style(StyleType::HOVER);
    button.update(0.05F);
    ui_test::draw_node(button, "state-size-transition");
    const float middle = button.layout().size().x;
    CHECK(middle > 160.0F);
    CHECK(button.layout().size().y == Catch::Approx(44.0F));

    button.update(0.06F);
    ui_test::draw_node(button, "state-size-transition");
    CHECK(button.layout().size().x > middle);
    CHECK(button.layout().size_spec().width.mode == LayoutSizeMode::Fit);
    CHECK(button.layout().size().y == Catch::Approx(44.0F));
}

TEST_CASE("size ranges clamp both axes and resolve mixed units after parent resizing", "[layout][size-limits]") {
    ui_test::ImGuiContext context({800.0F, 300.0F});
    Container root("range-root");
    root.set_size({px(400.0F), px(120.0F)});
    auto& child = root.add<ButtonWidget>("range-child", LayoutSize{{px(10.0F), px(100.0F)}, {px(20.0F), px(60.0F)}});
    ui_test::draw_node(root, "size-range-window");
    CHECK(child.layout().size().x == Catch::Approx(100.0F));
    CHECK(child.layout().size().y == Catch::Approx(60.0F));

    child.set_size({px(150.0F), px(20.0F)}, {percent(50.0F), px(20.0F)});
    child.set_anchor(Anchor::BottomRight);
    ui_test::draw_node(root, "size-range-window");
    CHECK(child.layout().size().x == Catch::Approx(200.0F));
    CHECK(child.layout().visual_rect().max.x == Catch::Approx(root.layout().visual_rect().max.x));

    for (const auto& [width, expected] : {std::pair{200.0F, 150.0F}, std::pair{600.0F, 300.0F}}) {
        root.set_size({px(width), px(120.0F)});
        ui_test::draw_node(root, "size-range-window");
        CHECK(child.layout().size().x == Catch::Approx(expected));
    }
}

TEST_CASE("bounded fit measurements propagate to parents without percentage feedback", "[layout][size-limits]") {
    ui_test::ImGuiContext context({400.0F, 220.0F});
    Container root("bounded-fit-root");
    root.set_size({fit(), fit()});
    auto& child = root.add<MeasuredLayoutNode>(ImVec2{200.0F, 24.0F});
    child.set_size({{fit(), px(10.0F), px(100.0F)}, fit()});
    for (const auto& [width, expected] : {std::pair{200.0F, 100.0F}, std::pair{5.0F, 10.0F}}) {
        child.set_intrinsic_size({width, 24.0F});
        ui_test::draw_node(root, "bounded-fit-window");
        CHECK(root.layout().size().x == Catch::Approx(expected));
        CHECK(child.layout().size().x == Catch::Approx(expected));
    }

    child.set_intrinsic_size({200.0F, 24.0F});
    child.set_size({{fit(), px(10.0F), percent(50.0F)}, fit()});
    ui_test::draw_node(root, "bounded-fit-window");
    CHECK(root.layout().size().x == Catch::Approx(200.0F));
    CHECK(child.layout().size().x == Catch::Approx(100.0F));
}

TEST_CASE("stacks redistribute capped grow shares in either direction", "[layout][size-limits]") {
    for (bool horizontal : {false, true}) {
        DYNAMIC_SECTION((horizontal ? "horizontal" : "vertical")) {
            const int main = horizontal ? 0 : 1;
            const auto size = [horizontal](LayoutAxis axis) {
                return horizontal ? LayoutSize{axis, grow()} : LayoutSize{grow(), axis};
            };

            ui_test::ImGuiContext context({500.0F, 500.0F});
            Container root("bounded-stack", horizontal ? StackDirection::Horizontal : StackDirection::Vertical);
            root.set_size(horizontal ? LayoutSize{px(316.0F), px(100.0F)} : LayoutSize{px(100.0F), px(316.0F)});
            root.set_spacing(8.0F);
            auto& first = root.add<LayoutProbeNode>("first");
            auto& second = root.add<LayoutProbeNode>("second");
            auto& third = root.add<LayoutProbeNode>("third");
            first.set_size(size({px(20.0F), px(40.0F)}));
            second.set_size(size({px(20.0F), px(80.0F)}));
            third.set_size(size(grow(2.0F)));
            ui_test::draw_node(root, "bounded-stack-window");
            CHECK(first.layout().size()[main] == Catch::Approx(40.0F));
            CHECK(second.layout().size()[main] == Catch::Approx(80.0F));
            CHECK(third.layout().size()[main] == Catch::Approx(180.0F));

            third.set_visible(false);
            root.set_content_alignment(Anchor::BottomRight);
            ui_test::draw_node(root, "bounded-stack-window");
            CHECK(second.layout().visual_rect().max.x == Catch::Approx(root.layout().visual_rect().max.x));
            CHECK(second.layout().visual_rect().max.y == Catch::Approx(root.layout().visual_rect().max.y));

            root.set_size({px(100.0F), px(100.0F)});
            root.set_spacing(0.0F);
            first.set_size(size({px(80.0F), px(200.0F)}));
            second.set_size(size({px(60.0F), px(200.0F)}));
            ui_test::draw_node(root, "bounded-stack-window");
            CHECK(first.layout().size()[main] == Catch::Approx(80.0F));
            CHECK(second.layout().size()[main] == Catch::Approx(60.0F));
        }
    }
}

TEST_CASE("growing containers remeasure intrinsic content independently of sizing history", "[Container][layout][regression]") {
    ui_test::ImGuiContext context({320.0F, 220.0F});
    Container root("intrinsic-root");
    root.set_size({fit(), fit()});
    auto& content = root.add<Container>("growing-content");
    content.set_size({grow(), grow()});
    auto& item = content.add<MeasuredLayoutNode>(ImVec2{80.0F, 24.0F});

    const auto draw_frame = [&] { ui_test::draw_node(root, "intrinsic-container-window"); };
    draw_frame();
    REQUIRE(root.layout().size().x == Catch::Approx(80.0F));
    REQUIRE(root.layout().size().y == Catch::Approx(24.0F));

    content.set_size({fit(), fit()});
    draw_frame();
    content.set_size({grow(), grow()});
    draw_frame();
    REQUIRE(root.layout().size().x == Catch::Approx(80.0F));
    REQUIRE(root.layout().size().y == Catch::Approx(24.0F));

    item.set_intrinsic_size({120.0F, 32.0F});
    draw_frame();
    REQUIRE(root.layout().size().x == Catch::Approx(120.0F));
    REQUIRE(root.layout().size().y == Catch::Approx(32.0F));
}

TEST_CASE("flexible content keeps footer at the bottom while footer determines fit width", "[Container][layout][regression]") {
    ui_test::ImGuiContext context({320.0F, 220.0F});
    Container root("footer-root");
    root.set_size({fit(), px(180.0F)});
    root.configure_all_styles([](Style& style) { style.border(BorderEdge{0.0F, BorderType::None}).padding({}); });
    root.add<MeasuredLayoutNode>(ImVec2{24.0F, 24.0F});
    root.add<Container>("body").set_size({grow(), grow()});
    auto& footer = root.add<Container>("footer", StackDirection::Horizontal);
    footer.set_size({fit(), fit()});
    footer.configure_all_styles([](Style& style) { style.border(BorderEdge{0.0F, BorderType::None}).padding({}); });
    for (int index = 0; index < 3; ++index)
        footer.add<MeasuredLayoutNode>(ImVec2{40.0F, 24.0F});

    ui_test::draw_node(root, "footer-window");
    REQUIRE(root.layout().size().x == Catch::Approx(120.0F));
    REQUIRE(footer.layout().size().x == Catch::Approx(120.0F));
    REQUIRE(footer.layout().visual_rect().max.y == Catch::Approx(root.layout().visual_rect().max.y));
}

TEST_CASE("content alignment rearranges without remeasurement", "[layout][cleanup]") {
    class MeasuredContainer final : public Container {
    public:
        MeasuredContainer() : Container("measured") {}

        int measurements = 0;

    private:
        void on_measure() override {
            ++measurements;
            Container::on_measure();
        }
    };

    ui_test::ImGuiContext context({320.0F, 180.0F});
    MeasuredContainer root;
    root.set_size({px(200.0F), px(100.0F)});
    auto& child = root.add<LayoutProbeNode>(ImVec2{40.0F, 20.0F});
    ui_test::draw_node(root, "alignment-measure-test");
    const ImVec2 initial_position = child.layout().local_rect().min;
    const int measurements = root.measurements;

    root.set_content_alignment(Anchor::Center);
    ui_test::draw_node(root, "alignment-measure-test");
    CHECK(root.measurements == measurements);
    CHECK(child.layout().local_rect().min.x - initial_position.x == Catch::Approx(80.0F));
    CHECK(child.layout().local_rect().min.y - initial_position.y == Catch::Approx(40.0F));

    root.set_content_alignment(Anchor::BottomRight);
    ui_test::draw_node(root, "alignment-measure-test");
    CHECK(root.measurements == measurements);
    CHECK(child.layout().local_rect().min.x - initial_position.x == Catch::Approx(160.0F));
    CHECK(child.layout().local_rect().min.y - initial_position.y == Catch::Approx(80.0F));
}

TEST_CASE("fit containers preserve content when border thickness changes", "[layout][regression]") {
    ui_test::ImGuiContext context({320.0F, 180.0F});
    Container root("tabs-root");
    root.set_size({px(280.0F), px(140.0F)});

    auto& header = root.add<Container>("tabs-header", StackDirection::Horizontal);
    header.set_size({grow(), fit()});
    header.set_spacing(15.0F);
    header.style().padding({12.0F, 12.0F}).box_sizing(BoxSizing::BorderBox).border_bottom(1.5F);
    header.add<LayoutProbeNode>("tab", ImVec2{80.0F, 20.0F});

    const auto draw_frame = [&root] { ui_test::draw_node(root, "tabs-border-test"); };

    draw_frame();
    const ImVec2 initial_size = header.layout().visual_rect().size();

    header.style().border_thickness(8.0F);
    draw_frame();

    const ImVec2 updated_size = header.layout().visual_rect().size();
    REQUIRE(updated_size.x == Catch::Approx(initial_size.x));
    REQUIRE(updated_size.y == Catch::Approx(52.0F));
}

TEST_CASE("nested fit containers preserve content through padding resize and insertion", "[layout][regression]") {
    Runtime runtime;
    ui::Surface surface = ui_test::make_surface(runtime);
    auto& root = surface.root().add<Container>("root");
    root.set_size({px(280.0F), px(180.0F)});

    auto& wrapper = root.add<Container>("wrapper", StackDirection::Horizontal);
    wrapper.set_size({grow(), fit()});
    wrapper.set_content_alignment(Anchor::CenterLeft);
    wrapper.configure_all_styles([](Style& style) { style.padding({10.0F, 8.0F}); });

    auto& inner = wrapper.add<Container>("inner", StackDirection::Horizontal);
    inner.set_size({grow(), fit()});
    inner.set_content_alignment(Anchor::CenterLeft);
    inner.configure_all_styles([](Style& style) { style.padding({10.0F, 8.0F}).border(BorderEdge{2.0F, BorderType::Solid}); });
    auto& content = inner.add<MeasuredLayoutNode>(ImVec2{40.0F, 13.0F});
    content.set_size({grow(), grow()});

    const auto context = ui_test::prepare_surface(surface, {400.0F, 240.0F});
    const auto draw_frame = [&] {
        ui_test::draw_surface(surface, 1.0F);
        REQUIRE(wrapper.layout().visual_rect().size().y == Catch::Approx(wrapper.layout().size().y));
        REQUIRE(inner.layout().visual_rect().size().y == Catch::Approx(inner.layout().size().y));
    };
    draw_frame();
    REQUIRE(inner.layout().size().x == Catch::Approx(260.0F));
    REQUIRE(content.layout().size().y >= content.layout().intrinsic_size().y);
    const float initial_height = wrapper.layout().size().y;

    draw_frame();
    REQUIRE(wrapper.layout().size().y == Catch::Approx(initial_height));
    REQUIRE(content.layout().size().y >= content.layout().intrinsic_size().y);

    wrapper.configure_all_styles([](Style& style) { style.padding({20.0F, 16.0F}); });
    root.set_size({px(180.0F), px(180.0F)});
    draw_frame();
    REQUIRE(inner.layout().size().x == Catch::Approx(140.0F));
    REQUIRE(wrapper.layout().size().y == Catch::Approx(initial_height + 16.0F));
    REQUIRE(content.layout().size().y >= content.layout().intrinsic_size().y);

    root.set_size({px(380.0F), px(180.0F)});
    draw_frame();
    REQUIRE(inner.layout().size().x == Catch::Approx(340.0F));

    // insert a taller fixed child after drawing. fit height expands and grow receives the remaining width.
    inner.set_spacing(8.0F);
    auto& added = inner.add<LayoutProbeNode>("added", ImVec2{18.0F, 18.0F});
    draw_frame();
    REQUIRE(added.layout().visual_rect().size().y == Catch::Approx(18.0F));
    REQUIRE(wrapper.layout().size().y == Catch::Approx(initial_height + 21.0F));
    REQUIRE(
        content.layout().size().x + added.layout().size().x + 8.0F == Catch::Approx(inner.layout().visual_rect().size().x - 24.0F)
    );
}

TEST_CASE("box sizing includes borders and preserves insets and size limits", "[layout][size-limits]") {
    ui_test::ImGuiContext context({240.0F, 180.0F});
    Container root("box-inset-root");
    root.set_size({px(200.0F), px(150.0F)});

    auto& fixed = root.add<Container>("fixed");
    fixed.set_size({px(20.0F), px(20.0F)});
    fixed.style().padding({4.0F, 5.0F}).border(BorderEdge{3.0F, BorderType::Solid});
    auto& fixed_content = fixed.add<LayoutProbeNode>();
    fixed_content.set_size({grow(), grow()});

    auto& percentage = root.add<Container>("percentage");
    percentage.set_size({percent(10.0F), percent(10.0F)});
    percentage.style().padding({4.0F, 5.0F}).border(BorderEdge{3.0F, BorderType::Solid}).box_sizing(BoxSizing::ContentBox);
    auto& percentage_content = percentage.add<LayoutProbeNode>();
    percentage_content.set_size({grow(), grow()});

    const auto draw_frame = [&root] { ui_test::draw_node(root, "box-inset-test"); };

    draw_frame();
    REQUIRE(fixed.layout().size().x == Catch::Approx(20.0F));
    REQUIRE(fixed.layout().size().y == Catch::Approx(20.0F));
    REQUIRE(fixed_content.layout().size().x == Catch::Approx(6.0F));
    REQUIRE(fixed_content.layout().size().y == Catch::Approx(4.0F));
    REQUIRE(percentage.layout().size().x == Catch::Approx(34.0F));
    REQUIRE(percentage.layout().size().y == Catch::Approx(31.0F));
    REQUIRE(percentage_content.layout().size().x == Catch::Approx(20.0F));
    REQUIRE(percentage_content.layout().size().y == Catch::Approx(15.0F));

    fixed.style().box_sizing(BoxSizing::ContentBox);
    draw_frame();
    REQUIRE(fixed.layout().size().x == Catch::Approx(34.0F));
    REQUIRE(fixed.layout().size().y == Catch::Approx(36.0F));

    fixed.style().box_sizing(BoxSizing::BorderBox);
    fixed.set_size({px(10.0F), px(10.0F)});
    percentage.style().box_sizing(BoxSizing::BorderBox);
    draw_frame();

    REQUIRE(fixed.layout().size().x == Catch::Approx(14.0F));
    REQUIRE(fixed.layout().size().y == Catch::Approx(16.0F));
    REQUIRE(fixed_content.layout().size().x == Catch::Approx(0.0F));
    REQUIRE(fixed_content.layout().size().y == Catch::Approx(0.0F));
    REQUIRE(percentage.layout().size().x == Catch::Approx(20.0F));
    REQUIRE(percentage.layout().size().y == Catch::Approx(16.0F));
    REQUIRE(percentage_content.layout().size().x == Catch::Approx(6.0F));
    REQUIRE(percentage_content.layout().size().y == Catch::Approx(0.0F));

    fixed.set_size({{px(10.0F), px(24.0F)}, px(20.0F)});
    for (BoxSizing sizing : {BoxSizing::ContentBox, BoxSizing::BorderBox}) {
        fixed.style().box_sizing(sizing);
        draw_frame();
        CHECK(fixed.layout().size().x == Catch::Approx(sizing == BoxSizing::ContentBox ? 38.0F : 24.0F));
    }
}

TEST_CASE("asymmetric padding margins and borders determine child position and fit size", "[layout][style]") {
    ui_test::ImGuiContext context({240.0F, 180.0F});
    Container root("asymmetric-insets");
    root.set_size({fit(), fit()});
    root.style()
        .padding({3.0F, 5.0F, 7.0F, 11.0F})
        .border(
            BorderEdges{
                {2.0F, BorderType::Solid},
                {4.0F, BorderType::Dashed},
                {6.0F, BorderType::Dotted},
                {8.0F, BorderType::Solid},
            }
        );

    auto& child = root.add<Container>("asymmetric-child");
    child.set_size({px(20.0F), px(10.0F)});
    child.style().margin({13.0F, 17.0F, 19.0F, 23.0F});
    ui_test::draw_node(root, "asymmetric-insets-test");

    const Rect outer = root.layout().visual_rect();
    const Rect inner = child.layout().visual_rect();
    CHECK(outer.size().x == Catch::Approx(70.0F));
    CHECK(outer.size().y == Catch::Approx(78.0F));
    CHECK(inner.min.x - outer.min.x == Catch::Approx(18.0F));
    CHECK(inner.min.y - outer.min.y == Catch::Approx(26.0F));
}

TEST_CASE("grow distributes content space after box insets", "[layout]") {
    ui_test::ImGuiContext context({220.0F, 180.0F});
    Container root("grow-insets-root");
    root.set_size({px(180.0F), px(120.0F)});

    auto& fixed = root.add<LayoutProbeNode>(ImVec2{80.0F, 20.0F});
    auto& growing = root.add<Container>("growing");
    growing.set_size({px(80.0F), grow()});
    growing.style().padding({0.0F, 8.0F}).border(BorderEdge{2.0F, BorderType::Solid});
    auto& growing_content = growing.add<LayoutProbeNode>();
    growing_content.set_size({grow(), grow()});

    ui_test::draw_node(root, "grow-insets-test");

    REQUIRE(fixed.layout().size().y == Catch::Approx(20.0F));
    REQUIRE(growing.layout().size().y == Catch::Approx(100.0F));
    REQUIRE(growing_content.layout().size().x == Catch::Approx(76.0F));
    REQUIRE(growing_content.layout().size().y == Catch::Approx(80.0F));
}

TEST_CASE("fit parents retain a growing child's box insets", "[layout]") {
    ui_test::ImGuiContext context({220.0F, 180.0F});
    Container root("fit-grow-root");
    root.set_size({px(180.0F), px(120.0F)});

    auto& fit_parent = root.add<Container>("fit-parent");
    fit_parent.set_size({fit(), fit()});
    auto& growing = fit_parent.add<Container>("growing");
    growing.set_size({grow(), grow()});
    growing.style().padding({3.0F, 4.0F}).border(BorderEdge{2.0F, BorderType::Solid});

    ui_test::draw_node(root, "fit-grow-test");

    REQUIRE(fit_parent.layout().size().x == Catch::Approx(10.0F));
    REQUIRE(fit_parent.layout().size().y == Catch::Approx(12.0F));
    REQUIRE(growing.layout().size().x == Catch::Approx(10.0F));
    REQUIRE(growing.layout().size().y == Catch::Approx(12.0F));
}

TEST_CASE("positioned styled children apply margins around their placement") {
    ui_test::ImGuiContext context({240.0F, 160.0F});

    Container container("positioned-margin-container");
    container.set_size({px(200.0F), px(100.0F)});
    auto& child = container.add<TextWidget>("child");
    child.set_size({px(20.0F), px(10.0F)});
    child.style().margin({5.0F, 7.0F});
    child.set_layout({
        .size = {px(20.0F), px(10.0F)},
        .placement = {.offset = {20.0F, 30.0F}},
        .in_flow = false,
    });

    ui_test::draw_node(container, "positioned-margin-test");

    REQUIRE(child.layout().local_rect().min.x == Catch::Approx(25.0F));
    REQUIRE(child.layout().local_rect().min.y == Catch::Approx(37.0F));
}

TEST_CASE("positioned children resolve percentage sizes and custom anchors from their container") {
    ui_test::ImGuiContext context({240.0F, 160.0F});

    Container container("positioned-percent-container");
    container.set_size({px(200.0F), px(100.0F)});
    container.style().padding({});
    auto& child = container.add<LayoutProbeNode>();
    child.set_layout({
        .size = {percent(50.0F), percent(25.0F)},
        .placement =
            {
                .anchor = {0.25F, 0.75F},
                .origin = {0.5F, 1.0F},
            },
        .in_flow = false,
    });

    ui_test::draw_node(container, "positioned-percent-test");

    REQUIRE(child.layout().size().x == Catch::Approx(100.0F));
    REQUIRE(child.layout().size().y == Catch::Approx(25.0F));
    REQUIRE(child.layout().local_rect().min.x == Catch::Approx(0.0F));
    REQUIRE(child.layout().local_rect().min.y == Catch::Approx(50.0F));
}

TEST_CASE("placement changes preserve measured sizing") {
    MeasuredLayoutNode node({32.0F, 18.0F});

    LayoutConfig config = node.layout().config();
    config.placement.anchor = Anchor::Center;
    config.placement.origin = Anchor::Center;
    config.in_flow = false;
    node.set_layout(config);

    ui_test::ImGuiContext context({120.0F, 80.0F});
    ui_test::draw_node(node, "implicit-sizing-test");

    REQUIRE(node.layout().size().x == Catch::Approx(32.0F));
    REQUIRE(node.layout().size().y == Catch::Approx(18.0F));
}

TEST_CASE("stack layout places auto-sized children after their measured height") {
    ui_test::ImGuiContext context({240.0F, 160.0F});

    Container stack("auto-size-stack");
    stack.set_size({px(200.0F), px(100.0F)});
    stack.set_spacing(4.0F);
    stack.add<MeasuredLayoutNode>(ImVec2{40.0F, 18.0F});
    stack.add<MeasuredLayoutNode>(ImVec2{60.0F, 24.0F});

    const auto draw_frame = [&stack] { ui_test::draw_window("stack-auto-size-test", {240.0F, 160.0F}, [&] { stack.draw(); }); };

    draw_frame();

    const Rect first = stack.children()[0]->layout().visual_rect();
    const Rect second = stack.children()[1]->layout().visual_rect();
    REQUIRE(first.size().y == Catch::Approx(18.0F));
    REQUIRE(second.size().y == Catch::Approx(24.0F));
    REQUIRE(second.min.y == Catch::Approx(first.max.y + 4.0F));
    REQUIRE(stack.children()[1]->layout().config().placement.offset.y == Catch::Approx(0.0F));
}

TEST_CASE("tree layout hides closed descendants without reserving their flow space", "[TreeContainer][layout]") {
    ui_test::ImGuiContext context({240.0F, 160.0F});

    Container page("tree-page");
    page.set_size({px(200.0F), px(120.0F)});
    page.style().padding({});

    auto& tree = page.add<TreeContainer>("tree");
    auto& child = tree.add<LayoutProbeNode>("child", ImVec2{40.0F, 20.0F});
    auto& sibling = page.add<LayoutProbeNode>("sibling", ImVec2{40.0F, 20.0F});

    ui_test::draw_node(page, "closed-tree-test");

    REQUIRE_FALSE(child.layout().visual_rect().valid());
    REQUIRE(sibling.layout().visual_rect().min.y >= tree.layout().visual_rect().max.y);
}

TEST_CASE("tree layout nests expanded descendants in imgui flow", "[TreeContainer][layout][regression]") {
    ui_test::ImGuiContext context({240.0F, 220.0F});

    Container page("nested-tree-page");
    page.set_size({px(200.0F), px(180.0F)});
    page.style().padding({});

    auto& outer = page.add<TreeContainer>("outer");
    auto& inner = outer.add<TreeContainer>("inner");
    auto& sibling = page.add<LayoutProbeNode>("sibling", ImVec2{40.0F, 20.0F});

    const auto draw_frame = [&] { draw_open_tree(page, "nested-tree-test"); };

    draw_frame();
    draw_frame();

    REQUIRE(inner.layout().visual_rect().min.y > outer.layout().visual_rect().min.y);
    REQUIRE(sibling.layout().visual_rect().min.y >= outer.layout().visual_rect().max.y);
}

TEST_CASE("tree layout allocates grow children inside its indented body", "[TreeContainer][layout][regression]") {
    class TreeBodyProbe final : public Node {
    public:
        ImVec2 cursor;

    private:
        bool paint() override {
            cursor = ImGui::GetCursorScreenPos();
            ImGui::Dummy(layout().size());
            return true;
        }
    };

    ui_test::ImGuiContext context({240.0F, 160.0F});
    Container page("tree-body-page");
    page.set_size({px(200.0F), px(120.0F)});
    page.style().padding({});

    auto& tree = page.add<TreeContainer>("tree");
    tree.set_size({px(120.0F), px(60.0F)});
    auto& child = tree.add<TreeBodyProbe>();
    child.set_size({grow(), grow()});

    draw_open_tree(page, "tree-body-test");

    const Rect tree_rect = tree.layout().visual_rect();
    REQUIRE(child.cursor.x == Catch::Approx(tree_rect.min.x + ImGui::GetStyle().IndentSpacing));
    REQUIRE(child.layout().size().x == Catch::Approx(tree_rect.max.x - child.cursor.x));
    REQUIRE(child.layout().size().y == Catch::Approx(tree_rect.max.y - child.cursor.y));
}

TEST_CASE("tree layout fit width contains its indented child", "[TreeContainer][layout][regression]") {
    ui_test::ImGuiContext context({240.0F, 160.0F});

    Container page("tree-fit-page");
    page.set_size({px(200.0F), px(120.0F)});
    page.style().padding({});

    auto& tree = page.add<TreeContainer>("tree");
    tree.set_size({fit(), fit()});
    auto& child = tree.add<LayoutProbeNode>("child", ImVec2{80.0F, 20.0F});

    const auto draw_frame = [&] { draw_open_tree(page, "tree-fit-test"); };

    draw_frame();
    draw_frame();

    REQUIRE(child.layout().visual_rect().max.x <= tree.layout().visual_rect().max.x);
}

TEST_CASE("tree layout fit height contains later descendants", "[TreeContainer][layout][regression]") {
    ui_test::ImGuiContext context({240.0F, 200.0F});

    Container page("tree-fit-height-page");
    page.set_size({px(200.0F), px(160.0F)});
    page.style().padding({});

    auto& tree = page.add<TreeContainer>("tree");
    tree.set_size({fit(), fit()});
    auto& content = tree.add<Container>("content");
    content.set_size({fit(), fit()});
    content.add<LayoutProbeNode>("content-item", ImVec2{80.0F, 40.0F});
    auto& later_tree = tree.add<TreeContainer>("later-tree");

    const auto draw_frame = [&page] { ui_test::draw_node(page, "tree-fit-height-test"); };

    draw_frame();
    const ImVec2 tree_click = {tree.layout().visual_rect().min.x + 4.0F, tree.layout().visual_rect().min.y + 4.0F};
    ImGui::GetIO().AddMousePosEvent(tree_click.x, tree_click.y);
    ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, true);
    draw_frame();
    ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, false);
    draw_frame();
    draw_frame();

    const Rect tree_rect = tree.layout().visual_rect();
    REQUIRE(content.layout().visual_rect().max.y <= tree_rect.max.y);
    REQUIRE(later_tree.layout().visual_rect().max.y <= tree_rect.max.y);
}

TEST_CASE("stack layout excludes explicitly positioned children from its flow") {
    ui_test::ImGuiContext context({240.0F, 160.0F});

    Container stack("positioned-child-stack");
    stack.set_size({px(200.0F), px(100.0F)});
    stack.set_spacing(4.0F);
    stack.style().padding({});
    auto& first = stack.add<LayoutProbeNode>(ImVec2{30.0F, 10.0F});
    auto& positioned = stack.add<LayoutProbeNode>(ImVec2{80.0F, 40.0F});
    positioned.set_layout({
        .size = {px(80.0F), px(40.0F)},
        .placement = {.offset = {100.0F, 20.0F}},
        .in_flow = false,
    });
    auto& second = stack.add<LayoutProbeNode>(ImVec2{30.0F, 10.0F});

    ui_test::draw_node(stack, "positioned-child-stack-test");

    REQUIRE(second.layout().local_rect().min.y == Catch::Approx(first.layout().local_rect().min.y + 14.0F));
    REQUIRE(positioned.layout().local_rect().min.x == Catch::Approx(100.0F));
    REQUIRE(positioned.layout().local_rect().min.y == Catch::Approx(20.0F));
}

TEST_CASE("fit content container applies styled margins around flow children", "[Container][layout][style]") {
    ui_test::ImGuiContext context({240.0F, 160.0F});

    Container stack("fit-content-margin-stack");
    stack.set_size({fit(), fit()});
    stack.set_spacing(4.0F);
    auto& first = stack.add<TextWidget>("first");
    first.set_size({px(30.0F), px(10.0F)});
    first.style().margin({3.0F, 5.0F});
    auto& second = stack.add<TextWidget>("second");
    second.set_size({px(10.0F), px(10.0F)});

    ui_test::draw_node(stack, "fit-content-margin-stack-test");

    REQUIRE(stack.layout().size().x == Catch::Approx(36.0F));
    REQUIRE(stack.layout().size().y == Catch::Approx(34.0F));
    REQUIRE(first.layout().local_rect().min.x == Catch::Approx(3.0F));
    REQUIRE(first.layout().local_rect().min.y == Catch::Approx(5.0F));
    REQUIRE(second.layout().local_rect().min.y == Catch::Approx(24.0F));
}

TEST_CASE("fit-height container fills its available width without stretching children", "[Container][layout]") {
    ui_test::ImGuiContext context({240.0F, 160.0F});

    Container root("fit-height-root");
    root.set_size({px(200.0F), px(100.0F)});
    root.style().padding({});

    auto& field = root.add<Container>("fit-height-field");
    field.set_size({grow(), fit()});
    field.style().padding({});
    auto& child = field.add<LayoutProbeNode>();
    child.set_size({grow(), px(20.0F)});

    ui_test::draw_node(root, "fit-height-stack-test");

    REQUIRE(field.layout().size().x == Catch::Approx(200.0F));
    REQUIRE(field.layout().size().y == Catch::Approx(20.0F));
    REQUIRE(field.children()[0]->layout().size().x == Catch::Approx(200.0F));
}

TEST_CASE("fit content stack remeasures after direction and spacing changes") {
    ui_test::ImGuiContext context({240.0F, 160.0F});

    Container stack("fit-content-remeasure");
    stack.set_size({fit(), fit()});
    stack.set_spacing(4.0F);
    stack.configure_all_styles([](Style& style) { style.padding({7.0F, 5.0F}); });
    auto& first = stack.add<LayoutProbeNode>(ImVec2{30.0F, 10.0F});
    auto& second = stack.add<LayoutProbeNode>(ImVec2{50.0F, 20.0F});

    const auto draw_frame = [&stack] { ui_test::draw_node(stack, "fit-content-remeasure-test"); };

    draw_frame();
    REQUIRE(stack.layout().size().x == Catch::Approx(64.0F));
    REQUIRE(stack.layout().size().y == Catch::Approx(44.0F));
    REQUIRE(second.layout().local_rect().min.y - first.layout().local_rect().min.y == Catch::Approx(14.0F));

    stack.set_spacing(10.0F);
    draw_frame();
    REQUIRE(stack.layout().size().x == Catch::Approx(64.0F));
    REQUIRE(stack.layout().size().y == Catch::Approx(50.0F));

    stack.set_direction(StackDirection::Horizontal);
    draw_frame();
    REQUIRE(stack.layout().size().x == Catch::Approx(104.0F));
    REQUIRE(stack.layout().size().y == Catch::Approx(30.0F));
    REQUIRE(second.layout().local_rect().min.x - first.layout().local_rect().min.x == Catch::Approx(40.0F));
    REQUIRE(second.layout().local_rect().min.y == Catch::Approx(first.layout().local_rect().min.y));
}

TEST_CASE("visibility changes in an anchored overlay do not move its fixed sibling") {
    ui_test::ImGuiContext context({640.0F, 360.0F});

    Container overlay("overlay", StackDirection::Horizontal);
    overlay.set_layout({
        .size = {fit(), fit()},
        .placement = {.anchor = Anchor::TopRight, .origin = Anchor::TopRight, .offset = {-20.0F, 20.0F}},
        .in_flow = false,
    });
    overlay.style().padding({});

    auto& optional_panel = overlay.add<LayoutProbeNode>(ImVec2{120.0F, 80.0F});
    optional_panel.set_visible(false);
    auto& dynamic_panel = overlay.add<LayoutProbeNode>(ImVec2{240.0F, 160.0F});

    const auto draw_frame = [&overlay] {
        ui_test::draw_window("overlay-visibility-test", {640.0F, 360.0F}, [&] { overlay.draw(); });
    };

    draw_frame();
    const float initial_x = dynamic_panel.layout().visual_rect().min.x;

    optional_panel.set_visible(true);
    draw_frame();
    const float shown_x = dynamic_panel.layout().visual_rect().min.x;
    const float shown_panel_right = optional_panel.layout().visual_rect().max.x;

    optional_panel.set_visible(false);
    draw_frame();
    const float hidden_x = dynamic_panel.layout().visual_rect().min.x;

    REQUIRE(shown_x == Catch::Approx(initial_x));
    REQUIRE(shown_panel_right <= shown_x);
    REQUIRE(hidden_x == Catch::Approx(initial_x));
}

TEST_CASE("inline layers keep anchored children in place when the parent starts scrolling") {
    class ScrollParent final : public Container {
    public:
        ScrollParent() : Container("scrolling-parent") {}

    protected:
        bool paint() override {
            ImGui::SetNextWindowContentSize({400.0F, 700.0F});
            return Container::paint();
        }
    };

    ui_test::ImGuiContext context({640.0F, 360.0F});
    ScrollParent parent;
    parent.set_size({px(600.0F), px(300.0F)});
    parent.set_scrollable(true);
    parent.style().padding({14.0F, 14.0F});

    auto& layer = parent.add<LayerContainer>("inline-overlay");
    auto& panel = layer.add<LayoutProbeNode>(ImVec2{120.0F, 80.0F});
    panel.set_layout({
        .size = {px(120.0F), px(80.0F)},
        .placement = {.anchor = Anchor::TopRight, .origin = Anchor::TopRight, .offset = {-20.0F, 20.0F}},
        .in_flow = false,
    });

    const auto draw_frame = [&] { ui_test::draw_window("inline-scroll-test", {640.0F, 360.0F}, [&] { parent.draw(); }); };
    draw_frame();
    draw_frame();
    const Rect initial = panel.layout().visual_rect();

    parent.scroll().seek_to({0.0F, 60.0F}, ScrollBehavior::Instant);
    draw_frame();
    draw_frame();
    REQUIRE(parent.scroll().position().y > 0.0F);
    REQUIRE(panel.layout().visual_rect().min.x == Catch::Approx(initial.min.x));
    REQUIRE(panel.layout().visual_rect().min.y == Catch::Approx(initial.min.y));
}

TEST_CASE("horizontal flow follows intrinsic size changes across native items") {
    class FixedItemNode final : public Node {
    public:
        explicit FixedItemNode(ImVec2 size) {
            set_size({px(size.x), px(size.y)});
        }

    private:
        bool paint() override {
            ImGui::Button("native item", layout().size());
            return true;
        }
    };

    ui_test::ImGuiContext context({640.0F, 180.0F});

    Node root("root");
    auto& stack = root.add<Container>("stack", StackDirection::Horizontal);
    stack.set_size({px(620.0F), px(120.0F)});
    stack.set_spacing(8.0F);
    stack.configure_all_styles([](Style& style) { style.padding({8.0F, 8.0F}); });
    auto& measured = stack.add<MeasuredLayoutNode>(ImVec2{60.0F, 20.0F});
    stack.add<FixedItemNode>(ImVec2{180.0F, 30.0F});
    stack.add<LayoutProbeNode>("after-native", ImVec2{30.0F, 20.0F});

    const auto draw_frame = [&root] { ui_test::draw_window("horizontal-stack-test", {640.0F, 180.0F}, [&] { root.draw(); }); };

    draw_frame();

    const Rect text = stack.children()[0]->layout().visual_rect();
    const Rect item = stack.children()[1]->layout().visual_rect();
    const Rect trailing = stack.children()[2]->layout().visual_rect();
    REQUIRE(text.valid());
    REQUIRE(item.valid());
    REQUIRE(item.min.x >= text.max.x + 8.0F);
    REQUIRE(trailing.min.x >= item.max.x + 8.0F);

    measured.set_intrinsic_size({120.0F, 20.0F});
    draw_frame();

    const Rect resized_text = stack.children()[0]->layout().visual_rect();
    const Rect repositioned_item = stack.children()[1]->layout().visual_rect();
    REQUIRE(resized_text.size().x > text.size().x);
    REQUIRE(repositioned_item.min.x >= resized_text.max.x + 8.0F);
}

TEST_CASE("stack divides remaining main-axis space between flexible children", "[layout][regression]") {
    ui_test::ImGuiContext context({360.0F, 140.0F});

    Container stack("flexible-stack", StackDirection::Horizontal);
    stack.set_size({px(300.0F), px(80.0F)});
    stack.set_spacing(5.0F);
    stack.style().padding({10.0F, 10.0F});

    auto& fixed = stack.add<LayoutProbeNode>();
    fixed.set_size({px(60.0F), px(20.0F)});
    auto& first_flexible = stack.add<LayoutProbeNode>();
    first_flexible.set_size({grow(), px(20.0F)});
    auto& hidden = stack.add<LayoutProbeNode>();
    hidden.set_size({px(200.0F), px(20.0F)});
    hidden.set_visible(false);
    auto& second_flexible = stack.add<LayoutProbeNode>();
    second_flexible.set_size({grow(), px(20.0F)});

    ui_test::draw_node(stack, "flexible-stack-test");

    REQUIRE(fixed.layout().size().x == Catch::Approx(60.0F));
    REQUIRE(first_flexible.layout().size().x == Catch::Approx(105.0F));
    REQUIRE(second_flexible.layout().size().x == Catch::Approx(105.0F));
    REQUIRE(second_flexible.layout().local_rect().min.x == Catch::Approx(first_flexible.layout().local_rect().min.x + 110.0F));

    second_flexible.set_size({grow(2.0F), px(20.0F)});
    ui_test::draw_node(stack, "flexible-stack-test");
    REQUIRE(fixed.layout().size().x == Catch::Approx(60.0F));
    REQUIRE(first_flexible.layout().size().x == Catch::Approx(70.0F));
    REQUIRE(second_flexible.layout().size().x == Catch::Approx(140.0F));
}

TEST_CASE("stack resolves percentage children from its content box", "[layout]") {
    ui_test::ImGuiContext context({360.0F, 140.0F});

    Container stack("percentage-stack", StackDirection::Horizontal);
    stack.set_size({px(300.0F), px(80.0F)});
    stack.style().padding({});
    auto& child = stack.add<Container>("percentage");
    child.set_size({percent(50.0F), percent(50.0F)});
    child.configure_all_styles([](Style& style) { style.padding({10.0F, 5.0F}).box_sizing(BoxSizing::ContentBox); });

    ui_test::draw_node(stack, "percentage-stack-test");

    REQUIRE(child.layout().size().x == Catch::Approx(170.0F));
    REQUIRE(child.layout().size().y == Catch::Approx(50.0F));

    stack.set_size({px(180.0F), px(60.0F)});
    ui_test::draw_node(stack, "percentage-stack-test");
    REQUIRE(child.layout().size().x == Catch::Approx(110.0F));
    REQUIRE(child.layout().size().y == Catch::Approx(40.0F));
}

TEST_CASE("explicit fit retains measured size beside a growing sibling", "[layout]") {
    ui_test::ImGuiContext context({240.0F, 140.0F});

    Container stack("fit-text-stack", StackDirection::Horizontal);
    stack.set_size({px(200.0F), px(80.0F)});
    stack.style().padding({});

    auto& measured = stack.add<MeasuredLayoutNode>(ImVec2{40.0F, 18.0F});
    measured.set_size({fit(), fit()});
    auto& fill = stack.add<Node>("fill");
    fill.set_size({grow(), px(20.0F)});

    ui_test::draw_node(stack, "fit-text-stack-test");

    REQUIRE(measured.layout().size().x == Catch::Approx(40.0F));
    REQUIRE(measured.layout().size().y == Catch::Approx(18.0F));
    REQUIRE(fill.layout().size().x == Catch::Approx(160.0F));
}

TEST_CASE("containers vertically stack flexible children by default", "[layout][regression]") {
    ui_test::ImGuiContext context({220.0F, 240.0F});

    Container stack("vertical-flexible-stack");
    stack.set_size({px(120.0F), grow()});
    stack.style().padding({6.0F, 6.0F});
    auto& fixed = stack.add<LayoutProbeNode>();
    fixed.set_size({grow(), px(30.0F)});
    auto& flexible = stack.add<LayoutProbeNode>();
    flexible.set_size({grow(), grow()});

    const auto draw_frame = [&stack](float height) {
        ui_test::draw_window("vertical-flexible-stack-test", {220.0F, height}, [&] { stack.draw(); });
    };

    draw_frame(140.0F);
    const float initial_height = flexible.layout().size().y;
    draw_frame(220.0F);

    REQUIRE(fixed.layout().size().x == Catch::Approx(108.0F));
    REQUIRE(flexible.layout().size().x == Catch::Approx(108.0F));
    REQUIRE(flexible.layout().size().y > initial_height);
    REQUIRE(fixed.layout().size().y + flexible.layout().size().y == Catch::Approx(stack.layout().size().y - 12.0F));
}

TEST_CASE("font inheritance remeasures content after style changes and reattachment", "[layout][regression]") {
    class FixedItemNode final : public Node {
    public:
        FixedItemNode() {
            set_size({px(100.0F), px(30.0F)});
        }

    private:
        bool paint() override {
            ImGui::Button("sibling", layout().size());
            return true;
        }
    };

    ui_test::ImGuiContext context({480.0F, 140.0F});

    ImFontConfig default_font_config;
    default_font_config.SizePixels = 13.0F;
    ImFont* small_font = ImGui::GetIO().Fonts->AddFontDefault(&default_font_config);
    ImFontConfig large_font_config;
    large_font_config.SizePixels = 28.0F;
    ImFont* large_font = ImGui::GetIO().Fonts->AddFontDefault(&large_font_config);
    ui_test::ImGuiContext::build_fonts();

    Container parent("font-parent");
    parent.set_size({px(460.0F), px(100.0F)});
    auto& stack = parent.add<Container>("font-stack", StackDirection::Horizontal);
    stack.set_size({px(440.0F), px(60.0F)});
    stack.set_spacing(8.0F);
    stack.add<TextWidget>("notifications: 0");
    stack.add<FixedItemNode>();

    const auto draw_frame = [&parent] {
        ui_test::draw_window("inherited-font-layout-test", {480.0F, 140.0F}, [&] { parent.draw(); });
    };

    draw_frame();
    parent.configure_all_styles([large_font](Style& style) { style.font(large_font); });
    draw_frame();

    const Rect text = stack.children()[0]->layout().visual_rect();
    const Rect sibling = stack.children()[1]->layout().visual_rect();
    const float expected_text_width = large_font->CalcTextSizeA(large_font->LegacySize, FLT_MAX, 0.0F, "notifications: 0").x;

    REQUIRE(text.valid());
    REQUIRE(text.size().x == Catch::Approx(expected_text_width).margin(1.0F));
    REQUIRE(sibling.min.x >= text.max.x + 8.0F);

    Container other_parent("other-parent");
    other_parent.set_size({px(460.0F), px(100.0F)});
    other_parent.set_font(small_font);
    other_parent.add(parent.detach(stack));
    ui_test::draw_node(other_parent, "inherited-font-layout-test");

    const Rect moved_text = stack.children()[0]->layout().visual_rect();
    const float expected_small_width = small_font->CalcTextSizeA(small_font->LegacySize, FLT_MAX, 0.0F, "notifications: 0").x;
    REQUIRE(moved_text.size().x == Catch::Approx(expected_small_width).margin(1.0F));
    REQUIRE(moved_text.size().x < text.size().x);
}

TEST_CASE("ineligible children preserve native mouse pass-through", "[layout][input]") {
    class WindowProbe final : public Container {
    public:
        using Container::Container;
        bool passes_mouse = false;

    private:
        bool paint() override {
            const bool painted = Container::paint();
            passes_mouse = (ImGui::GetCurrentWindow()->Flags & ImGuiWindowFlags_NoMouseInputs) != 0;
            return painted;
        }
    };

    ui_test::ImGuiContext context({320.0F, 220.0F});
    LayerContainer layer("pass-through-layer");
    auto& container = layer.add<WindowProbe>("pass-through-content");
    container.set_input_mode(InputMode::None);
    auto& child = container.add<Node>("input-child");
    child.set_input_mode(InputMode::Target);
    bool eligible = false;

    SECTION("hidden") {
        child.set_visible(false);
    }
    SECTION("disabled") {
        child.set_input_enabled(false);
    }
    SECTION("pending removal") {
        container.remove(child);
    }
    SECTION("eligible") {
        eligible = true;
    }

    ui_test::draw_node(layer, "pass-through-window");
    CHECK(container.passes_mouse == !eligible);
}

TEST_CASE("resizable container stays within its parent bounds") {
    ui_test::ImGuiContext context({320.0F, 220.0F});
    const BoxSizing sizing = GENERATE(BoxSizing::BorderBox, BoxSizing::ContentBox);

    ResizableContainer resizable("resizable");
    InputRouter router;
    resizable.set_input_router(&router);
    resizable.configure_all_styles([sizing](Style& style) {
        style.padding({5.0F, 7.0F}).border(BorderEdge{2.0F, BorderType::Solid}).box_sizing(sizing);
    });
    const ImVec2 insets = sizing == BoxSizing::ContentBox ? ImVec2{14.0F, 18.0F} : ImVec2{};
    resizable.set_size({px(80.0F - insets.x), px(60.0F - insets.y)});
    resizable.set_resize(ResizeAxes::Both);

    const auto draw_frame = [&resizable] {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({0.0F, 0.0F});
        ImGui::SetNextWindowSize({320.0F, 220.0F});
        ImGui::Begin("resize-root");
        ImGui::BeginChild("resize-parent", {120.0F, 90.0F});
        const int foreground_vertices = ImGui::GetForegroundDrawList()->VtxBuffer.Size;
        resizable.draw();
        REQUIRE(ImGui::GetForegroundDrawList()->VtxBuffer.Size == foreground_vertices);
        ImGui::EndChild();
        ImGui::End();
        ImGui::EndFrame();
    };

    router.begin_frame();
    draw_frame();
    const Rect initial_rect = resizable.layout().visual_rect();
    const ImVec2 handle_position = {initial_rect.max.x - 5.0F, initial_rect.max.y - 5.0F};

    REQUIRE(router.node_at({initial_rect.min.x + 5.0F, initial_rect.min.y + 5.0F}) == nullptr);
    REQUIRE(router.node_at(handle_position) == &resizable);

    UiEvent hover = UiEvent::make(EventType::PointerMove);
    hover.position = handle_position;
    router.dispatch(hover);
    REQUIRE(ImGui::GetMouseCursor() == ImGuiMouseCursor_Arrow);

    UiEvent down = UiEvent::make(EventType::PointerDown);
    down.position = handle_position;
    down.button = PointerButton::Left;
    REQUIRE(router.dispatch(down));
    REQUIRE(resizable.resizing());

    UiEvent move = UiEvent::make(EventType::PointerMove);
    move.position = handle_position;
    REQUIRE(router.dispatch(move));
    router.begin_frame();
    draw_frame();
    CHECK(resizable.layout().size().x == Catch::Approx(80.0F));
    CHECK(resizable.layout().size().y == Catch::Approx(60.0F));

    move = UiEvent::make(EventType::PointerMove);
    move.position = {300.0F, 200.0F};
    REQUIRE(router.dispatch(move));

    router.begin_frame();
    draw_frame();

    UiEvent up = UiEvent::make(EventType::PointerUp);
    up.position = move.position;
    up.button = PointerButton::Left;

    SECTION("normal release") {
        REQUIRE(router.dispatch(up));
    }
    SECTION("intercepted release") {
        router.on_event([](UiEvent& event) {
            if (event.type == EventType::PointerUp) event.stop_propagation();
        });
        REQUIRE(router.dispatch(up));
        REQUIRE_FALSE(resizable.input_state().active);
    }
    SECTION("hidden during drag") {
        resizable.set_visible(false);
    }
    SECTION("disabled during drag") {
        resizable.set_input_enabled(false);
    }
    SECTION("capture transferred during drag") {
        Node next("next");
        next.set_input_router(&router);
        REQUIRE(router.capture_pointer(next));
    }

    REQUIRE_FALSE(resizable.resizing());

    REQUIRE(resizable.layout().size().x > 80.0F);
    REQUIRE(resizable.layout().size().x <= 120.0F);
    REQUIRE(resizable.layout().size().y <= 90.0F);
}

TEST_CASE("parent resizing rearranges descendants without repeating intrinsic measurement", "[layout][regression]") {
    class MeasureProbeNode final : public Node {
    public:
        MeasureProbeNode() : Node("measure-probe") {
            set_size({grow(), fit()});
        }

        int measure_count = 0;

    private:
        void on_measure() override {
            ++measure_count;
            set_measured_size({20.0F, 20.0F});
        }
    };

    ui_test::ImGuiContext context({320.0F, 180.0F});
    Container container("resized-container");
    container.set_size({px(120.0F), px(80.0F)});
    auto& probe = container.add<MeasureProbeNode>();

    const auto draw_frame = [&container] { ui_test::draw_node(container, "container-resize-test"); };

    draw_frame();
    REQUIRE(probe.measure_count == 1);

    container.set_size({px(220.0F), px(80.0F)});
    draw_frame();
    REQUIRE(probe.layout().size().x == Catch::Approx(220.0F));
    REQUIRE(probe.layout().size().y == Catch::Approx(20.0F));
    REQUIRE(probe.measure_count == 1);

    probe.invalidate_measure();
    draw_frame();
    REQUIRE(probe.measure_count == 2);
}

TEST_CASE("container after decorations use final bounds above nested child windows") {
    ui_test::ImGuiContext context({320.0F, 220.0F});

    ResizableContainer resizable("decorated-resizable");
    resizable.set_size({px(180.0F), px(120.0F)});
    auto& nested = resizable.add<Container>("nested");
    nested.set_size({grow(), grow()});
    nested.add<TextWidget>("nested content");

    Rect after_rect{};
    ImDrawList* after_draw_list = nullptr;
    resizable.after().set_draw_callback([&](const PaintContext& context) {
        after_rect = context.rect;
        after_draw_list = &context.painter.draw_list();
    });

    ui_test::draw_window("decorated-resizable-root", {320.0F, 220.0F}, [&] { resizable.draw(); });

    const Rect visual_rect = resizable.layout().visual_rect();
    REQUIRE(after_rect.min.x == Catch::Approx(visual_rect.min.x));
    REQUIRE(after_rect.min.y == Catch::Approx(visual_rect.min.y));
    REQUIRE(after_rect.max.x == Catch::Approx(visual_rect.max.x));
    REQUIRE(after_rect.max.y == Catch::Approx(visual_rect.max.y));
    REQUIRE(after_draw_list == ImGui::GetForegroundDrawList());
}

TEST_CASE("transparent anchored layers keep children at the layer origin", "[LayerContainer][layout][regression]") {
    ui_test::ImGuiContext context({320.0F, 220.0F});
    Container root("anchored-layer-root");
    root.set_size({px(180.0F), px(120.0F)});
    root.configure_all_styles([](Style& style) { style.border(BorderEdge{0.0F, BorderType::None}).padding({}); });
    auto& layer = root.add<LayerContainer>("anchored-layer");
    layer.set_size({fit(), fit()});
    layer.set_anchor(Anchor::BottomRight);
    layer.configure_all_styles([](Style& style) {
        style.background_color(rgba(0, 0, 0, 0));
        style.border(BorderEdge{0.0F, BorderType::None});
        style.padding({});
    });
    auto& child = layer.add<MeasuredLayoutNode>(ImVec2{24.0F, 24.0F});

    const auto check_position = [&] {
        ui_test::draw_window("anchored-layer-window", {320.0F, 220.0F}, [&] { root.draw(); });
        const Rect layer_rect = layer.layout().visual_rect();
        const Rect child_rect = child.layout().visual_rect();
        REQUIRE(child_rect.min.x == Catch::Approx(layer_rect.min.x));
        REQUIRE(child_rect.min.y == Catch::Approx(layer_rect.min.y));
        const Rect root_content = root.layout().visual_rect();
        REQUIRE(layer_rect.max.x == Catch::Approx(root_content.max.x));
        REQUIRE(layer_rect.max.y == Catch::Approx(root_content.max.y));
    };

    check_position();
    layer.configure_all_styles([](Style& style) { style.background_color(rgba(20, 24, 32, 240)); });
    check_position();
    layer.configure_all_styles([](Style& style) { style.background_color(rgba(0, 0, 0, 0)); });
    check_position();
}

TEST_CASE("inline layer padding scopes descendant layout") {
    ui_test::ImGuiContext context({320.0F, 220.0F});
    Container root("padded-layer-root");
    root.set_size({px(180.0F), px(120.0F)});
    auto& layer = root.add<LayerContainer>("padded-layer", LayerMode::Inline);
    layer.configure_all_styles([](Style& style) { style.padding({17.0F, 13.0F}); });
    auto& probe = layer.add<LayoutProbeNode>("probe", ImVec2{20.0F, 10.0F});

    ui_test::draw_window("padded-layer-window", {320.0F, 220.0F}, [&] { root.draw(); });

    const Rect layer_rect = layer.layout().visual_rect();
    const Rect probe_rect = probe.layout().visual_rect();
    REQUIRE(probe_rect.min.x == Catch::Approx(layer_rect.min.x + 17.0F));
    REQUIRE(probe_rect.min.y == Catch::Approx(layer_rect.min.y + 13.0F));
}

TEST_CASE("inline layers scale vertices across reused child windows", "[LayerContainer][transform][regression]") {
    class TransformLayer final : public LayerContainer {
    public:
        TransformLayer() : LayerContainer("transform-layer") {}

        ImDrawList* draw_list = nullptr;
        int first_vertex = 0;

    private:
        bool paint() override {
            const bool painted = LayerContainer::paint();
            draw_list = ImGui::GetWindowDrawList();
            first_vertex = draw_list->VtxBuffer.Size;
            const Rect rect = layout().visual_rect();
            draw_list->AddRectFilled(rect.min, rect.max, IM_COL32_WHITE);
            return painted;
        }
    };

    ui_test::ImGuiContext context({320.0F, 220.0F});
    Container root("transform-root");
    root.set_size({px(200.0F), px(120.0F)});
    auto& layer = root.add<TransformLayer>();
    layer.set_size({px(100.0F), px(60.0F)});
    layer.configure_all_styles([](Style& style) { style.padding({1.0F, 1.0F}).scale(0.0F); });
    layer.animate().to(StyleAnimationProperty::Scale, ImVec2{1.0F, 1.0F}, {0.2F});

    for (int frame = 0; frame < 3; ++frame) {
        root.update(0.05F);
        ui_test::draw_window("layer-transform-window", {320.0F, 220.0F}, [&] { root.draw(); });

        REQUIRE(layer.draw_list->VtxBuffer.Size >= layer.first_vertex + 4);
        const ImVec2 min = layer.draw_list->VtxBuffer[layer.first_vertex].pos;
        const ImVec2 max = layer.draw_list->VtxBuffer[layer.first_vertex + 2].pos;
        const float scale = 0.25F * static_cast<float>(frame + 1);
        REQUIRE(max.x - min.x == Catch::Approx(100.0F * scale));
        REQUIRE(max.y - min.y == Catch::Approx(60.0F * scale));
    }
}

TEST_CASE("font size changes remeasure inherited text without loading another font", "[layout][font]") {
    ui_test::ImGuiContext context({320.0F, 220.0F});
    ImGui::GetIO().Fonts->Clear();
    ImGui::GetIO().BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
    ImGui::GetStyle().FontScaleDpi = 1.5F;
    ImFont* font = ImGui::GetIO().Fonts->AddFontDefaultVector();
    const int font_count = ImGui::GetIO().Fonts->Fonts.Size;

    Container root("dynamic-font-root");
    root.set_font(font, 16.0F);
    auto& text = root.add<TextWidget>("dynamic size");
    text.before().set_draw_callback([&](const PaintContext&) {
        CHECK(ImGui::GetFont() == font);
        CHECK(ImGui::GetFontSize() == Catch::Approx(text.font_size() * 1.5F));
    });
    ui_test::draw_node(root, "dynamic-font-window");
    const ImVec2 small = text.layout().visual_rect().size();

    root.configure_all_styles([](Style& style) { style.font_size(28.0F); });
    ui_test::draw_node(root, "dynamic-font-window");

    REQUIRE(text.font() == font);
    REQUIRE(text.font_size() == Catch::Approx(28.0F));
    REQUIRE(text.layout().visual_rect().size().x > small.x);
    REQUIRE(text.layout().visual_rect().size().y == Catch::Approx(42.0F));
    REQUIRE(ImGui::GetIO().Fonts->Fonts.Size == font_count);
}

TEST_CASE("layer modes preserve explicit sizes", "[LayerContainer][layout][regression]") {
    ui_test::ImGuiContext context({320.0F, 220.0F});

    LayerContainer window_layer("sized-window-layer", LayerMode::Window);
    window_layer.set_size({px(180.0F), px(80.0F)});
    ui_test::draw_node(window_layer, "sized-window-parent");
    REQUIRE(window_layer.layout().visual_rect().size().x == Catch::Approx(180.0F));
    REQUIRE(window_layer.layout().visual_rect().size().y == Catch::Approx(80.0F));

    Container root("sized-layer-root");
    root.set_size({px(180.0F), px(120.0F)});
    auto& inline_layer = root.add<LayerContainer>("sized-layer");
    inline_layer.set_size({grow(), px(40.0F)});
    ui_test::draw_window("sized-layer-window", {320.0F, 220.0F}, [&] { root.draw(); });
    REQUIRE(inline_layer.layout().size().x == Catch::Approx(180.0F));
    REQUIRE(inline_layer.layout().size().y == Catch::Approx(40.0F));
    REQUIRE(inline_layer.layout().visual_rect().size().y == Catch::Approx(40.0F));
}

TEST_CASE("popup layers stay inside the viewport", "[LayerContainer][popup][layout]") {
    ui_test::ImGuiContext context({320.0F, 220.0F});
    LayerContainer layer("popup-layer", LayerMode::Popup);
    layer.set_size({px(180.0F), px(80.0F)});
    layer.set_popup_position({300.0F, 210.0F});
    layer.open_popup();

    ui_test::draw_node(layer, "popup-parent");

    const Rect popup = layer.layout().visual_rect();
    const Rect work_area = viewport_work_area();
    REQUIRE(ImGui::GetCurrentContext()->OpenPopupStack.Size == 1);
    REQUIRE((ImGui::GetCurrentContext()->OpenPopupStack[0].Window->Flags & ImGuiWindowFlags_Popup) != 0);
    REQUIRE(popup.min.x >= work_area.min.x);
    REQUIRE(popup.min.y >= work_area.min.y);
    REQUIRE(popup.max.x <= work_area.max.x);
    REQUIRE(popup.max.y <= work_area.max.y);
}

TEST_CASE("movable window layers preserve imgui geometry within size limits", "[LayerContainer][layout][size-limits]") {
    class FloatingLayer final : public LayerContainer {
    public:
        FloatingLayer() : LayerContainer("floating-layer", LayerMode::Window) {}

    protected:
        ImGuiWindowFlags window_flags() const override {
            return LayerContainer::window_flags() & ~(ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);
        }
    };

    ui_test::ImGuiContext context({320.0F, 220.0F});
    Container root("floating-parent");
    root.set_size({px(280.0F), px(180.0F)});
    auto& layer = root.add<FloatingLayer>();
    layer.set_size({px(200.0F), px(100.0F)});
    auto& child = layer.add<LayoutProbeNode>();
    child.set_size({grow(), grow()});

    ui_test::draw_node(root, "floating-window-parent");
    ImGui::SetWindowPos("floating-layer", {30.0F, 40.0F});
    ImGui::SetWindowSize("floating-layer", {150.0F, 80.0F});
    ui_test::draw_node(root, "floating-window-parent");

    const Rect rect = layer.layout().visual_rect();
    REQUIRE(rect.min.x == Catch::Approx(30.0F));
    REQUIRE(rect.min.y == Catch::Approx(40.0F));
    REQUIRE(rect.size().x == Catch::Approx(150.0F));
    REQUIRE(rect.size().y == Catch::Approx(80.0F));
    REQUIRE(child.layout().visual_rect().size().x == Catch::Approx(150.0F));
    REQUIRE_FALSE((ImGui::FindWindowByName("floating-layer")->Flags & ImGuiWindowFlags_NoMove) != 0);

    layer.set_size({{px(200.0F), px(100.0F), percent(50.0F)}, {px(100.0F), px(40.0F), px(90.0F)}});
    ImGui::SetWindowSize("floating-layer", {500.0F, 200.0F});
    ui_test::draw_node(root, "floating-window-parent");
    CHECK(layer.layout().size().x == Catch::Approx(140.0F));
    CHECK(layer.layout().size().y == Catch::Approx(90.0F));

    root.set_size({px(180.0F), px(180.0F)});
    ImGui::SetWindowSize("floating-layer", {10.0F, 10.0F});
    ui_test::draw_node(root, "floating-window-parent");
    CHECK(layer.layout().size().x == Catch::Approx(100.0F));
    CHECK(layer.layout().size().y == Catch::Approx(40.0F));

    layer.set_size({{px(100.0F), px(10.0F), px(20.0F)}, px(40.0F)});
    ImGui::SetWindowSize("floating-layer", {100.0F, 40.0F});
    ui_test::draw_node(root, "floating-window-parent");
    CHECK(layer.layout().size().x == Catch::Approx(20.0F));
}

TEST_CASE("nodes without explicit positions follow the ImGui cursor") {
    class FlowNode final : public Node {
    public:
        explicit FlowNode(std::string id) : Node(std::move(id)) {
            set_input_mode(InputMode::Target);
        }

        bool paint() override {
            ImGui::Dummy({20.0F, 10.0F});
            return true;
        }
    };

    ui_test::ImGuiContext context({200.0F, 120.0F});
    ImGui::NewFrame();
    ImGui::SetNextWindowPos({0.0F, 0.0F});
    ImGui::SetNextWindowSize({200.0F, 120.0F});
    ImGui::Begin("flow-test");

    FlowNode first("first");
    FlowNode second("second");
    FlowNode same_line("same-line");

    first.draw();
    second.draw();
    const Rect first_rect = first.layout().visual_rect();
    const Rect second_rect = second.layout().visual_rect();

    ImGui::SameLine();
    same_line.draw();
    const Rect same_line_rect = same_line.layout().visual_rect();

    REQUIRE(second_rect.min.y > first_rect.min.y);
    REQUIRE(same_line_rect.min.x > second_rect.min.x);

    Node logical_root("logical-root");
    logical_root.add<FlowNode>("routed-child");

    ImGui::SetCursorPos({0.0F, 60.0F});
    logical_root.draw();

    const ImVec2 routed_position = logical_root.children().front()->layout().visual_rect().min;
    REQUIRE(routed_position.y == Catch::Approx(60.0F));

    ImGui::End();
    ImGui::Render();
}

TEST_CASE("draw_at_cursor interleaves positioned nodes with native ImGui items", "[layout]") {
    ui_test::ImGuiContext context({200.0F, 120.0F});
    LayoutProbeNode node("positioned", {20.0F, 10.0F});
    node.set_anchor(Anchor::BottomRight);

    ImVec2 cursor;
    ImVec2 next_cursor;
    ui_test::draw_window("interleaved-layout-test", [&] {
        ImGui::Dummy({30.0F, 10.0F});
        cursor = ImGui::GetCursorScreenPos();
        node.draw_at_cursor();
        next_cursor = ImGui::GetCursorScreenPos();
        ImGui::Dummy({30.0F, 10.0F});
    });

    REQUIRE(node.layout().visual_rect().min.x == Catch::Approx(cursor.x));
    REQUIRE(node.layout().visual_rect().min.y == Catch::Approx(cursor.y));
    REQUIRE(next_cursor.y > node.layout().visual_rect().max.y);
}

TEST_CASE("node screen rectangles follow scrollable child windows") {
    class ScrollProbeNode final : public Node {
    public:
        explicit ScrollProbeNode(std::string node_id) : Node(std::move(node_id)) {
            set_size({px(40.0F), px(20.0F)});
        }

        bool paint() override {
            actual_position = ImGui::GetCursorScreenPos();
            ImGui::Dummy(layout().size());
            return true;
        }

        ImVec2 actual_position;
    };

    class ScrollProbeContainer final : public Container {
    public:
        explicit ScrollProbeContainer(bool horizontal)
            : Container(horizontal ? "horizontal-scroll-probe" : "vertical-scroll-probe"), m_horizontal(horizontal) {
            set_size({px(100.0F), px(50.0F)});
            set_scrollable(!horizontal, horizontal);
        }

        bool scroll_to_end = false;
        ImVec2 current_scroll;

    protected:
        bool paint() override {
            const float content_height = layout().size().y - ImGui::GetStyle().ScrollbarSize;
            ImGui::SetNextWindowContentSize(m_horizontal ? ImVec2{400.0F, content_height} : ImVec2{100.0F, 400.0F});
            return Container::paint();
        }

        void on_draw_end() override {
            if (scroll_to_end) {
                if (m_horizontal) {
                    ImGui::SetScrollX(100.0F);
                } else {
                    ImGui::SetScrollY(100.0F);
                }
            }

            Container::on_draw_end();
        }

        void draw_children() override {
            current_scroll = {ImGui::GetScrollX(), ImGui::GetScrollY()};
            ui::Container::draw_children();
        }

    private:
        bool m_horizontal = false;
    };

    ui_test::ImGuiContext context({240.0F, 160.0F});

    const auto verify_scroll = [](bool horizontal) {
        ScrollProbeContainer container(horizontal);
        ScrollProbeNode* target = nullptr;
        for (int index = 0; index < (horizontal ? 1 : 8); ++index) {
            target = &container.add<ScrollProbeNode>(std::format("item-{}", index));
        }

        const auto draw_frame = [&container] {
            ui_test::draw_window("scroll-root", {240.0F, 160.0F}, [&] { container.draw(); });
        };

        draw_frame();
        container.scroll_to_end = true;
        draw_frame();
        draw_frame();

        REQUIRE((horizontal ? container.current_scroll.x : container.current_scroll.y) > 0.0F);
        REQUIRE(target->layout().visual_rect().min.x == Catch::Approx(target->actual_position.x));
        REQUIRE(target->layout().visual_rect().min.y == Catch::Approx(target->actual_position.y));
    };

    verify_scroll(false);
    verify_scroll(true);
}

TEST_CASE("scrollable containers seek smoothly by default and accept instant seeks") {
    class ScrollProbe final : public Container {
    public:
        ScrollProbe() : Container("scroll-seek-probe") {
            set_size({px(100.0F), px(50.0F)});
            set_scrollable(true);
        }

        ImGuiWindowFlags window_flags = 0;

    protected:
        bool paint() override {
            ImGui::SetNextWindowContentSize({100.0F, 400.0F});
            const bool result = Container::paint();
            window_flags = ImGui::GetCurrentWindow()->Flags;
            return result;
        }
    };

    ui_test::ImGuiContext context({240.0F, 160.0F});
    ScrollProbe container;
    const auto draw_frame = [&] { ui_test::draw_window("scroll-seek-root", {240.0F, 160.0F}, [&] { container.draw(); }); };

    draw_frame();
    REQUIRE((container.window_flags & ImGuiWindowFlags_NoScrollWithMouse) != 0);
    container.scroll().seek_to({0.0F, 100.0F});
    draw_frame();
    REQUIRE(container.scroll().position().y < 100.0F);
    draw_frame();
    REQUIRE(container.scroll().position().y > 0.0F);
    REQUIRE(container.scroll().position().y < 100.0F);

    container.scroll().seek_to({0.0F, 200.0F}, ScrollBehavior::Instant);
    draw_frame();
    draw_frame();
    REQUIRE(container.scroll().position().y == Catch::Approx(200.0F));

    container.scroll().set_behaviour(ScrollBehavior::Instant).seek_by({0.0F, -50.0F});
    draw_frame();
    draw_frame();
    REQUIRE(container.scroll().position().y == Catch::Approx(150.0F));

    InputRouter router;
    container.set_input_router(&router);
    const auto draw_routed_frame = [&] {
        router.begin_frame();
        draw_frame();
    };
    container.scroll().seek_to({0.0F, container.scroll().max().y}, ScrollBehavior::Instant);
    draw_routed_frame();
    draw_routed_frame();

    UiEvent wheel = UiEvent::make(EventType::Scroll);
    const Rect rect = container.layout().visual_rect();
    wheel.position = {(rect.min.x + rect.max.x) * 0.5F, (rect.min.y + rect.max.y) * 0.5F};
    wheel.scroll = {0.0F, -0.5F};
    router.dispatch(wheel);
    REQUIRE(wheel.native_input_blocked);
    REQUIRE_FALSE(wheel.propagation_stopped);

    container.scroll().seek_to({0.0F, 0.0F}, ScrollBehavior::Instant);
    draw_routed_frame();
    draw_routed_frame();
    container.scroll().set_behaviour(ScrollBehavior::Smooth);
    UiEvent next_wheel = UiEvent::make(EventType::Scroll);
    next_wheel.position = wheel.position;
    next_wheel.scroll = wheel.scroll;
    router.dispatch(next_wheel);
    REQUIRE(next_wheel.native_input_blocked);
    float previous_scroll = container.scroll().position().y;
    for (int frame = 0; frame < 15; ++frame) {
        draw_routed_frame();
        REQUIRE(container.scroll().position().y >= previous_scroll);
        REQUIRE(container.scroll().position().y < 100.0F);
        previous_scroll = container.scroll().position().y;
    }
}

TEST_CASE("inline overlay keeps resizable children valid while the parent scrolls", "[LayerContainer][scroll][regression]") {
    class ScrollContainer final : public Container {
    public:
        ScrollContainer() : Container("overlay-scroll-parent") {
            set_size({px(120.0F), px(80.0F)});
            set_scrollable(true);
        }

        bool scroll_to_end = false;

    protected:
        bool paint() override {
            ImGui::SetNextWindowContentSize({120.0F, 400.0F});
            return Container::paint();
        }

        void on_draw_end() override {
            if (scroll_to_end) {
                ImGui::SetScrollY(100.0F);
            }
            Container::on_draw_end();
        }
    } parent;

    parent.add<LayoutProbeNode>("content", ImVec2{120.0F, 400.0F});
    auto& overlay = parent.add<LayerContainer>("overlay");
    auto& panel = overlay.add<Container>("dynamic-section");
    panel.set_direction(StackDirection::Horizontal);
    panel.set_spacing(8.0F);
    panel.set_layout({
        .size = {fit(), fit()},
        .placement = {.anchor = Anchor::TopRight, .origin = Anchor::TopRight},
        .in_flow = false,
    });
    panel.configure_all_styles([](Style& style) {
        style.padding({14.0F, 14.0F})
            .background_color(rgb(0.1F, 0.1F, 0.1F))
            .border(BorderEdge{1.0F, BorderType::Solid})
            .border_color(rgb(0.5F, 0.5F, 0.5F));
    });
    auto& controls = panel.add<Container>("dynamic-node-controls");
    controls.set_size({fit(), fit()});
    controls.set_spacing(8.0F);
    for (int index = 0; index < 4; ++index) {
        controls.add<LayoutProbeNode>(std::format("control-{}", index), ImVec2{120.0F, 36.0F});
    }
    auto& list = panel.add<Container>("dynamic-list");
    list.set_size({px(100.0F), grow()});
    list.set_spacing(8.0F);
    list.add<TextWidget>("dynamic nodes");
    list.add<TextWidget>("click a list item");
    auto& resize = list.add<ResizableContainer>("dynamic-nodes");
    resize.set_size({px(80.0F), grow()});
    resize.set_resize(ResizeAxes::Both).set_scrollable(true);
    resize.configure_all_styles([](Style& style) { style.padding({20.0F, 20.0F}).background_color(rgb(0.2F, 0.2F, 0.2F)); });
    resize.add<LayoutProbeNode>("row", ImVec2{80.0F, 200.0F});

    ui_test::ImGuiContext context({240.0F, 160.0F});
    const auto draw_frame = [&parent] { ui_test::draw_node(parent, "positioned-scroll-test"); };

    draw_frame();
    draw_frame();
    const ImVec2 initial_size = resize.layout().size();
    parent.scroll_to_end = true;
    draw_frame();
    draw_frame();

    REQUIRE(panel.layout().visual_rect().valid());
    REQUIRE(resize.layout().visual_rect().valid());
    REQUIRE(resize.layout().size().x == Catch::Approx(initial_size.x));
    REQUIRE(resize.layout().size().y == Catch::Approx(initial_size.y));
}

TEST_CASE("stack auto-sized axes reflow when the parent grows", "[layout][regression]") {
    ui_test::ImGuiContext context({400.0F, 180.0F});

    Container stack("responsive-stack");
    stack.set_size({grow(), px(80.0F)});
    Node& hidden = stack.add<Node>("hidden-child");
    hidden.set_size({px(40.0F), px(40.0F)});
    hidden.set_visible(false);
    Container& child = stack.add<Container>("stretching-child");
    child.set_size({grow(), px(20.0F)});

    const auto draw_frame = [&stack](float width) {
        ui_test::draw_window("responsive-stack-test", {width, 160.0F}, [&] { stack.draw(); });
    };

    draw_frame(220.0F);
    const float initial_stack_width = stack.layout().size().x;
    const float initial_child_width = child.layout().size().x;

    draw_frame(360.0F);

    REQUIRE(child.layout().local_rect().min.y == Catch::Approx(child.layout().parent_content_rect().min.y));
    REQUIRE(stack.layout().size().x > initial_stack_width);
    REQUIRE(child.layout().size().x > initial_child_width);
    REQUIRE(child.layout().size().x == Catch::Approx(stack.layout().size().x - stack.style().padding().horizontal()));
}

class VirtualRow : public ui::Node {
public:
    VirtualRow(int index, std::vector<int>& drawn) : m_index(index), m_drawn(drawn) {
        set_input_mode(InputMode::Target);
    }

private:
    bool paint() override {
        m_drawn.push_back(m_index);
        ImGui::Dummy(layout().size());
        return true;
    }

    int m_index;
    std::vector<int>& m_drawn;
};

class VirtualListProbe : public ui::VirtualLayout {
public:
    VirtualListProbe() : VirtualLayout("virtual-list", 20.0F) {
        set_size({ui::px(180.0F), ui::px(100.0F)});
    }

    float scroll = 0.0F;
    float max_scroll = 0.0F;
    float requested_scroll = 0.0F;

private:
    void draw_children() override {
        scroll = ImGui::GetScrollY();
        max_scroll = ImGui::GetScrollMaxY();
        VirtualLayout::draw_children();
        ImGui::SetScrollY(requested_scroll);
    }
};

void draw_virtual_list(ui::VirtualLayout& list) {
    ui_test::draw_window("virtual-layout-test", {240.0F, 180.0F}, [&] {
        list.update(1.0F);
        list.draw();
    });
}

void set_virtual_items(ui::VirtualLayout& list, size_t count, std::vector<int>& drawn, std::map<size_t, VirtualRow*>& cache) {
    list.set_items(count, [&list, &drawn, &cache](size_t index) -> ui::Node& {
        const auto found = cache.find(index);
        if (found != cache.end()) return *found->second;

        auto& row = list.add<VirtualRow>(static_cast<int>(index), drawn);
        cache.emplace(index, &row);
        return row;
    });
}

TEST_CASE("wheel inside a virtual list scrolls it before its parent", "[layout][virtual-layout][regression]") {
    ui_test::ImGuiContext context({240.0F, 180.0F});
    InputRouter router;
    Container parent("nested-scroll-parent");
    parent.set_size({px(200.0F), px(120.0F)});
    parent.set_scrollable(true);
    parent.set_input_router(&router);

    auto& list = parent.add<VirtualLayout>("nested-list", 20.0F);
    list.set_size({px(180.0F), px(80.0F)});
    std::vector<int> drawn;
    std::map<size_t, VirtualRow*> cache;
    set_virtual_items(list, 1000, drawn, cache);
    parent.add<LayoutProbeNode>(ImVec2{180.0F, 400.0F});
    parent.add<LayerContainer>("nested-overlay");

    const auto frame = [&] {
        router.begin_frame();
        ui_test::draw_window("nested-scroll-root", {240.0F, 180.0F}, [&] {
            parent.update(1.0F);
            parent.draw();
        });
    };
    frame();
    frame();
    REQUIRE(parent.scroll().max().y > 0.0F);
    REQUIRE(list.scroll().max().y > 0.0F);

    UiEvent wheel = UiEvent::make(EventType::Scroll);
    const Rect rect = list.layout().visual_rect();
    wheel.position = {rect.min.x + 10.0F, rect.min.y + 10.0F};
    wheel.scroll = {0.0F, -1.0F};
    router.dispatch(wheel);
    frame();
    frame();

    REQUIRE(list.scroll().position().y > 0.0F);
    REQUIRE(parent.scroll().position().y == 0.0F);

    list.scroll().seek_to({0.0F, list.scroll().max().y}, ScrollBehavior::Instant);
    frame();
    frame();

    wheel = UiEvent::make(EventType::Scroll);
    wheel.position = {rect.min.x + 10.0F, rect.min.y + 10.0F};
    wheel.scroll = {0.0F, -1.0F};
    router.dispatch(wheel);
    frame();
    frame();

    REQUIRE(parent.scroll().position().y > 0.0F);
}

TEST_CASE("virtual layout creates visible rows lazily and reuses the caller cache", "[layout][virtual-layout]") {
    ui_test::ImGuiContext context({240.0F, 180.0F});
    ui::InputRouter router;
    VirtualListProbe list;
    std::vector<int> drawn;
    std::map<size_t, VirtualRow*> cache;
    list.set_input_router(&router);
    static_cast<Container&>(list).set_spacing(3.0F);
    list.configure_all_styles([](ui::Style& style) { style.padding({7.0F, 5.0F}); });
    set_virtual_items(list, 1000, drawn, cache);
    REQUIRE(list.item_count() == 1000);
    REQUIRE(list.children().empty());
    REQUIRE(cache.empty());

    const auto frame = [&] {
        drawn.clear();
        router.begin_frame();
        draw_virtual_list(list);
    };
    frame();
    frame();

    REQUIRE_FALSE(drawn.empty());
    REQUIRE(drawn.front() == 0);
    REQUIRE(drawn.size() <= 6);
    REQUIRE(cache.size() <= 6);
    REQUIRE(list.max_scroll == Catch::Approx((1000.0F * 23.0F) - 3.0F - 90.0F));
    REQUIRE(router.stats().entry_count <= 8);
    const auto& first = *cache.at(0);
    REQUIRE(first.layout().size().y == Catch::Approx(20.0F));
    const ImVec2 position = first.layout().visual_rect().min;
    REQUIRE(router.node_at({position.x + 2.0F, position.y + 2.0F}) == &first);

    list.requested_scroll = 2300.0F;
    frame();
    frame();
    REQUIRE(list.scroll == Catch::Approx(2300.0F));
    REQUIRE(drawn.front() >= 99);
    REQUIRE(std::find(drawn.begin(), drawn.end(), 100) != drawn.end());
    REQUIRE(drawn.size() <= 6);
    REQUIRE(router.stats().entry_count <= 8);
    REQUIRE(cache.size() <= 12);

    list.requested_scroll = list.max_scroll;
    frame();
    frame();
    REQUIRE(drawn.back() == 999);
    REQUIRE(drawn.size() <= 6);
    REQUIRE(cache.size() <= 18);

    const size_t cached_count = cache.size();
    list.requested_scroll = 0.0F;
    frame();
    frame();
    REQUIRE(cache.size() == cached_count);
    REQUIRE(cache.at(0) == &first);
    REQUIRE(list.children().size() == cached_count);

    auto removed = list.detach(*cache.at(0));
    cache.erase(0);
    frame();
    REQUIRE(cache.at(0) != removed.get());
    REQUIRE(cache.size() == cached_count);
}

TEST_CASE("virtual layout clips inside expanded rows and removes manual offsets", "[layout][virtual-layout]") {
    ui_test::ImGuiContext context({240.0F, 180.0F});
    VirtualListProbe list;
    std::vector<int> drawn;
    std::map<size_t, VirtualRow*> cache;
    set_virtual_items(list, 100, drawn, cache);
    list.set_extra_offset(2, 300.0F);
    list.set_extra_offset(20, 70.0F);
    const auto frame = [&] {
        drawn.clear();
        draw_virtual_list(list);
    };
    frame();
    frame();
    REQUIRE(list.max_scroll == Catch::Approx(2270.0F));
    REQUIRE(cache.at(2)->layout().size().y == Catch::Approx(320.0F));
    REQUIRE_FALSE(cache.contains(20));

    list.requested_scroll = 150.0F;
    frame();
    frame();
    REQUIRE(std::find(drawn.begin(), drawn.end(), 2) != drawn.end());
    const ui::Rect expanded = cache.at(2)->layout().visual_rect();
    REQUIRE(expanded.min.y < list.layout().visual_rect().min.y);
    REQUIRE(expanded.max.y > list.layout().visual_rect().max.y);
    REQUIRE(drawn.size() < 10);

    list.requested_scroll = 380.0F;
    frame();
    frame();
    REQUIRE(std::find(drawn.begin(), drawn.end(), 4) != drawn.end());
    REQUIRE(cache.at(4)->layout().local_rect().min.y == Catch::Approx(380.0F));

    list.set_extra_offset(2, 0.0F);
    frame();
    frame();
    REQUIRE(list.extra_offset(2) == 0.0F);
    REQUIRE(list.max_scroll == Catch::Approx(1970.0F));
    list.clear_extra_offsets();
    frame();
    frame();
    REQUIRE(list.max_scroll == Catch::Approx(1900.0F));
}

TEST_CASE("virtual layout handles empty lists hidden slots and fit measurement", "[layout][virtual-layout]") {
    ui_test::ImGuiContext context({240.0F, 180.0F});
    VirtualListProbe list;
    std::vector<int> drawn;
    std::map<size_t, VirtualRow*> cache;
    draw_virtual_list(list);
    REQUIRE(list.max_scroll == 0.0F);

    list.set_size({ui::px(180.0F), ui::fit()});
    list.set_spacing(4.0F);
    list.set_items(2, [&list, &drawn, &cache](size_t index) -> ui::Node& {
        const auto found = cache.find(index);
        if (found != cache.end()) return *found->second;

        auto& row = list.add<VirtualRow>(static_cast<int>(index), drawn);
        row.set_visible(index != 0);
        cache.emplace(index, &row);
        return row;
    });
    list.set_extra_offset(1, 10.0F);
    draw_virtual_list(list);
    draw_virtual_list(list);
    REQUIRE(list.layout().measured_size().y == Catch::Approx(54.0F));
    REQUIRE_FALSE(drawn.empty());
    REQUIRE(drawn.front() == 1);
    REQUIRE(cache.at(1)->layout().local_rect().min.y == Catch::Approx(24.0F));

    list.set_items(0);
    draw_virtual_list(list);
    REQUIRE(list.extra_offset(1) == 0.0F);
    list.set_items(2);
    draw_virtual_list(list);
    REQUIRE(list.layout().measured_size().y == Catch::Approx(44.0F));
    REQUIRE(cache.size() == 2);

    list.set_items(0, {});
    REQUIRE(list.item_count() == 0);
}

TEST_CASE("virtual layout rejects invalid dimensions and indices", "[layout][virtual-layout]") {
    REQUIRE_THROWS_AS(ui::VirtualLayout("invalid", 0.0F), std::invalid_argument);
    ui::VirtualLayout list("list", 20.0F);
    REQUIRE_THROWS_AS(list.set_item_height(-1.0F), std::invalid_argument);
    REQUIRE_THROWS_AS(list.set_spacing(std::numeric_limits<float>::infinity()), std::invalid_argument);
    REQUIRE_THROWS_AS(list.set_extra_offset(0, 10.0F), std::out_of_range);
    REQUIRE_THROWS_AS(list.set_items(10, {}), std::invalid_argument);
    REQUIRE_THROWS_AS(list.set_items(std::numeric_limits<size_t>::max()), std::length_error);
    list.set_items(1, [&list](size_t) -> ui::Node& { return list.add<ui::Node>(); });
    REQUIRE_THROWS_AS(list.set_extra_offset(0, -1.0F), std::invalid_argument);
    REQUIRE_THROWS_AS(list.set_extra_offset(0, std::numeric_limits<float>::quiet_NaN()), std::invalid_argument);
}

TEST_CASE("virtual layout overscan includes configurable neighbors across expanded rows", "[layout][virtual-layout]") {
    ui_test::ImGuiContext context({240.0F, 180.0F});
    VirtualListProbe list;
    std::vector<int> drawn;
    std::map<size_t, VirtualRow*> cache;
    set_virtual_items(list, 1000, drawn, cache);
    list.set_spacing(3.0F);
    list.set_extra_offset(100, 300.0F);
    list.set_extra_offset(103, 60.0F);

    const auto frame = [&] {
        drawn.clear();
        draw_virtual_list(list);
    };
    frame();
    frame();
    const float max_scroll = list.max_scroll;

    for (float scroll : {0.0F, 1171.0F, 2400.0F, max_scroll}) {
        list.set_overscan(0);
        list.requested_scroll = scroll;
        frame();
        frame();
        REQUIRE_FALSE(drawn.empty());
        const int first = drawn.front();
        const int last = drawn.back();

        for (int extra : {3, 7}) {
            list.set_overscan(static_cast<size_t>(extra));
            frame();
            REQUIRE(drawn.front() == std::max(0, first - extra));
            REQUIRE(drawn.back() == std::min(999, last + extra));
            REQUIRE(drawn.size() == static_cast<size_t>(drawn.back() - drawn.front() + 1));
            REQUIRE(list.max_scroll == Catch::Approx(max_scroll));
        }

        list.set_overscan(0);
        frame();
        REQUIRE(drawn.front() == first);
        REQUIRE(drawn.back() == last);
    }

    list.set_overscan(std::numeric_limits<size_t>::max());
    frame();
    REQUIRE(drawn.front() == 0);
    REQUIRE(drawn.back() == 999);
    REQUIRE(drawn.size() == 1000);
}

TEST_CASE("virtual layout measures newly created subtrees on their first draw", "[layout][virtual-layout]") {
    ui_test::ImGuiContext context({240.0F, 180.0F});
    VirtualListProbe list;
    ui::TextWidget* text = nullptr;
    list.set_items(1, [&list, &text](size_t) -> ui::Node& {
        auto& row = list.add<ui::Container>("lazy-row");
        text = &row.add<ui::TextWidget>("lazy text");
        return row;
    });
    draw_virtual_list(list);
    REQUIRE(text != nullptr);
    REQUIRE(text->layout().measured_size().x > 0.0F);
    REQUIRE(text->layout().measured_size().y > 0.0F);
}
