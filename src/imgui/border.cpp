#include <arbor/imgui/border.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>

using namespace ui;

static constexpr float PI = std::numbers::pi_v<float>;
static constexpr float QUARTER_PI = PI * 0.25F;
static constexpr float HALF_PI = PI * 0.5F;
static constexpr float ARC_MAX_ERROR = 0.05f;

static bool is_selected(const BorderPathSegment& segment, uint8_t border) {
    return (segment.sides & border) != 0;
}

static ImVec2 point_at(const BorderPathSegment& segment, float distance) {
    const float progress = segment.length > 0.0F ? std::clamp(distance / segment.length, 0.0F, 1.0F) : 0.0F;
    if (segment.type == BorderPathSegmentType::Line) {
        return {
            std::lerp(segment.start.x, segment.end.x, progress),
            std::lerp(segment.start.y, segment.end.y, progress),
        };
    }

    // distance is normalized by arc length, keeping dash and dot spacing uniform around corners.
    const float angle = std::lerp(segment.start_angle, segment.end_angle, progress);
    const float radius = segment.length / std::abs(segment.end_angle - segment.start_angle);
    return {segment.center.x + (std::cos(angle) * radius), segment.center.y + (std::sin(angle) * radius)};
}

static float arc_max_step(const BorderPathSegment& segment) {
    const float sweep = std::abs(segment.end_angle - segment.start_angle);
    const float radius = segment.length / sweep;
    // for a chord angle theta, sagitta = radius * (1 - cos(theta / 2)).
    // solving it for theta yields the largest step below the configured arc error.
    return radius <= ARC_MAX_ERROR ? sweep : 2.0F * std::acos(std::clamp(1.0F - (ARC_MAX_ERROR / radius), -1.0F, 1.0F));
}

static void append_segment_range(
    ImDrawList& draw_list, const BorderPathSegment& segment, float start, float end, float max_step = 0.0f,
    bool include_end = true
) {
    if (end <= start) {
        return;
    }

    if (segment.type == BorderPathSegmentType::Line) {
        draw_list.PathLineToMergeDuplicate(point_at(segment, end));
        return;
    }

    const float sweep = std::abs(segment.end_angle - segment.start_angle);
    if (max_step <= 0.0F) {
        max_step = arc_max_step(segment);
    }

    const int steps = std::max(1, static_cast<int>(std::ceil(sweep * (end - start) / segment.length / max_step)));
    const int final_step = include_end ? steps : steps - 1;
    for (int step = 1; step <= final_step; ++step) {
        const float distance = std::lerp(start, end, static_cast<float>(step) / static_cast<float>(steps));
        draw_list.PathLineToMergeDuplicate(point_at(segment, distance));
    }
}

static std::size_t first_selected_run_segment(const BorderPath& path, uint8_t border) {
    for (std::size_t index = 0; index < path.size(); ++index) {
        const BorderPathSegment& segment = path[index];
        if (segment.length > 0.0F && !is_selected(segment, border)) {
            return (index + 1) % path.size();
        }
    }
    return 0;
}

template <typename OnSegment, typename OnRunBreak>
static void walk_selected_segments(const BorderPath& path, uint8_t border, OnSegment&& on_segment, OnRunBreak&& on_run_break) {
    // start after an unselected segment so a selected run is never split by the path seam.
    const std::size_t first = first_selected_run_segment(path, border);

    for (std::size_t offset = 0; offset < path.size(); ++offset) {
        const BorderPathSegment& segment = path[(first + offset) % path.size()];
        if (segment.length <= 0.0F) {
            continue;
        }

        if (!is_selected(segment, border)) {
            on_run_break();
            continue;
        }

        on_segment(segment);
    }

    on_run_break();
}

template <typename OnSegment>
static bool walk_side_segments(const BorderPath& path, uint8_t side, OnSegment&& on_segment) {
    // include both half-corner arcs so patterns continue across the side.
    const std::size_t first = first_selected_run_segment(path, side);
    bool started = false;

    for (std::size_t offset = 0; offset < path.size(); ++offset) {
        const BorderPathSegment& segment = path[(first + offset) % path.size()];
        if (segment.length <= 0.0F) {
            continue;
        }

        if (!is_selected(segment, side)) {
            if (started) {
                break;
            }
            continue;
        }

        started = true;
        on_segment(segment);
    }

    return started;
}

static void stroke_solid_path(ImDrawList& draw_list, const BorderPath& path, uint8_t border, ImU32 color, float thickness) {
    bool has_path = false;
    const bool closed = border == BORDER_ALL;
    const ImDrawFlags flags = closed ? ImDrawFlags_Closed | ImDrawFlags_StrokeInside : ImDrawFlags_None;

    walk_selected_segments(
        path, border,
        [&](const BorderPathSegment& segment) {
            if (!has_path) {
                draw_list.PathLineTo(segment.start);
                has_path = true;
            }
            append_segment_range(draw_list, segment, 0.0f, segment.length, 0.0f, !closed || &segment != &path.back());
        },
        [&] {
            if (has_path) {
                draw_list.PathStroke(color, thickness, flags);
                has_path = false;
            }
        }
    );
}

