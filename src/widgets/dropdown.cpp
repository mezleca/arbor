#include <imgui-ui/widgets/dropdown.hpp>
#include <imgui-ui/layout/layer-container.hpp>

#include <imgui-ui/style/theme.hpp>
#include <imgui-ui/surface.hpp>
#include <imgui-ui/widgets/button.hpp>
#include <imgui-ui/widgets/text.hpp>

#include <algorithm>
#include <imgui.h>
#include <utility>

using namespace ui;

class ui::DropdownBody final : public LayerContainer {
public:
    explicit DropdownBody(DropdownWidget& owner);

    void rebuild_options();

private:
    bool paint() override;
    void draw_children() override;
    void on_layout() override;
    void apply_theme_defaults(const Theme& theme) override;
    void on_update(float dt) override;

    DropdownWidget& m_owner;
};

class ui::DropdownOptionNode final : public ButtonWidget {
public:
    DropdownOptionNode(DropdownWidget& owner, std::size_t index)
        : ButtonWidget(owner.m_options[index].label, {grow(), fit()}), m_owner(owner), m_index(index) {
        set_type_name("DropdownOption");
        set_text_alignment({0.0F, 0.5F});

        // each live option keeps its source index. count changes replace rows through deferred node removal.
        on_click([this] {
            if (m_owner.is_open()) {
                m_owner.select_value(m_owner.m_options[m_index].value);
            }
        });
    }

    bool accepts_input() const override {
        return m_owner.is_open() && ButtonWidget::accepts_input();
    }

protected:
    void apply_theme_defaults(const Theme& theme) override {
        const float rounding = m_index == 0 || m_index + 1 == m_owner.m_options.size() ? theme.controls.rounding : 0.0F;

        configure_all_styles([&theme, rounding](Style& style) {
            style.color(theme.text_color)
                .background_color(theme.transparent)
                .padding({10.0F, 4.0F})
                .border(BorderEdge{0.0F, BorderType::None})
                .border_radius(rounding)
                .cursor(ImGuiMouseCursor_Hand);
        });

        style(StyleType::HOVER).background_color(theme.controls.hover_color);
        style(StyleType::ACTIVE).background_color(theme.controls.active_color);
        style(StyleType::DISABLED).color(theme.text_secondary_color).cursor(ImGuiMouseCursor_NotAllowed);
    }

private:
    DropdownWidget& m_owner;
    std::size_t m_index;
};

class ui::DropdownTrigger final : public Widget {
public:
    explicit DropdownTrigger(DropdownWidget& owner) : Widget("trigger", "DropdownTrigger"), m_owner(owner) {
        set_size({grow(), fit()});
    }

private:
    std::string_view preview() const {
        const DropdownOption* selected = m_owner.selected_option();
        return selected == nullptr ? std::string_view(m_owner.m_placeholder) : std::string_view(selected->label);
    }

    void on_measure() override {
        const std::string_view text = preview();
        const ImVec2 text_size = ImGui::CalcTextSize(text.data(), text.data() + text.size());
        set_measured_content_size(
            {text_size.x + DropdownWidget::ARROW_SIZE.x + ImGui::GetStyle().ItemInnerSpacing.x,
             std::max(text_size.y, DropdownWidget::ARROW_SIZE.y)}
        );
    }

    void click_event(UiEvent& event) override {
        if (event.button != PointerButton::Left) {
            return;
        }

        if (m_owner.is_open()) {
            m_owner.close();
            if (m_owner.m_body->native_popup_open()) return;
        }
        m_owner.open();
    }

    void paint_content(const PaintContext& context) override {
        const ComputedStyle& current_style = context.style;
        const std::string_view text = preview();
        const ImVec2 text_size = ImGui::CalcTextSize(text.data(), text.data() + text.size());
        const Rect content = context.content_rect;

        context.painter.text(
            {content.min.x, content.min.y + ((content.size().y - text_size.y) * 0.5F)}, current_style.color().value, text
        );

        context.painter.triangle(
            {content.max.x - (DropdownWidget::ARROW_SIZE.x * 0.5F), content.min.y + (content.size().y * 0.5F)},
            DropdownWidget::ARROW_SIZE, current_style.color().value,
            m_owner.is_open() ? TriangleDirection::Up : TriangleDirection::Down
        );
    }

