#pragma once

#include "widget.hpp"
#include "text.hpp"
#include "value.hpp"

#include <algorithm>
#include <concepts>
#include <cstdint>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>

namespace ui {
    template <typename T>
    concept NumberInputType = std::is_arithmetic_v<T> && !std::same_as<T, bool> && sizeof(T) <= sizeof(std::uint64_t);

    template <NumberInputType T>
    class NumberInputFieldNode;

    template <NumberInputType T>
    class NumberInputWidget : public Widget {
    public:
        explicit NumberInputWidget(
            WidgetValue<T> value = {}, std::string label = {}, LabelPlacement placement = LabelPlacement::Inline,
            std::string id = {}
        );

        NumberInputWidget& set_label(std::string label, LabelPlacement placement = LabelPlacement::Inline);
        NumberInputWidget& set_minimum(double minimum);
        NumberInputWidget& set_maximum(double maximum);
        NumberInputWidget& set_range(double minimum, double maximum);
        NumberInputWidget& clear_range();
        NumberInputWidget& set_speed(float speed);
        NumberInputWidget& set_format(std::string format);
        NumberInputWidget& set_thumb_visible(bool visible);
        NumberInputWidget& set_thumb_size(float size);
        NumberInputWidget& set_thumb_color(Color color);
        NumberInputWidget& set_flags(ImGuiSliderFlags flags);
        ImGuiSliderFlags flags() const {
            return m_flags;
        }

        bool set_value(T value);

    protected:
        void apply_theme_defaults(const Theme& theme) override;

    private:
        friend class NumberInputFieldNode<T>;

        static constexpr ImGuiDataType scalar_type();
        void update_label_layout();
        Rect hit_rect(Rect visual_rect) const override;
        void mouse_press_event(UiEvent& event) override;
        bool draw_value();

        WidgetValue<T> m_value;
        TextWidget* m_label = nullptr;
        NumberInputFieldNode<T>* m_field = nullptr;
        std::string m_format;
        std::optional<double> m_minimum = 0;
        std::optional<double> m_maximum = 100;
        Color m_thumb_color;
        LabelPlacement m_label_placement = LabelPlacement::Inline;
        ImVec2 m_label_spacing;
        float m_speed;
        float m_thumb_size = 10.0F;
        ImGuiSliderFlags m_flags = ImGuiSliderFlags_None;
        bool m_thumb_visible = true;
    };

    template <NumberInputType T>
    class NumberInputFieldNode final : public Widget {
    public:
        explicit NumberInputFieldNode(NumberInputWidget<T>& owner)
            : Widget("input", "NumberInputField", InputMode::None), m_owner(owner) {
            set_size({grow(), grow()});
        }

    private:
        void on_measure() override {
            ImGui::PushFont(font(), font_size());
            const float height = ImGui::GetTextLineHeight();
            ImGui::PopFont();
            set_measured_content_size({0.0F, height});
        }

        bool paint() override {
            const ComputedStyle& current_style = computed_style();
            ImVec2 frame_padding = current_style.padding().window_padding();
            frame_padding.y = std::max(0.0F, (layout().size().y - ImGui::GetTextLineHeight()) * 0.5F);

            ImGui::PushID(this);

            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, frame_padding);
            ImGui::PushStyleVar(ImGuiStyleVar_GrabMinSize, m_owner.m_thumb_size);
            ImGui::PushStyleVar(ImGuiStyleVar_GrabRounding, current_style.border_radius());
            ImGui::PushStyleColor(ImGuiCol_SliderGrab, m_owner.m_thumb_color.rgba());
            ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, m_owner.m_thumb_color.rgba());

            ImGui::SetNextItemWidth(layout().size().x);

            if (m_owner.draw_value()) {
                m_owner.notify_change();
            }

            set_interaction_style(ImGui::IsItemHovered(), ImGui::IsItemActive(), m_owner.input_state().focused);
            painter().border(
                {ImGui::GetItemRectMin(), ImGui::GetItemRectMax()}, current_style, current_style.border_color().value
            );

            ImGui::PopStyleColor(2);
            ImGui::PopStyleVar(3);
            ImGui::PopID();

