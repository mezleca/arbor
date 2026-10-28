#pragma once

#include "widget.hpp"
#include "value.hpp"

#include <string>

namespace ui {
    class ColorPickerPopup;
    class ColorPickerPreviewNode;
    class ColorPickerControlsNode;
    class TextWidget;

    class ColorPickerWidget : public Widget {
    public:
        explicit ColorPickerWidget(
            WidgetValue<Color> value = {}, std::string label = {}, LabelPlacement placement = LabelPlacement::Above,
            std::string id = {}
        );

        ColorPickerWidget& set_label(std::string label, LabelPlacement placement = LabelPlacement::Above);
        bool set_color(const Color& color);
        void open();
        void close();

        bool is_open() const {
            return m_open;
        }

        TextWidget& label();
        Widget& preview();
        Container& popup();

    protected:
        void apply_theme_defaults(const Theme& theme) override;

    private:
        friend class ColorPickerPopup;
        friend class ColorPickerControlsNode;
        friend class ColorPickerPreviewNode;

        bool native_popup_open() const;

        WidgetValue<Color> m_value;
        TextWidget* m_label_node = nullptr;
        ColorPickerPreviewNode* m_preview = nullptr;
        ColorPickerPopup* m_popup = nullptr;
        bool m_open = false;
        LabelPlacement m_label_placement = LabelPlacement::Above;
        ImVec2 m_label_spacing = {8.0F, 10.0F};
    };
} // namespace ui
