#include <catch2/catch_test_macros.hpp>
#include <imgui_internal.h>

#include <arbor/input/router.hpp>
#include <arbor/layout/layer-container.hpp>
#include <arbor/surface.hpp>
#include <arbor/widgets/widget.hpp>
#include <arbor/widgets/button.hpp>
#include "imgui-context.hpp"

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

using namespace ui;

static UiEvent event_of(EventType type, ImVec2 position = {}, PointerButton button = PointerButton::Left) {
    UiEvent event = UiEvent::make(type);
    event.position = position;
    event.button = button;
    return event;
}

static UiEvent click_event(ImVec2 position = {}) {
    return event_of(EventType::Click, position);
}

class EventNode final : public Node {
public:
    explicit EventNode(std::string node_id, std::vector<std::string>& events) : Node(std::move(node_id)), m_events(events) {}

    bool handle_events = false;
    bool stop_events = false;

private:
    void event(UiEvent& event) override {
        m_events.push_back(id());
        if (stop_events) {
            event.stop_propagation();
        } else if (handle_events) {
            event.mark_handled();
        }
    }

    std::vector<std::string>& m_events;
};

class EventWidget final : public Widget {
public:
    explicit EventWidget(std::vector<std::string>& events) : Widget("widget"), m_events(events) {}

private:
    void event(UiEvent&) override {
        m_events.push_back("internal");
    }

    std::vector<std::string>& m_events;
};

class PointerCaptureNode final : public Node {
public:
    PointerCaptureNode(InputRouter& router, std::vector<EventType>& events) : Node("drag"), m_router(router), m_events(events) {}

private:
    void event(UiEvent& event) override {
        m_events.push_back(event.type);
        if (event.type == EventType::Cancel) REQUIRE(event.target == this);
        if (event.type == EventType::PointerDown) {
            REQUIRE(m_router.capture_pointer(*this));
        }
        event.mark_handled();
    }

    InputRouter& m_router;
    std::vector<EventType>& m_events;
};

class PointerEventNode final : public Node {
public:
    PointerEventNode(std::string node_id, std::vector<EventType>& events) : Node(std::move(node_id)), m_events(events) {}

private:
    void event(UiEvent& event) override {
        m_events.push_back(event.type);
        event.mark_handled();
    }

    std::vector<EventType>& m_events;
};

TEST_CASE("input router callbacks run before node routing") {
    std::vector<std::string> events;
    EventNode parent("parent", events);
    auto& child = parent.add<EventNode>("child", events);
    InputRouter router;
    router.register_target(child, {{0.0F, 0.0F}, {10.0F, 10.0F}});
    REQUIRE(router.set_focus(&child));
    events.clear();

    bool stop = false;
    router.on_event([&](UiEvent& event) {
        events.push_back("router");
        if (stop) {
            event.stop_propagation();
        } else {
            event.mark_handled();
        }
    });

    SECTION("keyboard and pointer events") {
        for (EventType type : {EventType::KeyDown, EventType::PointerDown}) {
            events.clear();
            auto event = event_of(type, {5.0F, 5.0F});
            REQUIRE(router.dispatch(event));
            REQUIRE(events == std::vector<std::string>{"router", "child", "parent"});
        }
    }

    SECTION("stopped keyboard and pointer events") {
        stop = true;
        for (EventType type : {EventType::KeyDown, EventType::PointerDown}) {
            events.clear();
            auto event = event_of(type, {5.0F, 5.0F});
            REQUIRE(router.dispatch(event));
            REQUIRE(events == std::vector<std::string>{"router"});
        }
    }
}

TEST_CASE("input router callbacks persist without nodes and stay local to their router") {
    InputRouter router;
    InputRouter other;
    int observed = 0;
    router.on_event([count = 0, &observed](UiEvent& event) mutable {
        observed = ++count;
        event.mark_handled();
    });

    for (int expected : {1, 2}) {
        router.begin_frame();
        auto key = event_of(EventType::KeyDown);
        REQUIRE(router.dispatch(key));
        REQUIRE(observed == expected);

        key = event_of(EventType::KeyDown);
        REQUIRE_FALSE(other.dispatch(key));
        REQUIRE(observed == expected);
    }

    router.on_event([&](UiEvent&) { observed = 10; });
    auto move = event_of(EventType::PointerMove);
    REQUIRE_FALSE(router.dispatch(move));
    REQUIRE(observed == 10);

    router.on_event({});
    move = event_of(EventType::PointerMove);
    REQUIRE_FALSE(router.dispatch(move));
    REQUIRE(observed == 10);
}

