#include <imgui-ui/resources/texture-registry.hpp>
#include <imgui-ui/widgets/image.hpp>
#include <imgui-ui/widgets/text-input.hpp>
#include <imgui-ui/widgets/context-menu.hpp>
#include "imgui-context.hpp"

#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <imgui_internal.h>
#include <memory>
#include <string>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

using namespace ui;

struct TextureProbe {
    int loads = 0;
    int destructions = 0;
    int uploads = 0;
    int releases = 0;
};

struct TextureLoaderProbe {
    std::unordered_map<std::string, std::shared_ptr<TextureProbe>> textures;
};

class CacheProbeTexture final : public Texture {
public:
    CacheProbeTexture(std::shared_ptr<TextureProbe> probe, std::size_t bytes) : m_probe(std::move(probe)), m_bytes(bytes) {}

    ~CacheProbeTexture() override {
        ++m_probe->destructions;
    }

    CacheProbeTexture(const CacheProbeTexture&) = delete;
    CacheProbeTexture& operator=(const CacheProbeTexture&) = delete;
    CacheProbeTexture(CacheProbeTexture&&) = delete;
    CacheProbeTexture& operator=(CacheProbeTexture&&) = delete;

    ImVec2 size() const override {
        return {4.0F, 4.0F};
    }

    ImTextureID get(ImVec2) override {
        if (m_contexts.insert(ImGui::GetCurrentContext()).second) {
            ++m_probe->uploads;
        }
        return 1;
    }

    void release_context(ImGuiContext* context) override {
        if (m_contexts.erase(context) != 0) {
            ++m_probe->releases;
        }
    }

    TextureMemoryUsage memory_usage(ImGuiContext* context) const override {
        const auto copies = context == nullptr ? m_contexts.size() : static_cast<std::size_t>(m_contexts.contains(context));
        return {m_bytes, m_bytes * copies};
    }

private:
    std::shared_ptr<TextureProbe> m_probe;
    std::unordered_set<ImGuiContext*> m_contexts;
    std::size_t m_bytes;
};

class CacheProbeLoader final : public TextureLoader {
public:
    explicit CacheProbeLoader(std::shared_ptr<TextureLoaderProbe> probe) : m_probe(std::move(probe)) {}

    std::unique_ptr<Texture> load(const std::filesystem::path&, std::string id) override {
        return load(std::string_view{}, std::move(id));
    }

    std::unique_ptr<Texture> load(std::string_view, std::string id) override {
        if (id == "null") {
            return {};
        }
        if (id == "error") {
            throw std::runtime_error("decode failed");
        }
        auto& probe = m_probe->textures[id];
        if (!probe) {
            probe = std::make_shared<TextureProbe>();
        }
        ++probe->loads;
        return std::make_unique<CacheProbeTexture>(probe, id == "default" || id == "context-menu-chevron" ? 0 : 64);
    }

private:
    std::shared_ptr<TextureLoaderProbe> m_probe;
};

struct TextureCacheFixture {
    std::shared_ptr<TextureLoaderProbe> probe = std::make_shared<TextureLoaderProbe>();
    TextureRegistry registry{std::make_unique<CacheProbeLoader>(probe)};
};

TEST_CASE("texture handles share loads and keep cached resources after a scope ends", "[texture-cache]") {
    TextureCacheFixture fixture;
    auto& registry = fixture.registry;
    {
        auto first = registry.acquire("cover", std::string_view{"data"});
        auto second = registry.acquire("cover", std::filesystem::path{"different-source"});
        REQUIRE(first == second);
        REQUIRE(fixture.probe->textures.at("cover")->loads == 1);
        const auto entries = registry.entries();
        const auto cover = std::ranges::find(entries, "cover", &TextureEntryInfo::id);
        REQUIRE(cover != entries.end());
        REQUIRE(cover->handle_count == 2);
        REQUIRE(cover->size.x == 4.0f);
        REQUIRE(cover->memory.cpu_bytes == 64);
        REQUIRE(cover->retention == TextureRetention::Cached);
        REQUIRE(fixture.probe->textures.at("cover")->uploads == 0);
        REQUIRE(registry.stats().referenced_count == 1);
    }
    registry.collect();
    REQUIRE(registry.stats().cpu_bytes == 64);
    REQUIRE(fixture.probe->textures.at("cover")->destructions == 0);
    REQUIRE(registry.find_handle("cover") != nullptr);
}

