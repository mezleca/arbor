#include <imgui-ui/style/tween/animator.hpp>
#include <imgui-ui/style/state.hpp>
#include <imgui-ui/runtime.hpp>
#include <imgui-ui/diagnostics/debugger.hpp>
#include <imgui-ui/layout/container.hpp>
#include <imgui-ui/layout/layer-container.hpp>
#include <imgui-ui/layout/virtual-layout.hpp>
#include <imgui-ui/resources/texture-registry.hpp>
#include <imgui-ui/surface.hpp>
#include <imgui-ui/widgets/box.hpp>
#include <imgui-ui/widgets/button.hpp>
#include <imgui-ui/widgets/checkbox.hpp>
#include <imgui-ui/widgets/color-picker.hpp>
#include <imgui-ui/widgets/context-menu.hpp>
#include <imgui-ui/widgets/dropdown.hpp>
#include <imgui-ui/widgets/image.hpp>
#include <imgui-ui/widgets/number-input.hpp>
#include <imgui-ui/widgets/text.hpp>
#include <imgui-ui/widgets/text-input.hpp>
#include <imgui-ui/imgui/painter.hpp>
#include "imgui-context.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <imgui.h>
#include <imgui_internal.h>
#include <algorithm>
#include <cfloat>
#include <limits>
#include <string>
#include <vector>

using namespace ui;

TEST_CASE("checkbox input is limited to its box", "[CheckboxWidget][input][regression]") {
    Runtime runtime;
    ui::Surface surface = ui_test::make_surface(runtime);
    bool checked = false;
    auto& checkbox = surface.root().add<CheckboxWidget>(checked, "checkbox");

    const auto surface_context = ui_test::prepare_surface(surface, {400.0F, 180.0F});

    ui_test::draw_surface(surface);

    const Rect widget_rect = checkbox.layout().visual_rect();
    const BoxInsets padding = checkbox.style().padding();
    const Rect frame_rect = Rect::from_position_size(
        {widget_rect.min.x + padding.left,
         widget_rect.min.y + ((widget_rect.size().y - checkbox.frame().layout().size().y) * 0.5F)},
        checkbox.frame().layout().size()
    );
    REQUIRE(widget_rect.valid());
    REQUIRE(frame_rect.valid());
    REQUIRE(frame_rect.max.x < widget_rect.max.x);

    const ImVec2 frame_center = ui_test::center(frame_rect);
    REQUIRE(surface.input_router().node_at(frame_center) == &checkbox);

    const ImVec2 label_position = {
        (frame_rect.max.x + widget_rect.max.x) * 0.5F,
        (widget_rect.min.y + widget_rect.max.y) * 0.5F,
    };
    REQUIRE(widget_rect.contains(label_position));
    REQUIRE_FALSE(frame_rect.contains(label_position));
    REQUIRE(surface.input_router().node_at(label_position) != &checkbox);
}

TEST_CASE("widget tooltips follow pointer hover", "[Widget][tooltip]") {
    Runtime runtime;
    ui::Surface surface = ui_test::make_surface(runtime);
    auto& box = surface.root().add<BoxWidget>("box");
    box.set_size({px(120.0F), px(80.0F)});

    int tooltip_draws = 0;
    box.set_tooltip([&tooltip_draws] { ++tooltip_draws; });

    const auto surface_context = ui_test::prepare_surface(surface, {200.0F, 150.0F});
    ui_test::draw_surface(surface);
    REQUIRE(tooltip_draws == 0);

    UiEvent move = ui_test::pointer_event(EventType::PointerMove, ui_test::center(box.layout().visual_rect()));
    surface.dispatch(move);
    ui_test::draw_surface(surface);
    REQUIRE(tooltip_draws == 1);

    move = ui_test::pointer_event(EventType::PointerMove, {190.0F, 140.0F});
    surface.dispatch(move);
    ui_test::draw_surface(surface);
    REQUIRE(tooltip_draws == 1);
}

TEST_CASE("text input hover and focus are limited to its field", "[TextInputWidget][input][regression]") {
    Runtime runtime;
    ui::Surface surface = ui_test::make_surface(runtime);
    std::string value;
    auto& input = surface.root().add<TextInputWidget>(value, "search");
    input.set_label("search label");
    input.set_size({px(300.0F), fit()});

    const auto surface_context = ui_test::prepare_surface(surface, {400.0F, 180.0F});
    ui_test::draw_surface(surface);
    auto* field = static_cast<StyledNode*>(input.find("input"));
    auto* label = input.find("label");
    REQUIRE(field != nullptr);
    REQUIRE(label != nullptr);

    auto& router = surface.input_router();
    const ImVec2 label_center = ui_test::center(label->layout().visual_rect());
    const ImVec2 field_center = ui_test::center(field->layout().visual_rect());
    REQUIRE_FALSE(field->layout().visual_rect().contains(label_center));

    UiEvent move = ui_test::pointer_event(EventType::PointerMove, label_center);
    router.dispatch(move);
    REQUIRE_FALSE(input.input_state().hovered);
    REQUIRE(field->style_type() == StyleType::DEFAULT);

    UiEvent press = ui_test::pointer_event(EventType::PointerDown, label_center);
    router.dispatch(press);
    REQUIRE_FALSE(input.input_state().focused);

    move = ui_test::pointer_event(EventType::PointerMove, field_center);
    router.dispatch(move);
    REQUIRE(input.input_state().hovered);
    REQUIRE(field->style_type() == StyleType::HOVER);
    REQUIRE_FALSE(input.input_state().focused);

    press = ui_test::pointer_event(EventType::PointerDown, field_center);
    router.dispatch(press);
    REQUIRE(input.input_state().focused);
    REQUIRE(field->style_type() == StyleType::ACTIVE);
}

TEST_CASE("nested containers keep default padding empty and route checkbox clicks", "[container][input][regression]") {
    Runtime runtime;
    ui::Surface surface = ui_test::make_surface(runtime);
    bool checked = false;

    auto& page = surface.root().add<Container>("page");
    page.set_size({px(320.0F), px(120.0F)});
    auto& section = page.add<Container>("section");
    auto& form = section.add<Container>("form");
    auto& checkbox = form.add<CheckboxWidget>(checked, "enabled");

    const auto surface_context = ui_test::prepare_surface(surface, {400.0F, 180.0F});

    ui_test::draw_surface(surface);

    const Rect widget_rect = checkbox.layout().visual_rect();
    const BoxInsets padding = checkbox.style().padding();
    const Rect rect = Rect::from_position_size(
        {widget_rect.min.x + padding.left, widget_rect.min.y + padding.top}, checkbox.frame().layout().size()
    );
    const ImVec2 position = ui_test::center(rect);
    REQUIRE(surface.input_router().node_at(position) == &checkbox);

    UiEvent down = ui_test::pointer_event(EventType::PointerDown, position);
    surface.dispatch(down);

    UiEvent up = ui_test::pointer_event(EventType::PointerUp, position);
    surface.dispatch(up);
    REQUIRE(checked);
}