    void input_state_changed() override {
        StyledNode::input_state_changed();
        if (m_owner.is_open()) {
            set_visual_style(StyleType::ACTIVE);
        }
    }

    DropdownWidget& m_owner;
};

DropdownBody::DropdownBody(DropdownWidget& owner) : LayerContainer("body", LayerMode::Popup, "DropdownBody"), m_owner(owner) {
    set_layout({.size = {px(0.0F), fit()}, .in_flow = false});
    set_spacing(0.0F);
    fade_out();
    rebuild_options();
}

void DropdownBody::on_layout() {
    LayerContainer::on_layout();
    assign_size({m_owner.m_trigger->layout().size().x, layout().size().y});
}

void DropdownBody::apply_theme_defaults(const Theme& theme) {
    configure_all_styles([&theme](Style& style) {
        style.color(theme.text_color)
            .background_color(theme.controls.background_color)
            .border(BorderEdge{theme.controls.border_thickness, BorderType::Solid})
            .border_color(theme.controls.border_color)
            .border_radius(theme.controls.rounding)
            .padding({});
    });
}

void DropdownBody::rebuild_options() {
    const auto live_count = std::ranges::count(children(), false, &Node::removal_pending);
    if (static_cast<std::size_t>(live_count) == m_owner.m_options.size()) {
        std::size_t index = 0;
        for (const auto& child : children()) {
            if (child->removal_pending()) continue;

            static_cast<DropdownOptionNode&>(*child).set_text(m_owner.m_options[index++].label);
        }
        return;
    }

    clear();
    for (std::size_t index = 0; index < m_owner.m_options.size(); ++index) {
        add<DropdownOptionNode>(m_owner, index);
    }
}

bool DropdownBody::paint() {
    if (m_owner.m_visibility == DropdownVisibility::Closed) {
        return LayerContainer::paint();
    }

    const Rect trigger_rect = m_owner.m_trigger->layout().visual_rect();
    set_popup_position({trigger_rect.min.x, trigger_rect.max.y + DropdownWidget::POPUP_GAP});
    if (LayerContainer::paint()) {
        const Rect body_rect = layout().visual_rect();

        // block the body while keeping its option rows targetable.
        surface().input_router().register_blocker(*this, body_rect);
        return true;
    }

    if (m_owner.is_open()) m_owner.close();
    return false;
}

void DropdownBody::draw_children() {
    std::size_t index = 0;
    for (const auto& child : children()) {
        if (child->removal_pending()) continue;

        auto& option = static_cast<DropdownOptionNode&>(*child);
        const InputState& input = option.input_state();
        if (!input.hovered && !input.active && !input.focused) {
            option.set_visual_style(
                m_owner.m_options[index].value == m_owner.m_value.value() ? StyleType::ACTIVE : StyleType::DEFAULT
            );
        }
        ++index;
    }

    Container::draw_children();
}

void DropdownBody::on_update(float) {
    if (m_owner.m_visibility != DropdownVisibility::Closing || opacity() > VISIBILITY_OPACITY_THRESHOLD) {
        return;
    }

    m_owner.finish_close();
}

const DropdownOption* DropdownWidget::find_option(std::string_view option_value) const {
    const auto option = std::ranges::find(m_options, option_value, &DropdownOption::value);
    return option == m_options.end() ? nullptr : &*option;
}

const DropdownOption* DropdownWidget::selected_option() const {
    return find_option(m_value.value());
}

void DropdownWidget::open() {
    if (m_visibility == DropdownVisibility::Open) {
        return;
    }

    m_visibility = DropdownVisibility::Open;
    m_body->open_popup();

    m_trigger->set_open_style(true);
    m_body->set_input_enabled(true);
    m_body->fade_in({TRANSITION_DURATION, easing::linear});
}

