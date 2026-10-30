#include <imgui-ui/widgets/text-input.hpp>

#include <imgui-ui/style/theme.hpp>
#include <imgui-ui/surface.hpp>
#include <imgui-ui/widgets/image.hpp>
#include <imgui-ui/widgets/text.hpp>

#include <cfloat>
#include <imgui_stdlib.h>
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

private:
    void on_measure() override {
        ImGui::PushFont(font(), font_size());
        const float line_height = ImGui::GetTextLineHeight();
        ImGui::PopFont();
        set_measured_size({0.0F, line_height});
    }

    bool paint() override {
        // transfer the widget's routed focus request to imgui immediately before drawing the native field.
        if (m_owner.m_focus_requested) {
            ImGui::SetKeyboardFocusHere();
            m_owner.m_focus_requested = false;
        }

        ImGui::PushID(this);
        const float padding_y = std::max(0.0F, (layout().size().y - layout().measured_size().y) * 0.5F);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {ImGui::GetStyle().FramePadding.x, padding_y});
        ImGui::SetNextItemWidth(-FLT_MIN);
        m_changed =
            m_owner.m_value.edit([this](std::string& value) { return ImGui::InputText("##value", &value, m_owner.flags()); });
        ImGui::PopStyleVar();
        ImGui::PopID();
        return true;
    }

    TextInputWidget& m_owner;
    bool m_changed = false;
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

    m_icon_node = &m_input_node->add<ImageWidget>();
    m_icon_node->set_id("icon");
    m_icon_node->set_input_enabled(false);
    m_icon_node->set_visible(false);

    m_field_node = &m_input_node->add<TextInputFieldNode>(*this);
    set_label(std::move(label), placement);
}

void TextInputWidget::event(UiEvent& event) {
    if (event.type == EventType::PointerDown && event.button == PointerButton::Left) {
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
}

void TextInputWidget::input_state_changed() {
    Container::input_state_changed();
    const InputState& state = input_state();
    m_input_node->set_interaction_style(state.hovered, state.active, state.focused);
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
    return *this;
}

TextInputWidget& TextInputWidget::set_flags(ImGuiInputTextFlags flags) {
    m_flags = flags;
    return *this;
}

bool TextInputWidget::set_value(std::string value) {
    if (!m_value.set(std::move(value))) {
        return false;
    }

    notify_change();
    return true;
}

void TextInputWidget::on_draw_end() {
    if (m_icon_node->visible()) {
        const ImVec4 icon_color =
            input_state().hovered || input_state().active ? surface().theme().text_color : surface().theme().text_secondary_color;
        m_icon_node->style().color(ImColor(icon_color));
    }

    if (m_field_node->changed()) {
        notify_change();
    }

    Container::on_draw_end();
}