TEST_CASE("image fit preserves the texture aspect ratio", "[ImageWidget][fit]") {
    class ProbeTexture final : public Texture {
    public:
        ImVec2 size() const override {
            return {200.0F, 100.0F};
        }

        ImTextureID get(ImVec2 size) override {
            requested_size = size;
            return {};
        }

        void release_context(ImGuiContext*) override {}

        ImVec2 requested_size;
    };

    ui_test::ImGuiContext context({240.0F, 180.0F});
    const auto draw = [](ImageFit fit) {
        ProbeTexture texture;
        ImageWidget image(&texture);
        image.set_size({px(100.0F), px(100.0F)});
        image.set_fit(fit);

        ui_test::draw_node(image, "image-fit-test");
        return texture.requested_size;
    };

    const ImVec2 cover_size = draw(ImageFit::Cover);
    REQUIRE(cover_size.x == Catch::Approx(200.0F));
    REQUIRE(cover_size.y == Catch::Approx(100.0F));

    ProbeTexture texture;
    Container container("responsive-image");
    container.set_size({px(100.0F), px(100.0F)});
    auto& image = container.add<ImageWidget>(&texture);
    image.set_size({grow(), grow()});
    image.set_fit(ImageFit::Contain);

    const auto draw_container = [&container] { ui_test::draw_node(container, "responsive-image-test"); };

    draw_container();
    REQUIRE(texture.requested_size.x == Catch::Approx(100.0F));
    REQUIRE(texture.requested_size.y == Catch::Approx(50.0F));

    container.set_size({px(200.0F), px(100.0F)});
    draw_container();
    REQUIRE(texture.requested_size.x == Catch::Approx(200.0F));
    REQUIRE(texture.requested_size.y == Catch::Approx(100.0F));
}

TEST_CASE("dropdown opens from a nested container without extending its parent", "[dropdown][container][regression]") {
    Runtime runtime;
    ui::Surface surface = ui_test::make_surface(runtime);
    std::string value = "light";

    auto& page = surface.root().add<Container>("page");
    page.set_size({px(360.0F), px(200.0F)});
    auto& section = page.add<Container>("section");
    auto& form = section.add<Container>("form");
    auto& dropdown = form.add<DropdownWidget>(
        value, std::vector<DropdownOption>{{"light", "light"}, {"dark", "dark"}}, "", LabelPlacement::Above, "theme"
    );
    dropdown.set_size({px(180.0F), px(32.0F)});
    bool checked = false;
    auto& checkbox = surface.root().add<CheckboxWidget>(checked, "enabled");
    checkbox.set_size({px(180.0F), px(32.0F)});

    const auto surface_context = ui_test::prepare_surface(surface, {400.0F, 240.0F});

    ui_test::draw_surface(surface);
    const Rect trigger_rect = dropdown.trigger().layout().visual_rect();
    const ImVec2 trigger_center = ui_test::center(trigger_rect);

    UiEvent down = ui_test::pointer_event(EventType::PointerDown, trigger_center);
    surface.dispatch(down);

    UiEvent up = ui_test::pointer_event(EventType::PointerUp, trigger_center);
    surface.dispatch(up);

    ui_test::draw_surface(surface);
    REQUIRE(dropdown.is_open());
    const Rect body_rect = dropdown.body().layout().visual_rect();
    for (const auto& child : dropdown.body().children()) {
        UiEvent move = ui_test::pointer_event(EventType::PointerMove, ui_test::center(child->layout().visual_rect()));
        surface.dispatch(move);
        REQUIRE(child->input_state().hovered);
        REQUIRE(ImGui::GetMouseCursor() == ImGuiMouseCursor_Hand);
    }
    surface.input_router().register_target(checkbox, body_rect);

    const ImVec2 option_position = ui_test::center(body_rect);
    REQUIRE(surface.input_router().node_at(option_position) != &checkbox);

    down = ui_test::pointer_event(EventType::PointerDown, option_position);
    surface.dispatch(down);
    up = ui_test::pointer_event(EventType::PointerUp, option_position);
    surface.dispatch(up);
    REQUIRE_FALSE(checked);
}

TEST_CASE("color picker opens outside its parent and blocks content input", "[color-picker][container][regression]") {
    Runtime runtime;
    ui::Surface surface = ui_test::make_surface(runtime);
    Color color = rgb(0.26F, 0.59F, 0.98F);
    bool checked = false;

    auto& page = surface.root().add<Container>("page");
    page.set_size({px(360.0F), px(200.0F)});
    auto& section = page.add<Container>("section");
    auto& picker = section.add<ColorPickerWidget>(color, "color");
    auto& checkbox = surface.root().add<CheckboxWidget>(checked, "enabled");

    const auto surface_context = ui_test::prepare_surface(surface, {400.0F, 300.0F});
    ui_test::draw_surface(surface);

    const ImVec2 preview_center = ui_test::center(picker.preview().layout().visual_rect());
    UiEvent down = ui_test::pointer_event(EventType::PointerDown, preview_center);
    UiEvent up = ui_test::pointer_event(EventType::PointerUp, preview_center);
    surface.dispatch(down);
    surface.dispatch(up);

    ui_test::draw_surface(surface);
    REQUIRE(picker.is_open());
    const Rect popup_rect = picker.popup().layout().visual_rect();
    REQUIRE(popup_rect.valid());
    REQUIRE(picker.popup().layout().intrinsic_size().y > picker.popup().children().back()->layout().intrinsic_size().y);

    const ImVec2 popup_center = ui_test::center(popup_rect);
    down.position = popup_center;
    up.position = popup_center;
    surface.dispatch(down);
    surface.dispatch(up);
    REQUIRE_FALSE(down.native_input_blocked);
    REQUIRE_FALSE(up.native_input_blocked);

    surface.input_router().register_target(checkbox, popup_rect);
    surface.dispatch(down);
    surface.dispatch(up);
    REQUIRE_FALSE(checked);
}

TEST_CASE("color pickers share the native popup stack", "[color-picker][regression]") {
    Runtime runtime;
    ui::Surface surface = ui_test::make_surface(runtime);
    Color first_color = rgb(0.26F, 0.59F, 0.98F);
    Color second_color = rgb(0.98F, 0.59F, 0.26F);
    auto& first = surface.root().add<ColorPickerWidget>(first_color, "first");
    auto& second = surface.root().add<ColorPickerWidget>(second_color, "second");

    const auto surface_context = ui_test::prepare_surface(surface, {640.0F, 480.0F});
    first.open();
    ui_test::draw_surface(surface);
    REQUIRE(first.is_open());

    second.open();
    ui_test::draw_surface(surface);
    ui_test::draw_surface(surface);

    REQUIRE_FALSE(first.is_open());
    REQUIRE(second.is_open());
}

TEST_CASE("dropdown options use framework input and select their value", "[DropdownWidget][input][regression]") {
    Runtime runtime;
    ui::Surface surface = ui_test::make_surface(runtime);
    std::string value = "light";
    int changes = 0;
    auto& dropdown = surface.root().add<DropdownWidget>(
        value, std::vector<DropdownOption>{{"light", "light"}, {"dark", "dark"}}, "", LabelPlacement::Above, "theme"
    );
    dropdown.set_size({px(180.0F), px(32.0F)});
    dropdown.on_change([&changes] { ++changes; });

    const auto surface_context = ui_test::prepare_surface(surface, {400.0F, 240.0F});

    ui_test::draw_surface(surface);
    const Rect trigger_rect = dropdown.trigger().layout().visual_rect();
    const ImVec2 trigger_center = ui_test::center(trigger_rect);

    UiEvent down = ui_test::pointer_event(EventType::PointerDown, trigger_center);
    surface.dispatch(down);
    REQUIRE(down.native_input_blocked);

    UiEvent up = ui_test::pointer_event(EventType::PointerUp, trigger_center);
    surface.dispatch(up);
    REQUIRE(up.native_input_blocked);
    ui_test::draw_surface(surface);

    const Rect body_rect = dropdown.body().layout().visual_rect();
    const float item_height = body_rect.size().y * 0.5F;
    const ImVec2 option_position = {
        ui_test::center(body_rect).x,
        body_rect.min.y + (item_height * 1.5F),
    };
    const ui::Node* option = surface.input_router().node_at(option_position);
    REQUIRE(option != nullptr);
    REQUIRE(option->type_name() == "DropdownOption");

    UiEvent move = ui_test::pointer_event(EventType::PointerMove, option_position);
    surface.dispatch(move);
    REQUIRE(ImGui::GetMouseCursor() == ImGuiMouseCursor_Hand);

    down = ui_test::pointer_event(EventType::PointerDown, option_position);
    surface.dispatch(down);
    up = ui_test::pointer_event(EventType::PointerUp, option_position);
    surface.dispatch(up);
    REQUIRE(value == "dark");
    REQUIRE_FALSE(dropdown.is_open());

    ui_test::draw_surface(surface);
    REQUIRE(changes == 1);
}

