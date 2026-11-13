#pragma once

#include "widget.hpp"
#include "value.hpp"
#include "../resources/texture.hpp"

#include <string>
#include <string_view>

namespace ui {
    class Texture;
    class ImageWidget;
    class TextWidget;
    class TextInputFieldNode;

    enum class TextSubmitMode : uint8_t {
        Enter,
        CtrlEnter,
    };

    struct TextSelection {
        int start = 0;
        int end = 0;
        int cursor = 0;
    };

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
        TextInputWidget& set_multiline(bool multiline = true);
        /// grows with explicit lines between minimum and maximum rows, then scrolls
        TextInputWidget& set_rows(int minimum, int maximum);
        TextInputWidget& set_submit_mode(TextSubmitMode mode);
        /// resize is reserved for the string adapter, other callback flags use this callback
        TextInputWidget& set_callback(ImGuiInputTextCallback callback, void* user_data = nullptr);
        /// reports enter separately from text edits, including submission of an unchanged value
        TextInputWidget& on_submit(std::function<void()> callback);
        TextInputWidget& on_commit(std::function<void()> callback);
        /// requests native focus on the next draw, including before the first hit-test registration
        void focus();
        /// offsets are utf-8 byte boundaries, selection is applied on the next active draw
        void set_selection(int start, int end);
        bool replace_selection(std::string_view text);

        const std::string& value() const {
            return m_value.value();
        }

        TextSelection selection() const {
            return m_selection;
        }

        bool multiline() const {
            return m_multiline;
        }

        TextWidget& label();
        const TextWidget& label() const;
        StyledNode& field();
        const StyledNode& field() const;
        ImageWidget& icon();
        const ImageWidget& icon() const;
        Container& leading();
        Container& trailing();
        const Container& leading() const {
            return *m_leading;
        }

        const Container& trailing() const {
            return *m_trailing;
        }

        Container& frame() {
            return *m_input_node;
        }
        const Container& frame() const {
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
        std::function<void()> m_on_commit;
        TextWidget* m_label_node = nullptr;
        Container* m_input_node = nullptr;
        ImageWidget* m_icon_node = nullptr;
        TextInputFieldNode* m_field_node = nullptr;
        Container* m_leading = nullptr;
        Container* m_trailing = nullptr;
        ImGuiInputTextCallback m_callback = nullptr;
        void* m_callback_data = nullptr;
        TextSelection m_selection;
        LabelPlacement m_label_placement = LabelPlacement::Inline;
        ImVec2 m_label_spacing;
        ImGuiInputTextFlags m_flags = ImGuiInputTextFlags_None;
        TextSubmitMode m_submit_mode = TextSubmitMode::CtrlEnter;
        int m_minimum_rows = 3;
        int m_maximum_rows = 8;
        bool m_multiline = false;
        bool m_selection_requested = false;
        bool m_focus_requested = false;
        bool m_reload_requested = false;
    };
} // namespace ui
