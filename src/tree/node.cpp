#include <arbor/tree/node.hpp>

#include <arbor/diagnostics/profiler.hpp>
#include <arbor/input/router.hpp>
#include <arbor/surface.hpp>
#include <imgui_internal.h>
#include <algorithm>
#include <cstddef>
#include <stdexcept>
#include <utility>

using namespace ui;
static uint64_t next_node_id = 1;

template <typename NodeType>
static NodeType* find_node(NodeType& root, std::string_view id) {
    if (root.removal_pending()) {
        return nullptr;
    }

    if (root.id() == id) {
        return &root;
    }

    for (const auto& child : root.children()) {
        NodeType* result = find_node(*child, id);

        if (result != nullptr) {
            return result;
        }
    }

    return nullptr;
}

Node::Node(std::string id) : m_id(std::move(id)), m_identity(next_node_id++) {}

Node& Node::add(std::unique_ptr<Node> child) {
    if (child == nullptr) {
        throw std::invalid_argument("cannot add a null child");
    }

    Node& result = *child;

    // connect the parent and inherited services before applying the surface theme to the subtree.
    result.set_parent(this);
    result.set_input_router(m_input_router);
    if (m_surface != nullptr) result.apply_theme(m_surface->theme());

    m_children.emplace_back(std::move(child));
    refresh_input_state();
    invalidate_measure();
    return result;
}

Node& Node::set_size(LayoutSize size) {
    if (m_layout.size_spec() == size) return *this;
    request_size(size);
    return *this;
}

void Node::request_size(LayoutSize size) {
    apply_size_spec(size);
    invalidate_measure();
}

Node& Node::set_size(LayoutBounds minimum, LayoutBounds maximum) {
    return set_size({{minimum.width, maximum.width}, {minimum.height, maximum.height}});
}

Node& Node::set_layout(LayoutConfig config) {
    if (m_layout.config() == config) return *this;

    // size and flow changes remeasure ancestors. placement alone keeps the existing measurements.
    const bool measure_changed = m_layout.size_spec() != config.size || m_layout.in_flow() != config.in_flow;
    if (m_layout.size_spec() != config.size) request_size(config.size);
    m_layout.set_config(config);
    if (measure_changed) invalidate_measure();

    return *this;
}

Node& Node::set_anchor(ImVec2 anchor, ImVec2 origin) {
    LayoutConfig config = m_layout.config();
    config.placement.anchor = anchor;
    config.placement.origin = origin;
    config.in_flow = false;
    return set_layout(config);
}

Node::~Node() {
    if (m_input_router != nullptr) m_input_router->detach(*this);

    // children must not propagate input state into a parent's partially destroyed child list
    for (const auto& child : m_children) {
        child->m_parent = nullptr;
    }
}

void Node::set_input_state(InputState state) {
    if (m_input_state == state) {
        return;
    }

    m_input_state = state;
    refresh_input_state();
}

void Node::refresh_input_state() {
    // combine direct input with the children's aggregates, then pass the changed aggregate to the parent.
    InputState state = m_input_state;
    for (const auto& child : m_children) {
        if (child->m_removal_pending) continue;

        state.hovered |= child->m_subtree_input_state.hovered;
        state.active |= child->m_subtree_input_state.active;
    }

    if (state == m_subtree_input_state) return;

    m_subtree_input_state = state;
    // ancestor styles must be current before descendants resolve inherited properties.
    if (m_parent != nullptr) m_parent->refresh_input_state();
    input_state_changed();
}

Surface& Node::surface() const {
    if (m_surface == nullptr) {
        throw std::logic_error("node is not attached to a Surface surface");
    }

    return *m_surface;
}

EffectRegistry* Node::effect_registry() const {
    return m_surface == nullptr ? nullptr : &m_surface->effects();
}

PaintState& Node::paint_state() const {
    if (m_surface != nullptr) return m_surface->paint_state();
    if (m_parent != nullptr) return m_parent->paint_state();

    // detached trees share state at their root until attachment supplies the surface state.
    if (m_detached_paint_state == nullptr) m_detached_paint_state = std::make_unique<PaintState>();

    return *m_detached_paint_state;
}

void Node::set_input_enabled(bool enabled) {
    if (m_input_enabled == enabled) {
        return;
    }

    m_input_enabled = enabled;
    if (!enabled) clear_input_state();
}

void Node::set_enabled(bool enabled) {
    if (m_enabled == enabled) return;

    m_enabled = enabled;
    if (!enabled) clear_input_state();
    refresh_disabled_state();
}

void Node::refresh_disabled_state() {
    disabled_changed();
    for (const auto& child : m_children) {
        child->refresh_disabled_state();
    }
}

