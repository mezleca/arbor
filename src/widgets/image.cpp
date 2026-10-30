#include <imgui-ui/widgets/image.hpp>
#include <imgui-ui/resources/texture-registry.hpp>

#include <algorithm>

using namespace ui;

ImageWidget::ImageWidget(Texture* texture) : Widget({}, "Image", InputMode::None), m_texture(texture) {
    set_size({fit(), fit()});
}

ImageWidget::ImageWidget(TextureHandle texture) : ImageWidget(texture.get()) {
    m_texture_handle = std::move(texture);
}

ImageWidget& ImageWidget::set_texture(Texture* texture) {
    if (m_texture == texture) {
        return *this;
    }

    m_texture = texture;
    m_texture_handle.reset();
    invalidate_measure();
    return *this;
}

ImageWidget& ImageWidget::set_texture(TextureHandle texture) {
    // transfer ownership
    set_texture(texture.get());
    m_texture_handle = std::move(texture);
    return *this;
}

ImVec2 ImageWidget::measure_content(ImVec2 available) const {
    const ImVec2 children_size = measure_children(available);
    if (m_texture != nullptr) {
        const ImVec2 texture_size = m_texture->size();
        return {
            std::max(texture_size.x, children_size.x),
            std::max({texture_size.y, children_size.y, font_size() * computed_style().line_height()})
        };
    }
    return children_size;
}

void ImageWidget::paint_content(const PaintContext& context) {
    const Rect content = context.content_rect;
    if (m_texture == nullptr || !content.valid()) return;

    const ImVec2 content_size = content.size();
    Rect image = content;

    if (m_fit != ImageFit::Fill) {
        const ImVec2 texture_size = m_texture->size();
        if (texture_size.x > 0.0F && texture_size.y > 0.0F) {
            const float scale = m_fit == ImageFit::Contain
                                    ? std::min(content_size.x / texture_size.x, content_size.y / texture_size.y)
                                    : std::max(content_size.x / texture_size.x, content_size.y / texture_size.y);
            const ImVec2 size = {texture_size.x * scale, texture_size.y * scale};
            image = Rect::from_position_size(
                {content.min.x + ((content_size.x - size.x) * 0.5F), content.min.y + ((content_size.y - size.y) * 0.5F)}, size
            );
        }
    }

    const ImTextureID texture_id = m_texture->get(image.size());
    ImDrawList& draw_list = context.painter.draw_list();
    const bool clip_image = m_fit == ImageFit::Cover;
    if (clip_image) {
        draw_list.PushClipRect(content.min, content.max, true);
    }

    context.painter.image(texture_id, image, {0, 0}, {1, 1}, context.style.color().value, context.style.border_radius());

    if (clip_image) {
        draw_list.PopClipRect();
    }
}
