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
        ImageWidget& set_alignment(ImVec2 alignment);
        ImageWidget& set_uv(ImVec2 minimum, ImVec2 maximum);

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
        ImVec2 m_alignment{0.5f, 0.5f};
        ImVec2 m_uv_min{0.0f, 0.0f};
        ImVec2 m_uv_max{1.0f, 1.0f};
    };

} // namespace ui