TEST_CASE("cpu texture budget evicts unused covers in access order and reloads on request", "[texture-cache]") {
    TextureCacheFixture fixture;
    auto& registry = fixture.registry;
    registry.set_cache_config({.cpu_budget_bytes = 64});
    registry.acquire("older", std::string_view{});
    registry.acquire("recent", std::string_view{});
    registry.find_handle("older");
    registry.entries();
    registry.collect();
    REQUIRE(registry.find_handle("older") != nullptr);
    REQUIRE(registry.find_handle("recent") == nullptr);
    REQUIRE(registry.stats().cpu_bytes == 64);
    REQUIRE(fixture.probe->textures.at("recent")->destructions == 1);
    registry.acquire("recent", std::string_view{});
    REQUIRE(fixture.probe->textures.at("recent")->loads == 2);
}

TEST_CASE("texture budgets preserve active handles and legacy raw pointers", "[texture-cache]") {
    TextureCacheFixture fixture;
    auto& registry = fixture.registry;
    auto live = registry.acquire("live", std::string_view{});
    Texture* raw = registry.add("legacy", std::string_view{});
    registry.acquire("promoted", std::string_view{});
    const Texture* promoted = std::as_const(registry).find("promoted");
    registry.acquire("resident", std::string_view{}, TextureRetention::Resident);
    registry.set_cache_config({.cpu_budget_bytes = 0, .gpu_budget_bytes = 0});
    registry.collect();
    REQUIRE(registry.stats().cpu_bytes == 256);
    REQUIRE(raw->size().x == 4.0F);
    REQUIRE(promoted->size().x == 4.0F);
    live.reset();
    registry.collect();
    REQUIRE(registry.find_handle("live") == nullptr);
    REQUIRE(registry.stats().cpu_bytes == 192);
}

TEST_CASE("transient textures follow widget ownership and raw to handle upgrades", "[texture-cache][widget]") {
    TextureCacheFixture fixture;
    auto& registry = fixture.registry;
    {
        auto handle = registry.acquire("icon", std::string_view{}, TextureRetention::Transient);
        ImageWidget image(handle.get());
        image.set_texture(handle);
        TextInputWidget input;
        input.set_icon(handle);
        ContextMenuWidget menu({ContextMenuItem::submenu("sub", {ContextMenuItem::action("item")})}, handle);
        handle.reset();
        registry.collect();
        REQUIRE(fixture.probe->textures.at("icon")->destructions == 0);
        REQUIRE(registry.stats().referenced_count == 1);
        image.set_texture(nullptr);
        input.set_icon(nullptr);
        registry.collect();
        REQUIRE(fixture.probe->textures.at("icon")->destructions == 0);
    }
    registry.collect();
    REQUIRE(fixture.probe->textures.at("icon")->destructions == 1);
    REQUIRE(registry.find_handle("icon") == nullptr);
}

TEST_CASE("gpu collection protects the current frame and reuploads idle referenced textures", "[texture-cache]") {
    ui_test::ImGuiContext context({64.0F, 64.0F});
    TextureCacheFixture fixture;
    auto& registry = fixture.registry;
    registry.set_cache_config({.gpu_budget_bytes = 0});
    auto handle = registry.acquire("cover", std::string_view{});
    handle->get({4.0F, 4.0F});
    registry.collect();
    REQUIRE(registry.stats().gpu_bytes == 64);
    ++ImGui::GetCurrentContext()->FrameCount;
    registry.collect();
    REQUIRE(registry.stats().gpu_bytes == 0);
    REQUIRE(registry.stats().cpu_bytes == 64);
    REQUIRE(handle->get({4.0F, 4.0F}) != 0);
    REQUIRE(fixture.probe->textures.at("cover")->uploads == 2);
    registry.release_context(ImGui::GetCurrentContext());
    REQUIRE(fixture.probe->textures.at("cover")->releases == 2);
}