TEST_CASE("text measurement follows style insets animation and line height", "[TextWidget][layout][style][animation]") {
    Runtime runtime;
    ui::Surface surface = ui_test::make_surface(runtime);
    TextWidget text("padded text");
    text.configure_all_styles([](Style& style) { style.padding({}); });
    const auto surface_context = ui_test::prepare_surface(surface, {400.0F, 180.0F});
    surface.begin_frame();
    ImVec2 expected = ImGui::CalcTextSize("padded text");

    SECTION("padding and borders") {
        text.configure_all_styles([](Style& style) { style.padding({5.0F, 3.0F}).border(BorderEdge{1.0F, BorderType::Solid}); });
        expected.x += 12.0F;
        expected.y += 8.0F;
    }
    SECTION("animated padding") {
        text.animate().to(StyleAnimationProperty::PaddingX, 10.0F, {0.1F, easing::linear});
        text.update(0.05F);
        expected.x += 10.0F;
    }
    SECTION("line height") {
        text.set_text("first line\nsecond line");
        text.configure_all_styles([](Style& style) { style.line_height(1.5F); });
        expected = ImGui::CalcTextSize("first line\nsecond line");
        expected.y *= 1.5F;
    }

    ImGui::Begin("text-measurement");
    text.draw();
    ImGui::End();
    surface.end_frame();

    CHECK(text.layout().size().x == Catch::Approx(expected.x));
    CHECK(text.layout().size().y == Catch::Approx(expected.y));
}

TEST_CASE("compact buttons keep their text when bottom borders clip the line", "[ButtonWidget][render][regression]") {
    Runtime runtime;
    ui::Surface surface = ui_test::make_surface(runtime);
    auto& tabs = surface.root().add<Container>("tabs", StackDirection::Horizontal);
    tabs.set_size({px(300), fit()});
    auto& button = tabs.add<ButtonWidget>("properties");
    button.set_text_overflow(TextOverflow::Ellipsis);
    button.configure_all_styles([](Style& style) {
        style.padding({}).font_size(25).color(rgb(27, 194, 67)).overflow(Overflow::Hidden);
        style.background_color(rgba(0, 0, 0, 0)).border(BorderEdge{0, BorderType::None}).border_color(rgb(100, 100, 100));
    });
    const auto context = ui_test::prepare_surface(surface, {400, 200});
    SECTION("button border") {
        button.configure_all_styles([](Style& style) { style.border_bottom(1.5F); });
    }
    SECTION("tab bar border") {
        tabs.configure_all_styles([](Style& style) { style.border_bottom(1.5F); });
    }
    ui_test::draw_surface(surface);

    int text_vertices = 0;
    for (const auto* window : ImGui::GetCurrentContext()->Windows) {
        for (const auto& vertex : window->DrawList->VtxBuffer) {
            if (vertex.col == IM_COL32(27, 194, 67, 255)) ++text_vertices;
        }
    }
    CHECK(text_vertices > 0);
}

TEST_CASE("wrapped ellipsis truncates the last visible line", "[TextWidget][render][regression]") {
    ui_test::ImGuiContext context({400.0F, 200.0F});
    ImGui::NewFrame();
    ImGui::SetNextWindowPos({0.0F, 0.0F});
    ImGui::SetNextWindowSize({400.0F, 200.0F});
    ImGui::Begin("wrapped-ellipsis");
    const ImVec2 position = ImGui::GetCursorScreenPos();
    const float width = ImGui::CalcTextSize("one two").x;
    const float height = ImGui::GetTextLineHeight();
    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    TextValue whole("one two three four");
    const int whole_start = draw_list->VtxBuffer.Size;
    Painter(*draw_list).text(position, rgb(1, 1, 1), whole);
    const int whole_vertices = draw_list->VtxBuffer.Size - whole_start;

    TextValue text("one two three four");
    text.set_wrap(width);
    const int start = draw_list->VtxBuffer.Size;
    const ImVec4 clip = {position.x, position.y, position.x + width, position.y + height};
    Painter(*draw_list).text_ellipsis(position, rgb(1, 1, 1), text, clip);
    const int count = draw_list->VtxBuffer.Size - start;
    CHECK(count > 0);
    CHECK(count < whole_vertices);

    const ImVec4 partial_next_line = {clip.x, clip.y, clip.z, clip.w + height * 0.5F};
    const int partial_start = draw_list->VtxBuffer.Size;
    Painter(*draw_list).text_ellipsis(position, rgb(1, 1, 1), text, partial_next_line);
    CHECK(draw_list->VtxBuffer.Size - partial_start == count);

    // imgui floors child extents, so a fitting line can end up one pixel wider than its content rect.
    TextValue fitting("MMMMMM");
    const float text_width = ImGui::GetFont()->CalcTextSizeA(ImGui::GetFontSize(), FLT_MAX, 0.0F, "MMMMMM").x;
    const int full_start = draw_list->VtxBuffer.Size;
    Painter(*draw_list).text(position, rgb(1, 1, 1), fitting);
    const int full_vertices = draw_list->VtxBuffer.Size - full_start;

    const auto truncates = [&](float clip_width) {
        const Painter painter(*draw_list);
        const ImVec4 threshold_clip =
            painter.visible_clip({position.x, position.y, position.x + clip_width, position.y + height});
        const int start = draw_list->VtxBuffer.Size;
        painter.text_ellipsis(position, rgb(1, 1, 1), fitting, threshold_clip);
        return draw_list->VtxBuffer.Size - start < full_vertices;
    };

    CHECK_FALSE(truncates(text_width - 0.75F));
    CHECK(truncates(text_width - 1.25F));

    // an ancestor overflow clip narrows the visible extent even when the node content still fits.
    draw_list->PushClipRect(position, {position.x + text_width - 2.0F, position.y + height}, true);
    CHECK(truncates(text_width));
    draw_list->PopClipRect();
    ImGui::End();
    ImGui::EndFrame();
}

TEST_CASE("value widgets notify only when their value changes", "[Widget][change]") {
    bool checked = false;
    int number = 1;
    std::string choice = "one";
    std::string text = "before";
    int changes = 0;

    CheckboxWidget checkbox(checked, "checked");
    NumberInputWidget<int> input(number);
    DropdownWidget dropdown(choice, {{"one", "one"}, {"two", "two"}});
    TextInputWidget text_input(text);

    checkbox.on_change([&changes] { ++changes; });
    input.on_change([&changes] { ++changes; });
    dropdown.on_change([&changes] { ++changes; });
    text_input.on_change([&changes] { ++changes; });

    REQUIRE(checkbox.set_checked(true));
    REQUIRE(changes == 1);
    REQUIRE(input.set_value(2));
    REQUIRE(changes == 2);
    REQUIRE(dropdown.select_value("two"));
    REQUIRE(changes == 3);
    REQUIRE(text_input.set_value("after"));
    REQUIRE(changes == 4);

    REQUIRE_FALSE(checkbox.set_checked(true));
    REQUIRE_FALSE(input.set_value(2));
    REQUIRE_FALSE(dropdown.select_value("two"));
    REQUIRE_FALSE(text_input.set_value("after"));
    REQUIRE(changes == 4);

    int value_changes = 0;
    WidgetValue<bool> owned_value(true);
    owned_value.on_change([&value_changes](const bool& value) {
        ++value_changes;
        REQUIRE_FALSE(value);
    });
    CheckboxWidget owned_checkbox(std::move(owned_value), "owned");

    REQUIRE(owned_checkbox.set_checked(false));
    REQUIRE(value_changes == 1);
    REQUIRE_FALSE(owned_checkbox.set_checked(false));
    REQUIRE(value_changes == 1);
}