Node& Node::set_input_mode(InputMode mode, Rect area) {
    if (m_input_mode == mode && m_input_area.min.x == area.min.x && m_input_area.min.y == area.min.y &&
        m_input_area.max.x == area.max.x && m_input_area.max.y == area.max.y) {
        return *this;
    }

    if (m_input_router != nullptr) m_input_router->m_hit_test.erase(*this);

    m_input_area = area;
    m_input_mode = mode;
    return *this;
}

BoxInsets Node::layout_margin() const {
    return {};
}

void Node::dispatch_event(UiEvent& event) {
    this->event(event);

    if (removal_pending() || event.propagation_stopped) {
        return;
    }

    switch (event.type) {
        case EventType::KeyDown:
            key_press_event(event);
            break;
        case EventType::KeyUp:
            key_release_event(event);
            break;
        case EventType::PointerDown:
            mouse_press_event(event);
            break;
        case EventType::PointerUp:
            mouse_release_event(event);
            break;
        case EventType::PointerMove:
            mouse_move_event(event);
            break;
        case EventType::Scroll:
            wheel_event(event);
            break;
        default:
            break;
    }
}

void Node::resolve_position(bool at_cursor) {
    const ImVec2 size = m_layout.size();

    if (!has_draw_context()) {
        const Rect rect = Rect::from_position_size(m_layout.active_placement().offset, size);
        m_layout.set_arranged_rects(rect, rect);
        return;
    }

    // flow nodes keep imgui's cursor. arranged nodes resolve their anchor in the parent's content box.
    ImVec2 local_position = ImGui::GetCursorPos();
    if (m_layout.has_position() && !at_cursor) {
        const Rect parent_rect = m_layout.parent_content_rect();
        const Placement& placement = m_layout.active_placement();
        const ImVec2 anchor = placement.anchor;
        const ImVec2 origin = placement.origin;
        local_position = {
            parent_rect.min.x + (parent_rect.size().x * anchor.x) - (size.x * origin.x) + placement.offset.x,
            parent_rect.min.y + (parent_rect.size().y * anchor.y) - (size.y * origin.y) + placement.offset.y,
        };
        ImGui::SetCursorPos(local_position);
    }

    // store cursor-relative bounds for placement and screen bounds for paint and hit testing.
    m_layout.set_arranged_rects(
        Rect::from_position_size(local_position, size), Rect::from_position_size(ImGui::GetCursorScreenPos(), size)
    );
}

void Node::capture_parent_content() {
    if (!has_draw_context()) {
        m_layout.set_parent_content_rect({});
        return;
    }

    // add the window scroll to the cursor start so both content edges use the same local coordinates.
    const ImVec2 scroll = {ImGui::GetScrollX(), ImGui::GetScrollY()};
    const ImVec2 cursor = ImGui::GetCursorPos();
    const ImVec2 start = ImGui::GetCursorStartPos();
    const ImVec2 available = ImGui::GetContentRegionAvail();

    m_layout.set_parent_content_rect(
        {{start.x + scroll.x, start.y + scroll.y}, {cursor.x + available.x, cursor.y + available.y}}, available
    );
}

void Node::set_input_router(InputRouter* router) {
    if (m_input_router == router) return;

    clear_input_state();
    bind_input_router(router);
}

void Node::bind_input_router(InputRouter* router) {
    if (m_input_router == router) return;

    InputRouter* previous_router = m_input_router;
    if (previous_router != nullptr) std::erase(previous_router->m_attached_nodes, this);
    m_input_router = router;
    if (router != nullptr) router->m_attached_nodes.push_back(this);

    for (const auto& child : m_children) {
        if (child->m_input_router == previous_router) {
            child->bind_input_router(router);
        } else {
            child->set_input_router(router);
        }
    }
}

void Node::set_surface(Surface* surface) {
    m_surface = surface;
    surface_changed();
    invalidate_measure();
}

void Node::set_parent(Node* parent) {
    m_parent = parent;

    set_surface(parent != nullptr ? parent->m_surface : nullptr);
    disabled_changed();
    m_profiler = parent != nullptr ? parent->m_profiler : nullptr;

    for (const auto& child : m_children) {
        child->set_parent(this);
    }
}

void Node::detach_input_router(InputRouter& router) {
    if (m_input_router != &router) return;
    m_input_router = nullptr;
    m_input_state = {};
    m_subtree_input_state = {};
}

void Node::clear_input_state() {
    if (m_input_router != nullptr) m_input_router->clear_input_state(*this);
}

bool Node::has_draw_context() const {
    const ImGuiContext* context = ImGui::GetCurrentContext();
    return context != nullptr && context->CurrentWindow != nullptr &&
           (m_parent != nullptr || !context->CurrentWindow->IsFallbackWindow);
}

