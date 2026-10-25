#include <imgui-ui/layout/container.hpp>
#include <imgui-ui/layout/geometry.hpp>
#include <imgui-ui/constants.hpp>

#include <algorithm>
#include <cmath>
#include <imgui_internal.h>
#include <utility>

using namespace ui;

static bool is_flow_child(const Node& child) {
    return !child.removal_pending() && child.visible() && child.layout().in_flow();
}

static ImVec2 stack_available_size(const Node& child, ImVec2 content_size) {
    const BoxInsets margin = child.layout_margin();
    return {std::max(0.0F, content_size.x - margin.horizontal()), std::max(0.0F, content_size.y - margin.vertical())};
}

Container::Container(std::string id, std::string_view type_name)
    : Container(std::move(id), StackDirection::Vertical, type_name) {}

Container::Container(std::string id, StackDirection direction, std::string_view type_name)
    : StyledNode(std::move(id), type_name), m_direction(direction) {
    set_input_mode(InputMode::Target);
    configure_all_styles([](Style& style) { style.padding({}).box_sizing(BoxSizing::BorderBox); });
}

Container& Container::set_scrollable(bool vertical, bool horizontal) {
    m_scroll.set_axes(vertical, horizontal);
    return *this;
}

void Container::dispatch_event(UiEvent& event) {
    Node::dispatch_event(event);
    scroll_event(event);
}

void Container::scroll_event(UiEvent& event) {
    // block imgui's wheel path even at the limit so only framework ancestors can scroll.
    if (!removal_pending() && !event.propagation_stopped && event.type == EventType::Scroll && !event.default_prevented &&
        scroll_active()) {
        event.block_native_input();
        if (m_scroll.wheel(event.scroll)) event.stop_propagation();
    }
}

bool Container::accepts_input() const {
    return Node::accepts_input() && accepts_visual_input();
}

Container& Container::set_direction(StackDirection direction) {
    if (m_direction == direction) {
        return *this;
    }

    m_direction = direction;
    invalidate_measure();
    return *this;
}

Container& Container::set_content_alignment(ImVec2 alignment) {
    const ImVec2 resolved = {
        std::clamp(alignment.x, 0.0F, 1.0F),
        std::clamp(alignment.y, 0.0F, 1.0F),
    };

    if (m_content_alignment.x == resolved.x && m_content_alignment.y == resolved.y) {
        return *this;
    }

    m_content_alignment = resolved;
    return *this;
}

Container& Container::set_wrap(StackWrap wrap) {
    if (m_wrap == wrap) return *this;

    m_wrap = wrap;
    invalidate_measure();
    return *this;
}

Container& Container::set_spacing(float spacing) {
    const float resolved = std::max(0.0F, spacing);
    if (m_spacing == resolved) {
        return *this;
    }

    m_spacing = resolved;
    invalidate_measure();
    return *this;
}

void Container::apply_theme_defaults(const Theme& theme) {
    configure_all_styles([&theme](Style& style) { style.scrollbar(theme.scrollbar); });
}

void Container::on_measure() {
    const float unbounded = std::numeric_limits<float>::infinity();
    set_measured_size(outer_size(measure_content({unbounded, unbounded})));
}

ImVec2 Container::measure_content(ImVec2 available) const {
    return measure_children(available);
}

ImVec2 Container::measure_size(ImVec2 allocation) const {
    if (children().empty()) return Node::measure_size(allocation);

    ImVec2 measured = outer_size(measure_content(content_size(allocation)));

    // line breaks change the cross extent, not the container's intrinsic main extent.
    if (m_wrap != StackWrap::NoWrap) {
        const int main = m_direction == StackDirection::Horizontal ? 0 : 1;
        measured[main] = layout().measured_size()[main];
    }
    return measured;
}

void Container::measure_item(LayoutItem& item, ImVec2 available) const {
    const NodeLayout& layout = item.node->layout();
    const ImVec2 measured = item.node->measure_size(item.size);
    item.preferred = layout.intrinsic_size(measured);
    item.size = layout.resolve_size(available, measured);
}

