#include <arbor/resources/font-registry.hpp>
#include <arbor/widgets/button.hpp>
#include <arbor/widgets/checkbox.hpp>
#include <arbor/widgets/color-picker.hpp>
#include <arbor/widgets/context-menu.hpp>
#include <arbor/widgets/dropdown.hpp>
#include <arbor/widgets/file-dialog.hpp>
#include <arbor/widgets/number-input.hpp>
#include <arbor/widgets/text-input.hpp>
#include <arbor/widgets/text.hpp>
#include "imgui-context.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <algorithm>
#include <array>
#include <cfloat>

using namespace ui;

TEST_CASE("inherited font sizes refresh after edits and reparenting", "[fonts][inheritance]") {
    ui_test::ImGuiContext context({320.0f, 180.0f});
    ImFontConfig config;
    config.SizePixels = 27.0f;
    ImFont* alternate = ImGui::GetIO().Fonts->AddFontDefaultVector(&config);
    ui_test::ImGuiContext::build_fonts();

    ui_test::draw_window("font-size-inheritance", [&] {
        Container first("first");
        Container second("second");
        first.style().font_size(18.0f);
        second.style().font_size(12.0f);
        auto& text = first.add<TextWidget>("text");
        CHECK(text.font_size() == 18.0f);

        // parent edits and reparenting must discard the size resolved by the previous query
        first.style().font_size(24.0f);
        CHECK(text.font_size() == 24.0f);
        second.add(first.detach(text));
        CHECK(text.font_size() == 12.0f);

        text.style().font_size(30.0f);
        second.style().font_size(40.0f);
        CHECK(text.font_size() == 30.0f);
        text.style().font_size(0.0f);
        CHECK(text.font_size() == 40.0f);

        // an unset size follows the current native font scope even after inheritance has been cached
        second.style().font_size(0.0f);
        const float native_size = text.font_size();
        CHECK(native_size != alternate->LegacySize);
        ImGui::PushFont(alternate, alternate->LegacySize);
        CHECK(text.font_size() == alternate->LegacySize);
        ImGui::PopFont();
        CHECK(text.font_size() == native_size);
    });
}

TEST_CASE("file font weights select real instances and cache them per context", "[fonts][weight]") {
    FontRegistry fonts;
    Font* family = fonts.add("inter", std::filesystem::path(ARBOR_ASSETS_DIR) / "fonts/Inter.ttf");
    ui_test::ImGuiContext first({320.0f, 180.0f});
    ImGui::GetIO().BackendFlags |= ImGuiBackendFlags_RendererHasTextures;

    ImFont* regular = family->get(FontWeight::Regular);
    ImFont* bold = family->get(FontWeight::Bold);
    REQUIRE(regular != nullptr);
    REQUIRE(bold != nullptr);
    CHECK(regular != bold);
    CHECK(regular->Sources.front()->FontData == bold->Sources.front()->FontData);
    CHECK(regular->Sources.front()->FontDataOwnedByAtlas);
    CHECK_FALSE(bold->Sources.front()->FontDataOwnedByAtlas);
    CHECK(family->get() == regular);
    CHECK(family->get(FontWeight::Bold) == bold);
    const float regular_width = regular->CalcTextSizeA(32.0f, FLT_MAX, 0.0f, "MMMMMMMM").x;
    const float bold_width = bold->CalcTextSizeA(32.0f, FLT_MAX, 0.0f, "MMMMMMMM").x;
    CHECK(regular_width != Catch::Approx(bold_width));

    {
        ui_test::ImGuiContext second({320.0f, 180.0f});
        REQUIRE(family->get(FontWeight::Bold) != nullptr);
        CHECK(family->get(FontWeight::Bold) != bold);
        fonts.release_context(ImGui::GetCurrentContext());
    }
    CHECK(family->get(FontWeight::Bold) == bold);
    fonts.release_context(ImGui::GetCurrentContext());
}