TEST_CASE("paint-only animation overrides do not invalidate measurement", "[VisualState][animation][regression]") {
    VisualState state;
    int invalidations = 0;
    state.set_change_callback(&invalidations, [](void* owner, bool) { ++*static_cast<int*>(owner); });
    state.animate().to(StyleAnimationProperty::Color, rgb(1.0F, 0.0F, 0.0F), {0.2F, easing::linear});
    state.update(0.1F);
    REQUIRE(invalidations == 0);

    state.cancel_animations();
    state.animate().to(StyleAnimationProperty::PaddingX, 10.0F, {0.2F, easing::linear});
    state.update(0.1F);
    REQUIRE(invalidations == 1);
    REQUIRE(state.computed_style().padding().left == Catch::Approx(5.0F));
}

TEST_CASE("click feedback preserves unrelated overrides without remeasurement", "[animation][cleanup]") {
    class MeasuredButton final : public ButtonWidget {
    public:
        MeasuredButton() : ButtonWidget("click") {}

        using ButtonWidget::dispatch_event;

        int measurements = 0;

    private:
        void on_measure() override {
            ++measurements;
            ButtonWidget::on_measure();
        }
    };

    ui_test::ImGuiContext context({320.0F, 180.0F});
    MeasuredButton button;
    button.animate().to(StyleAnimationProperty::PaddingX, 20.0F);
    button.update(0.0F);
    ui_test::draw_node(button, "feedback-measure-test");
    const int measurements = button.measurements;

    button.configure_all_styles([](Style& style) { style.color(rgb(255, 0, 0)).alpha(0.7F); });
    button.style(StyleType::ACTIVE).background_color(rgb(200, 100, 50));
    int clicks = 0;
    button.on_click([&] {
        ++clicks;
        CHECK(button.computed_style().padding().left == Catch::Approx(20.0F));
    });
    UiEvent click = UiEvent::make(EventType::Click);
    click.button = PointerButton::Left;
    button.dispatch_event(click);
    CHECK(clicks == 1);
    button.update(0.0F);
    CHECK(button.computed_style().background_color().value == button.style(StyleType::ACTIVE).background_color().value);

    for (float dt : {0.05F, 0.06F, 0.1F}) {
        button.update(dt);
        ui_test::draw_node(button, "feedback-measure-test");
    }
    CHECK(button.computed_style().padding().left == Catch::Approx(20.0F));
    CHECK(button.measurements == measurements);
    CHECK(button.computed_style().background_color().value == button.style().background_color().value);
}

TEST_CASE("checkbox feedback releases only its background override", "[CheckboxWidget][cleanup]") {
    class TestCheckbox final : public CheckboxWidget {
    public:
        explicit TestCheckbox(bool& value) : CheckboxWidget(value, "check") {}

        using CheckboxWidget::dispatch_event;
    };

    bool value = false;
    TestCheckbox checkbox(value);
    checkbox.frame().animate().to(StyleAnimationProperty::PaddingX, 20.0F);
    checkbox.update(0.0F);
    UiEvent click = UiEvent::make(EventType::Click);
    click.button = PointerButton::Left;
    checkbox.dispatch_event(click);
    CHECK(value);
    for (float dt : {0.0F, 0.05F, 0.12F})
        checkbox.update(dt);
    CHECK(checkbox.frame().computed_style().padding().left == Catch::Approx(20.0F));
    CHECK(checkbox.frame().computed_style().background_color().value == checkbox.frame().style().background_color().value);
}

TEST_CASE("hidden and pending styled nodes skip imgui style work", "[StyledNode][cleanup]") {
    Node root;
    auto& hidden = root.add<StyledNode>();
    hidden.set_visible(false);
    auto& pending = root.add<StyledNode>();
    root.remove(pending);
    ::ImGuiContext* previous = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(nullptr);
    hidden.draw();
    pending.draw();
    ImGui::SetCurrentContext(previous);
    CHECK_FALSE(hidden.visible());
    CHECK(pending.removal_pending());
}

TEST_CASE("text input label changes preserve child sizing", "[TextInputWidget][cleanup]") {
    std::string value;
    TextInputWidget input(value);
    Node* label = input.find("label");
    Node* field = input.find("input");
    REQUIRE(label != nullptr);
    REQUIRE(field != nullptr);
    CHECK(label->layout().size_spec().width.mode == LayoutSizeMode::Fit);
    CHECK(field->layout().size_spec().width.mode == LayoutSizeMode::Grow);
    label->set_size({px(70.0F), fit()});
    input.set_label("label", LabelPlacement::Above);
    CHECK(label->layout().size_spec().width.mode == LayoutSizeMode::Fixed);
    CHECK(label->layout().size_spec().width.value == Catch::Approx(70.0F));
    input.set_label("");
    CHECK_FALSE(label->visible());
}

TEST_CASE("variable transitions remain reported for absent and differently typed targets", "[Style][variables][cleanup]") {
    Style style;
    Style target;
    style.variables().set("value", FloatValue{0.0F});
    target.variables().set("value", FloatValue{10.0F, {1.0F, easing::linear}});
    CHECK(Style::lerp(style, target, 0.25F));
    CHECK(style.variables().get<FloatValue>("value")->value == Catch::Approx(2.5F));

    Style absent;
    CHECK(Style::lerp(style, absent, 0.25F));
    target.variables().set("value", Vec2Value{{1.0F, 2.0F}});
    CHECK(Style::lerp(style, target, 0.25F));
    target.variables().set("value", FloatValue{20.0F, {1.0F, easing::linear}});
    CHECK(Style::lerp(style, target, 0.5F));
    CHECK(style.variables().get<FloatValue>("value")->value == Catch::Approx(11.25F));
    CHECK_FALSE(Style::lerp(style, target, 0.5F));
    CHECK(style.variables().get<FloatValue>("value")->value == Catch::Approx(20.0F));
}

TEST_CASE("style configuration stays separate from displayed transitions", "[VisualState][style]") {
    VisualState state;
    state.style().line_height(1.0F);
    state.style(StyleType::HOVER).line_height(2.0F, {0.5F, easing::linear});
    state.set_style(StyleType::HOVER);
    state.update(0.25F);

    REQUIRE(&state.style() == &state.style(StyleType::DEFAULT));
    REQUIRE(state.computed_style().line_height() == Catch::Approx(1.5F));

    state.style().line_height(3.0F);
    REQUIRE(state.style(StyleType::DEFAULT).line_height() == Catch::Approx(3.0F));
    REQUIRE(state.computed_style().line_height() == Catch::Approx(1.5F));
    state.update(0.25F);
    REQUIRE(state.computed_style().line_height() == Catch::Approx(2.0F));

    state.set_style(StyleType::DEFAULT);
    state.update(0.0F);
    REQUIRE(state.computed_style().line_height() == Catch::Approx(3.0F));
}

TEST_CASE("style transitions remain active until their duration ends", "[VisualState][transition]") {
    VisualState state;
    state.style(StyleType::HOVER).line_height(2.0F, {0.5F, easing::out_cubic});

    state.snap_to_style(StyleType::DEFAULT);
    state.set_style(StyleType::HOVER);
    state.update(0.45F);

    REQUIRE(state.computed_style().line_height() < 2.0F);
    REQUIRE(state.transitioning());

    state.update(0.05F);

    REQUIRE(state.computed_style().line_height() == Catch::Approx(2.0F));
    REQUIRE_FALSE(state.transitioning());
}