void DropdownWidget::close() {
    if (m_visibility != DropdownVisibility::Open) {
        return;
    }

    m_visibility = DropdownVisibility::Closing;
    m_trigger->set_open_style(false);

    // keep the body registered while it fades out. closing rows cannot be selected.
    m_body->set_input_enabled(true);
    m_body->fade_out({TRANSITION_DURATION, easing::linear});
}

void DropdownWidget::finish_close() {
    m_visibility = DropdownVisibility::Closed;
    m_body->close_popup();
    m_body->set_input_enabled(false);
}

DropdownWidget::DropdownWidget(
    WidgetValue<std::string> value, std::vector<DropdownOption> options, std::string label, LabelPlacement placement,
    std::string id
)
    : Widget(std::move(id), "Dropdown"), m_value(std::move(value)), m_options(std::move(options)) {
    set_size({fit(), fit()});

    m_label_node = &add<TextWidget>("");
    m_label_node->set_visible(false);
    m_trigger = &add<DropdownTrigger>(*this);
    m_body = &add<DropdownBody>(*this);
    m_body->set_input_enabled(false);
    set_label(std::move(label), placement);
}

void DropdownWidget::event(UiEvent& event) {
    if (event.type == EventType::PointerDown || event.type == EventType::PointerUp) {
        event.block_native_input();
    }
}

void DropdownWidget::apply_theme_defaults(const Theme& theme) {
    Container::apply_theme_defaults(theme);
    set_font(surface().get_primary_font(), theme.controls.font_size);

    configure_all_styles([&theme](Style& style) {
        style.color(theme.text_color)
            .background_color(theme.transparent)
            .border_color(theme.controls.border_color)
            .border(BorderEdge{0.0F, BorderType::None})
            .border_radius(theme.controls.rounding)
            .border_thickness(theme.controls.border_thickness)
            .padding({});
    });

    m_label_spacing = {theme.metrics.item_inner_spacing.x, theme.metrics.item_spacing.y};
    update_label_layout();
    m_label_node->style().color(theme.text_color);
    m_trigger->configure_all_styles([&theme](Style& style) { style.control(theme).cursor(ImGuiMouseCursor_Hand); });
    m_trigger->style(StyleType::HOVER).background_color(theme.controls.hover_color);
    m_trigger->style(StyleType::ACTIVE).background_color(theme.controls.active_color);
    m_trigger->style(StyleType::DISABLED)
        .color(theme.text_secondary_color)
        .background_color(theme.background_secondary_color)
        .border_color(theme.border_color)
        .cursor(ImGuiMouseCursor_NotAllowed);
    m_label_node->style(StyleType::DISABLED).color(theme.text_secondary_color);
    style(StyleType::DISABLED).cursor(ImGuiMouseCursor_NotAllowed);
}

DropdownWidget& DropdownWidget::set_label(std::string label, LabelPlacement placement) {
    m_label_node->set_visible(!label.empty());
    m_label_node->set_text(std::move(label));
    m_label_placement = placement;
    update_label_layout();
    return *this;
}

void DropdownWidget::update_label_layout() {
    set_content_alignment({0.0F, m_label_placement == LabelPlacement::Inline ? 0.5F : 0.0F});
    set_label_layout(m_label_placement, m_label_spacing);
}

bool DropdownWidget::select_value(std::string_view value) {
    const DropdownOption* option = find_option(value);
    if (option == nullptr || m_value.value() == option->value) {
        return false;
    }

    close();
    m_value.set(option->value);
    invalidate_measure();
    notify_change();
    return true;
}

DropdownWidget& DropdownWidget::set_placeholder(std::string placeholder) {
    if (m_placeholder == placeholder) {
        return *this;
    }

    m_placeholder = std::move(placeholder);
    invalidate_measure();
    return *this;
}

DropdownWidget& DropdownWidget::set_options(std::vector<DropdownOption> options) {
    if (m_options == options) {
        return *this;
    }

    m_options = std::move(options);
    m_body->rebuild_options();
    invalidate_measure();
    return *this;
}

Widget& DropdownWidget::trigger() const {
    return *m_trigger;
}

Container& DropdownWidget::body() const {
    return *m_body;
}
