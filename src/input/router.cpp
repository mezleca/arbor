#include <arbor/input/router.hpp>
#include <arbor/tree/node.hpp>

#include <algorithm>
#include <utility>

using namespace ui;

static InputState set_input_flag(InputState state, bool InputState::* flag, bool enabled) {
    state.*flag = enabled;
    return state;
}

static int pointer_button_index(PointerButton button) {
    const int index = std::to_underlying(button) - 1;
    return index >= 0 && index < static_cast<int>(POINTER_BUTTON_COUNT) ? index : -1;
}

static bool is_input_target(const Node* node) {
    return node != nullptr && node->accepts_input();
}

void InputRouter::set_input_target(Node*& current, Node* next, bool InputState::* flag) {
    if (current == next) return;
    if (current != nullptr) current->set_input_state(set_input_flag(current->input_state(), flag, false));
    current = next;
    if (current != nullptr) current->set_input_state(set_input_flag(current->input_state(), flag, true));
}

InputRouter::~InputRouter() {
    for (Node* node : m_attached_nodes) {
        node->detach_input_router(*this);
    }
}

InputRouter& InputRouter::on_event(InputCallback callback) {
    m_on_event = std::move(callback);
    return *this;
}

void InputRouter::begin_frame() {
    m_hit_test.begin_frame(m_attached_nodes.size() + 8);
    clear_inactive_targets();
}

void InputRouter::set_debug_inspect_mode(bool enabled) {
    if (m_debug_inspect_mode == enabled) {
        return;
    }

    m_debug_inspect_mode = enabled;
    if (enabled) {
        cancel_capture();
        m_pressed = {};
        set_hovered({});
        set_input_target(m_active_node, nullptr, &InputState::active);
        set_focus(nullptr);
    }
}

void InputRouter::set_debug_pointer_blocked(bool blocked) {
    if (m_debug_pointer_blocked == blocked) {
        return;
    }

    m_debug_pointer_blocked = blocked;
    if (blocked) {
        set_hovered({});
    }
}

void InputRouter::clear_input_state(Node& subtree) {
    // remove hit regions and their hover flags before clearing focus, capture and stored presses.
    m_hit_test.erase_subtree(subtree);
    std::erase_if(m_hovered_nodes, [this, &subtree](Node* node) {
        if (!subtree.contains(node)) return false;

        node->set_input_state(set_input_flag(node->input_state(), &InputState::hovered, false));
        return true;
    });
    if (m_hovered_nodes.empty()) set_hovered({});

    if (subtree.contains(m_active_node)) set_input_target(m_active_node, nullptr, &InputState::active);
    if (subtree.contains(m_focused_node)) set_focus(nullptr);
    release_pointer(subtree);
}

void InputRouter::detach(Node& subtree) {
    // clear focus without dispatch before discarding references to nodes being destroyed or detached.
    if (subtree.contains(m_focused_node)) set_input_target(m_focused_node, nullptr, &InputState::focused);
    clear_input_state(subtree);

    std::erase_if(m_attached_nodes, [&subtree](Node* node) { return subtree.contains(node); });
}

void InputRouter::refresh_pointer_state(ImVec2 position) {
    if (!ImGui::IsMousePosValid(&position)) {
        return;
    }

    if (m_debug_inspect_mode || m_debug_pointer_blocked) {
        set_hovered({});
        return;
    }

    set_hovered(m_hit_test.route_at(position, EventType::PointerMove));
}

void InputRouter::set_hovered(const HitTestIndex::Route& route) {
    Node* owner = route.targets.empty() && route.blocker ? route.blocker->node : nullptr;
    if (!is_input_target(owner)) owner = nullptr;

    for (Node* node : m_hovered_nodes) {
        const bool hit = std::ranges::contains(route.targets, node, &HitTestIndex::Entry::node);
        if (!hit && node != owner) node->set_input_state(set_input_flag(node->input_state(), &InputState::hovered, false));
    }

    m_hovered_nodes.clear();

    for (const auto& entry : route.targets) {
        m_hovered_nodes.push_back(entry.node);
        entry.node->set_input_state(set_input_flag(entry.node->input_state(), &InputState::hovered, true));
    }

    if (owner != nullptr) {
        m_hovered_nodes.push_back(owner);
        owner->set_input_state(set_input_flag(owner->input_state(), &InputState::hovered, true));
    }

    if (m_hovered_nodes.empty() && ImGui::GetCurrentContext() != nullptr &&
        ImGui::GetMouseCursor() != ImGuiMouseCursor_NotAllowed) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Arrow);
    }
}

