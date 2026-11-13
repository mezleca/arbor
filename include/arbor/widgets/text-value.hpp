#pragma once

#include <imgui.h>
#include <string>
#include <utility>

namespace ui {
    class TextValue {
    public:
        explicit TextValue(std::string text = {}) : m_text(std::move(text)) {}

        const char* c_str() const {
            return m_text.c_str();
        }

        const std::string& str() const {
            return m_text;
        }

        void set_font(ImFont* font, float size = 0.0F);
        void set_wrap(float wrap_width);
        void set_line_height(float multiplier);

        /// measured in native window units, matching resolved layout sizes.
        ImVec2 text_size() const;

        ImFont* font() const {
            return m_font;
        }

        float wrap_width() const {
            return m_wrap_width;
        }

        float line_height_multiplier() const {
            return m_line_height_multiplier;
        }

        bool set(std::string text);

    private:
        void recompute() const;

        std::string m_text;
        ImFont* m_font = nullptr;
        float m_font_size = 0.0F;
        float m_font_density = 1.0f;
        mutable ImVec2 m_text_size;
        float m_line_height_multiplier = 1.0F;
        float m_wrap_width = -1.0F;
        mutable bool m_dirty = true;
    };

} // namespace ui
