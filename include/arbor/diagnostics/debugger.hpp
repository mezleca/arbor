#pragma once

#include "../input/event.hpp"
#include "../layout/container.hpp"
#include "../style/style.hpp"

#include <imgui.h>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ui {
    class Surface;
    class Node;
    class Style;
    class StyledNode;
    class Texture;
    class DebuggerPopupState;
    class DebuggerProfileState;

    class Debugger final : public Container {
    public:
        explicit Debugger(Surface& target);
        ~Debugger();

        Debugger(const Debugger&) = delete;
        Debugger& operator=(const Debugger&) = delete;

        void set_open(bool open);
        void toggle() {
            set_open(!m_open);
        }

        bool is_open() const {
            return m_open;
        }

        /// intercepts inspect and panel events before content routing. content resize drags still reach the root.
        bool handle_input(UiEvent& event);
        void handle_hotkey();
        void render();

        void set_style(const ImGuiStyle& style);
        using StyledNode::set_font;
        void set_font(std::string_view id, int size);
        void set_hotkey(ImGuiKeyChord hotkey);

    private:
        friend class Surface;

        bool blocks_pointer_input() const {
            return m_inspect_mode || m_inspect_pointer_capture;
        }

        void render_toolbar();
        void render_node_list(float height);
        void render_sections();
        void render_node_tree(Node& node, int depth);
        void render_properties();
        void render_node_properties();
        void render_profiling();
        void render_highlight_properties();
        void render_layout_properties();
        void render_style_properties();
        static void render_style_controls(Style& style, bool is_line = false, std::span<Style*> all_styles = {});
        void render_decoration_properties(StyledNode& node);
        void render_style_variables(Style& style, std::span<Style*> all_styles = {});
        void draw_property_section(std::string_view label);
        void end_property_section();
        bool handle_inspect_event(UiEvent& event);
        bool handles_content_resize(const UiEvent& event) const;
        void draw_highlight();
        void refresh_highlight();
        void synchronize_targets();
        void set_inspect_mode(bool enabled);
        void finish_popup_restore();
        void update_profile_snapshot();
        void set_target(Node* target);
        void remove_target();
        void set_overlay_focus(bool focused);
        void update_overlay_focus(ImVec2 position);
        bool should_restore_flow_position(const LayoutConfig& config) const;
        bool overlay_contains(ImVec2 position) const;

        bool paint() override;
        void draw_children() override;
        void apply_theme_defaults(const Theme& theme) override;

        Surface& m_target;
        Texture* m_inspect_icon = nullptr;
        Texture* m_close_icon = nullptr;
        Rect m_highlight{};
        bool m_highlight_valid = false;
        Color m_anchor_color = rgb(0.26F, 0.59F, 0.98F);
        Color m_origin_color = rgb(1.0F, 0.25F, 0.25F);
        int m_highlight_line_thickness = 1;
        Rect m_overlay_rect{};
        ImGuiID m_overlay_window_id = 0;
        Node* m_node_target = nullptr;
        Node* m_hover_target = nullptr;

        // removed nodes stay alive because application widgets may retain raw child pointers.
        std::vector<std::unique_ptr<Node>> m_detached_nodes;
        std::vector<std::string> m_variable_names;
        uint64_t m_target_identity = 0;
        uint64_t m_hover_identity = 0;
        int m_inspected_style = 0;
        float m_node_list_ratio = 0.6F;
        double m_overlay_hover_started = -1.0;
        ImGuiKeyChord m_hotkey = ImGuiMod_Shift | ImGuiKey_D;
        bool m_open = false;
        bool m_inspect_mode = false;
        bool m_target_was_flow_position = false;
        bool m_highlight_selected = false;
        bool m_show_anchor = false;
        bool m_show_origin = false;
        bool m_select_properties = false;
        bool m_scroll_to_target = false;
        bool m_overlay_focused = false;
        bool m_overlay_pointer_capture = false;
        bool m_inspect_pointer_capture = false;
        bool m_property_section_open = false;
        std::unique_ptr<DebuggerPopupState> m_popup_state;
        std::unique_ptr<DebuggerProfileState> m_profile_state;
    };
} // namespace ui