TEST_CASE("released animation properties return to the active style", "[VisualState][animation]") {
    VisualState state;
    const Color default_color = rgb(0.0F, 0.0F, 0.0F);
    const Color hover_color = rgb(0.0F, 1.0F, 0.0F);
    const Color flash_color = rgb(1.0F, 0.0F, 0.0F);
    state.style(StyleType::DEFAULT).background_color(default_color);
    state.style(StyleType::HOVER).background_color(hover_color);
    state.animate()
        .to(StyleAnimationProperty::BackgroundColor, flash_color)
        .then(0.05F)
        .release(StyleAnimationProperty::BackgroundColor, {0.1F, easing::linear});

    state.update(0.0F);
    REQUIRE(state.computed_style().background_color().value.rgba().x == Catch::Approx(1.0F));

    state.set_style(StyleType::HOVER);
    state.update(0.1F);

    REQUIRE(state.computed_style().background_color().value.rgba().x == Catch::Approx(0.5F));
    REQUIRE(state.computed_style().background_color().value.rgba().y == Catch::Approx(0.5F));

    state.update(0.05F);

    REQUIRE(state.computed_style().background_color().value.rgba().x == Catch::Approx(0.0F));
    REQUIRE(state.computed_style().background_color().value.rgba().y == Catch::Approx(1.0F));
    REQUIRE_FALSE(state.transitioning());
}

TEST_CASE("interrupted animations release from their displayed value", "[VisualState][animation]") {
    VisualState state;
    state.configure_all_styles([](Style& style) { style.padding({2.0F, 0.0F}); });
    state.animate().to(StyleAnimationProperty::PaddingX, 20.0F, {0.2F, easing::linear});
    state.update(0.1F);

    state.animate().release(StyleAnimationProperty::PaddingX, {0.1F, easing::linear});
    state.update(0.05F);

    REQUIRE(state.computed_style().padding().left == Catch::Approx(6.5F));

    state.update(0.05F);

    REQUIRE(state.computed_style().padding().left == Catch::Approx(2.0F));
    REQUIRE_FALSE(state.transitioning());
}

TEST_CASE("animation sequences transform style presentation values", "[VisualState][animation][transform]") {
    VisualState state;
    state.animate()
        .to(StyleAnimationProperty::Rotation, 40.0F, {0.2F, easing::linear})
        .to(StyleAnimationProperty::Scale, ImVec2{1.4F, 0.8F}, {0.2F, easing::linear});
    state.update(0.1F);

    REQUIRE(state.computed_style().rotation() == Catch::Approx(20.0F));
    REQUIRE(state.computed_style().scale().x == Catch::Approx(1.2F));
    REQUIRE(state.computed_style().scale().y == Catch::Approx(0.9F));

    state.animate().by(StyleAnimationProperty::Rotation, 30.0F, {0.1F, easing::linear});
    state.update(0.1F);

    REQUIRE(state.computed_style().rotation() == Catch::Approx(50.0F));

    state.animate().release_all({0.1F, easing::linear});
    state.update(0.1F);

    REQUIRE(state.computed_style().rotation() == Catch::Approx(0.0F));
    REQUIRE(state.computed_style().scale().x == Catch::Approx(1.0F));
    REQUIRE(state.computed_style().scale().y == Catch::Approx(1.0F));
}

TEST_CASE("animation sequence steps continue from the preceding track", "[VisualState][animation]") {
    VisualState state;
    state.animate()
        .to(StyleAnimationProperty::Scale, ImVec2{2.0F, 2.0F}, {0.1F, easing::linear})
        .then()
        .to(StyleAnimationProperty::Scale, ImVec2{3.0F, 3.0F}, {0.1F, easing::linear});

    state.update(0.1F);
    REQUIRE(state.computed_style().scale().x == Catch::Approx(2.0F));

    state.update(0.05F);
    REQUIRE(state.computed_style().scale().x == Catch::Approx(2.5F));
}

TEST_CASE("styled nodes rotate their generated vertices without changing layout", "[Widget][style][transform]") {
    class TransformProbeWidget final : public Widget {
    public:
        TransformProbeWidget() : Widget("transform-probe") {}

    private:
        bool paint() override {
            const Rect rect = Rect::from_position_size(ImGui::GetCursorScreenPos(), layout().size());
            ImGui::Dummy(rect.size());
            ImGui::GetWindowDrawList()->AddRectFilled(rect.min, rect.max, IM_COL32_WHITE);
            return true;
        }
    };

    ui_test::ImGuiContext context({240.0F, 160.0F});
    TransformProbeWidget widget;
    widget.set_size({px(40.0F), px(20.0F)});
    widget.configure_all_styles([](Style& style) { style.rotation(90.0F); });

    ui_test::draw_window("styled-transform-test", [&] {
        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        const int first_vertex = draw_list->VtxBuffer.Size;
        widget.draw();

        const Rect layout_rect = widget.layout().visual_rect();

        float min_x = std::numeric_limits<float>::max();
        float max_x = std::numeric_limits<float>::lowest();
        float min_y = std::numeric_limits<float>::max();
        float max_y = std::numeric_limits<float>::lowest();

        for (int index = first_vertex; index < draw_list->VtxBuffer.Size; ++index) {
            const ImVec2 position = draw_list->VtxBuffer[index].pos;
            min_x = std::min(min_x, position.x);
            max_x = std::max(max_x, position.x);
            min_y = std::min(min_y, position.y);
            max_y = std::max(max_y, position.y);
        }

        REQUIRE(layout_rect.size().x == Catch::Approx(40.0F));
        REQUIRE(layout_rect.size().y == Catch::Approx(20.0F));
        REQUIRE(max_x - min_x == Catch::Approx(20.0F));
        REQUIRE(max_y - min_y == Catch::Approx(40.0F));
    });
}

TEST_CASE("style cursor follows hovered nodes", "[Style][cursor]") {
    ui_test::ImGuiContext context({320.0F, 180.0F});
    InputRouter router;
    Widget widget("cursor-widget");
    widget.style(StyleType::HOVER).cursor(ImGuiMouseCursor_Hand);

    ui_test::draw_window("style-cursor-test", [&] {
        router.begin_frame();
        router.register_target(widget, {{0.0F, 0.0F}, {40.0F, 20.0F}});

        UiEvent move = ui_test::pointer_event(EventType::PointerMove, {10.0F, 10.0F});
        router.dispatch(move);
        REQUIRE(ImGui::GetMouseCursor() == ImGuiMouseCursor_Hand);

        move.position = {100.0F, 100.0F};
        router.dispatch(move);
        REQUIRE(ImGui::GetMouseCursor() == ImGuiMouseCursor_Arrow);
    });
}

TEST_CASE("border alpha fades out when a hover state is cleared", "[VisualState][transition]") {
    VisualState state;
    const Color accent = rgb(233, 30, 115);
    const Color hidden_accent = rgba(accent.rgba().x, accent.rgba().y, accent.rgba().z, 0.0F);

    state.configure_all_styles([&](Style& style) { style.border_color(hidden_accent, 0.2F); });
    state.style(StyleType::HOVER).border_color(accent);

    state.set_style(StyleType::HOVER);
    state.update(0.2F);
    const float visible_alpha = state.computed_style().border_color().get().w;

    state.set_style(StyleType::DEFAULT);
    state.update(0.1F);
    const ImVec4 fading_color = state.computed_style().border_color().get();

    REQUIRE(visible_alpha > 0.0F);
    REQUIRE(fading_color.w > 0.0F);
    REQUIRE(fading_color.w < visible_alpha);
    REQUIRE(fading_color.x == Catch::Approx(accent.rgba().x));
    REQUIRE(fading_color.y == Catch::Approx(accent.rgba().y));
    REQUIRE(fading_color.z == Catch::Approx(accent.rgba().z));

    state.update(0.1F);
    REQUIRE(state.computed_style().border_color().get().w == Catch::Approx(0.0F));
}