TEST_CASE("controls inherit managed font weights and invalidate text measurements", "[fonts][weight][widgets]") {
    Runtime runtime;
    Font* family = runtime.fonts().add("inter", std::filesystem::path(ARBOR_ASSETS_DIR) / "fonts/Inter.ttf");
    Surface surface = ui_test::make_surface(runtime);
    surface.set_primary_font(family);
    const ImGuiContextScope context(surface.imgui_context());
    ImGui::GetIO().DisplaySize = {1200.0f, 1200.0f};
    ImGui::GetIO().BackendFlags |= ImGuiBackendFlags_RendererHasTextures;

    auto& group = surface.root().add<Container>("font-group");
    group.set_size({grow(), fit()});
    group.configure_all_styles([](Style& style) { style.font_weight(FontWeight::Bold).font_slant(FontSlant::Normal); });
    auto& text = group.add<TextWidget>("MMMMMMMM");
    text.style().font_size(32.0f);
    const std::array<StyledNode*, 9> controls{
        &text,
        &group.add<ButtonWidget>("Button"),
        &group.add<CheckboxWidget>(),
        &group.add<TextInputWidget>(),
        &group.add<NumberInputWidget<float>>(),
        &group.add<DropdownWidget>(),
        &group.add<ColorPickerWidget>(),
        &group.add<FileDialogWidget>(),
        &group.add<ContextMenuWidget>(),
    };

    surface.frame();
    ImFont* bold = family->get(FontWeight::Bold);
    for (auto* control : controls) {
        CHECK(control->font() == bold);
    }
    const float bold_width = text.layout().measured_size().x;

    text.style().font_weight(FontWeight::Regular);
    surface.frame();
    CHECK(text.font() == family->get(FontWeight::Regular));
    CHECK(text.layout().measured_size().x != Catch::Approx(bold_width));
    CHECK(controls[1]->font() == bold);

    text.style().font_weight(std::nullopt);
    surface.frame();
    CHECK(text.font() == bold);

    text.style(StyleType::HOVER).font_weight(FontWeight::Light);
    text.set_visual_style(StyleType::HOVER);
    text.update(1.0f);
    CHECK(text.font() == family->get(FontWeight::Light));
    text.set_visual_style(StyleType::DEFAULT);
    text.update(1.0f);
    CHECK(text.font() == bold);

    Font* replacement = runtime.fonts().add("replacement", std::filesystem::path(ARBOR_ASSETS_DIR) / "fonts/Inter.ttf");
    surface.set_primary_font(replacement);
    surface.frame();
    CHECK(controls[1]->font() == replacement->get(FontWeight::Bold));
}

#ifdef ARBOR_TEST_NATIVE_FONTS
TEST_CASE("system fonts filter character coverage and resolve unavailable families", "[fonts][system]") {
    FontRegistry fonts;
    fonts.refresh();
    const auto families = fonts.families();
    REQUIRE_FALSE(families.empty());

    const auto latin = fonts.families({.characters = U"Az"});
    REQUIRE_FALSE(latin.empty());
    for (const auto& family : latin) {
        CHECK(family.coverage.covers(U"Az"));
    }

    const auto sources = fonts.resolve({.main = FontSelection::family(latin.front().id), .fallback = FontSelection::none()});
    REQUIRE(sources.size() == 1);
    CHECK(fonts.resolve({.main = FontSelection::family("missing-family"), .fallback = FontSelection::none()}).size() == 1);
}

TEST_CASE("automatic font fallback selects one complementary face", "[fonts][system][automatic]") {
    FontRegistry registry;
    const auto families = registry.families();
    if (families.empty()) SKIP("requires installed fonts");

    CHECK(registry.resolve({}).size() <= 2);
    Font* font = registry.add("automatic", FontRequest{});
    REQUIRE(font != nullptr);
    ui_test::ImGuiContext context({320.0f, 180.0f});
    ImGui::GetIO().BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
    ImFont* loaded = font->get();
    REQUIRE(loaded != nullptr);

    CHECK(loaded->IsGlyphInFont(U'A'));
    registry.release_context(ImGui::GetCurrentContext());
}