TEST_CASE("input router callbacks can intercept synthesized clicks and captured releases") {
    InputRouter router;
    std::vector<EventType> events;
    PointerCaptureNode node(router, events);
    router.register_target(node, {{0.0F, 0.0F}, {10.0F, 10.0F}});
    std::vector<EventType> intercepted;
    EventType stopped = EventType::Click;
    router.on_event([&](UiEvent& event) {
        intercepted.push_back(event.type);
        if (event.type == EventType::PointerDown) event.block_native_input();
        if (event.type == stopped) event.stop_propagation();
    });

    SECTION("synthesized click") {
        auto down = event_of(EventType::PointerDown, {5.0F, 5.0F});
        router.dispatch(down);
        auto up = event_of(EventType::PointerUp, {5.0F, 5.0F});
        REQUIRE(router.dispatch(up));
        REQUIRE(intercepted == std::vector<EventType>{EventType::PointerDown, EventType::PointerUp, EventType::Click});
        REQUIRE(events == std::vector<EventType>{EventType::PointerDown, EventType::PointerUp});
    }

    SECTION("stopped release clears capture and press state") {
        stopped = EventType::PointerUp;
        auto down = event_of(EventType::PointerDown, {5.0F, 5.0F});
        router.dispatch(down);
        auto up = event_of(EventType::PointerUp, {5.0F, 5.0F});
        REQUIRE(router.dispatch(up));
        REQUIRE(up.native_input_blocked);
        REQUIRE_FALSE(node.input_state().active);
        REQUIRE(intercepted == std::vector<EventType>{EventType::PointerDown, EventType::PointerUp});

        auto move = event_of(EventType::PointerMove, {50.0F, 50.0F});
        REQUIRE_FALSE(router.dispatch(move));
        stopped = EventType::Cancel;
        up = event_of(EventType::PointerUp, {5.0F, 5.0F});
        router.dispatch(up);
        REQUIRE(events == std::vector<EventType>{EventType::PointerDown, EventType::Cancel, EventType::PointerUp});
    }

    SECTION("stopped press does not synthesize a click on release") {
        stopped = EventType::PointerDown;
        auto down = event_of(EventType::PointerDown, {5.0F, 5.0F});
        REQUIRE(router.dispatch(down));
        auto up = event_of(EventType::PointerUp, {5.0F, 5.0F});
        REQUIRE(router.dispatch(up));
        REQUIRE(up.native_input_blocked);
        REQUIRE(intercepted == std::vector<EventType>{EventType::PointerDown, EventType::PointerUp});
        REQUIRE(events == std::vector<EventType>{EventType::PointerUp});
    }

    SECTION("stopped move does not reach the captured node") {
        stopped = EventType::PointerMove;
        auto down = event_of(EventType::PointerDown, {5.0F, 5.0F});
        router.dispatch(down);

        auto move = event_of(EventType::PointerMove, {50.0F, 50.0F});
        REQUIRE(router.dispatch(move));
        REQUIRE(events == std::vector<EventType>{EventType::PointerDown});

        auto up = event_of(EventType::PointerUp, {5.0F, 5.0F});
        router.dispatch(up);
        REQUIRE(events == std::vector<EventType>{EventType::PointerDown, EventType::PointerUp, EventType::Click});
        REQUIRE_FALSE(node.input_state().active);
    }
}

TEST_CASE("event handlers can remove their widget while bubbling") {
    ui_test::ImGuiContext context({100.0F, 100.0F});

    class SelfRemovingWidget final : public Widget {
    public:
        SelfRemovingWidget(Node& owner, int& events, int& destructions)
            : Widget("self-removing"), m_owner(owner), m_events(events), m_destructions(destructions) {}

        ~SelfRemovingWidget() override {
            ++m_destructions;
        }

    private:
        void event(UiEvent&) override {
            ++m_events;
            m_owner.remove(*this);
        }

        Node& m_owner;
        int& m_events;
        int& m_destructions;
    };

    Node owner("owner");
    int events = 0;
    int destructions = 0;
    auto& widget = owner.add<SelfRemovingWidget>(owner, events, destructions);
    InputRouter router;
    owner.set_input_router(&router);
    router.register_target(widget, {{0.0F, 0.0F}, {40.0F, 20.0F}});

    UiEvent event = click_event();
    REQUIRE_FALSE(router.dispatch(event));
    REQUIRE(events == 1);
    REQUIRE(widget.removal_pending());
    REQUIRE(destructions == 0);

    event = click_event();
    REQUIRE_FALSE(router.dispatch(event));
    REQUIRE(events == 1);

    owner.update(0.0F);
    REQUIRE(destructions == 1);
    REQUIRE(owner.children().empty());
}

TEST_CASE("pointer capture stays exclusive across frames until the final button release") {
    InputRouter router;
    std::vector<EventType> events;
    std::vector<EventType> other_events;
    PointerCaptureNode node(router, events);
    PointerEventNode other("other", other_events);
    router.register_target(node, {{0.0F, 0.0F}, {10.0F, 10.0F}});

    auto down = event_of(EventType::PointerDown, {5.0F, 5.0F});
    router.dispatch(down);

    router.begin_frame();
    router.register_target(other, {{20.0F, 20.0F}, {30.0F, 30.0F}});
    down = event_of(EventType::PointerDown, {25.0F, 25.0F}, PointerButton::Right);
    router.dispatch(down);

    auto up = event_of(EventType::PointerUp, {25.0F, 25.0F}, PointerButton::Right);
    router.dispatch(up);
    REQUIRE(node.input_state().active);

    auto move = event_of(EventType::PointerMove, {100.0F, 100.0F});
    REQUIRE(router.dispatch(move));
    REQUIRE(events.back() == EventType::PointerMove);

    up = event_of(EventType::PointerUp, {25.0F, 25.0F});
    router.dispatch(up);
    REQUIRE_FALSE(node.input_state().active);
    move = event_of(EventType::PointerMove, {100.0F, 100.0F});
    REQUIRE_FALSE(router.dispatch(move));

    REQUIRE(other_events.empty());
    REQUIRE(
        events ==
        std::vector<EventType>{
            EventType::PointerDown, EventType::PointerDown, EventType::PointerUp, EventType::PointerMove, EventType::PointerUp
        }
    );
}