TEST_CASE("widget input requires both node and visual state to accept input", "[Widget][input]") {
    Widget widget("widget");
    InputRouter router;
    router.register_target(widget, {{0.0F, 0.0F}, {10.0F, 10.0F}});

    REQUIRE(widget.accepts_input());
    REQUIRE(router.node_at({5.0F, 5.0F}) == &widget);

    widget.set_input_enabled(false);
    REQUIRE_FALSE(widget.accepts_input());
    REQUIRE(router.node_at({5.0F, 5.0F}) == nullptr);

    widget.set_input_enabled(true);
    widget.set_visible(false);
    REQUIRE_FALSE(widget.accepts_input());

    widget.set_visible(true);
    widget.fade_out();
    REQUIRE_FALSE(widget.accepts_input());
    REQUIRE(router.node_at({5.0F, 5.0F}) == nullptr);
}

TEST_CASE("widget callbacks can prevent container wheel scrolling", "[Widget][input][scroll][regression]") {
    Runtime runtime;
    ui::Surface surface = ui_test::make_surface(runtime);
    auto& widget = surface.root().add<Widget>("scroll-widget");
    widget.set_size({px(180.0F), px(100.0F)});
    widget.set_scrollable(true);
    widget.scroll().set_behaviour(ScrollBehavior::Instant);
    widget.add<BoxWidget>("tall-content", LayoutSize{grow(), px(400.0F)});
    const auto context = ui_test::prepare_surface(surface, {300.0F, 200.0F});
    for (int frame = 0; frame < 3; ++frame) {
        ui_test::draw_surface(surface);
    }
    REQUIRE(widget.scroll().max().y > 0.0F);

    int callbacks = 0;
    widget.on_event(EventType::Scroll, [&callbacks](UiEvent& event) {
        ++callbacks;
        event.prevent_default();
    });
    UiEvent prevented = UiEvent::make(EventType::Scroll);
    prevented.scroll = {0.0F, -1.0F};
    InputRouter::dispatch_to(widget, prevented);
    CHECK(callbacks == 1);
    CHECK_FALSE(prevented.native_input_blocked);
    for (int frame = 0; frame < 3; ++frame) {
        ui_test::draw_surface(surface);
    }
    CHECK(widget.scroll().position().y == Catch::Approx(0.0F));

    widget.on_event(EventType::Scroll, [&callbacks](UiEvent&) { ++callbacks; });
    UiEvent allowed = UiEvent::make(EventType::Scroll);
    allowed.scroll = {0.0F, -1.0F};
    InputRouter::dispatch_to(widget, allowed);
    CHECK(callbacks == 2);
    CHECK(allowed.native_input_blocked);
    for (int frame = 0; frame < 3; ++frame) {
        ui_test::draw_surface(surface);
    }
    CHECK(widget.scroll().position().y > 0.0F);
}

TEST_CASE("styled nodes keep borders out of imgui style scope", "[Widget][style][regression]") {
    class StyleProbeWidget final : public Widget {
    public:
        StyleProbeWidget() : Widget("style-probe") {}

        ImVec2 observed_padding;
        float observed_rounding = 0.0F;
        float observed_border_size = 0.0F;
        float observed_alpha = 0.0F;
        ImVec4 observed_text;
        ImVec4 observed_background;

    private:
        bool paint() override {
            const ImGuiStyle& imgui_style = ImGui::GetStyle();
            observed_padding = imgui_style.FramePadding;
            observed_rounding = imgui_style.FrameRounding;
            observed_border_size = imgui_style.FrameBorderSize;
            observed_alpha = imgui_style.Alpha;
            observed_text = ImGui::GetStyleColorVec4(ImGuiCol_Text);
            observed_background = ImGui::GetStyleColorVec4(ImGuiCol_FrameBg);
            ImGui::Dummy({10.0F, 10.0F});
            return true;
        }
    };

    ui_test::ImGuiContext context({160.0F, 120.0F});
    StyleProbeWidget widget;

    widget.configure_all_styles([](Style& style) {
        style.color(rgb(51, 102, 153))
            .background_color(rgb(26, 77, 128))
            .padding({7.0F, 9.0F})
            .border(BorderEdge{3.0F, BorderType::Solid})
            .border_radius(6.0F)
            .alpha(0.5F);
    });

    widget.update(0.0F);

    const ImGuiStyle before = ImGui::GetStyle();
    ui_test::draw_node(widget, "styled-scope-test");

    REQUIRE(widget.observed_padding.x == Catch::Approx(7.0F));
    REQUIRE(widget.observed_padding.y == Catch::Approx(9.0F));
    REQUIRE(widget.observed_rounding == Catch::Approx(6.0F));
    REQUIRE(widget.observed_border_size == Catch::Approx(0.0F));
    REQUIRE(widget.observed_alpha == Catch::Approx(before.Alpha * 0.5F));
    REQUIRE(widget.observed_text.x == Catch::Approx(0.2F).margin(0.01F));
    REQUIRE(widget.observed_background.y == Catch::Approx(0.3F).margin(0.01F));
    REQUIRE(ImGui::GetStyle().FramePadding.x == Catch::Approx(before.FramePadding.x));
    REQUIRE(ImGui::GetStyle().Alpha == Catch::Approx(before.Alpha));
}

TEST_CASE("styled widgets advance visual state during update", "[Widget][style]") {
    ui_test::ImGuiContext context({160.0F, 120.0F});

    Widget widget("widget");
    widget.configure_all_styles([](Style& style) { style.color(rgb(0, 0, 0), 0.2F); });
    widget.style(StyleType::HOVER).color(rgb(255, 0, 0), 0.2F);
    widget.set_visual_style(StyleType::HOVER);

    widget.update(0.1F);
    const float color_after_update = widget.computed_style().color().get().x;
    REQUIRE(color_after_update == Catch::Approx(0.5F));

    ui_test::draw_node(widget, "style-tick-test");

    REQUIRE(widget.computed_style().color().get().x == Catch::Approx(color_after_update));
}

TEST_CASE("style variables stay local to their declared state", "[VisualState][variables]") {
    VisualState state;
    state.style(StyleType::HOVER).variables().set("line_width", FloatValue{2.0F, 0.15F});

    state.set_style(StyleType::HOVER);
    state.update(1.0F / 60.0F);
    REQUIRE(state.computed_style().variables().get<FloatValue>("line_width") != nullptr);

    state.set_style(StyleType::ACTIVE);
    state.update(1.0F / 60.0F);
    REQUIRE(state.computed_style().variables().get<FloatValue>("line_width") == nullptr);
}

