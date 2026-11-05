#pragma once

#include "text-value.hpp"
#include "text.hpp"
#include "widget.hpp"

#include <functional>
#include <imgui.h>
#include <utility>

namespace ui {
    class ButtonWidget : public Widget {
    public:
        /// an empty label leaves the content area available for composed child widgets
        explicit ButtonWidget(std::string text = {}, LayoutSize size = {fit(), fit()});
        const std::string& text() const {
            return m_text.str();
        }

        ButtonWidget& set_text_alignment(ImVec2 alignment) {
            m_text_alignment = alignment;
            return *this;
        }

        ButtonWidget& set_text(std::string text);
        ButtonWidget& set_text_overflow(TextOverflow overflow);

        /// runs after the click feedback is scheduled and before that animation advances.
        ButtonWidget& on_click(std::function<void()> callback) {
            m_on_click = std::move(callback);
            return *this;
        }

    protected:
        void apply_theme_defaults(const Theme& theme) override;
        void click_event(UiEvent&) override;
        void mouse_press_event(UiEvent& event) override;
        void key_press_event(UiEvent& event) override;
        void on_measure() override;
        ImVec2 measure_content(ImVec2 available) const override;

    private:
        void paint_content(const PaintContext& context) override;

        TextValue m_text;
        std::function<void()> m_on_click;
        ImVec2 m_text_alignment{0.5F, 0.5F};
        TextOverflow m_text_overflow = TextOverflow::Clip;
    };

} // namespace ui
