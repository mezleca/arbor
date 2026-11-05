#pragma once

#include "widget.hpp"
#include "value.hpp"
#include "../resources/texture.hpp"

#include <string>

namespace ui {
    class Texture;
    class ImageWidget;
    class TextWidget;
    class TextInputFieldNode;

    class TextInputWidget : public Widget {
    public:
        explicit TextInputWidget(
            WidgetValue<std::string> value = {}, std::string label = {}, LabelPlacement placement = LabelPlacement::Inline,
            std::string id = {}
        );

        TextInputWidget& set_label(std::string label, LabelPlacement placement = LabelPlacement::Inline);
        TextInputWidget& set_icon(Texture* icon);
        TextInputWidget& set_icon(TextureHandle icon);
        TextInputWidget& set_flags(ImGuiInputTextFlags flags);
        TextInputWidget& set_hint(std::string hint);
        /// reports enter separately from text edits, including submission of an unchanged value
        TextInputWidget& on_submit(std::function<void()> callback);

        Container& frame() {
            return *m_input_node;
        }
        ImGuiInputTextFlags flags() const {
            return m_flags;
        }

        bool set_value(std::string value);

    protected:
        void apply_theme_defaults(const Theme& theme) override;
        void event(UiEvent& event) override;
        void input_state_changed() override;
        Rect hit_rect(Rect visual_rect) const override;

    private:
        friend class TextInputFieldNode;

        void on_draw_end() override;

        WidgetValue<std::string> m_value;
        std::string m_hint;
        std::function<void()> m_on_submit;
        TextWidget* m_label_node = nullptr;
        Container* m_input_node = nullptr;
        ImageWidget* m_icon_node = nullptr;
        TextInputFieldNode* m_field_node = nullptr;
        LabelPlacement m_label_placement = LabelPlacement::Inline;
        ImVec2 m_label_spacing;
        ImGuiInputTextFlags m_flags = ImGuiInputTextFlags_None;
        bool m_focus_requested = false;
        bool m_reload_requested = false;
    };
} // namespace ui
