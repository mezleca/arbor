#include <arbor/style/values.hpp>

using namespace ui;

float ui::interpolate_value(float start, float target, float progress) {
    return std::lerp(start, target, progress);
}

int ui::interpolate_value(int start, int target, float progress) {
    return static_cast<int>(std::lround(std::lerp(static_cast<float>(start), static_cast<float>(target), progress)));
}

ImVec2 ui::interpolate_value(ImVec2 start, ImVec2 target, float progress) {
    return {std::lerp(start.x, target.x, progress), std::lerp(start.y, target.y, progress)};
}

Color ui::interpolate_value(const Color& start, const Color& target, float progress) {
    return interpolate_color(start, target, progress);
}

BoxInsets ui::interpolate_value(BoxInsets start, BoxInsets target, float progress) {
    return {
        std::lerp(start.left, target.left, progress),
        std::lerp(start.top, target.top, progress),
        std::lerp(start.right, target.right, progress),
        std::lerp(start.bottom, target.bottom, progress),
    };
}

BoxShadow ui::interpolate_value(const BoxShadow& start, const BoxShadow& target, float progress) {
    return {
        interpolate_value(start.offset, target.offset, progress),
        std::lerp(start.blur, target.blur, progress),
        std::lerp(start.spread, target.spread, progress),
        interpolate_color(start.color, target.color, progress),
    };
}
