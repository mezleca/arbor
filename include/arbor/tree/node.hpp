#pragma once

#include "../input/event.hpp"
#include "../layout/geometry.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ui {
    class InputRouter;
    class HitTestIndex;
    class Profiler;
    class EffectRegistry;
    class PaintState;
    class Surface;
    struct Theme;

    struct InputState {
        bool hovered = false;
        bool active = false;
        bool focused = false;

        constexpr bool operator==(const InputState&) const = default;
    };

    enum class InputMode : uint8_t {
        /// default for plain nodes. the node's background is not a pointer target.
        /// descendants can register their own targets independently.
        None,

        /// default for widgets and containers. the hit rectangle receives pointer events, including empty space between children.
        /// events continue to underlying targets unless propagation is stopped.
        Target,

        /// restricts pointer routing to this subtree within the hit rectangle. underlying targets do not receive events.
        /// the last attached active blocker also restricts keyboard routing to its subtree.
        Blocker,
    };

    class Node {
    public:
        /// creates a detached node. parent is assigned only after construction, when another node takes ownership with add.
        explicit Node(std::string id = {});
        Node(const Node&) = delete;
        virtual ~Node();
        Node& operator=(const Node&) = delete;

        template <typename T, typename... Args>
        T& add(Args&&... args) {
            return add(std::make_unique<T>(std::forward<Args>(args)...));
        }

        /// transfers ownership of a non-null child and connects its subtree to this parent. applies the theme when this
        /// parent belongs to a surface.
        Node& add(std::unique_ptr<Node> child);

        template <typename T>
        T& add(std::unique_ptr<T> child) {
            return static_cast<T&>(add(std::unique_ptr<Node>(std::move(child))));
        }

        virtual void update(float dt);

        void apply_theme(const Theme& theme);

        virtual void draw();

        /// draws this node at the current imgui cursor instead of its arranged flow position.
        /// use it when a custom parent interleaves tree nodes or other native imgui items with framework children.
        void draw_at_cursor();

        /// marks a direct child for removal. the parent destroys it before its next update.
        bool remove(Node& child);

        /// immediately transfers ownership of a direct child. use remove() from tree callbacks.
        std::unique_ptr<Node> detach(Node& child);

        /// defers child destruction until update, as remove() does for one child.
        void clear();

        void set_input_router(InputRouter* router);

        void set_profiler(Profiler* profiler);

        /// searches this node and its descendants by string id, skipping nodes pending removal. returns null when absent.
        Node* find(std::string_view id);

        const Node* find(std::string_view id) const;

        bool contains(const Node* node) const;

        const std::string& id() const {
            return m_id;
        }

        virtual std::string_view type_name() const {
            return "Node";
        }

        uint64_t identity() const {
            return m_identity;
        }

        void set_id(std::string id) {
            m_id = std::move(id);
        }

        Node* parent() {
            return m_parent;
        }

        const Node* parent() const {
            return m_parent;
        }

        const std::vector<std::unique_ptr<Node>>& children() const {
            return m_children;
        }

        bool visible() const {
            return m_visible;
        }

        bool removal_pending() const {
            return m_removal_pending;
        }

        /// hides or shows this subtree. hiding stops updates and drawing and clears its hit regions immediately.
        void set_visible(bool visible);

        /// disables input for this subtree without changing its visual state.
        void set_input_enabled(bool enabled);

        bool input_enabled() const {
            return m_input_enabled;
        }

        /// disables input and selects disabled styles throughout this subtree.
        void set_enabled(bool enabled);

        bool enabled() const {
            return m_enabled && (m_parent == nullptr || m_parent->enabled());
        }

        virtual bool accepts_input() const {
            return m_visible && m_input_enabled && m_enabled && !m_removal_pending &&
                   (m_parent == nullptr || m_parent->accepts_input());
        }

        const InputState& input_state() const {
            return m_input_state;
        }

        /// returns direct focus plus hover and active state from the subtree.
        InputState subtree_input_state() const {
            return m_subtree_input_state;
        }

        /// configures this node's persistent input behavior. a valid area is local to the node's visual rectangle
        /// and replaces hit_rect(). an empty area uses hit_rect().
        /// target receives pointer events without blocking underlying nodes. none leaves only descendant hit regions.
        /// blocker excludes unrelated branches behind this node within its hit area. descendants remain eligible.
        /// registering a target does not give it keyboard focus. use input_router().set_focus() for that.
        Node& set_input_mode(InputMode mode, Rect area = {});

        InputMode input_mode() const {
            return m_input_mode;
        }

        /// returns geometry from the last draw pass.
        const NodeLayout& layout() const {
            return m_layout;
        }

        virtual BoxInsets layout_margin() const;

        /// resolves an outer allocation, including content that depends on the available size.
        ImVec2 resolve_size(ImVec2 available) const;

        /// measures preferred outer content using an already resolved allocation. does not change the allocation.
        virtual ImVec2 measure_size(ImVec2 allocation) const;

        /// sets {width, height} using independent px(), percent(), fit() or grow() rules. preserves placement and in_flow.
        /// styled nodes write the request to every style state. invalidates measurements so the next layout pass resolves it.
        /// layout().size() is the outer allocation, while layout().size_spec() retains these rules.
        Node& set_size(LayoutSize size);

        /// grows between {minimum width, minimum height} and {maximum width, maximum height}, using px() or percent().
        /// equivalent to set_size({{min.width, max.width}, {min.height, max.height}}). existing placement is preserved.
        Node& set_size(LayoutBounds minimum, LayoutBounds maximum);

        /// replaces the entire config, including fields omitted by the caller. an omitted size becomes grow/grow,
        /// and omitted in_flow becomes true, even if an earlier set_size or set_anchor requested something else.
        /// styled nodes write config.size to every style state. size or flow changes invalidate measurements.
        /// placement-only changes retain the measured size.
        Node& set_layout(LayoutConfig config);

        /// equivalent to set_anchor(anchor, anchor). preserves the requested size and existing placement offset.
        Node& set_anchor(ImVec2 anchor) {
            return set_anchor(anchor, anchor);
        }

        /// aligns origin on this node with anchor in its parent's content box, then applies the existing offset.
        /// sets in_flow=false without replacing the size request. the node reserves no stack space and cannot enlarge
        /// a fit-sized parent, so it may overlap flowing siblings. its own children keep their existing flow rules.
        Node& set_anchor(ImVec2 anchor, ImVec2 origin);

        /// invalidates this node and its size-dependent ancestors.
        void invalidate_measure();

    protected:
        virtual void surface_changed() {}
        Surface& surface() const;
        EffectRegistry* effect_registry() const;
        PaintState& paint_state() const;

        virtual void dispatch_event(UiEvent& event);

        /// runs first for every event reaching this node. stopping propagation skips the specific handler and remaining routing.
        virtual void event(UiEvent&) {}

        /// runs after event() for the matching event type, unless propagation was stopped or this node was removed.
        virtual void key_press_event(UiEvent&) {}
        virtual void key_release_event(UiEvent&) {}
        virtual void mouse_press_event(UiEvent&) {}
        virtual void mouse_release_event(UiEvent&) {}
        virtual void mouse_move_event(UiEvent&) {}
        virtual void wheel_event(UiEvent&) {}

        /// returns the screen-space area used to select this node for pointer events. defaults to visual_rect.
        /// called after drawing when input mode is target or blocker and no explicit input area was supplied.
        /// the router clips the result to the current imgui clip. an invalid rectangle registers no target or blocker.
        /// override to restrict input to a child control or extend it to resize handles without changing layout bounds.
        virtual Rect hit_rect(Rect visual_rect) const {
            return visual_rect;
        }

        bool has_input_mode() const {
            return m_input_mode != InputMode::None;
        }

        /// returns true for direct children that own input. deeper containers decide their own pass-through window.
        bool has_direct_input_child() const {
            for (const auto& child : m_children) {
                if (child->m_visible && child->m_input_enabled && child->enabled() && !child->m_removal_pending &&
                    child->has_input_mode()) {
                    return true;
                }
            }
            return false;
        }

        virtual void input_state_changed() {}
        virtual void disabled_changed() {}

        bool has_parent_size() const;

        /// routes a size request to the storage that owns it. plain nodes keep it in the layout, styled nodes in styles.
        virtual void request_size(LayoutSize size);

        void apply_size_spec(LayoutSize size);
        void assign_size(ImVec2 size);

        void set_measured_size(ImVec2 size);

        ImVec2 content_size(ImVec2 size) const;
        ImVec2 outer_size(ImVec2 size) const;
        Rect content_rect(Rect rect) const;

        void set_visual_rect(Rect rect);

        /// updates layout and visual bounds together. paint may refine visual bounds afterward.
        void set_layout_rect(Rect rect);

        /// assigns an already resolved outer size and placement. custom allocations must apply constrain_size() first.
        static void arrange_child(Node& child, ImVec2 size, Placement placement = {});

        bool capture_pointer();
        void release_pointer();
        void register_scroll_target(Rect rect);

        virtual void on_update(float dt);
        virtual void advance_frame_state(float dt);

        virtual void apply_theme_defaults(const Theme&) {}

        /// computes intrinsic size after children are measured. parent allocation is resolved separately before paint.
        virtual void on_measure();

        /// runs before each paint. resolves size from available content space when the parent has not assigned it.
        virtual void on_layout();

        /// paints this node and opens its child scope.
        /// returns false to skip children and post-paint hooks.
        virtual bool paint();

        /// runs after layout and before paint, even when paint skips the subtree.
        virtual void draw_before();

        virtual void draw_children();

        /// closes the paint scope after children, before draw_after runs.
        virtual void on_draw_end();

        /// runs above the completed subtree after on_draw_end closes its paint scope.
        virtual void draw_after();

        virtual BoxInsets box_insets() const;
        virtual BoxSizing box_sizing() const;

        void set_input_state(InputState state);

    private:
        friend class Surface;
        friend class InputRouter;
        friend class HitTestIndex;

        void measure_tree();
        void set_parent(Node* parent);
        void set_surface(Surface* surface);
        void bind_input_router(InputRouter* router);
        void detach_input_router(InputRouter& router);
        void clear_input_state();
        bool has_draw_context() const;
        void refresh_input_state();
        void refresh_disabled_state();
        std::unique_ptr<Node> detach_child(size_t index);
        void capture_parent_content();
        void draw_impl(bool at_cursor);
        void prepare_layout(bool at_cursor);
        void resolve_position(bool at_cursor);
        void submit_positioned_item(bool at_cursor);

        std::string m_id;

        uint64_t m_identity = 0;
        Node* m_parent = nullptr;
        std::vector<std::unique_ptr<Node>> m_children;
        bool m_visible = true;
        bool m_input_enabled = true;
        bool m_enabled = true;
        bool m_measure_dirty = true;
        NodeLayout m_layout;
        Surface* m_surface = nullptr;
        mutable std::unique_ptr<PaintState> m_detached_paint_state;
        InputRouter* m_input_router = nullptr;
        Profiler* m_profiler = nullptr;
        Rect m_input_area{};
        InputMode m_input_mode = InputMode::None;
        InputState m_input_state;
        InputState m_subtree_input_state;
        bool m_removal_pending = false;
    };

} // namespace ui
