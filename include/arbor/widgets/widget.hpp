#pragma once

#include "../layout/container.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <imgui.h>
#include <memory>
#include <string>
#include <string_view>

namespace ui {
    enum class LabelPlacement : uint8_t {
        Inline,
        Above,
    };

    class Widget : public Container {
    public:
        explicit Widget(std::string id, std::string_view type_name = "Widget", InputMode input_mode = InputMode::Target);
        Widget(
            std::string id, StackDirection direction, std::string_view type_name = "Widget",
            InputMode input_mode = InputMode::Target
        );

        /// runs after internal event handling for every event reaching this widget, unless propagation was stopped.
        /// a nonempty callback registers a pointer target when input mode is none. it does not consume events automatically.
        Widget& on_event(InputCallback callback);

        /// pointer callbacks register this widget as a target when its input mode is none. they do not block other targets.
        Widget& on_event(EventType type, InputCallback callback);

        Widget& on_change(std::function<void()> callback);

    protected:
        void set_label_layout(LabelPlacement placement, ImVec2 spacing);
        void notify_change();
        void dispatch_event(UiEvent& event) override;
        virtual void click_event(UiEvent&) {}

    private:
        using EventCallbacks = std::array<InputCallback, static_cast<std::size_t>(EventType::Cancel) + 1>;

        InputCallback m_on_event;
        std::function<void()> m_on_change;
        std::unique_ptr<EventCallbacks> m_event_callbacks;
    };

} // namespace ui
