#include <imgui-ui/widgets/text-input.hpp>

#include <imgui-ui/style/theme.hpp>
#include <imgui-ui/surface.hpp>
#include <imgui-ui/widgets/image.hpp>
#include <imgui-ui/widgets/text.hpp>

#include <imgui_internal.h>
#include <algorithm>

using namespace ui;

class ui::TextInputFieldNode final : public StyledNode {
public:
    explicit TextInputFieldNode(TextInputWidget& owner) : StyledNode("text", "TextInput"), m_owner(owner) {
        set_size({grow(), fit()});
    }

    bool accepts_input() const override {
        return false;
    }

    bool changed() const {
        return m_changed;
    }

    bool submitted() const {
        return m_submitted;
    }

    bool committed() const {
        return m_committed;
    }

private:
    void on_measure() override {
        ImGui::PushFont(font(), font_size());
        const float line_height = ImGui::GetTextLineHeight();
        ImGui::PopFont();
        int rows = 1;
        if (m_owner.m_multiline) {
            const int lines = 1 + static_cast<int>(std::ranges::count(m_owner.value(), '\n'));
            rows = std::clamp(lines, m_owner.m_minimum_rows, m_owner.m_maximum_rows);
        }
        set_measured_size({0.0f, line_height * static_cast<float>(rows)});
    }

    static int input_callback(ImGuiInputTextCallbackData* data) {
        auto& field = *static_cast<TextInputFieldNode*>(data->UserData);
        auto& owner = field.m_owner;
        if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
            field.m_buffer->resize(static_cast<std::size_t>(data->BufTextLen));
            data->Buf = field.m_buffer->data();
            return 0;
        }
        int result = 0;
        if (owner.m_callback && (owner.m_flags & data->EventFlag)) {
            data->UserData = owner.m_callback_data;
            result = owner.m_callback(data);
        }
        if (owner.m_selection_requested && data->EventFlag != ImGuiInputTextFlags_CallbackCharFilter) {
            data->SelectionStart = std::clamp(owner.m_selection.start, 0, data->BufTextLen);
            data->SelectionEnd = std::clamp(owner.m_selection.end, 0, data->BufTextLen);
            data->CursorPos = data->SelectionEnd;
            owner.m_selection_requested = false;
        }
        return result;
    }

    bool paint() override {
        // transfer the widget's routed focus request to imgui immediately before drawing the native field.
        if (m_owner.m_focus_requested) {
            m_owner.surface().input_router().set_focus(&m_owner);
            ImGui::SetKeyboardFocusHere();
            m_owner.m_focus_requested = false;
        }

        ImGui::PushID(this);
        if (m_owner.m_reload_requested) {
            if (auto* state = ImGui::GetInputTextState(ImGui::GetID("##value"))) state->ReloadUserBufAndMoveToEnd();
            m_owner.m_reload_requested = false;
        }
        const float padding_y = std::max(0.0f, (layout().size().y - layout().measured_size().y) * 0.5f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {ImGui::GetStyle().FramePadding.x, padding_y});
        m_changed = m_owner.m_value.edit([this](std::string& value) {
            auto flags = m_owner.flags();
            if (m_owner.m_on_submit) flags |= ImGuiInputTextFlags_EnterReturnsTrue;
            if (m_owner.m_multiline) {
                flags |= ImGuiInputTextFlags_Multiline;
                flags &= ~ImGuiInputTextFlags_CallbackHistory;
                if (m_owner.m_submit_mode == TextSubmitMode::Enter) flags |= ImGuiInputTextFlags_CtrlEnterForNewLine;
            }
            flags |= ImGuiInputTextFlags_CallbackResize;
            if (m_owner.m_selection_requested) flags |= ImGuiInputTextFlags_CallbackAlways;
            m_buffer = &value;
            const bool accepted = ImGui::InputTextEx(
                "##value", m_owner.m_hint.c_str(), value.data(), static_cast<int>(value.capacity() + 1),
                {layout().size().x, m_owner.m_multiline ? layout().size().y : 0.0f}, flags, input_callback, this
            );
            m_submitted = m_owner.m_on_submit && accepted;
            return ImGui::IsItemEdited();
        });
        m_committed = ImGui::IsItemDeactivatedAfterEdit();
        if (auto* state = ImGui::GetInputTextState(ImGui::GetID("##value")); state && ImGui::IsItemActive()) {
            m_owner.m_selection = {state->GetSelectionStart(), state->GetSelectionEnd(), state->GetCursorPos()};
        }
        if (ImGui::IsItemActive() && !m_owner.input_state().focused) {
            m_owner.surface().input_router().set_focus(&m_owner);
            m_owner.m_focus_requested = false;
        }
        if (m_changed) invalidate_measure();
        ImGui::PopStyleVar();
        ImGui::PopID();
        return true;
    }

    TextInputWidget& m_owner;
    bool m_changed = false;
    bool m_submitted = false;
    bool m_committed = false;
    std::string* m_buffer = nullptr;
};