TEST_CASE("ancestor capture preserves child release and click unless dragging prevents the click") {
    InputRouter router;
    Widget owner("carousel");
    auto& card = owner.add<Widget>("card");
    Widget background("background");
    std::vector<EventType> events;
    int releases = 0;
    int background_releases = 0;
    bool dragging = false;

    owner.on_event(EventType::PointerDown, [&](UiEvent&) { REQUIRE(router.capture_pointer(owner)); });
    owner.on_event(EventType::PointerUp, [&](UiEvent& event) {
        ++releases;
        router.release_pointer();
        if (dragging) event.prevent_default();
    });
    card.on_event([&](UiEvent& event) { events.push_back(event.type); });
    background.on_event(EventType::PointerUp, [&](UiEvent&) { ++background_releases; });

    SECTION("click without dragging") {
        dragging = false;
    }
    SECTION("drag suppresses the click") {
        dragging = true;
    }

    const Rect bounds = {{0.0F, 0.0F}, {40.0F, 40.0F}};
    router.register_target(background, bounds);
    router.register_target(card, bounds);
    auto down = event_of(EventType::PointerDown, {5.0F, 5.0F});
    router.dispatch(down);

    router.begin_frame();
    router.register_target(background, bounds);
    router.register_target(card, bounds);
    auto up = event_of(EventType::PointerUp, {5.0F, 5.0F});
    router.dispatch(up);

    std::vector<EventType> expected = {EventType::PointerDown, EventType::PointerUp};
    if (!dragging) expected.push_back(EventType::Click);
    REQUIRE(events == expected);
    REQUIRE(releases == 1);
    REQUIRE(background_releases == 0);
}

TEST_CASE("capture transferred by a release handler remains active") {
    InputRouter router;
    std::vector<EventType> events;
    PointerEventNode next("next", events);
    Widget owner("owner");
    router.register_target(owner, {{0.0F, 0.0F}, {10.0F, 10.0F}});
    next.set_input_router(&router);
    REQUIRE(router.capture_pointer(owner));
    owner.on_event([&](UiEvent& event) {
        if (event.type == EventType::PointerUp) REQUIRE(router.capture_pointer(next));
    });

    auto up = event_of(EventType::PointerUp, {100.0F, 100.0F});
    router.dispatch(up);
    auto move = event_of(EventType::PointerMove, {100.0F, 100.0F});
    REQUIRE(router.dispatch(move));
    REQUIRE(events == std::vector<EventType>{EventType::PointerMove});
}

TEST_CASE("input router synthesizes clicks from matching pointer presses") {
    std::vector<EventType> events;
    PointerEventNode node("click", events);
    InputRouter router;
    router.register_target(node, {{0.0F, 0.0F}, {10.0F, 10.0F}});

    auto left_down = event_of(EventType::PointerDown, {5.0F, 5.0F});
    left_down.button = PointerButton::Left;
    REQUIRE(router.dispatch(left_down));

    auto left_up = event_of(EventType::PointerUp, {5.0F, 5.0F});
    left_up.button = PointerButton::Left;
    REQUIRE(router.dispatch(left_up));
    REQUIRE(events == std::vector<EventType>{EventType::PointerDown, EventType::PointerUp, EventType::Click});

    events.clear();
    auto right_down = event_of(EventType::PointerDown, {5.0F, 5.0F});
    right_down.button = PointerButton::Right;
    REQUIRE(router.dispatch(right_down));

    auto right_up = event_of(EventType::PointerUp, {5.0F, 5.0F});
    right_up.button = PointerButton::Right;
    REQUIRE(router.dispatch(right_up));
    REQUIRE(events == std::vector<EventType>{EventType::PointerDown, EventType::PointerUp, EventType::ContextClick});

    events.clear();
    auto drag_down = event_of(EventType::PointerDown, {5.0F, 5.0F});
    drag_down.button = PointerButton::Left;
    REQUIRE(router.dispatch(drag_down));

    auto drag_up = event_of(EventType::PointerUp, {20.0F, 20.0F});
    drag_up.button = PointerButton::Left;
    REQUIRE_FALSE(router.dispatch(drag_up));
    REQUIRE(events == std::vector<EventType>{EventType::PointerDown});
}