ImVec2 Container::measure_children(ImVec2 content_size, bool intrinsic) const {
    const int main = m_direction == StackDirection::Horizontal ? 0 : 1;
    const int cross = 1 - main;
    const float unbounded = std::numeric_limits<float>::infinity();
    const bool constrained = std::isfinite(content_size.x) || std::isfinite(content_size.y);

    // border inset arithmetic can leave an exactly fitting line a few float ulps short.
    const float tolerance = 4.0F * std::numeric_limits<float>::epsilon() * std::abs(content_size[main]);

    m_layout_items.clear();
    m_layout_lines.clear();
    LayoutLine line;

    // collect using hypothetical sizes. grow reserves its minimum until the line is complete.
    for (const auto& child : children()) {
        if (!is_flow_child(*child)) continue;

        const LayoutSize& request = child->layout().size_spec();
        const LayoutAxis& axis = main == 0 ? request.width : request.height;
        const BoxInsets margin = child->layout_margin();
        const ImVec2 available = stack_available_size(*child, content_size);

        LayoutItem item{
            .node = child.get(),
            .size = child->layout().resolve_size(available),
            .preferred = child->layout().intrinsic_size(),
            .margin = margin,
        };

        if (axis.mode == LayoutSizeMode::Grow && std::isfinite(available[main])) {
            const float insets = child->layout().box_insets().axis(main == 0);
            item.size[main] = axis.constrain(0.0F, available[main], insets, child->layout().box_sizing());
            item.maximum = axis.constrain(unbounded, available[main], insets, child->layout().box_sizing());
            item.weight = axis.value;
        } else if (constrained) {
            measure_item(item, available);
        }

        const float extent = item.size[main] + margin.axis(main == 0);
        const float gap = line.count > 0 ? m_spacing : 0.0F;
        const float required = line.main_size + gap + extent;
        if (m_wrap != StackWrap::NoWrap && line.count > 0 && required - content_size[main] > tolerance) {
            m_layout_lines.push_back(line);
            line = {.first = m_layout_items.size()};
        }

        line.main_size += (line.count > 0 ? m_spacing : 0.0F) + extent;
        line.grow_weight += item.weight;
        ++line.count;
        m_layout_items.push_back(item);
    }
    if (line.count > 0) m_layout_lines.push_back(line);

    ImVec2 measured{};
    for (auto& current : m_layout_lines) {
        resolve_line(current, content_size, intrinsic);
        measured[main] = std::max(measured[main], current.main_size);
        measured[cross] += current.cross_size;
    }
    if (!m_layout_lines.empty()) measured[cross] += m_spacing * static_cast<float>(m_layout_lines.size() - 1);
    return measured;
}

void Container::resolve_line(LayoutLine& line, ImVec2 available, bool intrinsic) const {
    const int main = m_direction == StackDirection::Horizontal ? 0 : 1;
    const int cross = 1 - main;
    float remaining = std::max(0.0F, available[main] - line.main_size);
    float grow_weight = line.grow_weight;

    // reaching a maximum removes that item from later shares. an uncapped pass consumes the remainder.
    while (remaining > 0.0F && grow_weight > 0.0F) {
        const float previous_weight = grow_weight;
        const float share = remaining / grow_weight;

        for (size_t index = line.first; index < line.first + line.count; ++index) {
            auto& item = m_layout_items[index];
            if (item.weight == 0.0F) {
                continue;
            }

            const float old_extent = item.size[main];
            const float extent = std::min(item.maximum, old_extent + (share * item.weight));

            remaining -= extent - old_extent;
            line.main_size += extent - old_extent;
            item.size[main] = extent;

            if (extent == item.maximum) {
                grow_weight -= item.weight;
                item.weight = 0.0F;
            }
        }

        if (grow_weight == previous_weight) {
            break;
        }
    }

    // final main allocations determine width-dependent content. keep natural extents separate from stretch sizes.
    const LayoutSize& own = layout().size_spec();
    const LayoutAxis& own_cross = cross == 0 ? own.width : own.height;
    float preferred_main = m_spacing * static_cast<float>(line.count - 1);
    for (size_t index = line.first; index < line.first + line.count; ++index) {
        auto& item = m_layout_items[index];
        const LayoutSize& request = item.node->layout().size_spec();
        const LayoutAxis& axis = main == 0 ? request.width : request.height;
        const LayoutAxis& cross_axis = cross == 0 ? request.width : request.height;

        if (axis.mode == LayoutSizeMode::Grow && line.grow_weight > 0.0F) {
            ImVec2 child_available = stack_available_size(*item.node, available);
            const float assigned = item.size[main];
            measure_item(item, child_available);
            item.size[main] = assigned;
        }

        const bool natural_cross =
            cross_axis.mode == LayoutSizeMode::Grow || (intrinsic && own_cross.mode == LayoutSizeMode::Fit);
        const float extent = natural_cross ? item.preferred[cross] : item.size[cross];
        line.cross_size = std::max(line.cross_size, extent + item.margin.axis(cross == 0));
        preferred_main += item.preferred[main] + item.margin.axis(main == 0);
    }

    // intrinsic measurement ignores unresolved percentages and grow allocations on a single line.
    if (intrinsic && m_wrap == StackWrap::NoWrap) line.main_size = preferred_main;
}

