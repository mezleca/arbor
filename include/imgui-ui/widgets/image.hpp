#pragma once

#include "widget.hpp"
#include "../resources/texture.hpp"

#include <cstdint>
#include <imgui.h>

namespace ui {
    class Texture;

    enum class ImageFit : uint8_t {
        Fill,
        Contain,
        Cover,
    };

    class ImageWidget : public Widget {
    public:
        explicit ImageWidget(Texture* texture = nullptr);
        explicit ImageWidget(TextureHandle texture);

        ImageWidget& set_texture(Texture* texture);
        ImageWidget& set_texture(TextureHandle texture);

        ImageWidget& set_fit(ImageFit fit) {
            m_fit = fit;
            return *this;
        }

    private:
        ImVec2 measure_content(ImVec2 available) const override;
        void paint_content(const PaintContext& context) override;

        Texture* m_texture = nullptr;
        TextureHandle m_texture_handle;
        ImageFit m_fit = ImageFit::Fill;
    };

} // namespace ui