TEST_CASE("input blocker consumes only its selected event mask") {
    std::vector<EventType> events;
    PointerEventNode target("target", events);
    InputRouter router;
    int target_events = 0;
    router.register_target(target, {{0.0F, 0.0F}, {100.0F, 100.0F}}, [&target_events](UiEvent&) { ++target_events; });
    int blocked_events = 0;
    router.register_blocker(
        {{25.0F, 25.0F}, {75.0F, 75.0F}}, [&blocked_events](UiEvent&) { ++blocked_events; }, EventMask::PointerDown
    );

    auto move = event_of(EventType::PointerMove, {50.0F, 50.0F});
    REQUIRE(router.dispatch(move));
    REQUIRE(events == std::vector<EventType>{EventType::PointerMove});
    REQUIRE(target_events == 1);

    auto down = event_of(EventType::PointerDown, {50.0F, 50.0F});
    REQUIRE(router.dispatch(down));
    REQUIRE(events == std::vector<EventType>{EventType::PointerMove});
    REQUIRE(blocked_events == 1);
    REQUIRE(target_events == 1);
}

TEST_CASE("owner-scoped blockers leave their descendants interactive") {
    std::vector<EventType> events;
    Node owner("overlay");
    owner.set_input_mode(InputMode::Blocker);
    auto* child_ptr = &owner.add<PointerEventNode>("child", events);

    InputRouter router;
    router.register_target(*child_ptr, {{0.0F, 0.0F}, {100.0F, 100.0F}});
    router.register_blocker(owner, {{0.0F, 0.0F}, {100.0F, 100.0F}});

    auto move = event_of(EventType::PointerMove, {50.0F, 50.0F});
    REQUIRE(router.dispatch(move));
    REQUIRE(events == std::vector<EventType>{EventType::PointerMove});
    REQUIRE(child_ptr->input_state().hovered);
    REQUIRE(owner.subtree_input_state().hovered);

    auto down = event_of(EventType::PointerDown, {50.0F, 50.0F});
    REQUIRE(router.dispatch(down));
    REQUIRE(child_ptr->input_state().active);
    REQUIRE(owner.subtree_input_state().active);

    REQUIRE(owner.remove(*child_ptr));
    REQUIRE_FALSE(owner.subtree_input_state().hovered);
    REQUIRE_FALSE(owner.subtree_input_state().active);
}

TEST_CASE("owner-scoped blockers receive hover and active state") {
    Node owner("overlay");
    InputRouter router;
    router.register_blocker(owner, {{0.0F, 0.0F}, {100.0F, 100.0F}});

    UiEvent move = event_of(EventType::PointerMove, {50.0F, 50.0F});
    REQUIRE(router.dispatch(move));
    REQUIRE(owner.input_state().hovered);

    UiEvent down = event_of(EventType::PointerDown, {50.0F, 50.0F});
    REQUIRE(router.dispatch(down));
    REQUIRE(owner.input_state().active);

    UiEvent up = event_of(EventType::PointerUp, {50.0F, 50.0F});
    REQUIRE(router.dispatch(up));
    REQUIRE_FALSE(owner.input_state().active);
}

TEST_CASE("input router restores focus to a blocker ancestor") {
    Node modal("modal");
    modal.set_input_mode(InputMode::Blocker);
    Node& panel = modal.add<Node>("panel");
    Node* input_ptr = &panel.add<Node>("input");

    InputRouter router;
    REQUIRE(router.set_focus(input_ptr));
    router.restore_focus(*input_ptr);
    REQUIRE(router.focused_node() == &modal);
}

TEST_CASE("input router invalidates inactive focus and pointer capture") {
    std::vector<std::string> events;
    EventNode node("input", events);
    node.handle_events = true;

    InputRouter router;
    REQUIRE(router.set_focus(&node));
    events.clear();

    node.set_visible(false);
    auto hidden_key = event_of(EventType::KeyDown);
    REQUIRE_FALSE(router.dispatch(hidden_key));
    REQUIRE(router.focused_node() == nullptr);
    REQUIRE(events.empty());

    node.set_visible(true);
    REQUIRE(router.set_focus(&node));
    events.clear();

    node.set_input_enabled(false);
    auto disabled_key = event_of(EventType::KeyDown);
    REQUIRE_FALSE(router.dispatch(disabled_key));
    REQUIRE(router.focused_node() == nullptr);
    REQUIRE(events.empty());

    node.set_input_enabled(true);
    REQUIRE(router.capture_pointer(node));
    events.clear();

    node.set_input_enabled(false);
    auto disabled_move = event_of(EventType::PointerMove, {100.0F, 100.0F});
    REQUIRE_FALSE(router.dispatch(disabled_move));
    REQUIRE(events == std::vector<std::string>{"input"});

    SECTION("focus-loss handler disables the destination") {
        Widget previous("previous");
        Node next("next");
        REQUIRE(router.set_focus(&previous));
        previous.on_event([&](UiEvent& event) {
            if (event.type == EventType::FocusLost) next.set_input_enabled(false);
        });
        REQUIRE_FALSE(router.set_focus(&next));
        REQUIRE(router.focused_node() == nullptr);
        REQUIRE_FALSE(next.input_state().focused);
    }
}

