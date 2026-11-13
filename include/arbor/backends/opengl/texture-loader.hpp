#pragma once

#include "../../resources/texture-registry.hpp"

namespace ui {
    class OpenGLTextureLoader final : public TextureLoader {
    public:
        std::unique_ptr<Texture> load(const std::filesystem::path& location, std::string id) override;
        std::unique_ptr<Texture> load(std::string_view content, std::string id) override;
    };
} // namespace ui