TEST_CASE("replacing a font releases old variants and refreshes inherited measurements", "[fonts][system][replace]") {
    Runtime runtime;
    auto& registry = runtime.fonts();
    const auto families = registry.families({.characters = U"AM"});
    if (families.size() < 2) SKIP("requires two installed Latin families");

    FontRequest request{.main = FontSelection::family(families[0].id), .fallback = FontSelection::none()};
    Font* family = registry.set("interface", request);
    Surface surface = ui_test::make_surface(runtime);
    surface.set_primary_font(family);
    const ImGuiContextScope context(surface.imgui_context());
    ImGui::GetIO().DisplaySize = {1200.0f, 1200.0f};
    ImGui::GetIO().BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
    auto& text = surface.root().add<TextWidget>("MMMMMMMM");
    text.configure_all_styles([](Style& style) { style.font_size(32.0f); });
    ui_test::draw_surface(surface, 0.2f);
    family->get(FontWeight::Bold);
    std::array<int, 2> source_counts{ImGui::GetIO().Fonts->Sources.Size, 0};

    for (int change = 1; change <= 4; ++change) {
        auto* previous = family->get();
        request.main = FontSelection::family(families[change % 2].id);
        CHECK(registry.set("interface", request) == family);
        surface.set_primary_font(family);
        surface.set_theme(surface.theme());
        CHECK(family->get() != previous);
        ui_test::draw_surface(surface, 0.2f);
        const int sources = ImGui::GetIO().Fonts->Sources.Size;
        if (change == 1) source_counts[1] = sources;
        CHECK(sources == source_counts[change % 2]);
        ImGui::PushFont(family->get(), 32.0f);
        const float expected = ImGui::CalcTextSize(text.text().c_str()).x;
        ImGui::PopFont();
        CHECK(text.layout().measured_size().x == Catch::Approx(expected));
    }
}

TEST_CASE("system font fallback merges CJK glyphs and keeps context ownership", "[fonts][system][cjk]") {
    FontRegistry registry;
    const auto families = registry.families({.characters = U"Az"});
    const auto main = std::ranges::find_if(families, [](const auto& family) { return !family.coverage.supports(U'中'); });
    const auto cjk = registry.families({.supports_any = {FontScript::Han}, .characters = U"中あ한"});
    if (main == families.end() || cjk.empty()) SKIP("requires a Latin face and an installed CJK face");

    const FontRequest request{
        .main = FontSelection::family(main->id),
        .fallback = FontSelection::family(cjk.front().id),
        .language = "ja",
    };
    const auto sources = registry.resolve(request);
    REQUIRE(sources.size() == 2);

    Font* font = registry.add("interface", request);
    REQUIRE(font != nullptr);
    CHECK(registry.add("interface", request) == font);

    ui_test::ImGuiContext first({320.0f, 180.0f});
    ImGui::GetIO().BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
    ImFont* loaded = font->get();
    REQUIRE(loaded != nullptr);
    CHECK(font->get() == loaded);

    registry.refresh();
    CHECK(registry.find("interface") == font);
    CHECK(font->get() == loaded);

    CHECK(loaded->IsGlyphInFont(U'A'));
    CHECK(loaded->IsGlyphInFont(U'中'));
    CHECK(loaded->IsGlyphInFont(U'あ'));
    CHECK(loaded->IsGlyphInFont(U'한'));
    ImFont* bold = font->get(FontWeight::Bold);
    REQUIRE(bold != nullptr);
    CHECK(bold->IsGlyphInFont(U'中'));
    CHECK(font->get(FontWeight::Bold) == bold);
    auto* baked = loaded->GetFontBaked(16.0f);
    REQUIRE(baked != nullptr);
    CHECK(baked->FindGlyphNoFallback(U'中') != nullptr);
    CHECK(baked->FindGlyphNoFallback(U'あ') != nullptr);
    CHECK(baked->FindGlyphNoFallback(U'한') != nullptr);

    {
        ui_test::ImGuiContext second({320.0f, 180.0f});
        ImFont* other = font->get();
        REQUIRE(other != nullptr);
        CHECK(other != loaded);
        CHECK(other->IsGlyphInFont(U'中'));
        registry.release_context(ImGui::GetCurrentContext());
    }
    CHECK(font->get() == loaded);
    registry.release_context(ImGui::GetCurrentContext());
}
#endif