void Node::set_visible(bool visible) {
    if (m_visible == visible) {
        return;
    }

    m_visible = visible;
    if (!visible) clear_input_state();

    refresh_input_state();
    invalidate_measure();
}

void Node::set_profiler(Profiler* profiler) {
    if (m_profiler == profiler) return;

    m_profiler = profiler;
    for (const auto& child : m_children) {
        child->set_profiler(profiler);
    }
}

bool Node::has_parent_size() const {
    return m_layout.m_has_parent_size;
}

void Node::apply_size_spec(LayoutSize size) {
    m_layout.set_size(size);
}

void Node::assign_size(ImVec2 size) {
    m_layout.assign_size(size);
}

ImVec2 Node::resolve_size(ImVec2 available) const {
    const LayoutSize& spec = m_layout.size_spec();
    const ImVec2 allocation = m_layout.resolve_size(available);
    if (spec.width.mode != LayoutSizeMode::Fit && spec.height.mode != LayoutSizeMode::Fit) {
        return allocation;
    }

    const ImVec2 measured = measure_size(allocation);
    return m_layout.resolve_size(available, measured);
}

ImVec2 Node::measure_size(ImVec2) const {
    return m_layout.measured_size();
}

void Node::set_measured_size(ImVec2 size) {
    m_layout.set_measured_size(size);
}

ImVec2 Node::content_size(ImVec2 size) const {
    const BoxInsets insets = box_insets();
    return {
        std::max(0.0F, size.x - insets.horizontal()),
        std::max(0.0F, size.y - insets.vertical()),
    };
}

ImVec2 Node::outer_size(ImVec2 size) const {
    const BoxInsets insets = box_insets();
    return {size.x + insets.horizontal(), size.y + insets.vertical()};
}

Rect Node::content_rect(Rect rect) const {
    const BoxInsets insets = box_insets();
    return Rect::from_position_size({rect.min.x + insets.left, rect.min.y + insets.top}, content_size(rect.size()));
}

void Node::set_visual_rect(Rect rect) {
    m_layout.set_visual_rect(rect);
}

void Node::set_layout_rect(Rect rect) {
    m_layout.set_layout_rect(rect);
}

void Node::arrange_child(Node& child, ImVec2 size, Placement placement) {
    child.m_layout.assign_size(size);
    child.m_layout.m_has_parent_size = true;
    child.m_layout.set_arranged_placement(placement);
}

bool Node::capture_pointer() {
    return m_input_router != nullptr && m_input_router->capture_pointer(*this);
}

void Node::release_pointer() {
    if (m_input_router != nullptr) m_input_router->release_pointer();
}

void Node::register_scroll_target(Rect rect) {
    // register only wheel input so a scrollable container does not steal clicks from its children.
    if (m_input_router != nullptr && rect.valid()) {
        m_input_router->register_target(*this, rect, {}, EventMask::Scroll);
    }
}

bool Node::remove(Node& child) {
    if (child.m_parent != this) {
        return false;
    }

    if (!child.m_removal_pending) {
        child.m_removal_pending = true;
        refresh_input_state();
        invalidate_measure();
    }

    return true;
}

std::unique_ptr<Node> Node::detach(Node& child) {
    if (child.m_parent != this) {
        return nullptr;
    }

    const auto it = std::ranges::find(m_children, &child, &std::unique_ptr<Node>::get);
    return it == m_children.end() ? nullptr : detach_child(static_cast<size_t>(it - m_children.begin()));
}

std::unique_ptr<Node> Node::detach_child(size_t index) {
    const auto it = m_children.begin() + static_cast<std::ptrdiff_t>(index);
    std::unique_ptr<Node> result = std::move(*it);
    m_children.erase(it);
    result->m_removal_pending = false;
    result->set_parent(nullptr);
    result->set_input_router(nullptr);
    refresh_input_state();
    invalidate_measure();
    return result;
}

void Node::clear() {
    if (m_children.empty()) {
        return;
    }

    bool changed = false;
    for (const auto& child : m_children) {
        changed |= !child->m_removal_pending;
        child->m_removal_pending = true;
    }

    if (changed) {
        refresh_input_state();
        invalidate_measure();
    }
}

Node* Node::find(std::string_view searched_id) {
    return find_node(*this, searched_id);
}

const Node* Node::find(std::string_view searched_id) const {
    return find_node(*this, searched_id);
}

bool Node::contains(const Node* node) const {
    for (const Node* current = node; current != nullptr; current = current->m_parent) {
        if (current == this) {
            return true;
        }
    }

    return false;
}