TEST_CASE("pointer presses outside a focused node clear focus") {
    std::vector<std::string> events;
    EventNode focused("focused", events);
    EventNode other("other", events);
    focused.handle_events = true;
    other.handle_events = true;

    InputRouter router;
    router.register_target(focused, {{0.0F, 0.0F}, {40.0F, 40.0F}});
    router.register_target(other, {{60.0F, 0.0F}, {100.0F, 40.0F}});
    REQUIRE(router.set_focus(&focused));
    events.clear();

    auto down = event_of(EventType::PointerDown, {80.0F, 20.0F});
    REQUIRE(router.dispatch(down));
    REQUIRE(router.focused_node() == nullptr);

    // focus loss is delivered before the press reaches the new target.
    REQUIRE(events == std::vector<std::string>{"focused", "other"});

    REQUIRE(router.set_focus(&focused));
    auto empty_down = event_of(EventType::PointerDown, {150.0F, 20.0F});
    router.dispatch(empty_down);
    REQUIRE(router.focused_node() == nullptr);
}

TEST_CASE("input router clears targets when a node is detached") {
    std::vector<std::string> events;
    Node parent("parent");
    EventNode* child_ptr = &parent.add<EventNode>("child", events);
    child_ptr->handle_events = true;

    InputRouter router;
    parent.set_input_router(&router);
    REQUIRE(router.set_focus(child_ptr));
    REQUIRE(router.capture_pointer(*child_ptr));
    router.register_target(*child_ptr, {{0.0F, 0.0F}, {10.0F, 10.0F}});
    events.clear();

    auto detached = parent.detach(*child_ptr);
    REQUIRE(detached != nullptr);
    events.clear();

    auto key = event_of(EventType::KeyDown);
    REQUIRE_FALSE(router.dispatch(key));
    REQUIRE(router.focused_node() == nullptr);
    REQUIRE(router.node_at({5.0F, 5.0F}) == nullptr);
    REQUIRE(events.empty());

    auto move = event_of(EventType::PointerMove, {100.0F, 100.0F});
    REQUIRE_FALSE(router.dispatch(move));
    REQUIRE(events.empty());
}

TEST_CASE("interrupted pointer capture notifies its owner but normal release does not") {
    InputRouter router;
    std::vector<EventType> events;
    PointerCaptureNode owner(router, events);
    Node other("other");
    owner.set_input_router(&router);
    other.set_input_router(&router);

    // hiding and disabling the owner interrupt a drag without a pointer release.
    REQUIRE(router.capture_pointer(owner));
    owner.set_visible(false);
    REQUIRE(events == std::vector<EventType>{EventType::Cancel});

    events.clear();
    owner.set_visible(true);
    REQUIRE(router.capture_pointer(owner));
    owner.set_input_enabled(false);
    REQUIRE(events == std::vector<EventType>{EventType::Cancel});

    // transferring capture cancels the previous owner once. normal release ends capture without cancellation.
    events.clear();
    owner.set_input_enabled(true);
    REQUIRE(router.capture_pointer(owner));
    REQUIRE(router.capture_pointer(owner));
    REQUIRE(events.empty());
    REQUIRE(router.capture_pointer(other));
    REQUIRE(events == std::vector<EventType>{EventType::Cancel});

    events.clear();
    REQUIRE(router.capture_pointer(owner));
    router.release_pointer();
    REQUIRE(events.empty());
}

TEST_CASE("input routers isolate focus and pointer capture between surfaces") {
    std::vector<std::string> events;
    EventNode surface_a_node("surface-a", events);
    EventNode surface_b_node("surface-b", events);
    surface_a_node.handle_events = true;
    surface_b_node.handle_events = true;

    InputRouter surface_a_router;
    InputRouter surface_b_router;
    REQUIRE(surface_a_router.set_focus(&surface_a_node));
    REQUIRE(surface_b_router.set_focus(&surface_b_node));
    REQUIRE(surface_a_router.capture_pointer(surface_a_node));
    REQUIRE(surface_b_router.capture_pointer(surface_b_node));

    events.clear();
    auto surface_a_key = event_of(EventType::KeyDown);
    auto surface_b_key = event_of(EventType::KeyDown);
    REQUIRE(surface_a_router.dispatch(surface_a_key));
    REQUIRE(surface_b_router.dispatch(surface_b_key));
    REQUIRE(events == std::vector<std::string>{"surface-a", "surface-b"});

    surface_a_router.set_focus(nullptr);
    surface_a_router.release_pointer();
    REQUIRE(surface_a_router.focused_node() == nullptr);
    REQUIRE(surface_b_router.focused_node() == &surface_b_node);

    auto surface_b_move = event_of(EventType::PointerMove, {100.0F, 100.0F});
    REQUIRE(surface_b_router.dispatch(surface_b_move));
    REQUIRE(events.back() == "surface-b");
}