void Container::arrange_children(ImVec2 content_size) {
    const int main = m_direction == StackDirection::Horizontal ? 0 : 1;
    const int cross = 1 - main;
    const ImVec2 measured = measure_children(content_size, false);
    if (m_layout_lines.empty()) return;

    // align-content: stretch divides spare cross space between lines, including a single wrapped line.
    const bool wrapping = m_wrap != StackWrap::NoWrap;
    const float extra = std::max(0.0F, content_size[cross] - measured[cross]) / static_cast<float>(m_layout_lines.size());
    float cross_cursor = 0.0F;

    for (const auto& line : m_layout_lines) {
        const float cross_size = wrapping ? line.cross_size + extra : content_size[cross];
        float cursor = (content_size[main] - line.main_size) * m_content_alignment[main];

        for (size_t index = line.first; index < line.first + line.count; ++index) {
            auto& item = m_layout_items[index];
            const LayoutSize& request = item.node->layout().size_spec();
            const LayoutAxis& cross_axis = cross == 0 ? request.width : request.height;
            if (cross_axis.mode == LayoutSizeMode::Grow) {
                const ImVec2 available = stack_available_size(*item.node, content_size);
                item.size[cross] = std::max(0.0F, cross_size - item.margin.axis(cross == 0));
                item.size[cross] = item.node->layout().constrain_size(item.size, available)[cross];
            }

            ImVec2 offset{item.margin.left, item.margin.top};
            if (m_wrap == StackWrap::WrapReverse) {
                offset[cross] = cross == 0 ? item.margin.right : item.margin.bottom;
            }
            offset[main] += cursor;
            offset[cross] +=
                cross_cursor + (cross_size - item.size[cross] - item.margin.axis(cross == 0)) * m_content_alignment[cross];
            if (m_wrap == StackWrap::WrapReverse) {
                offset[cross] = content_size[cross] - offset[cross] - item.size[cross];
            }

            arrange_child(*item.node, item.size, {.offset = offset});
            cursor += item.size[main] + item.margin.axis(main == 0) + m_spacing;
        }
        cross_cursor += cross_size + m_spacing;
    }
}

ImVec2 Container::child_window_size() const {
    return layout().size();
}

ImGuiWindowFlags Container::child_window_flags() const {
    ImGuiWindowFlags flags = constants::WIDGET_WINDOW_FLAGS;

    // keep the parent pass-through until this container owns input or scrolling.
    if (!has_input_mode() && !has_direct_input_child() && !m_scroll.enabled() &&
        (ImGui::GetCurrentWindow()->Flags & ImGuiWindowFlags_NoMouseInputs) != 0) {
        flags |= ImGuiWindowFlags_NoMouseInputs;
    }

    return flags;
}

Rect Container::shadow_rect(Rect child_rect) const {
    return child_rect;
}

ImVec2 Container::child_layout_size() const {
    const ImVec2 size = child_window_size();
    const ImVec2 scrollbars = ImGui::GetCurrentWindow()->ScrollbarSizes;
    return content_size({size.x - scrollbars.x, size.y - scrollbars.y});
}

void Container::draw_children() {
    const ImVec2 available = child_layout_size();
    arrange_children(available);
    for (const auto& child : children()) {
        if (child->removal_pending() || !child->visible()) {
            continue;
        }

        if (child->layout().in_flow()) {
            child->draw();
            continue;
        }

        const BoxInsets margin = child->layout_margin();
        Placement placement = child->layout().placement();
        const ImVec2 origin = placement.origin;
        placement.offset.x += (margin.left * (1.0F - origin.x)) - (margin.right * origin.x);
        placement.offset.y += (margin.top * (1.0F - origin.y)) - (margin.bottom * origin.y);
        const ImVec2 child_available = stack_available_size(*child, available);
        const ImVec2 size = child->resolve_size(child_available);
        arrange_child(*child, size, placement);
        child->draw();
    }
}
