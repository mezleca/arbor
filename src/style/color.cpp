#include <arbor/style/color.hpp>
#include <arbor/style/gradient-data.hpp>

#include <algorithm>
#include <cmath>
#include <stdexcept>

using namespace ui;

static bool equal_vec4(ImVec4 left, ImVec4 right) {
    return left.x == right.x && left.y == right.y && left.z == right.z && left.w == right.w;
}

GradientStop::GradientStop(float stop_position, const Color& stop_color) : position(stop_position), color(stop_color.rgba()) {
    if (stop_color.gradient() != nullptr) {
        throw std::invalid_argument("gradient stops must be solid colors");
    }
}

float Color::max_alpha() const {
    if (m_gradient == nullptr) {
        return m_rgba.w;
    }

    float alpha = 0.0F;
    for (uint8_t index = 0; index < m_gradient->count; ++index) {
        alpha = std::max(alpha, m_gradient->stops[index].color.w);
    }
    return alpha;
}

bool Color::operator==(const Color& other) const {
    if (!equal_vec4(m_rgba, other.m_rgba)) {
        return false;
    }
    if (m_gradient == other.m_gradient) {
        return true;
    }
    if (m_gradient == nullptr || other.m_gradient == nullptr) {
        return false;
    }

    const GradientData& left = *m_gradient;
    const GradientData& right = *other.m_gradient;
    if (left.type != right.type || left.count != right.count || left.start.x != right.start.x || left.start.y != right.start.y ||
        left.end.x != right.end.x || left.end.y != right.end.y) {
        return false;
    }

    for (uint8_t index = 0; index < left.count; ++index) {
        if (left.stops[index].position != right.stops[index].position ||
            !equal_vec4(left.stops[index].color, right.stops[index].color)) {
            return false;
        }
    }
    return true;
}

Color ui::rgb(float red, float green, float blue) {
    return rgba(red, green, blue, 1.0F);
}

Color ui::rgba(float red, float green, float blue, float alpha) {
    return ImVec4{
        std::clamp(red, 0.0F, 1.0F), std::clamp(green, 0.0F, 1.0F), std::clamp(blue, 0.0F, 1.0F), std::clamp(alpha, 0.0F, 1.0F)
    };
}

Color ui::rgb(int red, int green, int blue) {
    return rgba(red, green, blue, 255);
}

Color ui::rgba(int red, int green, int blue, int alpha) {
    return rgba(
        static_cast<float>(red) / 255.0F, static_cast<float>(green) / 255.0F, static_cast<float>(blue) / 255.0F,
        static_cast<float>(alpha) / 255.0F
    );
}

Color ui::gradient(GradientType type, std::initializer_list<GradientStop> stops, ImVec2 start, ImVec2 end) {
    if (stops.size() < 2 || stops.size() > MAX_GRADIENT_STOPS) {
        throw std::invalid_argument("gradient requires 2 to 8 stops");
    }

    if ((type == GradientType::Linear && start.x == end.x && start.y == end.y) ||
        (type == GradientType::Radial && (start.x == end.x || start.y == end.y))) {
        throw std::invalid_argument("gradient geometry has zero extent");
    }

    auto data = std::make_shared<GradientData>();
    data->type = type;
    data->start = start;
    data->end = end;
    data->count = static_cast<uint8_t>(stops.size());

    float previous = -1.0F;
    uint8_t index = 0;
    for (const GradientStop& stop : stops) {
        if (!std::isfinite(stop.position) || stop.position < 0.0F || stop.position > 1.0F || stop.position <= previous) {
            throw std::invalid_argument("gradient stops must be ordered in [0, 1]");
        }

        data->stops[index++] = stop;
        previous = stop.position;
    }

    ImVec4 fallback = data->stops[0].color;
    for (uint8_t stop = 1; stop < data->count; ++stop) {
        const GradientStop& next = data->stops[stop];
        if (next.position <= 0.5F) {
            fallback = next.color;
            continue;
        }

        const GradientStop& previous_stop = data->stops[stop - 1];
        if (previous_stop.position < 0.5F) {
            const float t = (0.5F - previous_stop.position) / (next.position - previous_stop.position);
            fallback = {
                std::lerp(previous_stop.color.x, next.color.x, t), std::lerp(previous_stop.color.y, next.color.y, t),
                std::lerp(previous_stop.color.z, next.color.z, t), std::lerp(previous_stop.color.w, next.color.w, t)
            };
        }

        break;
    }

    Color result{fallback};
    result.m_gradient = std::move(data);
    return result;
}

Color ui::gradient(GradientType type, std::initializer_list<GradientStop> stops) {
    return type == GradientType::Radial ? gradient(type, stops, {0.5F, 0.5F}, {1.0F, 1.0F})
                                        : gradient(type, stops, {0.0F, 0.0F}, {1.0F, 0.0F});
}

Color ui::interpolate_color(const Color& start, const Color& end, float progress) {
    if (progress >= 1.0F) return end;
    if (start.gradient() != nullptr || end.gradient() != nullptr) return start;

    const ImVec4 from = start.rgba();
    const ImVec4 to = end.rgba();
    return ImVec4{
        std::lerp(from.x, to.x, progress), std::lerp(from.y, to.y, progress), std::lerp(from.z, to.z, progress),
        std::lerp(from.w, to.w, progress)
    };
}
