#pragma once

#include "../style/styled-node.hpp"
#include "text-value.hpp"

#include <cstdint>
#include <string>

namespace ui {
    enum class TextOverflow : uint8_t {
        Clip,
        Ellipsis,
    };

    class TextWidget : public StyledNode {
    public:
        explicit TextWidget(std::string text);

        TextWidget& set_wrap(float width);
        TextWidget& set_wrap_available(bool wrap = true);
        TextWidget& set_alignment(ImVec2 alignment);
        TextWidget& set_overflow(TextOverflow overflow);
        const std::string& text() const {
            return m_text.str();
        }

        bool empty() const;
        TextWidget& set_text(std::string text);
        ImVec2 measure_size(ImVec2 allocation) const override;

    protected:
        void apply_theme_defaults(const Theme& theme) override;

    private:
        bool paint() override;
        void on_measure() override;

        mutable TextValue m_text;
        TextOverflow m_overflow = TextOverflow::Clip;
        ImVec2 m_alignment;
        bool m_wrap_available = false;
    };

} // namespace ui
