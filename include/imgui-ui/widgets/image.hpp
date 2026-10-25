#pragma once

#include "widget.hpp"

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

        ImageWidget& set_texture(Texture* texture) {
            if (m_texture == texture) {
                return *this;
            }

            m_texture = texture;
            invalidate_measure();
            return *this;
        }

        ImageWidget& set_fit(ImageFit fit) {
            m_fit = fit;
            return *this;
        }

    private:
        ImVec2 measure_content(ImVec2 available) const override;
        void paint_content(const PaintContext& context) override;
        Texture* m_texture = nullptr;
        ImageFit m_fit = ImageFit::Fill;
    };

} // namespace ui
