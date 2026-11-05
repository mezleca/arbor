#pragma once

#include "../string-hash.hpp"
#include "texture.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <imgui.h>
#include <memory>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <unordered_map>
#include <vector>

namespace ui {
    enum class TextureRetention : std::uint8_t {
        // retain cpu and gpu data until context/registry teardown.
        Resident,
        // evict idle gpu data under pressure and cpu data only after all handles are released.
        Cached,
        // release after the last handle disappears and every gpu context can be collected safely.
        Transient,
    };

    struct TextureCacheConfig {
        // global cpu budget and gpu budget per context. referenced/resident data can exceed these limits.
        std::size_t cpu_budget_bytes = std::numeric_limits<std::size_t>::max();
        std::size_t gpu_budget_bytes = std::numeric_limits<std::size_t>::max();
    };

    struct TextureCacheStats {
        std::size_t entry_count = 0;
        std::size_t referenced_count = 0;
        std::size_t cpu_bytes = 0;
        std::size_t gpu_bytes = 0;
    };

    struct TextureEntryInfo {
        std::string id;
        ImVec2 size;
        TextureMemoryUsage memory;
        TextureRetention retention = TextureRetention::Cached;
        std::size_t handle_count = 0;
    };

    class RegistryTexture;

    class TextureLoader {
    public:
        virtual ~TextureLoader() = default;

        // decodes the source during registration without requiring an imgui or graphics context.
        virtual std::unique_ptr<Texture> load(const std::filesystem::path& location, std::string id) = 0;
        virtual std::unique_ptr<Texture> load(std::string_view content, std::string id) = 0;
    };

    // registry access and handle use belong to the ui thread. the runtime must outlive its handles.
    class TextureRegistry final {
    public:
        explicit TextureRegistry(std::unique_ptr<TextureLoader> loader = nullptr, TextureCacheConfig config = {});
        TextureRegistry(const TextureRegistry&) = delete;
        TextureRegistry& operator=(const TextureRegistry&) = delete;

        // legacy raw access pins the resource until registry destruction.
        Texture* add(std::string id, const std::filesystem::path& location);
        Texture* add(std::string id, std::string_view content);
        Texture* find(std::string_view id);
        const Texture* find(std::string_view id) const;

        // an existing id reuses its source and retention policy. handles protect cpu data, not gpu residency.
        // io and decoding failures are logged and return an empty handle without registering an entry.
        TextureHandle
        acquire(std::string id, const std::filesystem::path& location, TextureRetention retention = TextureRetention::Cached);
        TextureHandle acquire(std::string id, std::string_view content, TextureRetention retention = TextureRetention::Cached);
        TextureHandle find_handle(std::string_view id) const;

        void set_cache_config(TextureCacheConfig config);
        const TextureCacheConfig& cache_config() const;
        // nullptr sums gpu bytes across all contexts. cpu bytes are always global.
        TextureCacheStats stats(ImGuiContext* context = nullptr) const;
        // copies metadata without retaining textures, uploading gpu data or updating eviction order
        std::vector<TextureEntryInfo> entries(ImGuiContext* context = nullptr) const;

        // called after rendering with this graphics/imgui context current. nullptr uses the current imgui context.
        // budgets are soft when referenced, resident, or current-frame resources prevent further eviction.
        // transient resources are released at the next safe collection after their last handle disappears.
        void collect(ImGuiContext* context = nullptr);
        void release_context(ImGuiContext* context);

    private:
        template <typename Source>
        TextureHandle load_asset(std::string id, Source&& source, TextureRetention retention);
        static Texture* pin(const TextureHandle& texture);
        TextureHandle store(std::string id, std::unique_ptr<Texture> texture, TextureRetention retention);

        std::unique_ptr<TextureLoader> m_loader;
        std::unordered_map<std::string, std::shared_ptr<RegistryTexture>, StringHash, std::equal_to<>> m_assets;
        TextureCacheConfig m_config;
        mutable std::uint64_t m_access_counter = 0;
    };
} // namespace ui
