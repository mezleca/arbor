#pragma once

#include "resources/font-registry.hpp"
#include "resources/texture-registry.hpp"
#include "style/theme.hpp"

#include <filesystem>
#include <memory>

namespace ui {
    struct RuntimeConfig {
        Theme theme{};
        std::filesystem::path performance_directory;
        std::unique_ptr<TextureLoader> texture_loader;
        TextureCacheConfig texture_cache;
    };

    class Runtime {
    public:
        explicit Runtime(RuntimeConfig config = {});

        const Theme& theme() const {
            return m_theme;
        }

        FontRegistry& fonts() {
            return m_fonts;
        }

        const FontRegistry& fonts() const {
            return m_fonts;
        }

        TextureRegistry& textures() {
            return m_textures;
        }

        const TextureRegistry& textures() const {
            return m_textures;
        }

        const std::filesystem::path& performance_directory() const {
            return m_performance_directory;
        }

    private:
        Theme m_theme;
        FontRegistry m_fonts;
        TextureRegistry m_textures;
        std::filesystem::path m_performance_directory;
    };
} // namespace ui
