#include <arbor/resources/texture-registry.hpp>
#include <arbor/resources/svg.hpp>

#include <utility>
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace ui;

class ui::RegistryTexture final : public Texture {
public:
    RegistryTexture(std::unique_ptr<Texture> texture, TextureRetention retention, std::uint64_t& access_counter)
        : m_texture(std::move(texture)), m_access_counter(access_counter), m_retention(retention) {
        touch();
    }

    ImVec2 size() const override {
        return m_texture->size();
    }

    ImTextureID get(ImVec2 size) override {
        const ImTextureID id = m_texture->get(size);
        ImGuiContext* context = ImGui::GetCurrentContext();
        touch();
        if (context != nullptr && id != ImTextureID{}) {
            m_context_frames[context] = ImGui::GetFrameCount();
        }
        return id;
    }

    void release_context(ImGuiContext* context) override {
        m_texture->release_context(context);
        m_context_frames.erase(context);
    }

    TextureMemoryUsage memory_usage(ImGuiContext* context) const override {
        return m_texture->memory_usage(context);
    }

    void touch() {
        m_last_used = ++m_access_counter;
    }

    // the registry needs frame-use metadata to evict only idle context data.
    friend class ui::TextureRegistry;

private:
    std::unique_ptr<Texture> m_texture;
    std::unordered_map<ImGuiContext*, int> m_context_frames;
    std::uint64_t& m_access_counter;
    std::uint64_t m_last_used = 0;
    TextureRetention m_retention;
};

template <typename Source>
TextureHandle TextureRegistry::load_asset(std::string id, Source&& source, TextureRetention retention) {
    if (auto existing = find_handle(id)) {
        return existing;
    }
    if (!m_loader) {
        std::cerr << "arbor: cannot load texture '" << id << "' without a TextureLoader\n";
        return {};
    }
    try {
        auto texture = m_loader->load(std::forward<Source>(source), id);
        return store(std::move(id), std::move(texture), retention);
    } catch (const std::runtime_error& error) {
        std::cerr << "arbor: cannot load texture '" << id << "': " << error.what() << '\n';
        return {};
    }
}

Texture* TextureRegistry::pin(const TextureHandle& texture) {
    if (texture) {
        static_cast<RegistryTexture&>(*texture).m_retention = TextureRetention::Resident;
    }
    return texture.get();
}

TextureRegistry::TextureRegistry(std::unique_ptr<TextureLoader> loader, TextureCacheConfig config)
    : m_loader(std::move(loader)), m_config(config) {
    if (m_loader == nullptr) {
        return;
    }

    add("default", DEFAULT_WARN_SVG);
    add("context-menu-chevron", CONTEXT_MENU_CHEVRON_SVG);
}

Texture* TextureRegistry::add(std::string id, const std::filesystem::path& location) {
    return pin(acquire(std::move(id), location, TextureRetention::Resident));
}

Texture* TextureRegistry::add(std::string id, std::string_view content) {
    return pin(acquire(std::move(id), content, TextureRetention::Resident));
}

Texture* TextureRegistry::find(std::string_view id) {
    return const_cast<Texture*>(static_cast<const TextureRegistry&>(*this).find(id));
}

const Texture* TextureRegistry::find(std::string_view id) const {
    return pin(find_handle(id));
}

TextureHandle TextureRegistry::acquire(std::string id, const std::filesystem::path& location, TextureRetention retention) {
    return load_asset(std::move(id), location, retention);
}

TextureHandle TextureRegistry::acquire(std::string id, std::string_view content, TextureRetention retention) {
    return load_asset(std::move(id), content, retention);
}

TextureHandle TextureRegistry::find_handle(std::string_view id) const {
    const auto found = m_assets.find(id);
    if (found == m_assets.end()) {
        return {};
    }
    found->second->touch();
    return found->second;
}

TextureHandle TextureRegistry::store(std::string id, std::unique_ptr<Texture> texture, TextureRetention retention) {
    if (!texture) {
        return {};
    }
    auto managed = std::make_shared<RegistryTexture>(std::move(texture), retention, m_access_counter);
    const auto [entry, inserted] = m_assets.try_emplace(std::move(id), std::move(managed));
    return entry->second;
}

void TextureRegistry::set_cache_config(TextureCacheConfig config) {
    m_config = config;
}

const TextureCacheConfig& TextureRegistry::cache_config() const {
    return m_config;
}

TextureCacheStats TextureRegistry::stats(ImGuiContext* context) const {
    TextureCacheStats result;
    result.entry_count = m_assets.size();
    for (const auto& [id, texture] : m_assets) {
        result.referenced_count += texture.use_count() > 1;
        const auto usage = texture->memory_usage(context);
        result.cpu_bytes += usage.cpu_bytes;
        result.gpu_bytes += usage.gpu_bytes;
    }
    return result;
}

std::vector<TextureEntryInfo> TextureRegistry::entries(ImGuiContext* context) const {
    std::vector<TextureEntryInfo> result;
    result.reserve(m_assets.size());
    for (const auto& [id, texture] : m_assets) {
        result.push_back({
            id,
            texture->size(),
            texture->memory_usage(context),
            texture->m_retention,
            static_cast<std::size_t>(texture.use_count() - 1),
        });
    }
    return result;
}

void TextureRegistry::collect(ImGuiContext* context) {
    if (context == nullptr) {
        context = ImGui::GetCurrentContext();
    } else if (context != ImGui::GetCurrentContext()) {
        throw std::invalid_argument("texture collection requires the current imgui/graphics context");
    }

    auto usage = stats(context);
    const int current_frame = context != nullptr ? ImGui::GetFrameCount() : -1;
    const bool pressure = usage.cpu_bytes > m_config.cpu_budget_bytes || usage.gpu_bytes > m_config.gpu_budget_bytes;
    std::vector<decltype(m_assets)::iterator> candidates;
    for (auto entry = m_assets.begin(); entry != m_assets.end(); ++entry) {
        const auto& texture = *entry->second;
        const auto frame = texture.m_context_frames.find(context);
        if (entry->second.use_count() > 1 && (frame == texture.m_context_frames.end() || frame->second == current_frame)) {
            continue;
        }
        if (texture.m_retention != TextureRetention::Resident &&
            (pressure || (texture.m_retention == TextureRetention::Transient && entry->second.use_count() == 1))) {
            candidates.push_back(entry);
        }
    }
    std::ranges::sort(candidates, [](const auto& left, const auto& right) {
        return left->second->m_last_used < right->second->m_last_used;
    });

    for (const auto& entry : candidates) {
        auto& texture = *entry->second;
        const bool unreferenced = entry->second.use_count() == 1;
        const bool discard =
            unreferenced && (texture.m_retention == TextureRetention::Transient || usage.cpu_bytes > m_config.cpu_budget_bytes);
        const auto frame = texture.m_context_frames.find(context);
        if (context != nullptr && frame != texture.m_context_frames.end() && frame->second != current_frame &&
            (discard || usage.gpu_bytes > m_config.gpu_budget_bytes)) {
            const auto before = texture.memory_usage(context);
            texture.release_context(context);
            usage.gpu_bytes -= before.gpu_bytes;
        }

        // other contexts must release their gpu objects before the owning cpu resource can be destroyed.
        if (discard && texture.m_context_frames.empty()) {
            usage.cpu_bytes -= texture.memory_usage(nullptr).cpu_bytes;
            m_assets.erase(entry);
        }
    }
}

void TextureRegistry::release_context(ImGuiContext* context) {
    for (const auto& [id, texture] : m_assets) {
        texture->release_context(context);
    }
}