TEST_CASE("context menu clamps its position and fades out", "[ContextMenuWidget]") {
    Runtime runtime;
    ui::Surface surface = ui_test::make_surface(runtime);
    const auto surface_context = ui_test::prepare_surface(surface, {320.0F, 240.0F});

    ContextMenuItems items;
    items.push_back({.label = "item"});
    items.push_back({.label = "second item"});
    auto& menu = surface.root().add<ContextMenuWidget>(std::move(items));

    REQUIRE_FALSE(menu.visible());
    menu.open_at({300.0F, 220.0F});
    ui_test::draw_surface(surface, 0.2F);

    REQUIRE(menu.is_open());
    const Rect menu_rect = menu.layout().visual_rect();
    REQUIRE(menu_rect.valid());
    REQUIRE(menu_rect.min.x >= 0.0F);
    REQUIRE(menu_rect.min.y >= 0.0F);
    REQUIRE(menu_rect.max.x <= 320.0F);
    REQUIRE(menu_rect.max.y <= 240.0F);
    const Rect second_item = menu.children().back()->layout().visual_rect();
    CHECK(surface.input_router().node_at(ui_test::center(second_item)) == menu.children().back().get());

    ImGui::GetIO().MousePos = ui_test::center(second_item);
    ui_test::draw_surface(surface, 0.01F);

    ImGui::GetIO().MousePos = {0.0F, 0.0F};
    ui_test::draw_surface(surface, 0.78F);
    REQUIRE(menu.is_open());

    ui_test::draw_surface(surface, 0.02F);
    REQUIRE_FALSE(menu.is_open());
    ui_test::draw_surface(surface, 0.2F);
    REQUIRE_FALSE(menu.visible());
}

TEST_CASE("context menu item callbacks can keep the root menu open", "[ContextMenuWidget]") {
    Runtime runtime;
    ui::Surface surface = ui_test::make_surface(runtime);
    const auto surface_context = ui_test::prepare_surface(surface, {320.0F, 240.0F});

    bool callback_called = false;
    ContextMenuItems items;
    items.push_back({
        .label = "keep open",
        .callback = [&callback_called](ContextMenuWidget& menu) {
            callback_called = true;
            menu.cancel_close();
        },
    });
    auto& menu = surface.root().add<ContextMenuWidget>(std::move(items));
    menu.open_at({20.0F, 20.0F});
    ui_test::draw_surface(surface, 0.2F);

    const Rect item_rect = menu.children().front()->layout().visual_rect();
    const ImVec2 item_position = {item_rect.min.x + 4.0F, item_rect.min.y + 4.0F};
    auto down = ui_test::pointer_event(EventType::PointerDown, item_position);
    auto up = ui_test::pointer_event(EventType::PointerUp, item_position);
    REQUIRE(surface.dispatch(down));
    REQUIRE(surface.dispatch(up));

    REQUIRE(callback_called);
    REQUIRE(menu.is_open());
    REQUIRE(menu.visible());
}

TEST_CASE("context menu blocks and closes on outside pointer input", "[ContextMenuWidget]") {
    Runtime runtime;
    ui::Surface surface = ui_test::make_surface(runtime);
    const auto surface_context = ui_test::prepare_surface(surface, {320.0F, 240.0F});

    int click_count = 0;
    auto& button = surface.root().add<ButtonWidget>("under menu", LayoutSize{px(100.0F), px(32.0F)});
    button.set_layout({
        .size = {px(100.0F), px(32.0F)},
        .placement = {.offset = {8.0F, 8.0F}},
        .in_flow = false,
    });
    button.on_click([&click_count] { ++click_count; });

    ContextMenuItems items;
    items.push_back({.label = "item"});
    auto& menu = surface.root().add<ContextMenuWidget>(std::move(items));
    menu.open_at({160.0F, 120.0F});
    ui_test::draw_surface(surface, 0.2F);

    auto down = ui_test::pointer_event(EventType::PointerDown, {20.0F, 20.0F});
    auto up = ui_test::pointer_event(EventType::PointerUp, {20.0F, 20.0F});
    REQUIRE(surface.dispatch(down));
    REQUIRE(surface.dispatch(up));
    REQUIRE_FALSE(menu.is_open());
    REQUIRE(click_count == 0);

    ui_test::draw_surface(surface, 0.2F);
    REQUIRE_FALSE(menu.visible());
}

TEST_CASE("context menu opens a submenu when its parent is hovered", "[ContextMenuWidget]") {
    Runtime runtime;
    ui::Surface surface = ui_test::make_surface(runtime);
    const auto surface_context = ui_test::prepare_surface(surface, {480.0F, 240.0F});

    ContextMenuItems children;
    children.push_back({.label = "child"});
    ContextMenuItems items;
    items.push_back({.label = "parent", .children = std::move(children)});
    auto& menu = surface.root().add<ContextMenuWidget>(std::move(items));
    menu.open_at({20.0F, 20.0F});
    ui_test::draw_surface(surface, 0.2F);

    const Rect item_rect = menu.children().front()->layout().visual_rect();
    const ImVec2 item_position = {item_rect.min.x + 4.0F, item_rect.min.y + 4.0F};
    auto move = ui_test::pointer_event(EventType::PointerMove, item_position);
    surface.dispatch(move);
    ImGui::GetIO().MousePos = item_position;
    ui_test::draw_surface(surface, 0.2F);

    auto* submenu = dynamic_cast<ContextMenuWidget*>(menu.children()[1].get());
    REQUIRE(submenu != nullptr);
    REQUIRE(submenu->visible());
    auto cross_gap = ui_test::pointer_event(
        EventType::PointerMove, {(item_rect.max.x + submenu->layout().visual_rect().min.x) * 0.5F, item_rect.min.y + 4.0F}
    );
    surface.dispatch(cross_gap);
    REQUIRE(submenu->is_open());

    auto enter_submenu = ui_test::pointer_event(
        EventType::PointerMove, {submenu->layout().visual_rect().min.x + 4.0F, submenu->layout().visual_rect().min.y + 4.0F}
    );
    surface.dispatch(enter_submenu);
    REQUIRE(submenu->is_open());

    auto leave_item = ui_test::pointer_event(
        EventType::PointerMove, {menu.layout().visual_rect().min.x + 1.0F, menu.layout().visual_rect().min.y + 1.0F}
    );
    surface.dispatch(leave_item);
    ImGui::GetIO().MousePos = leave_item.position;
    ui_test::draw_surface(surface, 0.81F);
    ui_test::draw_surface(surface, 0.0F);
    REQUIRE_FALSE(submenu->is_open());

    move = ui_test::pointer_event(EventType::PointerMove, item_position);
    surface.dispatch(move);
    ImGui::GetIO().MousePos = item_position;
    ui_test::draw_surface(surface, 0.2F);
    REQUIRE(submenu->is_open());

    ImGui::GetIO().MousePos = {460.0F, 220.0F};
    ui_test::draw_surface(surface, 0.2F);
    REQUIRE_FALSE(submenu->is_open());
    REQUIRE_FALSE(menu.is_open());
}

TEST_CASE("virtual rows expand and collapse independently", "[layout][virtual-layout]") {
    Runtime runtime;
    ui::Surface surface = ui_test::make_surface(runtime);
    auto& list = surface.root().add<VirtualLayout>("virtual-list", 24.0F);
    list.set_size({px(180.0F), px(72.0F)});
    list.set_items(100000, [&list, &surface](size_t index) -> Node& {
        auto& row = list.add<ButtonWidget>(std::to_string(index), LayoutSize{grow(), px(24.0F)});
        row.on_click([&list, index] { list.set_extra_offset(index, list.extra_offset(index) == 0.0F ? 64.0F : 0.0F); });
        return row;
    });
    REQUIRE(list.children().empty());

    const auto surface_context = ui_test::prepare_surface(surface, {240.0F, 180.0F});
    ui_test::draw_surface(surface);
    ui_test::draw_surface(surface);
    REQUIRE(list.children().size() < 10);
    REQUIRE(list.children().size() >= 2);
    Node* first = list.children()[0].get();
    Node* second = list.children()[1].get();

    UiEvent click = UiEvent::make(EventType::Click);
    InputRouter::dispatch_to(*first, click);
    REQUIRE(list.extra_offset(0) == 64.0F);
    InputRouter::dispatch_to(*second, click);
    REQUIRE(list.extra_offset(1) == 64.0F);
    InputRouter::dispatch_to(*first, click);
    REQUIRE(list.extra_offset(0) == 0.0F);
    REQUIRE(list.extra_offset(1) == 64.0F);
}