void InputRouter::register_target(Node& node, Rect rect, InputCallback callback, EventMask events) {
    node.set_input_router(this);
    m_hit_test.add(&node, HitTestIndex::EntryKind::Target, rect, events, std::move(callback));
}

void InputRouter::register_blocker(Rect rect, InputCallback callback, EventMask events) {
    m_hit_test.add(nullptr, HitTestIndex::EntryKind::Blocker, rect, events, std::move(callback));
}

void InputRouter::register_blocker(Node& owner, Rect rect, InputCallback callback, EventMask events) {
    owner.set_input_router(this);
    m_hit_test.add(&owner, HitTestIndex::EntryKind::Blocker, rect, events, std::move(callback));
}

bool InputRouter::capture_pointer(Node& node) {
    if (!is_input_target(&node)) {
        return false;
    }

    if (m_pointer_capture != &node) cancel_capture();
    if (!is_input_target(&node)) return false;

    m_pointer_capture = &node;
    return true;
}

void InputRouter::release_pointer() {
    m_pointer_capture = nullptr;
}

void InputRouter::cancel_capture() {
    Node* captured = std::exchange(m_pointer_capture, nullptr);
    if (captured == nullptr) return;

    // clear ownership before notifying the former owner, whose handler may release or transfer capture.
    UiEvent event = UiEvent::make(EventType::Cancel);
    event.target = captured;
    captured->dispatch_event(event);
}

void InputRouter::release_pointer(Node& subtree) {
    if (subtree.contains(m_pointer_capture)) {
        cancel_capture();
    }

    for (PressedPointer& pressed : m_pressed) {
        std::erase_if(pressed.targets, [&subtree](Node* node) { return subtree.contains(node); });
    }
}

bool InputRouter::set_focus(Node* node) {
    if (node != nullptr && !is_input_target(node)) return false;
    if (m_focused_node == node) return true;

    // notify the old branch, replace its focus flag, then notify the new branch.
    if (m_focused_node != nullptr) {
        UiEvent event = UiEvent::make(EventType::FocusLost);
        dispatch_to(*m_focused_node, event);
    }

    if (node != nullptr && !is_input_target(node)) {
        set_input_target(m_focused_node, nullptr, &InputState::focused);
        return false;
    }

    set_input_target(m_focused_node, node, &InputState::focused);

    if (node != nullptr) {
        UiEvent event = UiEvent::make(EventType::FocusGained);
        dispatch_to(*node, event);
    }

    return true;
}

void InputRouter::restore_focus(Node& subtree) {
    if (!subtree.contains(m_focused_node)) return;

    for (Node* ancestor = subtree.parent(); ancestor != nullptr; ancestor = ancestor->parent()) {
        if (ancestor->m_input_mode == InputMode::Blocker && is_input_target(ancestor)) {
            set_focus(ancestor);
            return;
        }
    }

    set_focus(nullptr);
}

bool InputRouter::dispatch(UiEvent& event) {
    clear_inactive_targets();

    if (m_debug_inspect_mode) {
        event.stop_propagation();
        return true;
    }

    if (m_on_event) m_on_event(event);

    if (contains(EventMask::Keyboard, event_mask(event.type))) {
        return event.propagation_stopped ? event.handled : dispatch_keyboard(event);
    }

    return contains(EventMask::Pointer, event_mask(event.type)) ? dispatch_pointer(event) : event.handled;
}

bool InputRouter::dispatch_pointer(UiEvent& event) {
    if (event.type == EventType::PointerUp) return dispatch_release(event);

    const int button_index = pointer_button_index(event.button);

    // keep the targets reached by this press and carry its native blocking and click prevention into release.
    if (event.type == EventType::PointerDown && button_index >= 0) {
        PressedPointer& pressed = m_pressed[button_index];
        pressed = {};
        pressed.down = true;

        route_pointer(event, &pressed.targets);

        if (event.default_prevented) pressed.targets.clear();
        pressed.native_input_blocked = event.native_input_blocked;
    } else {
        route_pointer(event);
    }

    return event.handled;
}

