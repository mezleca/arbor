#include <arbor/widgets/checkbox.hpp>
#include <arbor/style/theme.hpp>
#include <arbor/surface.hpp>
#include <arbor/widgets/text.hpp>

#include <algorithm>

using namespace ui;

CheckboxWidget::CheckboxWidget(WidgetValue<bool> value, std::string label, LabelPlacement placement, std::string id)
    : Widget(std::move(id), StackDirection::Horizontal, "Checkbox"), m_value(value) {
    set_size({fit(), fit()});
    set_content_alignment(Anchor::CenterLeft);

    // the checkbox owns the frame's hit region. decorative containers must not register additional targets.
    m_box_node = &add<Container>("box", "Box");
    m_box_node->set_input_mode(InputMode::None);
    m_box_node->set_size({px(m_box_size), px(m_box_size)});
    m_frame_node = &m_box_node->add<Container>("frame", "Box");
    m_frame_node->set_input_mode(InputMode::None);
    m_frame_node->set_size({grow(), grow()});
    m_fill_node = &m_frame_node->add<BoxWidget>("fill", LayoutSize{grow(), grow()});
    m_label_node = &add<TextWidget>("");
    set_label(std::move(label), placement);
    m_fill_node->set_visible(m_value.value());
}

void CheckboxWidget::apply_theme_defaults(const Theme& theme) {
    Container::apply_theme_defaults(theme);
    m_label_spacing = {theme.metrics.item_inner_spacing.x, theme.metrics.item_spacing.y};
    set_spacing(m_label_placement == LabelPlacement::Inline ? m_label_spacing.x : m_label_spacing.y);
    m_label_node->configure_all_styles([&theme](Style& style) { style.color(theme.text_color).padding({0.0F, 2.0F}); });

    configure_all_styles([&theme](Style& style) {
        style.color(theme.text_color).font_size(theme.controls.font_size).padding({4.0f, 4.0f});
    });

    m_frame_border_thickness = theme.controls.border_thickness;
    m_rounding = theme.controls.rounding;
    m_frame_node->configure_all_styles([&theme](Style& style) { style.control(theme); });

    m_fill_node->configure_all_styles([&theme](Style& style) {
        style.background_color(theme.controls.mark_color).border(BorderEdge{0.0F, BorderType::None});
    });

    m_frame_node->style(StyleType::HOVER).background_color(theme.controls.hover_color).border_color(theme.accent_hover_color);
    m_frame_node->style(StyleType::ACTIVE).background_color(theme.controls.active_color).border_color(theme.accent_color);
    m_frame_node->style(StyleType::DISABLED).background_color(theme.background_secondary_color).border_color(theme.border_color);
    m_fill_node->style(StyleType::DISABLED).background_color(theme.text_secondary_color);
    m_label_node->style(StyleType::DISABLED).color(theme.text_secondary_color);
    style(StyleType::DISABLED).cursor(ImGuiMouseCursor_NotAllowed);

    update_shape();
}

CheckboxWidget& CheckboxWidget::set_label(std::string label, LabelPlacement placement) {
    m_label_node->set_text(std::move(label));
    m_label_node->set_visible(!m_label_node->empty());
    if (m_label_placement != placement) {
        Node& last = placement == LabelPlacement::Above ? static_cast<Node&>(*m_box_node) : static_cast<Node&>(*m_label_node);
        add(detach(last));
    }
    m_label_placement = placement;
    set_label_layout(placement, m_label_spacing);
    return *this;
}

bool CheckboxWidget::set_checked(bool checked) {
    if (!m_value.set(checked)) {
        return false;
    }

    m_fill_node->set_visible(checked);
    notify_change();
    return true;
}

void CheckboxWidget::click_event(UiEvent& event) {
    if (event.button != PointerButton::Left) {
        return;
    }

    if (!set_checked(m_type == CheckboxType::Radio || !m_value.value())) {
        return;
    }

    m_frame_node->animate()
        .to(StyleAnimationProperty::BackgroundColor, m_frame_node->style(StyleType::ACTIVE).background_color().value)
        .then(0.04F)
        .release(StyleAnimationProperty::BackgroundColor, {0.12F, easing::out_quad});
}

CheckboxWidget& CheckboxWidget::set_type(CheckboxType type) {
    if (m_type == type) {
        return *this;
    }

    m_type = type;
    update_shape();
    return *this;
}

CheckboxWidget& CheckboxWidget::set_box_size(float size) {
    const float resolved = std::max(1.0F, size);
    if (m_box_size == resolved) {
        return *this;
    }

    m_box_size = resolved;
    m_box_node->set_size({px(m_box_size), px(m_box_size)});
    update_shape();
    return *this;
}

CheckboxWidget& CheckboxWidget::set_mark_color(const Color& color) {
    m_fill_node->configure_all_styles([&color](Style& style) { style.background_color(color); });
    return *this;
}

StyledNode& CheckboxWidget::frame() {
    return *m_frame_node;
}

const StyledNode& CheckboxWidget::frame() const {
    return *m_frame_node;
}

StyledNode& CheckboxWidget::fill() {
    return *m_fill_node;
}

const StyledNode& CheckboxWidget::fill() const {
    return *m_fill_node;
}

TextWidget& CheckboxWidget::label() {
    return *m_label_node;
}

const TextWidget& CheckboxWidget::label() const {
    return *m_label_node;
}

void CheckboxWidget::input_state_changed() {
    Container::input_state_changed();
    const InputState& state = input_state();
    m_frame_node->set_interaction_style(state.hovered, state.active, state.focused);
    m_fill_node->set_interaction_style(state.hovered, state.active, state.focused);
}

void CheckboxWidget::on_update(float) {
    m_fill_node->set_visible(m_value.value());
}

Rect CheckboxWidget::hit_rect(Rect visual_rect) const {
    return m_label_clickable ? visual_rect : m_frame_node->layout().visual_rect();
}

CheckboxWidget& CheckboxWidget::set_label_clickable(bool clickable) {
    m_label_clickable = clickable;
    return *this;
}

void CheckboxWidget::update_shape() {
    const float radius = m_type == CheckboxType::Radio ? m_box_size * 0.5F : std::min(m_rounding, m_box_size * 0.5F);
    m_frame_node->configure_all_styles([radius](Style& style) { style.border_radius(radius); });
    m_fill_node->configure_all_styles([radius](Style& style) { style.border_radius(radius); });

    const float fill_size = m_box_size * 0.60F;
    const float inset = std::max(0.0F, ((m_box_size - fill_size) * 0.5F) - m_frame_border_thickness);
    m_frame_node->configure_all_styles([inset](Style& style) { style.padding({inset, inset}); });
}