static constexpr uint8_t BORDER_SIDES[] = {BORDER_TOP, BORDER_RIGHT, BORDER_BOTTOM, BORDER_LEFT};

static void stroke_dashed_side(ImDrawList& draw_list, const BorderPath& path, uint8_t side, ImU32 color, float thickness) {
    // fit complete dash and gap periods to the side so both ends have equal spacing.
    const float preferred_dash = std::max(6.0F, thickness * 4.0F);
    const float preferred_gap = std::max(3.0F, thickness * 2.0F);
    const float preferred_period = preferred_dash + preferred_gap;
    float side_length = 0.0F;
    if (!walk_side_segments(path, side, [&](const BorderPathSegment& segment) { side_length += segment.length; })) {
        return;
    }

    const int dash_count = std::max(1, static_cast<int>(std::floor(side_length / preferred_period)));
    const float period = side_length / static_cast<float>(dash_count);
    if (dash_count == 1 && side_length <= preferred_dash) {
        stroke_solid_path(draw_list, path, side, color, thickness);
        return;
    }

    const float dash_length = std::min(preferred_dash, period);
    const float gap_length = period - dash_length;
    float remaining = gap_length * 0.5F;
    bool drawing = false;
    bool has_path = false;

    const auto flush = [&] {
        if (has_path) {
            draw_list.PathStroke(color, thickness);
            has_path = false;
        }
    };

    walk_side_segments(path, side, [&](const BorderPathSegment& segment) {
        const float max_step = segment.type == BorderPathSegmentType::Arc ? arc_max_step(segment) : 0.0F;
        float distance = 0.0F;
        while (distance < segment.length) {
            const float length = std::min(remaining, segment.length - distance);

            if (drawing) {
                if (!has_path) {
                    draw_list.PathLineTo(point_at(segment, distance));
                    has_path = true;
                }
                append_segment_range(draw_list, segment, distance, distance + length, max_step);
            }

            distance += length;
            remaining -= length;

            if (remaining <= 0.0001F) {
                drawing = !drawing;
                remaining = drawing ? dash_length : gap_length;
                if (!drawing) {
                    flush();
                }
            }
        }
    });
    flush();
}

static void stroke_path(
    ImDrawList& draw_list, const BorderPath& path, uint8_t border, ImU32 color, float thickness,
    void (*stroke_side)(ImDrawList&, const BorderPath&, uint8_t, ImU32, float)
) {
    for (const uint8_t side : BORDER_SIDES) {
        if ((border & side) != 0) stroke_side(draw_list, path, side, color, thickness);
    }
}

static void stroke_dashed_path(ImDrawList& draw_list, const BorderPath& path, uint8_t border, ImU32 color, float thickness) {
    stroke_path(draw_list, path, border, color, thickness, &stroke_dashed_side);
}

static void stroke_dotted_side(ImDrawList& draw_list, const BorderPath& path, uint8_t side, ImU32 color, float thickness) {
    // center dots over the side instead of restarting the pattern at each path segment.
    const float radius = std::max(0.5F, thickness * 0.5F);
    const float spacing = std::max(3.0F, thickness * 3.0F);
    static const auto unit_circle = [] {
        std::array<ImVec2, 24> points{};
        for (std::size_t index = 0; index < points.size(); ++index) {
            const float angle = std::numbers::pi_v<float> * 2.0F * static_cast<float>(index) / static_cast<float>(points.size());
            points[index] = {std::cos(angle), std::sin(angle)};
        }
        return points;
    }();

    int segments = 24;
    if (radius <= 5.0F) segments = 12;
    if (radius <= 1.5F) segments = 8;
    const int stride = static_cast<int>(unit_circle.size()) / segments;
    std::array<ImVec2, 24> points{};
    const auto draw_dot = [&](ImVec2 center) {
        if (radius < 0.75F) {
            draw_list.AddRectFilled({center.x - radius, center.y - radius}, {center.x + radius, center.y + radius}, color);
            return;
        }

        for (int index = 0; index < segments; ++index) {
            const ImVec2 unit = unit_circle[static_cast<std::size_t>(index) * static_cast<std::size_t>(stride)];
            points[static_cast<std::size_t>(index)] = {center.x + (unit.x * radius), center.y + (unit.y * radius)};
        }
        draw_list.AddConvexPolyFilled(points.data(), segments, color);
    };

    std::array<const BorderPathSegment*, 12> run{};
    std::size_t run_count = 0;
    float run_length = 0.0F;
    if (!walk_side_segments(path, side, [&](const BorderPathSegment& segment) {
            run[run_count++] = &segment;
            run_length += segment.length;
        })) {
        return;
    }

    const int dot_count = std::max(1, static_cast<int>(std::floor(run_length / spacing)));
    const float pitch = run_length / static_cast<float>(dot_count);
    for (int dot = 0; dot < dot_count; ++dot) {
        float distance = pitch * (static_cast<float>(dot) + 0.5F);
        for (std::size_t index = 0; index < run_count; ++index) {
            const BorderPathSegment& segment = *run[index];
            if (distance <= segment.length || index + 1 == run_count) {
                draw_dot(point_at(segment, distance));
                break;
            }
            distance -= segment.length;
        }
    }
}

