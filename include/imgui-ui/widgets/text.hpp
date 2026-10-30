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
        TextWidget& set_overflow(TextOverflow overflow);
        bool empty() const;
        TextWidget& set_text(std::string text);

    protected:
        void apply_theme_defaults(const Theme& theme) override;

    private:
        bool paint() override;
        void on_measure() override;

        TextValue m_text;
        TextOverflow m_overflow = TextOverflow::Clip;
    };

} // namespace ui
