#pragma once

#include "scroll.hpp"
#include "../style/styled-node.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace ui {
    struct LayoutItem {
        Node* node;
        ImVec2 size;
        ImVec2 preferred;
        float maximum = std::numeric_limits<float>::infinity();
        float weight = 0.0F;
        BoxInsets margin;
    };

    struct LayoutLine {
        size_t first = 0;
        size_t count = 0;
        float main_size = 0.0F;
        float cross_size = 0.0F;
        float grow_weight = 0.0F;
    };

    class Container : public StyledNode {
    public:
        /// its background receives pointer events without blocking targets underneath. use input mode blocker to block them.
        explicit Container(std::string id, std::string_view type_name = "Container");
        Container(std::string id, StackDirection direction, std::string_view type_name = "Container");

        /// enables the requested scrollbars. imgui may still show a vertical scrollbar when horizontal scrolling is enabled,
        /// even if vertical is false.
        Container& set_scrollable(bool vertical, bool horizontal = false);

        Scroll& scroll() {
            return m_scroll;
        }

        const Scroll& scroll() const {
            return m_scroll;
        }

        /// changes which axis stacks flowing children. hidden, removed and out-of-flow children reserve no space.
        Container& set_direction(StackDirection direction);

        /// nowrap by default. wrapping collects items using outer sizes, margins and spacing before sharing grow space
        /// per line. oversized items occupy a line alone. wrap-reverse swaps cross-start and cross-end.
        /// lines share spare cross space equally, as with css align-content: stretch. cross grow fills its own line.
        Container& set_wrap(StackWrap wrap);

        /// aligns the flowing group within available content space. named anchors map to normalized {x, y} factors,
        /// and explicit factors are clamped to [0, 1]. this does not change placement of out-of-flow children.
        /// wrapped groups align each line independently. cross alignment starts at cross-end with wrap-reverse.
        Container& set_content_alignment(ImVec2 alignment);

        /// pixel gap between consecutive flowing children and wrapped lines, clamped to zero. no gap surrounds the group.
        virtual Container& set_spacing(float spacing);

        bool accepts_input() const override;
        ImVec2 measure_size(ImVec2 allocation) const override;

    protected:
        void apply_theme_defaults(const Theme& theme) override;

        void on_measure() override;
        ImVec2 measure_children(
            ImVec2 available = {std::numeric_limits<float>::infinity(), std::numeric_limits<float>::infinity()},
            bool intrinsic = true
        ) const;
        virtual ImVec2 measure_content(ImVec2 available) const;
        void arrange_children(ImVec2 content_size);
        void draw_children() override;
        bool paint() override;
        void on_draw_end() override;

        virtual ImVec2 child_window_size() const;
        virtual ImGuiWindowFlags child_window_flags() const;
        virtual Rect shadow_rect(Rect child_rect) const;
        virtual void paint_surface(const PaintContext& context) const;
        virtual void paint_content(const PaintContext&) {}
        ImVec2 child_layout_size() const;

        void dispatch_event(UiEvent& event) override;
        void scroll_event(UiEvent& event);
        bool scrollable() const {
            return m_scroll.enabled();
        }

        bool scroll_active() const {
            return scrollable() && computed_style().overflow() != Overflow::Clip;
        }

    private:
        void measure_item(LayoutItem& item, ImVec2 available) const;
        void resolve_line(LayoutLine& line, ImVec2 available, bool intrinsic) const;

        Scroll m_scroll;
        mutable std::vector<LayoutItem> m_layout_items;
        mutable std::vector<LayoutLine> m_layout_lines;
        StackDirection m_direction = StackDirection::Vertical;
        StackWrap m_wrap = StackWrap::NoWrap;
        float m_spacing = 0.0F;
        ImVec2 m_content_alignment;
        bool m_content_clip_pushed = false;
        bool m_child_window_open = false;
    };
} // namespace ui
