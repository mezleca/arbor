#include <arbor/widgets/text-value.hpp>
#include <arbor/layout/geometry.hpp>

#include <algorithm>
#include <cmath>

using namespace ui;

void TextValue::set_font(ImFont* font, float size) {
    // zoom and framebuffer density can change hinted advances without changing the configured font size.
    const float density = ImGui::GetCurrentContext() != nullptr
                              ? dpi_scale() * ImGui::GetStyle().FontScaleMain * ImGui::GetIO().DisplayFramebufferScale.x
                              : 1.0f;
    if (font == m_font && size == m_font_size && density == m_font_density) return;

    m_font = font;
    m_font_size = size;
    m_font_density = density;
    m_dirty = true;
}

void TextValue::set_wrap(float wrap_width) {
    if (m_wrap_width == wrap_width) return;
    m_wrap_width = wrap_width;
    m_dirty = true;
}

void TextValue::set_line_height(float multiplier) {
    const float resolved = std::max(0.0F, multiplier);
    if (m_line_height_multiplier == resolved) return;
    m_line_height_multiplier = resolved;
    m_dirty = true;
}

ImVec2 TextValue::text_size() const {
    if (m_dirty) recompute();
    return m_text_size;
}

bool TextValue::set(std::string text) {
    if (m_text == text) return false;
    m_text = std::move(text);
    m_dirty = true;
    return true;
}

void TextValue::recompute() const {
    if (ImGui::GetCurrentContext() == nullptr) {
        m_text_size = {};
        return;
    }

    ImFont* font = m_font != nullptr ? m_font : ImGui::GetFont();
    ImGui::PushFont(font, m_font_size > 0.0F ? m_font_size : font->LegacySize);
    const float line_height = ImGui::GetTextLineHeight();
    m_text_size = ImGui::CalcTextSize(c_str(), nullptr, false, m_wrap_width * dpi_scale());

    // imgui measures wrapped and explicit lines using native line height.
    // recover that line count before applying the configured height multiplier.
    if (line_height > 0.0F) {
        m_text_size.y = std::round(m_text_size.y / line_height) * line_height * m_line_height_multiplier;
    }
    ImGui::PopFont();
    m_dirty = false;
}
