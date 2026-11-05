#pragma once

#include "widget.hpp"
#include "box.hpp"
#include "value.hpp"

#include <cstdint>
#include <string>

namespace ui {
    class TextWidget;

    enum class CheckboxType : uint8_t {
        Standard,
        Radio,
    };

    class CheckboxWidget : public Widget {
    public:
        explicit CheckboxWidget(
            WidgetValue<bool> value = {}, std::string label = {}, LabelPlacement placement = LabelPlacement::Inline,
            std::string id = {}
        );

        CheckboxWidget& set_label(std::string label, LabelPlacement placement = LabelPlacement::Inline);
        bool set_checked(bool checked);
        bool checked() const {
            return m_value.value();
        }

        CheckboxWidget& set_label_clickable(bool clickable);
        CheckboxWidget& set_type(CheckboxType type);
        CheckboxWidget& set_box_size(float size);
        CheckboxWidget& set_mark_color(const Color& color);

        StyledNode& frame();
        const StyledNode& frame() const;
        StyledNode& fill();
        const StyledNode& fill() const;

        TextWidget& label();
        const TextWidget& label() const;

    protected:
        void apply_theme_defaults(const Theme& theme) override;

    private:
        void click_event(UiEvent&) override;
        void input_state_changed() override;
        void on_update(float) override;
        Rect hit_rect(Rect visual_rect) const override;
        void update_shape();

        WidgetValue<bool> m_value;
        Container* m_box_node = nullptr;
        Container* m_frame_node = nullptr;
        BoxWidget* m_fill_node = nullptr;
        TextWidget* m_label_node = nullptr;
        CheckboxType m_type = CheckboxType::Standard;
        bool m_label_clickable = false;
        LabelPlacement m_label_placement = LabelPlacement::Inline;
        ImVec2 m_label_spacing = {8.0F, 10.0F};
        float m_box_size = 20.0F;
        float m_frame_border_thickness = 1.0F;
        float m_rounding = 4.0F;
    };
} // namespace ui
