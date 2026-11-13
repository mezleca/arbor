#pragma once

#include "hit-test-index.hpp"

#include <array>
#include <cstddef>
#include <vector>

namespace ui {
    class Node;
    class Debugger;
    struct InputState;

    inline constexpr std::size_t POINTER_BUTTON_COUNT = 3;

    struct InputRouterStats {
        /// entries registered after the most recent begin_frame().
        std::size_t entry_count = 0;

        /// entry inspections performed by target and blocker queries in this frame.
        std::size_t entry_checks = 0;
    };

    /// routes input through the node tree. drawing registers pointer regions each frame. dispatch selects targets
    /// using those regions, keyboard focus or pointer capture, then bubbles events through their parents.
    /// blockers restrict which branches receive input. the router stores node references without owning them.
    class InputRouter {
    public:
        InputRouter() = default;
        ~InputRouter();
        InputRouter(const InputRouter&) = delete;
        InputRouter& operator=(const InputRouter&) = delete;

        /// replaces the callback invoked before routing events to nodes. persists across frames.
        InputRouter& on_event(InputCallback callback);

        /// clears frame-local hit regions and their callbacks, then removes input state from inactive nodes.
        void begin_frame();

        /// attaches node to this router and adds a screen-space target independently of its input mode.
        void register_target(Node& node, Rect rect, InputCallback callback = {}, EventMask events = EventMask::Pointer);

        /// consumes selected events in rect and prevents imgui from receiving them.
        void register_blocker(Rect rect, InputCallback callback = {}, EventMask events = EventMask::Pointer);

        /// attaches owner and consumes selected events outside its input descendants within rect.
        void register_blocker(Node& owner, Rect rect, InputCallback callback = {}, EventMask events = EventMask::Pointer);

        /// routes presses and moves to node until all buttons are released. releases reach hit descendants and bubble to node.
        /// transferring capture or intercepting its release sends Cancel to the previous owner.
        bool capture_pointer(Node& node);
        void release_pointer();

        /// cancels capture and clears presses pointing into a subtree. the captured node receives Cancel.
        void release_pointer(Node& subtree);

        /// changes keyboard focus, or clears it with nullptr. sends focus loss before focus gain.
        /// a non-null node must be visible, enabled and accept input. input mode does not restrict explicit focus.
        bool set_focus(Node* node);

        /// moves focus from a subtree to its nearest active blocker ancestor.
        void restore_focus(Node& subtree);

        Node* focused_node() {
            return m_focused_node;
        }

        const Node* focused_node() const {
            return m_focused_node;
        }

        /// invokes the router callback before node routing. stop_propagation skips node delivery.
        bool dispatch(UiEvent& event);

        /// bypasses hit testing and delivers to target and its parents, including hidden or disabled nodes.
        /// pending removal or stop_propagation ends delivery. focus notifications use this path after input is disabled.
        static bool dispatch_to(Node& target, UiEvent& event);

        /// returns the front target or blocking owner using the same hit rules as event dispatch.
        Node* node_at(ImVec2 position, EventType type = EventType::PointerMove) const;

        /// returns current entry count and scans performed since begin_frame().
        InputRouterStats stats() const;

    private:
        friend class Node;
        friend class Debugger;

        struct PressedPointer {
            std::vector<Node*> targets;

            /// records the button even when delivery is stopped. capture ends after the last release.
            bool down = false;

            /// repeats native input blocking on the matching pointer release.
            bool native_input_blocked = false;
        };

        void set_debug_inspect_mode(bool enabled);
        void set_debug_pointer_blocked(bool blocked);

        /// removes subtree hit regions and clears its hover, active, focus, capture and presses.
        void clear_input_state(Node& subtree);

        /// clears every router reference into subtree before it is detached.
        void detach(Node& subtree);

        /// clears focus, capture, hover, active, and press state for inactive nodes.
        void clear_inactive_targets();

        /// clears capture before sending Cancel directly to its former owner, without bubbling.
        void cancel_capture();

        /// updates hover from a position without dispatching an event.
        void refresh_pointer_state(ImVec2 position);
        static void set_input_target(Node*& current, Node* next, bool InputState::* flag);
        void set_hovered(const HitTestIndex::Route& route);

        /// records press targets and native blocking. preventing the press clears its click candidates.
        bool dispatch_pointer(UiEvent& event);

        /// delivers release, ends capture and active state, then emits a click for matching press targets.
        bool dispatch_release(UiEvent& event);

        /// emits Click or ContextClick only for targets reached by both press and release and not prevented.
        bool dispatch_click(UiEvent& release, PressedPointer& pressed, const std::vector<Node*>& released);

        /// selects capture or a hit-test snapshot and updates hover and active state before delivery.
        /// reached optionally records delivered targets. a nonempty eligible list restricts click delivery.
        void route_pointer(UiEvent& event, std::vector<Node*>* reached = nullptr, const std::vector<Node*>& eligible = {});

        /// delivers entry callbacks and bubbles front to back, visiting shared ancestors once within the blocker scope.
        /// rechecks node attachment after callbacks. reached records only targets still attached at that point.
        void dispatch_targets(
            UiEvent& event, const HitTestIndex::Route& route, std::vector<Node*>* reached, const std::vector<Node*>& eligible
        );

        /// delivers to focus first, then reverse attachment order, restricted to the active blocking subtree.
        bool dispatch_keyboard(UiEvent& event);

        /// sets the branch target and bubbles through valid ancestors, stopping at scope or an already visited node.
        static void dispatch_branch(Node& target, UiEvent& event, std::vector<Node*>& visited, Node* scope = nullptr);

        HitTestIndex m_hit_test;
        InputCallback m_on_event;
        Node* m_focused_node = nullptr;
        bool m_debug_inspect_mode = false;
        bool m_debug_pointer_blocked = false;
        Node* m_pointer_capture = nullptr;
        std::vector<Node*> m_hovered_nodes;
        Node* m_active_node = nullptr;
        std::array<PressedPointer, POINTER_BUTTON_COUNT> m_pressed{};
        std::vector<Node*> m_attached_nodes;
    };

} // namespace ui
