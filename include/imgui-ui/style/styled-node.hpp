#pragma once

#include "paint-slot.hpp"
#include "state.hpp"
#include "../tree/node.hpp"

#include <imgui.h>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace ui {
    class StyledNode : public Node {
    public:
        explicit StyledNode(std::string id = {}, std::string_view type_name = "StyledNode");

        std::string_view type_name() const override {
            return m_type_name;
        }

        /// edits configured values, never the transition or animation snapshot.
        Style& style(StyleType type = StyleType::DEFAULT) {
            return m_state.style(type);
        }

        const Style& style(StyleType type = StyleType::DEFAULT) const {
            return m_state.style(type);
        }

        const ComputedStyle& computed_style() const {
            return m_state.computed_style();
        }

        template <typename Func>
        StyledNode& configure_all_styles(Func&& func) {
            m_state.configure_all_styles(std::forward<Func>(func));
            return *this;
        }

        StyleType style_type() const {
            return m_state.style_type();
        }

        void set_visual_style(StyleType type) {
            if (type == StyleType::DISABLED) {
                set_enabled(false);
            } else if (enabled()) {
                m_state.set_style(type);
            }
        }

        void set_interaction_style(bool hovered, bool active, bool focused = false) {
            if (enabled()) m_state.set_item_state(hovered, active, focused);
        }

        /// selects the active style while open and restores the hovered, active or focused style when closed.
        void set_open_style(bool open);

        StyleAnimationSequence animate() {
            return m_state.animate();
        }

        /// returns the timeline updated while this node is visible. its callbacks stop advancing when the node is hidden.
        Animator& animator() {
            return m_state.animator();
        }

        void cancel_animations() {
            m_state.cancel_animations();
        }

        void fade_in(TransitionSpec transition = OPACITY_TRANSITION) {
            m_state.fade_in(transition);
        }

        /// animates opacity to zero. the node remains visible until set_visible(false) is called.
        void fade_out(TransitionSpec transition = OPACITY_TRANSITION) {
            m_state.fade_out(transition);
        }

        /// shows a plain text tooltip while the pointer hovers this node.
        StyledNode& set_tooltip(std::string text);

        /// shows a tooltip while the pointer hovers this node. the callback emits the tooltip content.
        StyledNode& set_tooltip(std::function<void()> callback);

        void set_opacity(float opacity, TransitionSpec transition = OPACITY_TRANSITION) {
            m_state.set_opacity(opacity, transition);
        }

        float opacity() const {
            return m_state.opacity();
        }

        bool visually_visible() const {
            return m_state.is_visible();
        }

        bool accepts_visual_input() const {
            return m_state.accepts_input();
        }

        BoxInsets layout_margin() const override {
            return computed_style().margin();
        }

        /// creates the paint slot rendered before this node's contents on first access.
        PaintSlot& before();

        bool has_before() const {
            return m_before != nullptr;
        }

        /// creates the paint slot rendered after this node's contents on first access.
        PaintSlot& after();

        bool has_after() const {
            return m_after != nullptr;
        }

        void remove_before();
        void remove_after();

        /// remeasures descendants because they may inherit this font.
        StyledNode& set_font(ImFont* font, float size = 0.0F);

        /// resolves the local font, then the closest styled ancestor, then imgui's font.
        ImFont* font() const;

        /// resolves the nearest explicit size, then the selected font's default size.
        float font_size() const;

        void draw() override;

    protected:
        void surface_changed() override;

        void set_type_name(std::string_view type_name) {
            m_type_name = type_name;
        }

        void advance_frame_state(float dt) final;
        void input_state_changed() override;
        void disabled_changed() override;
        void draw_before() override;
        void draw_after() override;

        BoxInsets box_insets() const override;

        BoxSizing box_sizing() const override {
            return computed_style().box_sizing();
        }

        /// applies the size request to every style state, preserving the previous set_size behavior.
        void request_size(LayoutSize size) override;

        /// adds padding and borders to the content measurement, keeping at least one configured text line in height.
        void set_measured_content_size(ImVec2 size);

        Painter painter() const;
        void draw_surface(Rect rect, const std::optional<Color>& background = {}) const;

    private:
        static void style_changed(void* owner, bool font_changed);

        static void invalidate_font_cache_subtree(Node& node);

        static PaintSlot& ensure_slot(std::unique_ptr<PaintSlot>& slot, StyledNode* owner);
        void draw_slot(PaintSlot& slot, ImDrawList& draw_list);

        void update_cursor();

        void apply_style_size();
        ImVec2 resolve_outer_size(LayoutSize spec) const;
        void advance_size_transition(float dt);

        VisualState m_state;
        std::string_view m_type_name;
        std::unique_ptr<PaintSlot> m_before;
        std::unique_ptr<PaintSlot> m_after;
        mutable ImFont* m_cached_font = nullptr;
        mutable bool m_font_cache_valid = false;
        std::function<void()> m_tooltip_callback;
        std::string m_tooltip_text;
        LayoutSize m_size_target;
        Vec2Value m_animated_size;
        Vec2Value m_size_goal;
        bool m_has_size_target = false;
    };
} // namespace ui
