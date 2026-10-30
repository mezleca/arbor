#pragma once

#include "widget.hpp"
#include "value.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace ui {
    class TextWidget;
    class DropdownBody;
    class DropdownOptionNode;
    class DropdownTrigger;
    struct Theme;

    enum class DropdownVisibility : uint8_t {
        Closed,
        Open,
        Closing,
    };

    struct DropdownOption {
        std::string label;
        std::string value;

        bool operator==(const DropdownOption&) const = default;
    };

    class DropdownWidget : public Widget {
    public:
        explicit DropdownWidget(
            WidgetValue<std::string> value = {}, std::vector<DropdownOption> options = {}, std::string label = {},
            LabelPlacement placement = LabelPlacement::Above, std::string id = {}
        );

        DropdownWidget& set_label(std::string label, LabelPlacement placement = LabelPlacement::Above);
        DropdownWidget& set_placeholder(std::string placeholder);

        /// returns false if value is absent or already selected. a change closes the popup and notifies listeners.
        bool select_value(std::string_view value);

        /// replaces visible options without changing the bound value.
        DropdownWidget& set_options(std::vector<DropdownOption> options);
        void open();
        void close();

        bool is_open() const {
            return m_visibility == DropdownVisibility::Open;
        }

        TextWidget& label() {
            return *m_label_node;
        }

        Widget& trigger() const;

        Container& body() const;

    protected:
        void apply_theme_defaults(const Theme& theme) override;

    private:
        friend class DropdownBody;
        friend class DropdownOptionNode;
        friend class DropdownTrigger;

        const DropdownOption* find_option(std::string_view option_value) const;
        const DropdownOption* selected_option() const;
        void finish_close();
        void update_label_layout();
        void event(UiEvent& event) override;

        static constexpr ImVec2 ARROW_SIZE{8.0F, 4.0F};
        static constexpr float POPUP_GAP = 4.0F;
        static constexpr float TRANSITION_DURATION = 0.06F;

        WidgetValue<std::string> m_value;
        std::vector<DropdownOption> m_options;
        std::string m_placeholder = "select an option";
        TextWidget* m_label_node = nullptr;
        DropdownBody* m_body = nullptr;
        DropdownTrigger* m_trigger = nullptr;
        DropdownVisibility m_visibility = DropdownVisibility::Closed;
        LabelPlacement m_label_placement = LabelPlacement::Above;
        ImVec2 m_label_spacing = {8.0F, 10.0F};
    };
} // namespace ui
