#pragma once

#include <imgui.h>

#include <cstdint>
#include <functional>
#include <string>
#include <utility>

namespace ui {
    class Node;
    struct UiEvent;

    using InputCallback = std::function<void(UiEvent&)>;

    enum class EventType : uint8_t {
        PointerMove,
        PointerDown,
        PointerUp,
        Scroll,
        KeyDown,
        KeyUp,

        /// a matching pointer press and release occurred on one target.
        Click,

        /// a matching secondary pointer press and release occurred on one target.
        ContextClick,
        TextInput,
        FocusGained,
        FocusLost,
        Cancel,
    };

    enum class EventMask : uint16_t {
        None = 0,
        PointerMove = 1U << 0U,
        PointerDown = 1U << 1U,
        PointerUp = 1U << 2U,
        Scroll = 1U << 3U,
        KeyDown = 1U << 4U,
        KeyUp = 1U << 5U,
        Click = 1U << 6U,
        ContextClick = 1U << 7U,
        TextInput = 1U << 8U,
        FocusGained = 1U << 9U,
        FocusLost = 1U << 10U,
        Cancel = 1U << 11U,

        Pointer = PointerMove | PointerDown | PointerUp | Click | ContextClick | Scroll,
        Keyboard = KeyDown | KeyUp | TextInput | Cancel,
        All = Pointer | Keyboard | FocusGained | FocusLost,
    };

    constexpr EventMask operator|(EventMask left, EventMask right) {
        return static_cast<EventMask>(std::to_underlying(left) | std::to_underlying(right));
    }

    constexpr EventMask operator&(EventMask left, EventMask right) {
        return static_cast<EventMask>(std::to_underlying(left) & std::to_underlying(right));
    }

    constexpr bool contains(EventMask mask, EventMask value) {
        return (mask & value) == value;
    }

    constexpr EventMask event_mask(EventType type) {
        const auto index = std::to_underlying(type);
        return index <= std::to_underlying(EventType::Cancel) ? static_cast<EventMask>(1U << index) : EventMask::None;
    }

    enum class PointerButton : uint8_t {
        None,
        Left,
        Right,
        Middle,
    };

    struct UiEvent {
        /// target of the current branch. stays unchanged while bubbling through parents, then changes for underlying targets.
        Node* target = nullptr;

        ImVec2 position;
        ImVec2 scroll;

        std::string text;

        bool handled = false;
        bool propagation_stopped = false;
        bool default_prevented = false;
        bool native_input_blocked = false;

        EventType type;
        PointerButton button = PointerButton::None;
        ImGuiKey key = ImGuiKey_None;

        static UiEvent make(EventType type) {
            UiEvent event;
            event.type = type;
            return event;
        }

        /// handled alone leaves routing and native forwarding unchanged. stopping propagation also consumes the event
        /// and skips remaining handlers and routing branches. native blocking independently suppresses imgui forwarding.
        void mark_handled() {
            handled = true;
        }

        void stop_propagation() {
            handled = true;
            propagation_stopped = true;
        }

        /// suppresses click synthesis for the matching press or release.
        /// for wheel events, suppresses the container's scroll request.
        void prevent_default() {
            default_prevented = true;
        }

        void block_native_input() {
            native_input_blocked = true;
        }
    };

} // namespace ui