TextInputWidget::TextInputWidget(WidgetValue<std::string> value, std::string label, LabelPlacement placement, std::string id)
    : Widget(std::move(id), StackDirection::Horizontal, "TextInput"), m_value(std::move(value)) {
    set_size({grow(), fit()});

    set_content_alignment(Anchor::CenterLeft);
    m_label_node = &add<TextWidget>("");
    m_label_node->set_id("label");
    m_label_node->set_visible(false);

    m_input_node = &add<Container>("input", StackDirection::Horizontal, "TextInputField");
    m_input_node->set_input_mode(InputMode::None);
    m_input_node->set_size({grow(), fit()});
    m_input_node->set_content_alignment(Anchor::CenterLeft);

    m_leading = &m_input_node->add<Container>("leading", StackDirection::Horizontal);
    m_icon_node = &m_leading->add<ImageWidget>();
    m_icon_node->set_id("icon");
    m_icon_node->set_input_enabled(false);
    m_icon_node->set_visible(false);

    m_field_node = &m_input_node->add<TextInputFieldNode>(*this);
    m_trailing = &m_input_node->add<Container>("trailing", StackDirection::Horizontal);
    for (Container* slot : {m_leading, m_trailing}) {
        slot->set_input_mode(InputMode::None);
        slot->set_size({fit(), fit()});
        slot->set_content_alignment(Anchor::CenterLeft);
        slot->set_visible(false);
    }
    set_label(std::move(label), placement);
}

void TextInputWidget::event(UiEvent& event) {
    if (event.type == EventType::FocusGained && event.target == this) {
        m_focus_requested = true;
    }
    if (event.type == EventType::PointerDown && event.button == PointerButton::Left && event.target == this) {
        m_focus_requested = surface().input_router().set_focus(this);
    }

    if (event.type == EventType::Cancel || (event.type == EventType::KeyDown && event.key == Key::Escape)) {
        const bool focused = input_state().focused;
        if (focused) {
            surface().input_router().restore_focus(*this);
            event.stop_propagation();
        } else {
            event.mark_handled();
        }
    }
}

void TextInputWidget::apply_theme_defaults(const Theme& theme) {
    Container::apply_theme_defaults(theme);
    set_font(surface().get_primary_font(), theme.controls.font_size);
    const TransitionSpec transition{0.25F, easing::out_quad};
    m_label_spacing = {theme.metrics.item_inner_spacing.x, theme.metrics.item_spacing.y};
    set_label_layout(m_label_placement, m_label_spacing);
    m_input_node->set_spacing(theme.metrics.item_inner_spacing.x);
    m_leading->set_spacing(theme.metrics.item_inner_spacing.x);
    m_trailing->set_spacing(theme.metrics.item_inner_spacing.x);
    m_icon_node->set_size({px(18.0F), px(18.0F)});
    m_field_node->set_size({grow(), grow()});

    configure_all_styles([](Style& style) { style.padding({}).border(BorderEdge{0.0F, BorderType::None}); });

    m_input_node->configure_all_styles([&theme, transition](Style& style) { style.control(theme, transition); });

    for (StyleType type : {StyleType::ACTIVE, StyleType::FOCUS}) {
        m_input_node->style(type)
            .background_color(theme.controls.active_color, transition)
            .border_color(theme.accent_color, transition);
    }
    m_input_node->style(StyleType::HOVER).background_color(theme.controls.hover_color, transition);
    m_input_node->style(StyleType::DISABLED).background_color(theme.background_secondary_color).border_color(theme.border_color);
    style(StyleType::DISABLED).cursor(ImGuiMouseCursor_NotAllowed);

    m_field_node->configure_all_styles([&theme](Style& style) {
        style.color(theme.text_color).background_color(theme.transparent).padding({}).border(BorderEdge{0.0F, BorderType::None});
    });
    m_icon_node->configure_all_styles([&theme](Style& style) { style.color(theme.text_secondary_color); });
    m_icon_node->style(StyleType::HOVER).color(theme.text_color);
    m_icon_node->style(StyleType::ACTIVE).color(theme.text_color);
    m_icon_node->style(StyleType::DISABLED).color(theme.text_secondary_color);
}