TEST_CASE("texture collection waits for every context before destroying a resource", "[texture-cache]") {
    ui_test::ImGuiContext first({64.0F, 64.0F});
    ImGuiContext* first_context = ImGui::GetCurrentContext();
    TextureCacheFixture fixture;
    auto handle = fixture.registry.acquire("cover", std::string_view{}, TextureRetention::Transient);
    handle->get({4.0F, 4.0F});
    {
        ui_test::ImGuiContext second({64.0F, 64.0F});
        handle->get({4.0F, 4.0F});
        handle.reset();
        ++ImGui::GetCurrentContext()->FrameCount;
        REQUIRE_THROWS_AS(fixture.registry.collect(first_context), std::invalid_argument);
        fixture.registry.collect();
        REQUIRE(fixture.probe->textures.at("cover")->destructions == 0);
        REQUIRE(fixture.probe->textures.at("cover")->releases == 1);
    }
    ++ImGui::GetCurrentContext()->FrameCount;
    fixture.registry.collect();
    REQUIRE(fixture.probe->textures.at("cover")->releases == 2);
    REQUIRE(fixture.probe->textures.at("cover")->destructions == 1);
}

TEST_CASE("gpu budget evicts the oldest eligible upload while preserving pinned and current frame data", "[texture-cache]") {
    ui_test::ImGuiContext context({64.0F, 64.0F});
    TextureCacheFixture fixture;
    auto& registry = fixture.registry;
    registry.set_cache_config({.gpu_budget_bytes = 128});
    Texture* pinned = registry.add("pinned", std::string_view{});
    auto first = registry.acquire("first", std::string_view{});
    auto second = registry.acquire("second", std::string_view{});
    pinned->get({4.0F, 4.0F});
    first->get({4.0F, 4.0F});
    second->get({4.0F, 4.0F});
    registry.collect();
    REQUIRE(registry.stats().gpu_bytes == 192);
    ++ImGui::GetCurrentContext()->FrameCount;
    first->get({4.0F, 4.0F});
    registry.collect();
    REQUIRE(registry.stats().gpu_bytes == 128);
    REQUIRE(fixture.probe->textures.at("first")->releases == 0);
    REQUIRE(fixture.probe->textures.at("second")->releases == 1);
    REQUIRE(fixture.probe->textures.at("pinned")->releases == 0);
    registry.release_context(ImGui::GetCurrentContext());
}

TEST_CASE("texture cache handles failed loads without registering an entry", "[texture-cache]") {
    TextureCacheFixture fixture;
    const auto count = fixture.registry.stats().entry_count;
    REQUIRE(fixture.registry.acquire("null", std::string_view{}) == nullptr);
    REQUIRE(fixture.registry.acquire("error", std::string_view{}) == nullptr);
    REQUIRE(fixture.registry.add("error", std::filesystem::path{"unsupported.jpeg"}) == nullptr);
    REQUIRE(fixture.registry.stats().entry_count == count);
    TextureRegistry empty;
    REQUIRE(empty.acquire("cover", std::string_view{}) == nullptr);
    REQUIRE(empty.find("missing") == nullptr);
}

TEST_CASE("cover cache remains bounded across a large scrolling workload", "[texture-cache]") {
    TextureCacheFixture fixture;
    fixture.registry.set_cache_config({.cpu_budget_bytes = std::size_t{64} * 16});
    for (int page = 0; page < 100; ++page) {
        std::vector<TextureHandle> visible;
        visible.reserve(12);
        for (int item = 0; item < 12; ++item) {
            visible.push_back(fixture.registry.acquire(std::to_string((page * 12) + item), std::string_view{}));
        }
        fixture.registry.collect();
        REQUIRE(fixture.registry.stats().referenced_count == 12);
        REQUIRE(fixture.registry.stats().cpu_bytes <= std::size_t{64} * 16);
    }
    fixture.registry.set_cache_config({.cpu_budget_bytes = 0});
    fixture.registry.collect();
    REQUIRE(fixture.registry.stats().cpu_bytes == 0);
    REQUIRE(fixture.registry.stats().entry_count == 2);
}