TEST_CASE("anonymous blockers consume motion wheel and unmatched releases") {
    InputRouter router;
    router.register_blocker({{0.0F, 0.0F}, {200.0F, 200.0F}});

    for (EventType type : {EventType::PointerMove, EventType::Scroll, EventType::PointerUp}) {
        UiEvent event = event_of(type, {100.0F, 100.0F});
        REQUIRE(router.dispatch(event));
        REQUIRE(event.handled);
        REQUIRE(event.native_input_blocked);
    }
}

TEST_CASE("blocking entries clear hover behind them") {
    InputRouter router;
    Node target("target");
    router.register_target(target, {{0.0F, 0.0F}, {100.0F, 100.0F}});

    UiEvent move = event_of(EventType::PointerMove, {50.0F, 50.0F});
    REQUIRE_FALSE(router.dispatch(move));
    REQUIRE(target.input_state().hovered);

    router.register_blocker({{0.0F, 0.0F}, {100.0F, 100.0F}});
    move = event_of(EventType::PointerMove, {50.0F, 50.0F});
    REQUIRE(router.dispatch(move));
    REQUIRE_FALSE(target.input_state().hovered);
}

TEST_CASE("pointer blockers leave focused keyboard input available") {
    std::vector<std::string> events;
    EventNode content("content", events);
    content.handle_events = true;

    InputRouter router;
    REQUIRE(router.set_focus(&content));
    events.clear();

    router.register_target(content, {{0.0F, 0.0F}, {100.0F, 100.0F}});
    router.register_blocker({{0.0F, 0.0F}, {100.0F, 100.0F}});

    UiEvent down = event_of(EventType::PointerDown, {10.0F, 10.0F});
    REQUIRE(router.dispatch(down));
    REQUIRE(events.empty());
    REQUIRE(router.focused_node() == &content);

    UiEvent key = event_of(EventType::KeyDown);
    REQUIRE(router.dispatch(key));
    REQUIRE(events == std::vector<std::string>{"content"});
}

TEST_CASE("input router resolves overlapping targets by paint order and ancestry") {
    InputRouter router;
    Node bottom("bottom");
    Node top("top");

    router.begin_frame();
    router.register_target(bottom, {{0.0F, 0.0F}, {100.0F, 100.0F}});
    router.register_target(top, {{25.0F, 25.0F}, {75.0F, 75.0F}});

    REQUIRE(router.node_at({50.0F, 50.0F}) == &top);
    REQUIRE(router.node_at({10.0F, 10.0F}) == &bottom);
    REQUIRE(router.node_at({150.0F, 150.0F}) == nullptr);

    Node first("first");
    Node second("second");

    router.begin_frame();
    router.register_target(first, {{0.0F, 0.0F}, {20.0F, 20.0F}});
    router.register_target(second, {{0.0F, 0.0F}, {100.0F, 100.0F}});

    REQUIRE(router.node_at({10.0F, 10.0F}) == &second);

    Node parent("parent");
    Node* child_ptr = &parent.add<Node>("child");

    router.begin_frame();
    router.register_target(*child_ptr, {{0.0F, 0.0F}, {100.0F, 100.0F}});
    router.register_target(parent, {{0.0F, 0.0F}, {100.0F, 100.0F}});

    REQUIRE(router.node_at({50.0F, 50.0F}) == child_ptr);
}

TEST_CASE("specific input callbacks receive the same event after the general callback") {
    std::vector<std::string> events;
    EventWidget widget(events);
    widget.on_event([&events](UiEvent&) { events.push_back("general"); });

    const auto callback = [&events](UiEvent& event) {
        REQUIRE(event.target != nullptr);
        events.push_back("specific");
    };
    for (EventType type : {
             EventType::KeyDown,
             EventType::KeyUp,
             EventType::PointerDown,
             EventType::PointerUp,
             EventType::PointerMove,
             EventType::Scroll,
             EventType::Click,
             EventType::ContextClick,
             EventType::TextInput,
             EventType::FocusGained,
             EventType::FocusLost,
             EventType::Cancel,
         }) {
        widget.on_event(type, callback);
        events.clear();
        UiEvent event = event_of(type);
        REQUIRE_FALSE(InputRouter::dispatch_to(widget, event));
        REQUIRE(events == std::vector<std::string>{"internal", "general", "specific"});

        widget.on_event([](UiEvent& event) { event.stop_propagation(); });
        events.clear();
        event = event_of(type);
        REQUIRE(InputRouter::dispatch_to(widget, event));
        REQUIRE(events == std::vector<std::string>{"internal"});
        widget.on_event([&events](UiEvent&) { events.push_back("general"); });
    }
}

TEST_CASE("overlapping branches visit common ancestors once and stop explicitly") {
    std::vector<std::string> events;
    EventNode root("root", events);
    auto& bottom = root.add<EventNode>("bottom", events);
    auto& top = root.add<EventNode>("top", events);
    InputRouter router;
    router.register_target(bottom, {{0.0F, 0.0F}, {100.0F, 100.0F}});
    router.register_target(top, {{0.0F, 0.0F}, {100.0F, 100.0F}});

    UiEvent move = event_of(EventType::PointerMove, {50.0F, 50.0F});
    REQUIRE_FALSE(router.dispatch(move));
    REQUIRE(events == std::vector<std::string>{"top", "root", "bottom"});

    events.clear();
    top.stop_events = true;
    move = event_of(EventType::PointerMove, {50.0F, 50.0F});
    REQUIRE(router.dispatch(move));
    REQUIRE(events == std::vector<std::string>{"top"});
}