bool InputRouter::dispatch_release(UiEvent& event) {
    // take the press before callbacks can hide, detach, or release its targets.
    const int button_index = pointer_button_index(event.button);
    PressedPointer pressed;
    if (button_index >= 0) pressed = std::exchange(m_pressed[button_index], {});
    event.native_input_blocked |= pressed.native_input_blocked;

    const bool route_release = !event.propagation_stopped;
    Node* captured = m_pointer_capture;
    std::vector<Node*> released;
    route_pointer(event, &released);

    const bool released_all = !std::ranges::contains(m_pressed, true, &PressedPointer::down);

    // preserve capture transferred by the release handler. an intercepted release cancels the unfinished interaction.
    if (m_pointer_capture == captured) {
        if (!route_release) {
            cancel_capture();
        } else if (released_all) {
            release_pointer();
        }
    }

    if (released_all || !route_release) set_input_target(m_active_node, nullptr, &InputState::active);

    return route_release ? dispatch_click(event, pressed, released) : event.handled;
}

bool InputRouter::dispatch_click(UiEvent& release, PressedPointer& pressed, const std::vector<Node*>& released) {
    if (pressed.targets.empty() || release.default_prevented ||
        (release.button != PointerButton::Left && release.button != PointerButton::Right)) {
        return release.handled;
    }

    // intersect pressed and released targets before delivering a click through the same pointer route.
    std::erase_if(pressed.targets, [&released](Node* node) { return !std::ranges::contains(released, node); });

    if (pressed.targets.empty()) return release.handled;

    UiEvent click = UiEvent::make(release.button == PointerButton::Left ? EventType::Click : EventType::ContextClick);
    click.position = release.position;
    click.button = release.button;
    click.native_input_blocked = release.native_input_blocked;

    if (m_on_event) m_on_event(click);
    route_pointer(click, nullptr, pressed.targets);

    release.native_input_blocked |= click.native_input_blocked;
    return release.handled || click.handled;
}

void InputRouter::dispatch_branch(Node& target, UiEvent& event, std::vector<Node*>& visited, Node* scope) {
    event.target = &target;

    for (Node* current = &target; current != nullptr && !event.propagation_stopped; current = current->parent()) {
        if ((scope != nullptr && !scope->contains(current)) || !is_input_target(current)) break;
        if (std::ranges::contains(visited, current)) break;

        visited.push_back(current);
        current->dispatch_event(event);
    }
}

bool InputRouter::dispatch_keyboard(UiEvent& event) {
    // snapshot attachment order before handlers can mutate the tree. focus receives the event first.
    const std::vector<Node*> nodes = m_attached_nodes;
    std::vector<Node*> visited;
    Node* scope = nullptr;

    for (auto it = nodes.rbegin(); it != nodes.rend(); ++it) {
        if ((*it)->m_input_mode == InputMode::Blocker && is_input_target(*it)) {
            scope = *it;
            break;
        }
    }

    // preserve native forwarding when a focused descendant restores modal focus while handling this key.
    const bool native_focus_in_scope = scope != nullptr && m_focused_node != scope && scope->contains(m_focused_node);
    if (m_focused_node != nullptr && (scope == nullptr || scope->contains(m_focused_node))) {
        dispatch_branch(*m_focused_node, event, visited, scope);
    }

    for (auto it = nodes.rbegin(); it != nodes.rend() && !event.propagation_stopped; ++it) {
        if ((*it)->m_input_router != this) continue;
        dispatch_branch(**it, event, visited, scope);
    }

    if (scope != nullptr) {
        if (!native_focus_in_scope) event.block_native_input();
        event.mark_handled();
    }

    return event.handled;
}