TEST_CASE("dropdown option replacements retain only the current live rows", "[DropdownWidget][options][regression]") {
    Runtime runtime;
    ui::Surface surface = ui_test::make_surface(runtime);
    std::string value = "original";
    int changes = 0;
    auto& dropdown = surface.root().add<DropdownWidget>(
        value, std::vector<DropdownOption>{{"first", "first"}, {"second", "second"}}, "", LabelPlacement::Above, "replacement"
    );
    dropdown.set_size({px(180.0F), px(32.0F)});
    dropdown.on_change([&changes] { ++changes; });
    const uint64_t first_identity = dropdown.body().children().front()->identity();
    std::vector<DropdownOption> options{{"new first", "new-first"}, {"new second", "new-second"}};

    SECTION("same count retains row identities") {
        dropdown.set_options(options);
        REQUIRE(dropdown.body().children().front()->identity() == first_identity);
    }
    SECTION("different count replaces rows without immediate destruction") {
        options.push_back({"third", "third"});
        dropdown.set_options(options);
        REQUIRE(dropdown.body().children().front()->removal_pending());
    }
    SECTION("consecutive growth does not count pending rows") {
        dropdown.set_options({{"a", "a"}, {"b", "b"}, {"c", "c"}});
        options.insert(options.end(), {{"third", "third"}, {"fourth", "fourth"}, {"fifth", "fifth"}});
        dropdown.set_options(options);
    }
    SECTION("consecutive shrink and growth does not reuse stale indices") {
        dropdown.set_options({{"temporary", "temporary"}});
        options.push_back({"third", "third"});
        dropdown.set_options(options);
    }
    SECTION("same-count replacement after a rebuild retains live identities") {
        options.push_back({"third", "third"});
        dropdown.set_options(options);
        const uint64_t live_identity = dropdown.body().children().back()->identity();
        options.back() = {"changed third", "changed-third"};
        dropdown.set_options(options);
        REQUIRE(dropdown.body().children().back()->identity() == live_identity);
    }
    SECTION("empty replacement can be repopulated before the next frame") {
        dropdown.set_options({});
        dropdown.set_options(options);
    }

    std::vector<Node*> rows;
    for (const auto& child : dropdown.body().children()) {
        if (!child->removal_pending()) rows.push_back(child.get());
    }
    REQUIRE(rows.size() == options.size());
    REQUIRE(value == "original");
    REQUIRE(changes == 0);

    const auto surface_context = ui_test::prepare_surface(surface, {400.0F, 300.0F});
    ui_test::draw_surface(surface);
    dropdown.open();
    ui_test::draw_surface(surface, 0.2F);
    REQUIRE(dropdown.is_open());
    REQUIRE(dropdown.body().children().size() == options.size());
    const ImVec2 position = ui_test::center(rows.back()->layout().visual_rect());
    REQUIRE(surface.input_router().node_at(position) == rows.back());
    auto down = ui_test::pointer_event(EventType::PointerDown, position);
    auto up = ui_test::pointer_event(EventType::PointerUp, position);
    surface.dispatch(down);
    surface.dispatch(up);
    REQUIRE(value == options.back().value);
    REQUIRE(changes == 1);
    REQUIRE_FALSE(dropdown.is_open());
}

TEST_CASE("dropdown closing body blocks underlying input without selecting rows", "[DropdownWidget][fade][regression]") {
    Runtime runtime;
    ui::Surface surface = ui_test::make_surface(runtime);
    std::string value = "first";
    int changes = 0;
    int underlying_clicks = 0;
    auto& underlying = surface.root().add<ButtonWidget>("underlying");
    underlying.on_click([&underlying_clicks] { ++underlying_clicks; });
    auto& dropdown = surface.root().add<DropdownWidget>(
        value, std::vector<DropdownOption>{{"first", "first"}, {"second", "second"}}, "", LabelPlacement::Above, "fading"
    );
    dropdown.set_size({px(180.0F), px(32.0F)});
    dropdown.on_change([&changes] { ++changes; });
    const auto surface_context = ui_test::prepare_surface(surface, {400.0F, 300.0F});
    ui_test::draw_surface(surface);
    dropdown.open();
    ui_test::draw_surface(surface, 0.2F);
    REQUIRE(dropdown.is_open());
    const ImVec2 position = ui_test::center(dropdown.body().children().back()->layout().visual_rect());

    dropdown.close();
    dropdown.open();
    REQUIRE(dropdown.is_open());
    dropdown.close();
    ui_test::draw_surface(surface, 0.01F);
    REQUIRE(dropdown.body().visually_visible());
    REQUIRE(dropdown.body().input_enabled());
    REQUIRE_FALSE(dropdown.body().children().back()->accepts_input());
    surface.input_router().register_target(underlying, dropdown.body().layout().visual_rect());
    REQUIRE(surface.input_router().node_at(position) == &dropdown.body());
    auto down = ui_test::pointer_event(EventType::PointerDown, position);
    auto up = ui_test::pointer_event(EventType::PointerUp, position);
    surface.dispatch(down);
    surface.dispatch(up);
    REQUIRE(value == "first");
    REQUIRE(changes == 0);
    REQUIRE(underlying_clicks == 0);

    ui_test::draw_surface(surface, 0.2F);
    ui_test::draw_surface(surface, 0.0F);
    REQUIRE_FALSE(dropdown.body().input_enabled());
    REQUIRE_FALSE(dropdown.body().visually_visible());
    dropdown.open();
    ui_test::draw_surface(surface, 0.2F);
    REQUIRE(dropdown.is_open());
    REQUIRE(dropdown.body().children().back()->accepts_input());
}

TEST_CASE("dropdown observes an externally closed native popup before reopening", "[DropdownWidget][popup][regression]") {
    Runtime runtime;
    ui::Surface surface = ui_test::make_surface(runtime);
    std::string value = "first";
    auto& dropdown = surface.root().add<DropdownWidget>(
        value, std::vector<DropdownOption>{{"first", "first"}, {"second", "second"}}, "", LabelPlacement::Above, "external-close"
    );
    dropdown.set_size({px(180.0F), px(32.0F)});
    const auto surface_context = ui_test::prepare_surface(surface, {400.0F, 300.0F});
    ui_test::draw_surface(surface);
    dropdown.open();
    ui_test::draw_surface(surface, 0.2F);
    REQUIRE(dropdown.is_open());

    bool closed_native = false;
    auto& row = static_cast<StyledNode&>(*dropdown.body().children().front());
    row.before().set_draw_callback([&closed_native](const PaintContext&) {
        if (!closed_native) {
            ImGui::CloseCurrentPopup();
            closed_native = true;
        }
    });
    ui_test::draw_surface(surface, 0.0F);
    REQUIRE(closed_native);
    ui_test::draw_surface(surface, 0.0F);
    REQUIRE_FALSE(dropdown.is_open());
    REQUIRE(value == "first");
    ui_test::draw_surface(surface, 0.2F);
    ui_test::draw_surface(surface, 0.0F);
    REQUIRE_FALSE(dropdown.body().input_enabled());

    dropdown.open();
    ui_test::draw_surface(surface, 0.2F);
    REQUIRE(dropdown.is_open());
    const auto& option = dropdown.body().children().back();
    const ImVec2 position = ui_test::center(option->layout().visual_rect());
    REQUIRE(surface.input_router().node_at(position) == option.get());
    auto down = ui_test::pointer_event(EventType::PointerDown, position);
    auto up = ui_test::pointer_event(EventType::PointerUp, position);
    surface.dispatch(down);
    surface.dispatch(up);
    REQUIRE(value == "second");
}
