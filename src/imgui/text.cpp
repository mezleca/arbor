#include <imgui-ui/imgui/painter.hpp>
#include <imgui-ui/widgets/text-value.hpp>

#include <algorithm>
#include <cfloat>

using namespace ui;

void Painter::text(ImVec2 position, const Color& source, std::string_view text) const {
    m_draw_list.AddText(position, color(source), text.data(), text.data() + text.size());
}

void Painter::text(ImVec2 position, const Color& source, const TextValue& text, const ImVec4* clip_rect) const {
    const ImColor draw_color = color(source);
    ImFont* font = text.font() != nullptr ? text.font() : ImGui::GetFont();
    const float font_size = ImGui::GetFontSize();
    const float wrap_width = std::max(0.0F, text.wrap_width());
    if (text.line_height_multiplier() == 1.0F) {
        m_draw_list.AddText(font, font_size, position, draw_color, text.c_str(), nullptr, wrap_width, clip_rect);
        return;
    }

    const std::string& string = text.str();
    const char* const value = string.c_str();
    const char* const value_end = value + string.size();
    const float line_height = font_size * text.line_height_multiplier();
    float y = position.y;

    for (const char* paragraph = value;;) {
        const char* const paragraph_end = std::find(paragraph, value_end, '\n');
        const char* line = paragraph;

        do {
            const char* line_end = paragraph_end;
            if (wrap_width > 0.0F && line < paragraph_end) {
                line_end = font->CalcWordWrapPosition(font_size, line, paragraph_end, wrap_width);
                if (line_end == line) line_end = paragraph_end;
            }

            m_draw_list.AddText(font, font_size, {position.x, y}, draw_color, line, line_end, 0.0F, clip_rect);
            y += line_height;
            if (line_end == paragraph_end) break;

            line = line_end;
            while (line < paragraph_end && (*line == ' ' || *line == '\t')) {
                ++line;
            }
        } while (line < paragraph_end);

        if (paragraph_end == value_end) break;

        paragraph = paragraph_end + 1;
    }
}

ImVec4 Painter::visible_clip(ImVec4 clip) const {
    const ImVec2 max = m_draw_list.GetClipRectMax();
    clip.z = std::min(clip.z, max.x);
    clip.w = std::min(clip.w, max.y);
    return clip;
}

void Painter::text_ellipsis(ImVec2 position, const Color& source, const TextValue& text, ImVec4 clip_rect) const {
    if (position.x >= clip_rect.z) {
        return;
    }

    const ImColor draw_color = color(source);
    ImFont* font = text.font() != nullptr ? text.font() : ImGui::GetFont();
    const float font_size = ImGui::GetFontSize();
    const float line_height = font_size * text.line_height_multiplier();
    const float available_width = clip_rect.z - position.x;
    const float wrap_width = text.wrap_width() > 0.0F ? std::min(text.wrap_width(), available_width) : 0.0F;
    constexpr char ellipsis[] = "...";
    // imgui floors child window extents, so a fitting line can end up one pixel wider than its content rect.
    constexpr float FIT_TOLERANCE = 1.0F;
    const float ellipsis_width = font->CalcTextSizeA(font_size, FLT_MAX, 0.0F, ellipsis).x;
    const float truncated_width = std::max(0.0F, available_width - ellipsis_width);

    const auto draw = [&](float x, float y, const char* begin, const char* end) {
        m_draw_list.AddText(font, font_size, {x, y}, draw_color, begin, end, 0.0F, &clip_rect);
    };

    const std::string& string = text.str();
    const char* const value_end = string.c_str() + string.size();
    const char* line = string.c_str();

    for (float y = position.y; y < clip_rect.w; y += line_height) {
        const char* const paragraph_end = std::find(line, value_end, '\n');
        const char* line_end = paragraph_end;
        if (wrap_width > 0.0F && line < paragraph_end) {
            line_end = font->CalcWordWrapPosition(font_size, line, paragraph_end, wrap_width);
        }

        const bool hidden_lines = y + line_height + font_size > clip_rect.w && line_end < value_end;
        if (!hidden_lines && font->CalcTextSizeA(font_size, FLT_MAX, 0.0F, line, line_end).x <= available_width + FIT_TOLERANCE) {
            draw(position.x, y, line, line_end);
        } else {
            const char* visible_end = line;
            const float visible_width = font->CalcTextSizeA(font_size, truncated_width, 0.0F, line, line_end, &visible_end).x;
            draw(position.x, y, line, visible_end);
            draw(position.x + visible_width, y, ellipsis, nullptr);
        }

        if (line_end == value_end || hidden_lines) {
            break;
        }

        line = line_end == paragraph_end ? line_end + 1
                                         : std::find_if(line_end, paragraph_end, [](char c) { return c != ' ' && c != '\t'; });
    }
}