TEST_CASE("keyboard routing respects visibility focus and blocking subtrees") {
    std::vector<std::string> events;
    EventNode root("root", events);
    auto& focused = root.add<EventNode>("focused", events);
    root.add<EventNode>("other", events);
    auto& hidden = root.add<EventNode>("hidden", events);
    hidden.add<EventNode>("hidden-child", events);
    hidden.set_visible(false);
    auto& modal = root.add<EventNode>("modal", events);
    modal.set_input_mode(InputMode::Blocker);
    auto& field = modal.add<EventNode>("field", events);
    modal.set_visible(false);
    InputRouter router;
    root.set_input_router(&router);

    auto key = event_of(EventType::KeyDown);
    REQUIRE_FALSE(router.dispatch(key));
    REQUIRE(events == std::vector<std::string>{"other", "root", "focused"});

    router.set_focus(&focused);
    events.clear();
    key = event_of(EventType::KeyDown);
    router.dispatch(key);
    REQUIRE(events == std::vector<std::string>{"focused", "root", "other"});

    focused.stop_events = true;
    events.clear();
    key = event_of(EventType::KeyDown);
    router.dispatch(key);
    REQUIRE(events == std::vector<std::string>{"focused"});

    modal.set_visible(true);
    events.clear();
    key = event_of(EventType::KeyDown);
    REQUIRE(router.dispatch(key));
    REQUIRE(events == std::vector<std::string>{"field", "modal"});
    REQUIRE(key.native_input_blocked);

    router.set_focus(&field);
    events.clear();
    key = event_of(EventType::TextInput);
    REQUIRE(router.dispatch(key));
    REQUIRE(events == std::vector<std::string>{"field", "modal"});
    REQUIRE_FALSE(key.native_input_blocked);
}

TEST_CASE("dispatch snapshots tolerate registration removal and mutable callbacks") {
    std::vector<std::string> events;
    Node root("root");
    auto& bottom = root.add<EventNode>("bottom", events);
    auto& top = root.add<EventNode>("top", events);
    InputRouter router;
    root.set_input_router(&router);
    const Rect bounds = {{0.0F, 0.0F}, {100.0F, 100.0F}};
    std::unique_ptr<Node> detached;
    int observed = 0;

    SECTION("new targets wait for the next event") {
        router.register_target(bottom, bounds, [&](UiEvent&) { router.register_target(top, bounds); });
        auto move = event_of(EventType::PointerMove, {50.0F, 50.0F});
        router.dispatch(move);
        REQUIRE(events == std::vector<std::string>{"bottom"});

        events.clear();
        move = event_of(EventType::PointerMove, {50.0F, 50.0F});
        router.dispatch(move);
        REQUIRE(events == std::vector<std::string>{"top", "bottom"});
    }

    SECTION("detached targets leave the pending route") {
        router.register_target(bottom, bounds);
        router.register_target(top, bounds, [&](UiEvent&) { detached = root.detach(bottom); });
        auto down = event_of(EventType::PointerDown, {50.0F, 50.0F});
        router.dispatch(down);
        REQUIRE(detached != nullptr);
        REQUIRE(events == std::vector<std::string>{"top"});
    }

    SECTION("callback captures retain changes between events") {
        router.register_target(top, bounds, [count = 0, &observed](UiEvent&) mutable { observed = ++count; });
        for (int expected : {1, 2}) {
            auto move = event_of(EventType::PointerMove, {50.0F, 50.0F});
            router.dispatch(move);
            REQUIRE(observed == expected);
        }
    }
}