            return true;
        }

        NumberInputWidget<T>& m_owner;
    };

    template <NumberInputType T>
    NumberInputWidget<T>::NumberInputWidget(WidgetValue<T> value, std::string label, LabelPlacement placement, std::string id)
        : Widget(std::move(id), "NumberInput"), m_value(std::move(value)), m_format(std::floating_point<T> ? "%.3f" : ""),
          m_speed(std::floating_point<T> ? 0.1F : 1.0F) {
        set_size({grow(), fit()});
        m_label = &add<TextWidget>("");
        m_field = &add<NumberInputFieldNode<T>>(*this);
        set_label(std::move(label), placement);
    }

    template <NumberInputType T>
    constexpr ImGuiDataType NumberInputWidget<T>::scalar_type() {
        if constexpr (std::same_as<T, float>) {
            return ImGuiDataType_Float;
        } else if constexpr (std::same_as<T, double>) {
            return ImGuiDataType_Double;
        } else {
            static_assert(std::has_single_bit(sizeof(T)) && sizeof(T) <= 8);
            constexpr int size_index = std::countr_zero(sizeof(T));
            return ImGuiDataType_S8 + (2 * size_index) + !std::is_signed_v<T>;
        }
    }

    template <NumberInputType T>
    void NumberInputWidget<T>::apply_theme_defaults(const Theme& theme) {
        Container::apply_theme_defaults(theme);
        const TransitionSpec transition{0.25F, easing::out_quad};
        set_font(surface().get_primary_font(), theme.controls.font_size);

        m_thumb_color = theme.controls.mark_color;
        m_thumb_size = theme.controls.thumb_size;
        m_label_spacing = {theme.metrics.item_inner_spacing.x, theme.metrics.item_spacing.y};

        update_label_layout();
        configure_all_styles([](Style& style) { style.padding({}).border(BorderEdge{0.0F, BorderType::None}); });

        m_field->configure_all_styles([&theme, transition](Style& style) { style.control(theme, transition); });
        m_field->style(StyleType::HOVER).background_color(theme.controls.hover_color, transition);
        m_field->style(StyleType::ACTIVE)
            .background_color(theme.controls.active_color, transition)
            .border_color(theme.accent_color, transition);

        m_field->style(StyleType::FOCUS).border_color(theme.accent_color);
        m_field->style(StyleType::DISABLED)
            .color(theme.text_secondary_color)
            .background_color(theme.background_secondary_color)
            .border_color(theme.border_color);

        style(StyleType::DISABLED).cursor(ImGuiMouseCursor_NotAllowed);
    }

    template <NumberInputType T>
    NumberInputWidget<T>& NumberInputWidget<T>::set_label(std::string label, LabelPlacement placement) {
        m_label->set_text(std::move(label));
        m_label_placement = placement;
        update_label_layout();
        return *this;
    }

    template <NumberInputType T>
    void NumberInputWidget<T>::update_label_layout() {
        m_label->set_visible(!m_label->empty());
        set_content_alignment({0.0F, m_label_placement == LabelPlacement::Inline ? 0.5F : 0.0F});
        set_label_layout(m_label_placement, m_label_spacing);
    }

    template <NumberInputType T>
    Rect NumberInputWidget<T>::hit_rect(Rect) const {
        return m_field->layout().visual_rect();
    }

    template <NumberInputType T>
    void NumberInputWidget<T>::mouse_press_event(UiEvent& event) {
        if (event.button == PointerButton::Left) {
            surface().input_router().set_focus(this);
        }
    }

    template <NumberInputType T>
    NumberInputWidget<T>& NumberInputWidget<T>::set_minimum(double minimum) {
        m_minimum = minimum;
        return *this;
    }

    template <NumberInputType T>
    NumberInputWidget<T>& NumberInputWidget<T>::set_maximum(double maximum) {
        m_maximum = maximum;
        return *this;
    }

    template <NumberInputType T>
    NumberInputWidget<T>& NumberInputWidget<T>::set_range(double minimum, double maximum) {
        m_minimum = std::min(minimum, maximum);
        m_maximum = std::max(minimum, maximum);
        return *this;
    }

    template <NumberInputType T>
    NumberInputWidget<T>& NumberInputWidget<T>::clear_range() {
        m_minimum.reset();
        m_maximum.reset();
        return *this;
    }

    template <NumberInputType T>
    NumberInputWidget<T>& NumberInputWidget<T>::set_speed(float speed) {
        m_speed = std::max(0.0F, speed);
        return *this;
    }

    template <NumberInputType T>
    NumberInputWidget<T>& NumberInputWidget<T>::set_format(std::string format) {
        m_format = std::move(format);
        return *this;
    }

    template <NumberInputType T>
    NumberInputWidget<T>& NumberInputWidget<T>::set_thumb_visible(bool visible) {
        m_thumb_visible = visible;
        return *this;
    }

    template <NumberInputType T>
    NumberInputWidget<T>& NumberInputWidget<T>::set_thumb_size(float size) {
        m_thumb_size = std::max(1.0F, size);
        return *this;
    }

    template <NumberInputType T>
    NumberInputWidget<T>& NumberInputWidget<T>::set_thumb_color(Color color) {
        m_thumb_color = std::move(color);
        return *this;
    }

    template <NumberInputType T>
    NumberInputWidget<T>& NumberInputWidget<T>::set_flags(ImGuiSliderFlags flags) {
        m_flags = flags;
        return *this;
    }

    template <NumberInputType T>
    bool NumberInputWidget<T>::set_value(T value) {
        if (!m_value.set(std::move(value))) {
            return false;
        }

        notify_change();
        return true;
    }

    template <NumberInputType T>
    bool NumberInputWidget<T>::draw_value() {
        constexpr ImGuiDataType data_type = scalar_type();
        const T minimum = static_cast<T>(m_minimum.value_or(0.0));
        const T maximum = static_cast<T>(m_maximum.value_or(0.0));
        const void* minimum_ptr = m_minimum.has_value() ? &minimum : nullptr;
        const void* maximum_ptr = m_maximum.has_value() ? &maximum : nullptr;
        const char* format = m_format.empty() ? nullptr : m_format.c_str();

        return m_value.edit([&](T& value) {
            if (m_thumb_visible && m_minimum.has_value() && m_maximum.has_value() && maximum > minimum) {
                return ImGui::SliderScalar("##value", data_type, &value, &minimum, &maximum, format, m_flags);
            }

            return ImGui::DragScalar("##value", data_type, &value, m_speed, minimum_ptr, maximum_ptr, format, m_flags);
        });
    }
} // namespace ui