void InputRouter::route_pointer(UiEvent& event, std::vector<Node*>* reached, const std::vector<Node*>& eligible) {
    if (event.propagation_stopped) return;

    if (event.type == EventType::PointerMove && m_pointer_capture != nullptr) {
        dispatch_to(*m_pointer_capture, event);
        return;
    }

    // snapshot before callbacks can open another layer. newly registered targets wait for the next event.
    auto route = m_hit_test.route_at(event.position, event.type);
    Node* scope = route.blocker ? route.blocker->node : nullptr;
    Node* front = route.targets.empty() ? scope : route.targets.front().node;

    // release focus before dispatch so the pressed widget can claim it. an ownerless blocker absorbs the press instead.
    if (event.type == EventType::PointerDown && m_focused_node != nullptr && !m_focused_node->contains(front) &&
        (!route.blocker || scope != nullptr)) {
        set_focus(nullptr);
    }

    if (event.type != EventType::Scroll && event.type != EventType::Click && event.type != EventType::ContextClick) {
        set_hovered(route);
    }

    if (event.type == EventType::PointerDown) {
        set_input_target(m_active_node, m_pointer_capture != nullptr ? m_pointer_capture : front, &InputState::active);
    }

    if (m_pointer_capture != nullptr && (event.type == EventType::PointerDown || event.type == EventType::PointerUp)) {
        Node* captured = m_pointer_capture;
        if (event.type == EventType::PointerDown || !captured->contains(front)) {
            if (reached != nullptr && (event.type == EventType::PointerDown || front == captured)) reached->push_back(captured);
            dispatch_to(*captured, event);
            return;
        }

        // release reaches hit descendants before bubbling to capture. unrelated overlapping branches stay excluded.
        std::erase_if(route.targets, [captured](const auto& entry) { return !captured->contains(entry.node); });
    }

    dispatch_targets(event, route, reached, eligible);
}

void InputRouter::dispatch_targets(
    UiEvent& event, const HitTestIndex::Route& route, std::vector<Node*>* reached, const std::vector<Node*>& eligible
) {
    Node* scope = route.blocker ? route.blocker->node : nullptr;
    std::vector<Node*> visited;

    // no descendant was hit. deliver only to the blocker and suppress native input.
    if (route.blocker && route.targets.empty()) {
        event.block_native_input();
        event.target = scope;
        if (route.blocker->callback) (*route.blocker->callback)(event);
        if (scope != nullptr) dispatch_branch(*scope, event, visited, scope);
        event.stop_propagation();
        return;
    }

    const auto still_attached = [this](Node* node) { return node->m_input_router == this && is_input_target(node); };

    for (const auto& entry : route.targets) {
        if (event.propagation_stopped) break;

        if (!still_attached(entry.node)) continue;
        if (std::ranges::contains(visited, entry.node)) continue;
        if (!eligible.empty() && !std::ranges::contains(eligible, entry.node)) continue;

        event.target = entry.node;
        if (entry.callback) {
            (*entry.callback)(event);

            // callbacks can detach or disable the target before bubbling.
            if (!still_attached(entry.node)) continue;
        }

        if (reached != nullptr) reached->push_back(entry.node);
        dispatch_branch(*entry.node, event, visited, scope);
    }

    // descendants keep native input, but propagation cannot escape the blocker.
    if (route.blocker) event.stop_propagation();
}

bool InputRouter::dispatch_to(Node& target, UiEvent& event) {
    event.target = &target;

    for (Node* current = &target; current != nullptr && !event.propagation_stopped; current = current->parent()) {
        if (current->m_removal_pending) {
            break;
        }

        current->dispatch_event(event);
    }

    return event.handled;
}

Node* InputRouter::node_at(ImVec2 position, EventType type) const {
    const auto route = m_hit_test.route_at(position, type);
    if (!route.targets.empty()) return route.targets.front().node;

    return route.blocker ? route.blocker->node : nullptr;
}

InputRouterStats InputRouter::stats() const {
    return {.entry_count = m_hit_test.size(), .entry_checks = m_hit_test.checks()};
}

void InputRouter::clear_inactive_targets() {
    if (!is_input_target(m_focused_node)) {
        set_input_target(m_focused_node, nullptr, &InputState::focused);
    }

    if (!is_input_target(m_pointer_capture)) {
        cancel_capture();
    }

    std::erase_if(m_hovered_nodes, [this](Node* node) {
        if (is_input_target(node)) return false;

        node->set_input_state(set_input_flag(node->input_state(), &InputState::hovered, false));
        return true;
    });

    if (m_hovered_nodes.empty()) set_hovered({});
    if (!is_input_target(m_active_node)) set_input_target(m_active_node, nullptr, &InputState::active);

    for (PressedPointer& pressed : m_pressed) {
        std::erase_if(pressed.targets, [](Node* node) { return !is_input_target(node); });
    }
}