TEST_CASE("blockers suppress native scrollbar hover with a stationary pointer", "[Scrollbar][input][regression]") {
    class ScrollbarProbe : public Container {
    public:
        ScrollbarProbe() : Container("scrollbar-content") {}

        ImGuiWindow* window = nullptr;
        bool hover_color_drawn = false;

    protected:
        bool paint() override {
            const ImU32 hover_color = ImGui::GetColorU32(ImGuiCol_ScrollbarGrabHovered);
            const bool result = Container::paint();
            window = ImGui::GetCurrentWindow();
            const ImRect scrollbar = ImGui::GetWindowScrollbarRect(window, ImGuiAxis_Y);

            hover_color_drawn = false;

            // imgui can emit child-window decorations into the parent's draw list.
            for (const ImDrawList* draw_list : {window->DrawList, window->ParentWindow->DrawList}) {
                hover_color_drawn |=
                    std::any_of(draw_list->VtxBuffer.begin(), draw_list->VtxBuffer.end(), [&](const ImDrawVert& vertex) {
                        return vertex.col == hover_color && scrollbar.Contains(vertex.pos);
                    });
            }

            return result;
        }
    };

    for (bool modal : {false, true}) {
        DYNAMIC_SECTION((modal ? "modal backdrop" : "input blocker")) {
            Runtime runtime;
            Surface surface = ui_test::make_surface(runtime);
            Theme theme = surface.theme();
            theme.scrollbar.grab_color = rgb(0, 255, 0);
            theme.scrollbar.grab_hovered_color = rgb(255, 0, 255);
            surface.set_theme(theme);

            auto& content = surface.root().add<ScrollbarProbe>();
            content.set_size({px(180.0F), px(120.0F)});
            content.set_scrollable(true);
            content.add<Container>("long-content").set_size({grow(), px(480.0F)});

            auto& blocker = surface.root().add<LayerContainer>("scrollbar-blocker");
            blocker.set_input_mode(InputMode::Blocker);
            blocker.set_visible(false);
            if (modal) {
                auto& panel = blocker.add<Container>("modal-panel");
                panel.set_size({px(80.0F), px(50.0F)});
                panel.set_anchor(Anchor::Center);
            }

            const auto context = ui_test::prepare_surface(surface, {320.0F, 220.0F});

            // repeat draws without new motion events to exercise hover with an idle pointer.
            const auto draw_frames = [&] {
                for (int frame = 0; frame < 3; ++frame)
                    ui_test::draw_surface(surface);
            };
            draw_frames();
            REQUIRE(content.window != nullptr);
            REQUIRE(content.window->ScrollbarY);
            const ImRect scrollbar = ImGui::GetWindowScrollbarRect(content.window, ImGuiAxis_Y);
            const ImVec2 pointer = {scrollbar.GetCenter().x, scrollbar.Min.y + 8.0F};
            ImGui::GetIO().AddMousePosEvent(pointer.x, pointer.y);
            draw_frames();

            REQUIRE(content.hover_color_drawn);

            blocker.set_visible(true);
            draw_frames();
            CHECK_FALSE(content.input_state().hovered);
            CHECK(blocker.subtree_input_state().hovered);
            CHECK_FALSE(content.hover_color_drawn);

            blocker.set_visible(false);
            draw_frames();
            CHECK(content.hover_color_drawn);

            ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, true);
            draw_frames();
            REQUIRE(GImGui->ActiveId == ImGui::GetWindowScrollbarID(content.window, ImGuiAxis_Y));
            ImGui::GetIO().AddMousePosEvent(pointer.x, pointer.y + 40.0F);
            draw_frames();
            CHECK(content.window->Scroll.y > 0.0F);
            ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, false);
            draw_frames();
        }
    }
}

TEST_CASE("overlapping layers share hover and clicks unless explicitly blocking") {
    class TestLayer final : public LayerContainer {
    public:
        using LayerContainer::LayerContainer;

        int clicks = 0;
        int presses = 0;

    private:
        void event(UiEvent& event) override {
            if (event.type == EventType::Click) ++clicks;
        }

        void mouse_press_event(UiEvent& event) override {
            REQUIRE(event.target == this);
            ++presses;
        }
    };

    for (InputMode mode : {InputMode::Target, InputMode::Blocker}) {
        DYNAMIC_SECTION((mode == InputMode::Target ? "pass-through layer" : "blocking layer")) {
            Runtime runtime;
            Surface surface = ui_test::make_surface(runtime);
            auto& content = surface.root().add<Widget>("content");
            content.set_size({px(80.0F), px(40.0F)});
            auto& layer = surface.root().add<TestLayer>("layer");
            layer.set_size({px(80.0F), px(40.0F)});
            layer.set_anchor(Anchor::TopLeft);
            layer.set_input_mode(mode);
            int content_clicks = 0;
            content.on_event([&](UiEvent& event) {
                if (event.type == EventType::Click) ++content_clicks;
            });
            const auto context = ui_test::prepare_surface(surface, {200.0F, 100.0F});
            ui_test::draw_surface(surface);
            const ImVec2 position = ui_test::center(content.layout().visual_rect());
            auto move = event_of(EventType::PointerMove, position);
            surface.dispatch(move);
            REQUIRE(content.input_state().hovered == (mode == InputMode::Target));
            REQUIRE(layer.input_state().hovered);

            ImGui::GetIO().MousePos = position;
            ui_test::draw_surface(surface);
            REQUIRE(content.input_state().hovered == (mode == InputMode::Target));
            REQUIRE(layer.input_state().hovered);

            auto down = event_of(EventType::PointerDown, position);
            surface.dispatch(down);
            auto up = event_of(EventType::PointerUp, position);
            surface.dispatch(up);
            REQUIRE(layer.presses == 1);
            REQUIRE(layer.clicks == 1);
            REQUIRE(content_clicks == (mode == InputMode::Target ? 1 : 0));

            ImGui::GetIO().MousePos = {180.0F, 90.0F};
            move = event_of(EventType::PointerMove, ImGui::GetIO().MousePos);
            surface.dispatch(move);
            ui_test::draw_surface(surface);
            REQUIRE_FALSE(content.input_state().hovered);
            REQUIRE_FALSE(layer.input_state().hovered);
        }
    }
}