void TextInputWidget::input_state_changed() {
    Container::input_state_changed();
    const InputState& state = input_state();
    m_input_node->set_interaction_style(state.hovered, state.active, state.focused);
    m_icon_node->set_interaction_style(state.hovered, state.active, state.focused);
}

Rect TextInputWidget::hit_rect(Rect) const {
    return m_input_node->layout().visual_rect();
}

TextInputWidget& TextInputWidget::set_label(std::string label, LabelPlacement placement) {
    m_label_node->set_text(std::move(label));
    m_label_node->set_visible(!m_label_node->empty());
    m_label_placement = placement;
    set_label_layout(m_label_placement, m_label_spacing);
    return *this;
}

TextInputWidget& TextInputWidget::set_icon(Texture* icon) {
    m_icon_node->set_texture(icon);
    m_icon_node->set_visible(icon != nullptr);
    if (icon != nullptr || m_leading->children().size() == 1) m_leading->set_visible(icon != nullptr);
    return *this;
}

TextInputWidget& TextInputWidget::set_flags(ImGuiInputTextFlags flags) {
    m_flags = flags & ~ImGuiInputTextFlags_CallbackResize;
    return *this;
}

TextInputWidget& TextInputWidget::set_hint(std::string hint) {
    m_hint = std::move(hint);
    return *this;
}

TextInputWidget& TextInputWidget::on_submit(std::function<void()> callback) {
    m_on_submit = std::move(callback);
    return *this;
}

TextInputWidget& TextInputWidget::set_icon(TextureHandle icon) {
    set_icon(icon.get());
    m_icon_node->set_texture(std::move(icon));
    return *this;
}

bool TextInputWidget::set_value(std::string value) {
    if (!m_value.set(std::move(value))) {
        return false;
    }

    m_reload_requested = true;
    const int end = static_cast<int>(m_value.value().size());
    m_selection = {end, end, end};
    m_field_node->invalidate_measure();
    notify_change();
    return true;
}

void TextInputWidget::on_draw_end() {
    if (m_field_node->changed()) {
        notify_change();
    }
    if (!removal_pending() && m_field_node->submitted() && m_on_submit) m_on_submit();
    if (!removal_pending() && m_field_node->committed() && m_on_commit) m_on_commit();

    Container::on_draw_end();
}

TextInputWidget& TextInputWidget::set_multiline(bool multiline) {
    m_multiline = multiline;
    m_field_node->invalidate_measure();
    return *this;
}

TextInputWidget& TextInputWidget::set_rows(int minimum, int maximum) {
    m_minimum_rows = std::max(1, minimum);
    m_maximum_rows = std::max(m_minimum_rows, maximum);
    m_field_node->invalidate_measure();
    return *this;
}

TextInputWidget& TextInputWidget::set_submit_mode(TextSubmitMode mode) {
    m_submit_mode = mode;
    return *this;
}

TextInputWidget& TextInputWidget::set_callback(ImGuiInputTextCallback callback, void* user_data) {
    m_callback = callback;
    m_callback_data = user_data;
    return *this;
}

TextInputWidget& TextInputWidget::on_commit(std::function<void()> callback) {
    m_on_commit = std::move(callback);
    return *this;
}

void TextInputWidget::focus() {
    m_focus_requested = true;
}

void TextInputWidget::set_selection(int start, int end) {
    const int length = static_cast<int>(value().size());
    m_selection = {std::clamp(start, 0, length), std::clamp(end, 0, length), std::clamp(end, 0, length)};
    m_selection_requested = true;
}

bool TextInputWidget::replace_selection(std::string_view text) {
    const auto [first, last] = std::minmax(m_selection.start, m_selection.end);
    const int cursor = first + static_cast<int>(text.size());
    std::string replacement = value();
    replacement.replace(static_cast<std::size_t>(first), static_cast<std::size_t>(last - first), text);
    const bool changed = set_value(std::move(replacement));
    set_selection(cursor, cursor);
    return changed;
}

TextWidget& TextInputWidget::label() {
    return *m_label_node;
}

const TextWidget& TextInputWidget::label() const {
    return *m_label_node;
}

StyledNode& TextInputWidget::field() {
    return *m_field_node;
}

const StyledNode& TextInputWidget::field() const {
    return *m_field_node;
}

ImageWidget& TextInputWidget::icon() {
    return *m_icon_node;
}

const ImageWidget& TextInputWidget::icon() const {
    return *m_icon_node;
}

Container& TextInputWidget::leading() {
    m_leading->set_visible(true);
    return *m_leading;
}

Container& TextInputWidget::trailing() {
    m_trailing->set_visible(true);
    return *m_trailing;
}