void Node::update(float dt) {
    UI_PROFILE_NODE(m_profiler, "Node::update", m_identity);

    if (!m_visible || m_removal_pending) {
        return;
    }

    advance_frame_state(dt);
    if (!m_removal_pending) on_update(dt);
    if (m_removal_pending) {
        return;
    }

    // visit the children present after this node's update hook, removing pending children before their turn.
    size_t child_count = m_children.size();
    for (size_t index = 0; index < child_count && index < m_children.size();) {
        Node* child = m_children[index].get();
        if (child->m_removal_pending) {
            auto removed = std::move(m_children[index]);
            m_children.erase(m_children.begin() + static_cast<std::ptrdiff_t>(index));
            removed->m_parent = nullptr;
            --child_count;
            continue;
        }

        child->update(dt);
        if (m_removal_pending) {
            return;
        }

        ++index;
    }
}

void Node::apply_theme(const Theme& theme) {
    apply_theme_defaults(theme);
    invalidate_measure();
    for (const auto& child : m_children) {
        if (!child->m_removal_pending) {
            child->apply_theme(theme);
        }
    }
}

void Node::invalidate_measure() {
    m_layout.set_box_insets(box_insets());
    m_layout.set_box_sizing(box_sizing());
    m_measure_dirty = true;

    if (m_parent != nullptr && !m_parent->m_measure_dirty) {
        m_parent->invalidate_measure();
    }
}

void Node::measure_tree() {
    UI_PROFILE_NODE(m_profiler, "Node::measure", m_identity);

    if (!m_visible || m_removal_pending || !m_measure_dirty) {
        return;
    }

    // measure children first so containers use measurements from this frame.
    const size_t child_count = m_children.size();
    for (size_t index = 0; index < child_count && index < m_children.size(); ++index) {
        Node* child = m_children[index].get();
        if (!child->m_removal_pending) {
            child->measure_tree();
        }
    }

    on_measure();
    m_measure_dirty = false;
}

void Node::draw() {
    draw_impl(false);
}

void Node::draw_at_cursor() {
    draw_impl(true);
}

void Node::draw_impl(bool at_cursor) {
    UI_PROFILE_NODE(m_profiler, "Node::draw", m_identity);

    if (!m_visible || m_removal_pending) {
        return;
    }

    if (m_profiler != nullptr) {
        m_profiler->record_node_draw();
    }

    // measure and arrange before paint so input uses the bounds produced by this draw.
    prepare_layout(at_cursor);
    const bool is_disabled = !enabled();
    if (is_disabled) ImGui::BeginDisabled();
    draw_before();
    const bool draw_content = paint();

    if (draw_content) {
        draw_children();
        on_draw_end();
        draw_after();
    }

    if (is_disabled) ImGui::EndDisabled();
    submit_positioned_item(at_cursor);

    if (!draw_content || m_input_router == nullptr) return;

    UI_PROFILE_NODE(m_profiler, "Node::input", m_identity);
    if (has_input_mode()) {
        m_input_router->m_hit_test.register_node(*this, m_input_mode == InputMode::Blocker, m_input_area, m_layout.visual_rect());
    }

    if (m_parent == nullptr && ImGui::GetCurrentContext() != nullptr) {
        m_input_router->refresh_pointer_state(ImGui::GetIO().MousePos);
    }
}

void Node::submit_positioned_item(bool at_cursor) {
    if (!m_layout.has_position() || at_cursor) {
        return;
    }

    const ImGuiContext* context = ImGui::GetCurrentContext();
    if (context == nullptr || context->CurrentWindow == nullptr || context->CurrentWindow->IsFallbackWindow ||
        !context->CurrentWindow->DC.IsSetPos) {
        return;
    }

    // imgui requires an item after moving the cursor when the position extends the parent bounds.
    ImGui::Dummy({});
}

void Node::prepare_layout(bool at_cursor) {
    UI_PROFILE_NODE(m_profiler, "Node::layout", m_identity);

    if (m_measure_dirty) {
        measure_tree();
    }

    // keep the parent's assigned size through on_layout, then clear it so the next draw resolves a fresh allocation.
    capture_parent_content();
    on_layout();
    resolve_position(at_cursor);
    m_layout.m_has_parent_size = false;
}

void Node::draw_children() {
    for (const auto& child : m_children) {
        if (!child->m_removal_pending) {
            child->draw();
        }
    }
}

bool Node::paint() {
    return true;
}

void Node::on_layout() {
    if (!has_parent_size()) {
        assign_size(resolve_size(m_layout.available_size()));
    }
}

BoxInsets Node::box_insets() const {
    return {};
}

BoxSizing Node::box_sizing() const {
    return BoxSizing::ContentBox;
}

void Node::draw_before() {}
void Node::on_update(float) {}
void Node::advance_frame_state(float) {}
void Node::on_measure() {}
void Node::on_draw_end() {}
void Node::draw_after() {}