static void stroke_dotted_path(ImDrawList& draw_list, const BorderPath& path, uint8_t border, ImU32 color, float thickness) {
    stroke_path(draw_list, path, border, color, thickness, &stroke_dotted_side);
}

static BorderPathSegment line(ImVec2 start, ImVec2 end, uint8_t sides) {
    return {BorderPathSegmentType::Line, start, end, {}, 0.0F, 0.0F, std::hypot(end.x - start.x, end.y - start.y), sides};
}

static BorderPathSegment arc(ImVec2 center, float radius, float start_angle, float end_angle, uint8_t sides) {
    const auto point = [center, radius](float angle) {
        return ImVec2{center.x + (std::cos(angle) * radius), center.y + (std::sin(angle) * radius)};
    };
    return {
        BorderPathSegmentType::Arc,
        point(start_angle),
        point(end_angle),
        center,
        start_angle,
        end_angle,
        std::abs(end_angle - start_angle) * radius,
        sides,
    };
}

BorderPath ui::rounded_rect_border_path(Rect rect, float rounding) {
    const ImVec2 size = rect.size();
    const float width = std::max(0.0F, size.x);
    const float height = std::max(0.0F, size.y);
    const float radius = std::min({std::max(0.0F, rounding), width * 0.5F, height * 0.5F});

    const ImVec2 top_left = {rect.min.x + radius, rect.min.y + radius};
    const ImVec2 top_right = {rect.max.x - radius, rect.min.y + radius};
    const ImVec2 bottom_right = {rect.max.x - radius, rect.max.y - radius};
    const ImVec2 bottom_left = {rect.min.x + radius, rect.max.y - radius};

    // each corner is split between adjacent sides so partial borders stop at the corner midpoint.
    return {
        {
            line({top_left.x, rect.min.y}, {top_right.x, rect.min.y}, BORDER_TOP),
            arc(top_right, radius, -HALF_PI, -QUARTER_PI, BORDER_TOP),
            arc(top_right, radius, -QUARTER_PI, 0.0F, BORDER_RIGHT),
            line({rect.max.x, top_right.y}, {rect.max.x, bottom_right.y}, BORDER_RIGHT),
            arc(bottom_right, radius, 0.0F, QUARTER_PI, BORDER_RIGHT),
            arc(bottom_right, radius, QUARTER_PI, HALF_PI, BORDER_BOTTOM),
            line({bottom_right.x, rect.max.y}, {bottom_left.x, rect.max.y}, BORDER_BOTTOM),
            arc(bottom_left, radius, HALF_PI, QUARTER_PI * 3.0F, BORDER_BOTTOM),
            arc(bottom_left, radius, QUARTER_PI * 3.0F, PI, BORDER_LEFT),
            line({rect.min.x, bottom_left.y}, {rect.min.x, top_left.y}, BORDER_LEFT),
            arc(top_left, radius, PI, QUARTER_PI * 5.0F, BORDER_LEFT),
            arc(top_left, radius, QUARTER_PI * 5.0F, PI + HALF_PI, BORDER_TOP),
        },
    };
}

void ui::draw_border_path(
    ImDrawList& draw_list, const BorderPath& path, uint8_t border, const Color& color, float thickness, BorderType style
) {
    if (border == BORDER_NONE || thickness <= 0.0F) {
        return;
    }

    const ImU32 draw_color = ImColor{color.rgba()};
    if ((draw_color & IM_COL32_A_MASK) == 0) {
        return;
    }

    switch (style) {
        case BorderType::Solid:
            stroke_solid_path(draw_list, path, border, draw_color, thickness);
            return;
        case BorderType::Dashed:
            stroke_dashed_path(draw_list, path, border, draw_color, thickness);
            return;
        case BorderType::Dotted:
            stroke_dotted_path(draw_list, path, border, draw_color, thickness);
            return;
        case BorderType::None:
            return;
    }
}
